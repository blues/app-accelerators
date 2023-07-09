// Copyright 2023 Blues Inc.  All rights reserved.
// Use of this source code is governed by licenses granted by the
// copyright holder including that found in the LICENSE file.

#include <Arduino.h>
#include <Notecard.h>
#include "DS1603L.h"

#ifndef PRODUCT_UID
#define PRODUCT_UID "" // "com.my-company.my-name:my-project"
#pragma message "PRODUCT_UID is not defined in this example. Please ensure your Notecard has a product identifier set before running this example or define it in code here. More details at https://bit.ly/product-uid"
#endif

// When set to true, all monitoring notes are synced immediately to Notehub.
// When set to false, only alerts are synced immediately.
#ifndef SYNC_MONITORING_NOTES
#define SYNC_MONITORING_NOTES (false)
#endif

#ifndef DEFAULT_REPORT_INTERVAL
#define DEFAULT_REPORT_INTERVAL (1000 * 60 * 5)
#endif

#ifndef DEFAULT_POLL_ENVIRONMENT_INTERVAL
#define DEFAULT_POLL_ENVIRONMENT_INTERVAL (1000 * 60 * 5)
#endif

#ifndef DEFAULT_POLL_SENSORS_INTERVAL
#define DEFAULT_POLL_SENSORS_INTERVAL (1000 * 15)
#endif

#ifndef APP_NAME
#define APP_NAME "nf43"
#endif

#define debug Serial

#define DATA_FILE_NOTIFY "notify.qo"
#define DATA_FILE_MONITOR "monitor.qo"
#define DATA_FILE_ALERT "alert.qo"

#define DATA_FIELD_APP "app"
#define DATA_FIELD_SENSOR_OFFLINE "offline"
#define DATA_FIELD_FUEL_MM "fuel_mm"
#define DATA_FIELD_FUEL_PERCENT "fuel_pc"
#define DATA_FIELD_FUEL_GALLONS "gal"
#define DATA_FIELD_FUEL_REMAINING_MINS "mins"
#define DATA_FIELD_BURN_RATE_MM_PER_MIN "mm/min"
#define DATA_FIELD_BURN_RATE_GAL_PER_HR "gal/hr"
#define DATA_FIELD_TEXT "text"

#define ENV_FULL_HEIGHT_MM "full_height_mm"
#define ENV_EMPTY_HEIGHT_MM "empty_height_mm"
#define ENV_TANK_DIAMETER_MM "tank_diameter_mm"
#define ENV_ACOUSTIC_VELOCITY "acoustic_velocity"
#define ENV_ALERT_BURN_TIME_MINS "alert_burn_time_mins"
#define ENV_ALERT_FUEL_PERCENT "alert_fuel_percent"

#define ACOUSTIC_VELOCITY_WATER 1450          // acoustic velocity of water in m/s
#define ACOUSTIC_VELOCITY_LIQUID_PROPANE 1161 // acoustic velocity of liquid propane in m/s
#define CUBIC_CM_PER_GALLON (3785.41)         // cubic centimeters in a gallon
#define CUBIC_MM_PER_CUBIC_CM (10*10*10)      // cm^3 -> mm^3 factor

#define DEFAULT_FULL_HEIGHT_MM (305)
// the sensor has a minimum sensing height of 50mm, but it's quite inaccurate around there.
#define DEFAULT_EMPTY_HEIGHT_MM (60)
#define DEFAULT_TANK_DIAMETER_MM (312) // 12.3in
#define DEFAULT_ACOUSTIC_VELOCITY ACOUSTIC_VELOCITY_LIQUID_PROPANE
#define DEFAULT_ALERT_BURN_TIME_MINS (30)
#define DEFAULT_ALERT_FUEL_PERCENT (10)

/**
 * @brief Use serial input containing previously captured sensor values, rather than
 * reading the actual sensor itself.
 */
#define NF43_PSEUDOSENSOR (0)

/**
 * @brief Burn rate is presently an experimental feature to be evaluated with additional testing.
 */
#define NF43_BURNRATE (0)

// forward declarations needed by the Arduino .ino preprocessor
struct FuelGaugeData;
struct FuelGauge;

Notecard notecard;

#ifdef NF43_USE_ALT_SERIAL
HardwareSerial sensorSerial(A5, A4); // RX A5 (PC_5) and TX (unused) A4 (PC_4) and for USART3
#else
// Distance sensor on RX/TX
auto &sensorSerial = Serial1;
#endif

/**
 * @brief Implements an exponentially weighted moving average filter.
 */
struct Filter {
    double current, previous, alpha;

    /**
     * @brief Construct a new Filter object
     *
     * @param alpha The smoothing factor, between 0 and 1, represents how much of the current
     * value is used and how much the historical value is used.
     */
    Filter(double alpha) : alpha(alpha) {}

    void setInitialValue(double value) {
        current = previous = value;
    }

    double updateValue(double value) {
        current = (alpha * value) + ((1-alpha) * previous);
        previous = current;
        return current;
    }
};

/**
 * @brief Maintains the state of an alert, the threshold value and whether the corresponding
 * event has been sent.
 */
struct Alert {
    bool alert, sent;
    uint32_t threshold;

    Alert() {
        alert = false;
        sent = false;
    }

    void setAlert(bool alert) {
        if (alert != this->alert) {
            this->alert = alert;
            sent = false;
        }
    }

    /**
     * @brief Determines if an alert event needs sending.
     */
    bool sendAlertRequired() const {
        return alert && !sent;
    };

    /**
     * @brief Notifies this alert that the alert event was sent.
     */
    void alertSent() {
        sent = true;
    }
};

struct FuelGauge {

    // primary value, fuel height
    double fuelHeightMm;
    // secondary values, derived from the fuel height and tank dimensions
    double fuelRemainingPercent;
    double fuelRemainingGallons;

    // derivative of the fuel height over time
    double burnRateMmPerMin;
    // remaining fuel in minutes, based on the current rate of consumption
    double fuelRemainingMinutes;
    // burnRateMmPerMin scaled to gallons per hour.
    double burnRateGalPerHour;

    uint32_t emptyHeightMm = DEFAULT_EMPTY_HEIGHT_MM;
    uint32_t fullHeightMm = DEFAULT_FULL_HEIGHT_MM;
    uint32_t tankDiameterMm = DEFAULT_TANK_DIAMETER_MM;
    Alert alertFuelPercent;
    Alert alertBurnTimeRemaining;

    FuelGauge()
        : fuelMmFilter(0.1), burnRateMmPerMinFilter(0.2)
    {
        // assume online until informed otherwise.
        online = true;
        alertBurnTimeRemaining.threshold = DEFAULT_ALERT_BURN_TIME_MINS;
        alertFuelPercent.threshold = DEFAULT_ALERT_FUEL_PERCENT;
        clearAll();
    }

    void clearAll() {
        fuelHeightMm = fuelRemainingPercent = fuelRemainingGallons = burnRateMmPerMin = fuelRemainingMinutes
            = burnRateGalPerHour = NAN;
        fuelMmFilter.setInitialValue(NAN);
        burnRateMmPerMinFilter.setInitialValue(NAN);
    }

    bool isOnline() const {
        return online;
    }

    /**
     * @brief Set the Fuel Level in the tank. If the sensor was offline, the smoothing filters
     * are initialized and the rate of change set to 0.
     *
     * @param fuelLevelMm The measured fuel level in millimeters.
     */
    void setFuelLevel(uint32_t rawDistanceMm, double fuelLevelMm) {
        uint32_t now = millis();
        if (online) {
            // first reading after a series of invalid readings.
            if (isnan(fuelMmFilter.current)) {
                fuelMmFilter.setInitialValue(fuelLevelMm);
                updateFuelLevel(fuelLevelMm, 0);
            }
            else {
                updateFuelLevel(fuelLevelMm, now-lastReadingTimeMs);
            }
        } else {
            // sensor was offline, but is now online. Initialize the filter to clear any history.
            online = true;
            sendAlert("sensor online");
            fuelMmFilter.setInitialValue(fuelLevelMm);
            updateFuelLevel(fuelLevelMm, 0);
        }
        lastReadingTimeMs = now;
    }

    /**
     * @brief Notifies the fuel gauge that the distance sensor is offline.
     */
    void notifySensorOffline() {
        if (online) {
            online = false;
            sendAlert("sensor offline");
        }
    }

    /**
     * @brief Creates A new note.add request to the given file, optionally synced, with optional text.
     *
     * @param notefile The notefile the note/event is added to.
     * @param sync  Whether to immediately sync
     * @return J* The `note.add` request containing the event data.
     */
    J* buildEvent(const char* notefile, bool sync, const char* text) {
        J* event = notecard.newRequest("note.add");
        JAddStringToObject(event, "file", notefile);
        if (sync) {
            JAddBoolToObject(event, "sync", sync);
        }
        J* body = JAddObjectToObject(event, "body");
        addFuelGaugeProperties(body, text);
        return event;
    }

    /**
     * @brief Adds the properties to an event that describes the state of the fuel gauge.
     *
     * @param body The JSON object the properties are added to
     * @param text Optional text to add to the event.
     * @return J* body
     */
    J* addFuelGaugeProperties(J* body, const char* text=nullptr) {
        JAddStringToObject(body, DATA_FIELD_APP, APP_NAME);

        bool offline = !isOnline();
        if (offline) {
            JAddBoolToObject(body, DATA_FIELD_SENSOR_OFFLINE, true);
        }
        if (text) {
            JAddStringToObject(body, DATA_FIELD_TEXT, text);
        }
        addOptionalNumberToEvent(body, DATA_FIELD_FUEL_MM, fuelHeightMm);
        addOptionalNumberToEvent(body, DATA_FIELD_FUEL_PERCENT,  fuelRemainingPercent);
        addOptionalNumberToEvent(body, DATA_FIELD_FUEL_GALLONS,  fuelRemainingGallons);
#if NF43_BURNRATE
        addOptionalNumberToEvent(body, DATA_FIELD_FUEL_REMAINING_MINS, fuelRemainingMinutes);
        addOptionalNumberToEvent(body, DATA_FIELD_BURN_RATE_GAL_PER_HR, burnRateGalPerHour);
        addOptionalNumberToEvent(body, DATA_FIELD_BURN_RATE_MM_PER_MIN, burnRateMmPerMin);
#endif
        return body;
    }

private:

    /**
     * @brief Accepts a new reading for the fuel level. The fuel level is processed by a low
     * pass filter, and the rate of change is used to determine burn rate (also filtered), and burn time remaining.
     *
     * @param fuelLevelMm   The current fuel level in millimeters
     * @param duration      The duration of this sample in milliseconds - the time since the
     *  previous sample. This is used to determine the rate of fuel consumption.
     */
    void updateFuelLevel(double fuelLevelMm, uint32_t duration) {
        if (isnan(fuelLevelMm)) {
            clearAll();
            return;
        }

        // push the new fuel level through the filter
        double prevFuelLevel = fuelMmFilter.current;
        fuelHeightMm = fuelMmFilter.updateValue(fuelLevelMm);
        fuelRemainingPercent = (fuelHeightMm-emptyHeightMm)*100 / (fullHeightMm-emptyHeightMm);

        double delta = prevFuelLevel-fuelHeightMm;  // positive value means fuel is being consumed
        if (tankDiameterMm) {
            fuelRemainingGallons = tankMmToGal(fuelHeightMm-emptyHeightMm);
        }
        else {
            fuelRemainingGallons = NAN;
        }

        if (duration != 0) {
            // compute the raw burn rate and filter it.
            double rawBurnRateMmPerMin = delta * 60000/duration;
            burnRateMmPerMin = burnRateMmPerMinFilter.updateValue(rawBurnRateMmPerMin);

            // compute the burn rate in gallons per hour and remaining fuel volume
            // this assumes the tank has a circular horizontal cross section
            double deltaVolumeMm3PerMin = tankMmToGal(burnRateMmPerMin);
            burnRateGalPerHour = (deltaVolumeMm3PerMin*60)/(CUBIC_CM_PER_GALLON*CUBIC_MM_PER_CUBIC_CM);

            if (abs(burnRateMmPerMin) > 0.001) {
                fuelRemainingMinutes = fuelHeightMm / burnRateMmPerMin;
            }
            else {
                fuelRemainingMinutes = NAN;
            }
        }
        else {
            burnRateMmPerMinFilter.setInitialValue(0);
            fuelRemainingMinutes = NAN;
            burnRateGalPerHour = NAN;
            burnRateMmPerMin = NAN;
        }

        updateAlerts();
    }


    /**
     * @brief Converts millimeters to gallons, using the diameter of the tank.
     *
     * @param mm        The height millimeters to convert.
     * @return double   The number of gallons that correspond to the given height.
     */
    double tankMmToGal(double mm) {
        double radius = tankDiameterMm/2;
        double volumeMm3 = tankDiameterMm>1 ? PI*radius*radius*mm : NAN;
        double volumeGallons = volumeMm3 / CUBIC_MM_PER_CUBIC_CM / CUBIC_CM_PER_GALLON;
        return volumeGallons;
    }

    void updateAlerts() {
        sendAlertIfRequired(alertFuelPercent, fuelRemainingPercent, "%d % of fuel remaining.");
#if NF43_BURNRATE
        sendAlertIfRequired(alertBurnTimeRemaining, fuelRemainingMinutes, "%d minutes of fuel remaining.");
#endif
    }

    void sendAlertIfRequired(Alert& alert, double value, const char* format) {
        alert.setAlert(online && alert.threshold && !isnan(value) && value <= alert.threshold);

        if (alert.sendAlertRequired()) {
            char text[128];
            snprintf(text, sizeof(text), format, value);

            if (sendAlert(text)) {
                alert.alertSent();
            }
        }
    };

    /**
     * @brief Sends an alert event to `alert.qo` containing the current fuel gauge
     * data and an alert string.
     *
     * @param text
     * @return true
     * @return false
     */
    bool sendAlert(const char* text) {
        // build the alert event, starting with the fuel gauge info
        J* req = buildEvent(DATA_FILE_ALERT, true, text);
        bool sent = notecard.sendRequest(req);
        return sent;
    }

    /**
     * @brief Adds a number as a property of a JSON object when it is a valid number (not NAN).
     *
     * @param body The JSON object to add the number to
     * @param name The name of the JSON property
     * @param value The number to add
     */
    void addOptionalNumberToEvent(J* body, const char* name, double value) {
        if (!isnan(value)) {
            JAddNumberToObject(body, name, value);
        }
    }

    /**
     * @brief Whether the sensor is online or not.
     */
    bool online;
    uint32_t lastReadingTimeMs;

    Filter fuelMmFilter;
    Filter burnRateMmPerMinFilter;
};

/**
 * @brief An ultrasonic sensor that senses the height of the fuel in the tank.
 */
DS1603L sensor(sensorSerial);
/**
 * @brief FuelGauge that provides monitoring and alerts for the propane tank.
 */
FuelGauge fuelGauge;

uint32_t sendReportInterval = DEFAULT_REPORT_INTERVAL;
uint32_t sendReportMs;
uint32_t pollEnvironmentInterval = DEFAULT_POLL_ENVIRONMENT_INTERVAL;
uint32_t pollEnvironmentMs;
int64_t environmentModifiedTime;
uint32_t acousticVelocity = DEFAULT_ACOUSTIC_VELOCITY;

void jsonToStream(J* json, Stream& stream, size_t bufSize) {
    char buf[bufSize];
    JPrintPreallocated(json, buf, bufSize, false);
    debug.println(buf);
}

uint8_t readSensor()
{
    uint8_t status = sensor.readSensor();
    switch (status)
    {
    case DS1603L_READ_NO_DATA:
        // this is expected - a message is sent from the sensor just every few seconds
        // most of the polling calls to readSensor() return this value.
        break;
    case DS1603L_SENSOR_NOT_DETECTED:
        debug.printf("{\"text\":\"Distance sensor not detected.\",\"err\":1,\"time\":%d}", millis());
        debug.println();
        fuelGauge.notifySensorOffline();
        break;

    case DS1603L_READ_CHECKSUM_FAIL: // Checksum of the latest transmission failed.
        debug.printf("{\"text\":\"Data received; checksum failed.\",\"err\":2,\"value\":%d,\"time\":%d}", sensor.getDistance(), millis());
        debug.println();
        // drop it on the floor - the sensor will eventually return DS1603L_SENSOR_NOT_DETECTED
        // or return a valid reading
        break;

    case DS1603L_READ_SUCCESS: // Latest reading was valid and received successfully.
        uint16_t distance = sensor.getDistance();
        // Scale the measurement by the relative acoustic velocities of water and propane.
        // Fallback to the default should acousticVelocity be zero (it should never be zero, but defensive programming.)
        double corrected = double(distance) * double(ACOUSTIC_VELOCITY_WATER) / double(acousticVelocity ? acousticVelocity : ACOUSTIC_VELOCITY_LIQUID_PROPANE);
        debug.printf("{\"text\":\"Reading success.\",\"value\":%d,\"corrected\":%d,\"time\":%d}", uint32_t(distance), uint32_t(corrected), millis());
        debug.println();
        if (distance <= max(fuelGauge.emptyHeightMm, uint32_t(DEFAULT_EMPTY_HEIGHT_MM))) {
            // indicate that the reading is not reliable.
            corrected = NAN;
        }
        if (distance) {
            fuelGauge.setFuelLevel(distance, corrected);
            J* data = fuelGauge.addFuelGaugeProperties(JCreateObject());
            jsonToStream(data, debug, 256);
        }
        break;
    }
    return status;
}

void sendReport()
{
    J* req = fuelGauge.buildEvent(DATA_FILE_MONITOR, SYNC_MONITORING_NOTES, nullptr);
    notecard.sendRequest(req);
}

bool registerNoteTemplate()
{
    static bool registered = false;
    if (registered)
    {
        return true;
    }
    J *body = JCreateObject();
    JAddStringToObject(body, DATA_FIELD_APP, TSTRINGV);
    JAddStringToObject(body, DATA_FIELD_TEXT, TSTRINGV);
    JAddNumberToObject(body, DATA_FIELD_FUEL_MM, TFLOAT32);
    JAddNumberToObject(body, DATA_FIELD_FUEL_PERCENT, TFLOAT32);
    JAddNumberToObject(body, DATA_FIELD_FUEL_GALLONS, TFLOAT32);
#if NF43_BURNRATE
    JAddNumberToObject(body, DATA_FIELD_BURN_RATE_MM_PER_MIN, TFLOAT32);
    JAddNumberToObject(body, DATA_FIELD_BURN_RATE_GAL_PER_HR, TFLOAT32);
    JAddNumberToObject(body, DATA_FIELD_FUEL_REMAINING_MINS, TFLOAT32);
#endif
    J *req = notecard.newCommand("note.template");
    JAddStringToObject(req, "file", DATA_FILE_MONITOR);
    JAddItemToObject(req, "body", body);
    registered = notecard.sendRequest(req);
    if (!registered)
    {
        notecard.logDebug("Unable to register note template.\n");
    }
    return true;
}

/**
 * @brief Uses an `env.modified` request to determine if there are any environment variable
 * changes since the last update.
 *
 * @return true     The environment has changed.
 * @return false    The environment has not changed.
 */
bool hasEnvironmentChanged()
{
    bool changed = false;
    J *rsp = notecard.requestAndResponse(notecard.newRequest("env.modified"));
    if (rsp != NULL)
    {
        int64_t modified = (int64_t)JGetNumber(rsp, "time");
        changed = (!notecard.responseError(rsp) && environmentModifiedTime != modified);
        notecard.deleteResponse(rsp);
    }
    return changed;
}

/**
 * @brief Adds a notification to a JSON object about changes to the environment.
 *
 * @param changed   The JSON object that the notification is added to.
 * @param name      The name of the environment variable that changed.
 * @param oldValue  The previous value of the variable.
 * @param newValue  The current value of the variable.
 * @return J*       The notification that was added.
 */
J *addEnvironmentChangedNotification(J *changed, const char *name, J *oldValue, J *newValue)
{
    J *item = JCreateObject();
    if (oldValue)
    {
        JAddItemToObject(item, "old_value", oldValue);
    }
    if (newValue)
    {
        JAddItemToObject(item, "new_value", newValue);
    }
    JAddItemToObject(changed, name, item);
    return item;
}

J *addEnvironmentChangedNotification(J *changed, const char *name, double oldValue, double newValue)
{
    return addEnvironmentChangedNotification(changed, name, JCreateNumber(oldValue), JCreateNumber(newValue));
}

J *notifyEnvironmentError(J *errors, const char *name, const char *text, const char *value)
{
    J *item = JCreateObject();
    JAddStringToObject(item, "error", text);
    JAddStringToObject(item, "value", value);
    JAddItemToObject(errors, name, item);
    return item;
}

/**
 * @brief Updates an integer variable from the value in the environment.
 *
 * @param env       The environment variables
 * @param changed   Receives notifications of variables that changed.
 * @param error     Receives notificiations of environment errors.
 * @param result    The variable to store the value
 * @param envVar    The name of the environment variable to read.
 * @param multiplier A multiplier to use after fetching the value from the environment. This is useful
 *  when the variable value and internal value use different units. (Such as seconds vs milliseconds.)
 * @param defaultValue The default value to use when the value is not defined in the environment.
 * @returns true indicating the variable was set from the environment.
 */
bool setIntValueFromEnvironment(J *env, J *changed, J *error, uint32_t *result, const char *envVar, uint32_t multiplier, uint32_t defaultValue)
{
    const char *value_str = JGetString(env, envVar);
    uint32_t value = defaultValue;
    bool fromEnvironment = false;
    if (value_str && *value_str)
    { // variable is defined
        char *endptr;
        long env_value = strtol(value_str, &endptr, 10);
        if (*endptr || env_value < 0)
        {
            notifyEnvironmentError(error, envVar, "not a valid whole positive number.", value_str);
        }
        else
        {
            value = env_value * multiplier;
            fromEnvironment = true;
            if (*result != value)
            {
                addEnvironmentChangedNotification(changed, envVar, *result / multiplier, value);
            }
        }
    }
    *result = value;
    return fromEnvironment;
}

/**
 * @brief Handles updates to the environment for the app's tasks and report intervals.
 */
void environmentUpdated(J *env)
{
    J *changed = JCreateObject();
    J *errors = JCreateObject();

    setIntValueFromEnvironment(env, changed, errors, &pollEnvironmentInterval, "environment_update_mins", 1000 * 60, DEFAULT_POLL_ENVIRONMENT_INTERVAL);
    setIntValueFromEnvironment(env, changed, errors, &sendReportInterval, "report_mins", 1000 * 60, DEFAULT_REPORT_INTERVAL);

    setIntValueFromEnvironment(env, changed, errors, &acousticVelocity, ENV_ACOUSTIC_VELOCITY, 1, DEFAULT_ACOUSTIC_VELOCITY);
    setIntValueFromEnvironment(env, changed, errors, &fuelGauge.emptyHeightMm, ENV_EMPTY_HEIGHT_MM, 1, DEFAULT_EMPTY_HEIGHT_MM);
    setIntValueFromEnvironment(env, changed, errors, &fuelGauge.fullHeightMm, ENV_FULL_HEIGHT_MM, 1, DEFAULT_FULL_HEIGHT_MM);
    setIntValueFromEnvironment(env, changed, errors, &fuelGauge.tankDiameterMm, ENV_TANK_DIAMETER_MM, 1, DEFAULT_TANK_DIAMETER_MM);
    setIntValueFromEnvironment(env, changed, errors, &fuelGauge.alertFuelPercent.threshold, ENV_ALERT_FUEL_PERCENT, 1, DEFAULT_ALERT_FUEL_PERCENT);
#if NF43_BURNRATE
    setIntValueFromEnvironment(env, changed, errors, &fuelGauge.alertBurnTimeRemaining.threshold, ENV_ALERT_BURN_TIME_MINS, 1, DEFAULT_ALERT_BURN_TIME_MINS);
#endif
    debug.println("environment updates:");
    if (changed->child)
    {
        jsonToStream(changed, debug, 256);
    }
    else
    {
        debug.println("none");
    }
    debug.println("environment errors:");
    if (errors->child)
    {
        jsonToStream(errors, debug, 256);
    }
    else
    {
        debug.println("none");
    }
    debug.println();

    if (changed->child || errors->child)
    {
        J *req = notecard.newRequest("note.add");
        JAddStringToObject(req, "file", DATA_FILE_NOTIFY);
        J *body = NoteNewBody();
        if (changed->child)
        {
            JAddItemToObject(body, "updates", changed);
            changed = nullptr;
        }
        if (errors->child)
        {
            JAddItemToObject(body, "errors", errors);
            errors = nullptr;
        }
        JAddStringToObject(body, DATA_FIELD_APP, APP_NAME);
        NoteAddBodyToObject(req, body);
        notecard.sendRequest(req);
    }
    JDelete(changed);
    JDelete(errors);
}

bool readEnvironment()
{
    // Read all env vars from the notecard in one transaction
    J *rsp = notecard.requestAndResponse(notecard.newRequest("env.get"));
    if (rsp == NULL)
    {
        return false;
    }
    if (notecard.responseError(rsp))
    {
        notecard.deleteResponse(rsp);
        return false;
    }

    // Update the env modified time
    environmentModifiedTime = JGetNumber(rsp, "time");

    // Update the environment
    J *body = JGetObject(rsp, "body");
    if (body != NULL)
    {
        environmentUpdated(body);
    }

    // Done
    notecard.deleteResponse(rsp);
    return true;
}

void setup()
{
#ifndef NF43_NO_WAIT_FOR_SERIAL
    // uncomment to have the app wait for a Serial connection
    while (!debug) {}
#endif
    debug.begin(115200);
    debug.printf("{\"text\":\"started\",\"build\":\"" __DATE__ " " __TIME__ "\",\"time\":%d}", millis());
    debug.println();

    // distance sensor operates at 9600 baud
    sensorSerial.begin(9600);

    // Initialize Notecard library (without doing any I/O on this task)
    notecard.setDebugOutputStream(debug);
    notecard.begin();
    sensor.begin();

    J *req = notecard.newRequest("hub.set");
    JAddStringToObject(req, "product", PRODUCT_UID);
    JAddStringToObject(req, "mode", "continuous");
    JAddBoolToObject(req, "sync", true);
    notecard.sendRequest(req);

    readEnvironment();

    registerNoteTemplate();

    // initialize and read sensor. This generates an alert when the sensor is offline
    readSensor();
}

bool sync()
{
    J *req = notecard.newRequest("hub.sync");
    return notecard.sendRequest(req);
}

void loop()
{
    // allow manual triggering of tasks
    int c = Serial.read();
    switch (c)
    {
    // update environment
    case 'e':
        readEnvironment();
        break;
    // monitor sensor - sends only alerts immediately
    case 'm':
        readSensor();
        break;
    // report sensor - always sends a report
    case 'r':
        sendReport();
        break;
    case 's':
        sync();
        break;
    }

    readSensor();

    uint32_t now = millis();
    if (sendReportInterval && (now - sendReportMs) > sendReportInterval)
    {
        sendReportMs = now;
        sendReport();
    }

    if (pollEnvironmentInterval && (now - pollEnvironmentMs) > pollEnvironmentInterval)
    {
        pollEnvironmentMs = now;
        debug.println("checking environment...");
        readEnvironment();
    }
}

void NoteUserAgentUpdate(J *ua)
{
    JAddStringToObject(ua, DATA_FIELD_APP, APP_NAME);
}
