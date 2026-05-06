// Copyright 2023 Blues Inc.  All rights reserved.
// Use of this source code is governed by licenses granted by the
// copyright holder including that found in the LICENSE file.

#include <Arduino.h>
#include <string.h>
#include "Notecard.h"
#include "Adafruit_BME280.h"
#include "Adafruit_seesaw.h"
#include "greenhouse.h"

#define LIGHT_SENSOR_PIN (A1)
#define DATA_FILE_NOTIFY "notify.qo"
#define DATA_FILE_MONITOR "greenhouse.qo"
#define DATA_FILE_ALERT "alert.qo"

#define DATA_FIELD_APP "app"
#define DATA_FIELD_ALERT_SEQUENCE "alert_seq"
#define DATA_FIELD_ALERT_LEVEL "alert"
#define DATA_FIELD_AIR_TEMP "air_temp"
#define DATA_FIELD_AIR_HUMIDITY "air_humidity"
#define DATA_FIELD_AIR_PRESSURE "air_pressure"
#define DATA_FIELD_LIGHT_LEVEL "light_level"
#define DATA_FIELD_SOIL_TEMP "soil_temp"
#define DATA_FIELD_SOIL_MOISTURE "soil_moisture"

#ifndef PRODUCT_UID
#define PRODUCT_UID ""		// "com.my-company.my-name:my-project"
#pragma message "PRODUCT_UID is not defined in this example. Please ensure your Notecard has a product identifier set before running this example or define it in code here. More details at https://bit.ly/product-uid"
#endif

// When set to true, all monitoring notes are synced immediately to Notehub.
// When set to false, only alerts are synced immediately.
#ifndef SYNC_MONITORING_NOTES
#define SYNC_MONITORING_NOTES        (false)
#endif

#ifndef DEFAULT_REPORT_INTERVAL
#define DEFAULT_REPORT_INTERVAL (1000*60*5)
#endif

#ifndef DEFAULT_POLL_ENVIRONMENT_INTERVAL
#define DEFAULT_POLL_ENVIRONMENT_INTERVAL (1000*60*5)
#endif

#ifndef DEFAULT_POLL_SENSORS_INTERVAL
#define DEFAULT_POLL_SENSORS_INTERVAL (1000*15)
#endif

#ifndef APP_NAME
#define APP_NAME    "nf15"
#endif


void outputInterval(Stream& out, float vmin, float vmax) {
    out.print('[');
    if (isValueSet(vmin)) out.print(vmin);
    out.print(',');
    if (isValueSet(vmax)) out.print(vmax);
    out.print(']');
}

inline bool isEmptyInterval(float vmin, float vmax) {
    return vmin==vmax || (!isValueSet(vmin) && !isValueSet(vmax));
}

void outputThresholdAlert(Stream& out, const ThresholdAlert* a) {
    out.print(alertLevelString(a->alertLevel));
    if (a->threshold!=THRESHOLD_NONE) {
        out.print(": threshold ");
        out.print(thresholdString(a->threshold));
    }
}


#define debug Serial

Notecard notecard;

Adafruit_BME280 bme280;
bool bme280Initialized;
bool bme280Messaged;
float air_temp;
float air_humidity;
float air_pressure;
AlertIntervals air_temp_intervals;
AlertIntervals air_humidity_intervals;
AlertIntervals air_pressure_intervals;
ThresholdAlert air_temp_alert;
ThresholdAlert air_humidity_alert;
ThresholdAlert air_pressure_alert;

Adafruit_seesaw seesaw;
bool seesawInitialized;
bool seesawMessaged;
float soil_temp;
float soil_moisture;
AlertIntervals soil_temp_intervals;
AlertIntervals soil_moisture_intervals;
ThresholdAlert soil_temp_alert;
ThresholdAlert soil_moisture_alert;

float light_level;
AlertIntervals light_level_intervals;
ThresholdAlert light_level_alert;

AlertLevel alertLevel;
AlertSequence alertSequence;

uint32_t readSensorsInterval = DEFAULT_POLL_SENSORS_INTERVAL;
uint32_t readSensorsMs;
uint32_t sendReportInterval = DEFAULT_REPORT_INTERVAL;
uint32_t sendReportMs;
uint32_t pollEnvironmentInterval = DEFAULT_POLL_ENVIRONMENT_INTERVAL;
uint32_t pollEnvironmentMs;
int64_t environmentModifiedTime;

bool initializeBME280() {
    if (!bme280Initialized) {
        unsigned status = bme280.begin();
        if (!status && !bme280Messaged) {
            bme280Messaged = true;
            debug.println("Could not find a valid BME280 sensor, check wiring, address, sensor ID!");
            debug.print("SensorID was: 0x"); debug.println(bme280.sensorID(),16);
            debug.print("        ID of 0xFF probably means a bad address, a BMP 180 or BMP 085\n");
            debug.print("   ID of 0x56-0x58 represents a BMP 280,\n");
            debug.print("        ID of 0x60 represents a BME 280.\n");
            debug.print("        ID of 0x61 represents a BME 680.\n");
        }
        bme280Initialized = status;
    }
    return bme280Initialized;
}

bool initializeSeesaw() {
    if (!seesawInitialized) {
        seesawInitialized = seesaw.begin(0x36);
        if (seesawInitialized) {
            debug.print("seesaw started! version: ");
            debug.println(seesaw.getVersion(), HEX);
        }
        else if (!seesawMessaged) {
            debug.println("ERROR! seesaw not found");
            seesawMessaged = true;
        }
    }
    return seesawInitialized;
}

void readBME280() {
    if (initializeBME280()) {
        air_temp = bme280.readTemperature();
        air_humidity = bme280.readHumidity();
        air_pressure = bme280.readPressure()/100.0; // convert from Pascals hecto-Pascals which is more commonly used when reporting atmospheric pressure
        bme280Messaged = false; // re-print the message after a successful read should the sensor connection fail
        if (!isValueSet(air_temp) && !isValueSet(air_humidity) && !isValueSet(air_pressure)) {
            bme280Initialized = false;
        }
    }
    else {
        air_temp = SENSOR_VALUE_UNDEFINED;
        air_humidity = SENSOR_VALUE_UNDEFINED;
        air_pressure = SENSOR_VALUE_UNDEFINED;
    }
}

void readSeesaw() {
    if (initializeSeesaw()) {
        soil_temp = seesaw.getTemp();
        soil_moisture = seesaw.touchRead(0);
        seesawMessaged = false;
        if (!isValueSet(soil_temp) && !isValueSet(soil_moisture)) {
            seesawInitialized = false;
        }
    }
}

void readPhotosensor() {
    light_level = analogRead(LIGHT_SENSOR_PIN);
}

void readSensors() {
    readBME280();
    readSeesaw();
    readPhotosensor();
}

void outputSensorValue(Stream& s, const char* name, float value) {
    s.print(name);
    s.print(": ");
    if (isValueSet(value)) {
        s.print(value);
    }
    else {
        s.print("N/A");
    }
}

void outputAlertInterval(Stream& out, const char* name, const Interval* interval) {
    if (!isEmptyInterval(interval->vmin, interval->vmax)) {
        out.print(name);
        outputInterval(out, interval->vmin, interval->vmax);
        out.print(' ');
    }
}

void outputAlertIntervals(Stream& out, const AlertIntervals* intervals) {
    outputAlertInterval(out, "normal", &intervals->normal);
    outputAlertInterval(out, "warning", &intervals->warning);
}

void outputSensorReport(Stream& out, const char* name, float value, const ThresholdAlert* alert, const AlertIntervals* intervals) {
    outputSensorValue(out, name, value);
    out.print(' ');
    outputThresholdAlert(out, alert);
    out.print(' ');
    outputAlertIntervals(out, intervals);
    out.println();
}

void outputReport(Stream& out) {
    out.println("sensor readings:");
    outputSensorReport(out, DATA_FIELD_SOIL_TEMP, soil_temp, &soil_temp_alert, &soil_temp_intervals);
    outputSensorReport(out, DATA_FIELD_SOIL_MOISTURE, soil_moisture, &soil_moisture_alert, &soil_moisture_intervals);
    outputSensorReport(out, DATA_FIELD_AIR_TEMP, air_temp, &air_temp_alert, &air_temp_intervals);
    outputSensorReport(out, DATA_FIELD_AIR_PRESSURE, air_pressure, &air_pressure_alert, &air_pressure_intervals);
    outputSensorReport(out, DATA_FIELD_AIR_HUMIDITY, air_humidity, &air_humidity_alert, &air_humidity_intervals);
    outputSensorReport(out, DATA_FIELD_LIGHT_LEVEL, light_level, &light_level_alert, &light_level_intervals);
    out.println();
}

ThresholdAlert checkAlertThresholds(double v, const AlertIntervals* a) {
    ThresholdAlert result = NORMAL;
    if (isValueSet(v)) {
        Threshold t = checkInterval(a->warning.vmin, a->warning.vmax, v);
        if (t) {
            result.alertLevel = ALERT_LEVEL_CRITICAL;
        }
        else {
            t = checkInterval(a->normal.vmin, a->normal.vmax, v);
            if (t) {
                result.alertLevel = ALERT_LEVEL_WARNING;
            }
        }
        result.threshold = t;
    }
    return result;
}

inline void updateSensorAlert(float v, const AlertIntervals* intervals, ThresholdAlert* alert, AlertLevel* level) {
    *alert = checkAlertThresholds(v, intervals);
    *level = highestAlertLevel(*level, alert->alertLevel);
}

void checkAlerts() {
    // the overall alert level
    alertLevel = ALERT_LEVEL_NORMAL;

    updateSensorAlert(soil_temp, &soil_temp_intervals, &soil_temp_alert, &alertLevel);
    updateSensorAlert(soil_moisture, &soil_moisture_intervals, &soil_moisture_alert, &alertLevel);
    updateSensorAlert(air_temp, &air_temp_intervals, &air_temp_alert, &alertLevel);
    updateSensorAlert(air_humidity, &air_humidity_intervals, &air_humidity_alert, &alertLevel);
    updateSensorAlert(air_pressure, &air_pressure_intervals, &air_pressure_alert, &alertLevel);
    updateSensorAlert(light_level, &light_level_intervals, &light_level_alert, &alertLevel);

    bool wasAlerting = isAlertSequenceOngoing(alertSequence);
    bool nowAlerting = isAlertLevel(alertLevel);
    alertSequence = nowAlerting
        ? (wasAlerting ? ALERT_SEQ_SUBSEQUENT : ALERT_SEQ_FIRST)
        : (wasAlerting ? ALERT_SEQ_LAST : ALERT_SEQ_NONE);

    outputReport(debug);
}

void readAndCheckSensors() {
    readSensors();
    checkAlerts();
}

J* buildNote(J* req, bool immediate) {
    J* body = NoteNewBody();
    if (isAlertSequence(alertSequence)) {
        JAddStringToObject(body, DATA_FIELD_ALERT_SEQUENCE, alertSequenceString(alertSequence));
    }
    if (isAlertLevel(alertLevel)) {
        JAddStringToObject(body, DATA_FIELD_ALERT_LEVEL, alertLevelString(alertLevel));
    }

    JAddStringToObject(body, DATA_FIELD_APP, APP_NAME);

    if (immediate) {
        JAddBoolToObject(req, "sync", true);
    }
    NoteAddBodyToObject(req, body);
    return body;
}

void addSensorValue(J* body, const char* name, float value) {
    if (isValueSet(value)) {
        JAddNumberToObject(body, name, value);
    }
}

bool sendMonitorEvent(bool immediate) {
    J *req = notecard.newRequest("note.add");
    JAddStringToObject(req, "file", DATA_FILE_MONITOR);
    J* body = buildNote(req, immediate);
    addSensorValue(body, DATA_FIELD_SOIL_TEMP, soil_temp);
    addSensorValue(body, DATA_FIELD_SOIL_MOISTURE, soil_moisture);
    addSensorValue(body, DATA_FIELD_AIR_TEMP, air_temp);
    addSensorValue(body, DATA_FIELD_AIR_HUMIDITY, air_humidity);
    addSensorValue(body, DATA_FIELD_AIR_PRESSURE, air_pressure);
    addSensorValue(body, DATA_FIELD_LIGHT_LEVEL, light_level);
    return notecard.sendRequest(req);
}

void buildAlert(J* body, const char* sensor_name, float value, const ThresholdAlert* alert) {
    J* r = JAddObjectToObject(body, sensor_name);
    if (alert->alertLevel) {
        JAddStringToObject(r, DATA_FIELD_ALERT_LEVEL, alertLevelString(alert->alertLevel));
    }
    JAddStringToObject(r, "status", thresholdStatusString(alert->threshold));
    JAddNumberToObject(r, "value", value);
}

bool sendAlertThresholds(bool immediate) {
    J *req = notecard.newRequest("note.add");
    JAddStringToObject(req, "file", DATA_FILE_ALERT);

    J* body = buildNote(req, immediate);

    buildAlert(body, DATA_FIELD_SOIL_TEMP, soil_temp, &soil_temp_alert);
    buildAlert(body, DATA_FIELD_SOIL_MOISTURE, soil_moisture, &soil_moisture_alert);
    buildAlert(body, DATA_FIELD_AIR_TEMP, air_temp, &air_temp_alert);
    buildAlert(body, DATA_FIELD_AIR_HUMIDITY, air_humidity, &air_humidity_alert);
    buildAlert(body, DATA_FIELD_AIR_PRESSURE, air_pressure, &air_pressure_alert);
    buildAlert(body, DATA_FIELD_LIGHT_LEVEL, light_level, &light_level_alert);

    return notecard.sendRequest(req);
}

bool buildAndSendEvents(boolean immediate) {
    bool success = sendMonitorEvent(immediate);
    if (isAlertSequence(alertSequence)) {
        success = sendAlertThresholds(immediate);
    }
    return success;
}

bool sendIfAlert() {
    bool done = false;
    if (isAlertSequenceImmediate(alertSequence)) {
        debug.println("sending immediate alert");
        done = buildAndSendEvents(true);
    }
    return done;
}

bool sendReport() {
    debug.println("sending monitoring report");
    return buildAndSendEvents(isAlertSequenceImmediate(alertSequence));
}

void registerNoteTemplate() {
    static bool registered = false;
    if (registered) {
        return;
    }
    J *body = JCreateObject();
    JAddStringToObject(body, DATA_FIELD_ALERT_SEQUENCE, TSTRINGV);
    JAddStringToObject(body, DATA_FIELD_ALERT_LEVEL, TSTRINGV);
    JAddStringToObject(body, DATA_FIELD_APP, TSTRINGV);
    JAddNumberToObject(body, DATA_FIELD_SOIL_TEMP, TFLOAT32);
    JAddNumberToObject(body, DATA_FIELD_SOIL_MOISTURE, TFLOAT32);
    JAddNumberToObject(body, DATA_FIELD_AIR_TEMP, TFLOAT32);
    JAddNumberToObject(body, DATA_FIELD_AIR_HUMIDITY, TFLOAT32);
    JAddNumberToObject(body, DATA_FIELD_AIR_PRESSURE, TFLOAT32);
    JAddNumberToObject(body, DATA_FIELD_LIGHT_LEVEL, TFLOAT32);

    J* req = notecard.newCommand("note.template");
    JAddStringToObject(req, "file", DATA_FILE_MONITOR);
    JAddItemToObject(req, "body", body);
    registered = notecard.sendRequest(req);
    if (!registered) {
        notecard.logDebug("Unable to register note template.\n");
    }
}

bool hasEnvironmentChanged() {
    bool changed = false;
    J *rsp = notecard.requestAndResponse(notecard.newRequest("env.modified"));
    if (rsp != NULL) {
        int64_t modified = (int64_t) JGetNumber(rsp, "time");
        changed = (!notecard.responseError(rsp) && environmentModifiedTime != modified);
        notecard.deleteResponse(rsp);
    }
    return changed;
}

bool notifyEnvironmentChanged(J* changed, const char* name, double oldValue, double newValue) {
    J* item = JCreateObject();
    if (!isnan(oldValue)) {
        JAddNumberToObject(item, "old_value", oldValue);
    }
    if (!isnan(newValue)) {
        JAddNumberToObject(item, "new_value", newValue);
    }
    JAddItemToObject(changed, name, item);
    return item;
}

bool notifyEnvironmentError(J* errors, const char* name, const char* text, const char* value) {
    J* item = JCreateObject();
    JAddStringToObject(item, "error", text);
    JAddStringToObject(item, "value", value);
    JAddItemToObject(errors, name, item);
    return item;
}

void setTaskIntervalFromEnvironment(J* env, J* changed, J* error, uint32_t* interval_result, const char* envVar, uint32_t multiplier, uint32_t defaultInterval) {
    const char* interval_str = JGetString(env, envVar);
    uint32_t interval = defaultInterval;
    if (interval_str && *interval_str) {    // variable is defined
        char *endptr;
        long value = strtol(interval_str, &endptr, 10);
        if (*endptr || value<0) {
            notifyEnvironmentError(error, envVar, "not a valid whole positive number.", interval_str);
        }
        else {
            interval = value * multiplier;
            if (*interval_result!=interval) {
                notifyEnvironmentChanged(changed, envVar, *interval_result/multiplier, value);
            }
        }
    }
    *interval_result = interval;
}

inline bool compare_float(float a, float b, double epsilon=0.0001) {
    return (fabs(a - b) < epsilon);
}

bool updateIntervalThresholdFromEnvrionment(J* env, J* changed, J* errors, const char* sensor_name, const char* range_name, const char* threshold, float* result) {
    char varname[256];
    snprintf(varname, sizeof(varname), "%s_%s_%s", sensor_name, range_name, threshold);
    bool success = false;
    float r = SENSOR_VALUE_UNDEFINED;
    const char* value = JGetString(env, varname);
    if (value && *value) {          // environment variable has been set
        char* end;
        r = strtod(value, &end);
        if (end-value==strlen(value)) { // all characters parsed
            if (!compare_float(*result, r)) {
                notifyEnvironmentChanged(changed, varname, *result, r);
                *result = r;
            }
            success = true;
        }
        else {
            notifyEnvironmentError(errors, varname, "Value is not a number.", value);
        }
    }
    else {      // environment variable removed or unset
        *result = r;
        success = true;
    }
    return success;
}

// Updates the low and high thresholds for an interval on a sensor
void updateIntervalFromEnvironment(J* env, J* changed, J* errors,  const char* sensor_name, const char* range_name, Interval* interval) {
    updateIntervalThresholdFromEnvrionment(env, changed, errors, sensor_name, range_name, "low", &interval->vmin);
    updateIntervalThresholdFromEnvrionment(env, changed, errors, sensor_name, range_name, "high", &interval->vmax);
}

void updateAlertThresholdsFromEnvironment(J* env, J* changed, J* errors, const char* sensor_name, AlertIntervals* intervals) {
    updateIntervalFromEnvironment(env, changed, errors, sensor_name, "normal", &intervals->normal);
    updateIntervalFromEnvironment(env, changed, errors, sensor_name, "warning", &intervals->warning);
}

/**
 * @brief Handles updates to the environment for the app's tasks and report intervals.
 */
void environmentUpdated(J* env) {
    J* changed = JCreateObject();
    J* errors = JCreateObject();

    setTaskIntervalFromEnvironment(env, changed, errors, &pollEnvironmentInterval, "environment_update_mins", 1000*60, DEFAULT_POLL_ENVIRONMENT_INTERVAL);
    setTaskIntervalFromEnvironment(env, changed, errors, &readSensorsInterval, "monitor_secs", 1000, DEFAULT_POLL_SENSORS_INTERVAL);
    setTaskIntervalFromEnvironment(env, changed, errors, &sendReportInterval, "report_mins", 1000*60, DEFAULT_REPORT_INTERVAL);

    updateAlertThresholdsFromEnvironment(env, changed, errors, DATA_FIELD_SOIL_TEMP, &soil_temp_intervals);
    updateAlertThresholdsFromEnvironment(env, changed, errors, DATA_FIELD_SOIL_MOISTURE, &soil_moisture_intervals);

    updateAlertThresholdsFromEnvironment(env, changed, errors, DATA_FIELD_AIR_TEMP, &air_temp_intervals);
    updateAlertThresholdsFromEnvironment(env, changed, errors, DATA_FIELD_AIR_HUMIDITY, &air_humidity_intervals);
    updateAlertThresholdsFromEnvironment(env, changed, errors, DATA_FIELD_AIR_PRESSURE, &air_pressure_intervals);

    updateAlertThresholdsFromEnvironment(env, changed, errors, DATA_FIELD_LIGHT_LEVEL, &light_level_intervals);

    char buf[256];
    debug.println("environment updates:");
    if (changed->child) {
        JPrintPreallocated(changed, buf, sizeof(buf), false);
        debug.println(buf);
    }
    else {
        debug.println("none");
    }
    debug.println("environment errors:");
    if (errors->child) {
        JPrintPreallocated(errors, buf, sizeof(buf), false);
        debug.println(buf);
    }
    else {
        debug.println("none");
    }
    debug.println();

    if (changed->child || errors->child) {
        J* req = notecard.newRequest("note.add");
        JAddStringToObject(req, "file", DATA_FILE_NOTIFY);
        J* body = NoteNewBody();
        if (changed->child) {
            JAddItemToObject(body, "updates", changed);
            changed = nullptr;
        }
        if (errors->child) {
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

bool readEnvironment() {
    // Read all env vars from the notecard in one transaction
    J *rsp = notecard.requestAndResponse(notecard.newRequest("env.get"));
    if (rsp == NULL) {
        return false;
    }
    if (notecard.responseError(rsp)) {
        notecard.deleteResponse(rsp);
        return false;
    }

    // Update the env modified time
    environmentModifiedTime = JGetNumber(rsp, "time");

    // Update the environment
    J *body = JGetObject(rsp, "body");
    if (body != NULL) {
        environmentUpdated(body);
    }

    // Done
    notecard.deleteResponse(rsp);
    return true;
}

void setup()
{
    // uncomment to have the app wait for a Serial connection
    // while (!debug) {};
    debug.begin(115200);
    debug.println("*** " __DATE__ " " __TIME__ " ***");

    // Initialize Notecard library (without doing any I/O on this task)
    notecard.setDebugOutputStream(debug);
    notecard.begin();

    J *req = notecard.newRequest("hub.set");
    JAddStringToObject(req, "product", PRODUCT_UID);
    JAddStringToObject(req, "mode", "continuous");
    JAddBoolToObject(req, "sync", true);
    notecard.sendRequest(req);

    readEnvironment();

    pinMode(LIGHT_SENSOR_PIN, INPUT);

    registerNoteTemplate();

    // initialize and read sensors
    readSensors();
}

bool sync() {
    J* req = notecard.newRequest("hub.sync");
    return notecard.sendRequest(req);
}

void loop()
{
    // allow manual triggering of tasks
    int c = Serial.read();
    switch (c) {
        // update environment
        case 'e': readEnvironment(); break;
        // monitor sensors - sends only alerts immediately
        case 'm': readAndCheckSensors(); sendIfAlert(); break;
        // report sensors - always sends a report
        case 'r': readAndCheckSensors(); sendReport(); break;
        case 's': sync(); break;
    }

    uint32_t now = millis();
    bool sensorsUpdated = false;
    if (readSensorsInterval && (now-readSensorsMs)>readSensorsInterval) {
        readSensorsMs = now;
        readAndCheckSensors();
        sensorsUpdated = true;
        if (sendIfAlert()) {          // alert just sent, no need for a regular monitoring report
            sendReportMs = now;
        }
    }

    if (sendReportInterval && (now-sendReportMs)>sendReportInterval) {
        sendReportMs = now;
        if (!sensorsUpdated) {
            readAndCheckSensors();
            readSensorsMs = now;
        }
        sendReport();
    }

    if (pollEnvironmentInterval && (now-pollEnvironmentMs)>pollEnvironmentInterval) {
        pollEnvironmentMs = now;
        debug.println("checking environment...");
        readEnvironment();
    }
}

void NoteUserAgentUpdate(J *ua) {
   JAddStringToObject(ua, DATA_FIELD_APP, APP_NAME);
}
