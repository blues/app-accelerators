// Copyright 2023 Blues Inc.  All rights reserved.
// Use of this source code is governed by licenses granted by the
// copyright holder including that found in the LICENSE file.

// App definitions
#define APP_MAIN
#include "app.h"
#include "notecard-aux-serial.h"
#include "notecard-sleep.h"
#include "vl53l4cd_class.h"

#include <math.h>

// When set to false, only alerts are synced immediately.
#ifndef SYNC_MONITORING_NOTES
#define SYNC_MONITORING_NOTES        (false)
#endif

#ifndef DEFAULT_REPORT_INTERVAL
#define DEFAULT_REPORT_INTERVAL (0)
#endif

#ifndef DEFAULT_POLL_ENVIRONMENT_INTERVAL
#define DEFAULT_POLL_ENVIRONMENT_INTERVAL (1000*60*5)
#endif

#ifndef DEFAULT_POLL_SENSORS_INTERVAL
#define DEFAULT_POLL_SENSORS_INTERVAL (1000*2)
#endif

#ifndef APP_NAME
#define APP_NAME    "nf36"
#endif

#ifndef BATTERY_LOW_VOLTAGE
#define BATTERY_LOW_VOLTAGE (3.2)
#endif

#ifndef DEFAULT_TILT_ALERT_DEGREES
#define DEFAULT_TILT_ALERT_DEGREES (10)
#endif

#ifndef DEFAULT_WAIT_BEFORE_SLEEP_MS
#define DEFAULT_WAIT_BEFORE_SLEEP_MS (1000*5)
#endif

#ifndef DEFAULT_SLEEP_PERIOD_MS
#define DEFAULT_SLEEP_PERIOD_MS (1000*60*5)
#endif


/**
 * @brief Outbound queue for notifications about environment variable changes
 */
#define DATA_FILE_NOTIFY "notify.qo"

/**
 * @brief Outbound queue for vending events.
 */
#define DATA_FILE_VENDING "vending.qo"

/**
 * @brief Outbound queue for power notifications.
 */
#define DATA_FILE_POWER "power.qo"

/**
 * @brief Outbound queue for alerts regarding the vending machine.
 */
#define DATA_FILE_ALERT "alert.qo"

#define DATA_FIELD_APP "app"
#define DATA_FIELD_ACTIVE "active"
#define DATA_FIELD_COL "col"
#define DATA_FIELD_NAME "name"
#define DATA_FIELD_STATE "state"
#define DATA_FIELD_TEXT "text"
#define DATA_FIELD_CHANGES "changes"
#define DATA_FIELD_DISTANCE "dist_mm"
#define DATA_FIELD_ITEMS "items"

#define COLUMN_NAME_LEN 32
#define COLUMN_COUNT_MAX 7
/**
 * @brief The pin names corresponding to columns 1-7. The pin for a given column
 * should be connected to the `XSHUT` pin on the ToF sensor for that column.
 * This is not essential, but allows the app to switch off the ToF sensors when running
 * on battery.
 */
static uint8_t COLUMN_PINS[] = { D5, D6, D9, D10, D11, D12, D13 };
static_assert(sizeof(COLUMN_PINS)==COLUMN_COUNT_MAX, "There should be 7 columns max");

#ifndef NOTECARD_SEND_EVENT_TIMEOUT
#define NOTECARD_SEND_EVENT_TIMEOUT 5
#endif

 // forward declarations
struct SodaStack;
class DispensingColumn;


/**
 * @brief Adds a note to a queue, which appears as an event in Notehub.
 *
 * @param body The JSON body of the note.
 * @param file The notefile to add the note to
 * @param sync Whether the event should be synced immediately or sent on the next scheduled
 * sync.
 */
bool sendNotecardEvent(J* body, const char* file, bool sync) {
    J* req = notecard.newRequest("note.add");
    JAddStringToObject(req, "file", file);
    JAddItemToObject(req, "body", body);
    JAddStringToObject(body, DATA_FIELD_APP, APP_NAME);
    if (sync) {
        JAddBoolToObject(req, "sync", true);
    }
    return notecard.sendRequestWithRetry(req, NOTECARD_SEND_EVENT_TIMEOUT);
}

/**
 * @brief Describes an alert condition, which may be active or inactive.
 */
class Alert {

    bool active;
    const char* activeText;
    const char* inactiveText;

protected:
    virtual void buildEvent(J* body) {
        JAddStringToObject(body, DATA_FIELD_TEXT, getText());
        JAddBoolToObject(body, DATA_FIELD_ACTIVE, isActive());
    }

public:
    Alert(const char* active, const char* inactive) :
        active(false), activeText(active), inactiveText(inactive)
    {}

    bool setActive(bool active) {
        bool changed = this->active != active;
        if (changed) {
            this->active = active;
            sendEvent();
        }
        return changed;
    }

    bool isActive() { return active; }

    virtual const char* getText() {
        return active ? activeText : inactiveText;
    }

    virtual void sendEvent() {
        J* body = JCreateObject();
        buildEvent(body);
        sendNotecardEvent(body, DATA_FILE_ALERT, true);
    }
};

/**
 * @brief Describes an alert related to a dispensing column.
 */
class ColumnAlert : public Alert {

    DispensingColumn& col;

    virtual void buildEvent(J* event);

public:
    ColumnAlert(DispensingColumn& col, const char* active, const char* inactive)
    : Alert(active, inactive), col(col) {}
};

bool isTOfSensorReady(VL53L4CD& sensor) {
    uint8_t ready = 0;
    uint8_t error = sensor.VL53L4CD_CheckForDataReady(&ready);
    return !error && ready;
}

/**
 * @brief Reads the Time-of-flight distance sensor.
 *
 * @param sensor    The sensor to read
 * @param results   The results of the read
 * @return Zero on success, an error code otherwise, such as `VL53L4CD_ERROR_TIMEOUT`.
 */
int readToFSensor(VL53L4CD& sensor, VL53L4CD_Result_t& results) {
    sensor.VL53L4CD_ClearInterrupt();
    return sensor.VL53L4CD_GetResult(&results);
}

/**
 * @brief Describes a stack of soda cans, such as the distance to the first can for a full stack,
 * the height of each can, the distance to the low marker and distance when empty.
 */
struct SodaStack {
    /**
     * @brief The height in mm of each soda can being dispensed in mm.
     */
    uint16_t canHeight;

    /**
     * @brief The maximum distance to the first can when the stack is full in mm.
     */
    uint16_t fullDistance;

    /**
     * @brief The minimum distance to the first can when stack is
     * running low on cans.
     */
    uint16_t lowDistance;

    /**
     * @brief The distance to the base of the stack. When the measured distance
     * is greater than or equal to this value, the stack is considered empty.
     */
    uint16_t emptyDistance;

    SodaStack() {
        memset(this, 0, sizeof(*this));
    }

    bool isSet() {
        return emptyDistance && fullDistance;
    }
};

#define ITEM_COUNT_UNKNOWN (-1)

/**
 * @brief The state of a dispensing stack in the vending machine.
 * Provides the FillState and the number of items.
 */
struct DispensingItems {
    enum FillState {
        COLUMN_OFFLINE,
        COLUMN_EMPTY,
        COLUMN_LOW,        // wanted to use LOW but clashes with `LOW` #define from Arduino
        COLUMN_GOOD,
        COLUMN_FULL,
    } fillState;

    int16_t     itemCount;

    void reset() {
        fillState = COLUMN_OFFLINE;
        itemCount = ITEM_COUNT_UNKNOWN;
    }

    bool isOnline() const { return fillState!=COLUMN_OFFLINE; }

    static const char* toString(FillState state) {
        switch (state) {
            case COLUMN_OFFLINE: return "offline";
            case COLUMN_EMPTY: return "empty";
            case COLUMN_LOW: return "low";
            case COLUMN_GOOD: return "good";
            case COLUMN_FULL: return "full";
            default: return "";
        }
    }

    const char* fillStateString() const {
        return toString(fillState);
    }

};

#define TEXT_SENSOR_OFFLINE "Distance sensor offline"
#define TEXT_SENSOR_ONLINE "Distance sensor online"
#define DISTANCE_SENSOR_OFFLINE_TIMEOUT_MS (10*1000)

/**
 * @brief Monitors the state of a dispensing column. Reads the distance sensor and determines
 * the column state DispensingItems using the SodaStack distances. Sends an alert when
 * the sensor goes offline, and clears the alert when it comes online.
 */
class DispensingColumn {

    SodaStack columnSize;

    /**
     * @brief The most recent published state.
     */
    DispensingItems items;

    /**
     * @brief The 1-based column number for this column.
     */
    const uint8_t column;

    /**
     * @brief The column name for this column.
     */
    char name[COLUMN_NAME_LEN];

    /**
     * @brief Determine if this dispensing stack is enabled. When not enabled, the ToF sensor
     * is powered down to conserve power.
     */
    bool enabled;

    /**
     * @brief The I2C address of the ToF sensor.
     */
    const uint8_t sensorAddress;

    /**
     * @brief The ToF distance sensor
     */
    VL53L4CD distanceSensor;

    /**
     * @brief The most recently sampled distance
     */
    VL53L4CD_Result_t distance;

    /**
     * @brief An alert for the ToF sensor going offline.
     */
    ColumnAlert sensorOfflineAlert;

    /**
     * @brief The time of the last successful read from the distance sensor.
     * Used to set an alert when the sensor is offline. When non-zero, indicates that the sensor
     * has been successfully initialized.
     */
    uint32_t lastSuccessfulRead;
    /**
     * @brief When false, the sensor needs initializing.
     */
    bool sensorOnline;

    void reset() {
        memset(&distance, 0, sizeof(distance));
        items.reset();
    }

    /**
     * @brief Update the enabled state of this column.
     * 
     * @param enabled 
     */
    void updateEnabled(bool enabled) {
        this->enabled = enabled;
        if (!enabled) { // sensor is initialized as part of read to handle intermittent bus failures
            reset();
            deinitializeDistanceSensor();
            sensorOnline = false;
        }
    }

    int initializeDistanceSensor() {
        // Configure VL53L4CD satellite component.
        int status = distanceSensor.begin();

        //Initialize VL53L4CD satellite component.
        status = status || distanceSensor.InitSensor(sensorAddress);

        // Program the highest possible TimingBudget, without enabling the
        // low power mode. This should give the best accuracy
        status = status || distanceSensor.VL53L4CD_SetRangeTiming(200, 0);

        // Start Measurements
        status = status || distanceSensor.VL53L4CD_StartRanging();
        return status;
    }

    int deinitializeDistanceSensor() {
        distanceSensor.VL53L4CD_StopRanging();
        distanceSensor.VL53L4CD_Off();
        return 0;
    }

    /**
     * @brief Update the state of this column from the measured distance.
     */
    void updateColumnState() {
        DispensingItems prevItems(items);   // save the current state

        DispensingItems::FillState fillState = DispensingItems::COLUMN_OFFLINE;
        int itemCount;
        if (sensorOnline && !distance.range_status && columnSize.isSet()) {
            uint16_t d = distance.distance_mm;
            uint16_t halfItem = columnSize.canHeight/2;
            // this assumes the ToF sensor is placed at the top of the column. As the column
            // empties, the distance to the first can increases.
            if (d <= columnSize.fullDistance+halfItem) {
                fillState = DispensingItems::COLUMN_FULL;
            }
            else if (d >= columnSize.emptyDistance-halfItem) {
                fillState = DispensingItems::COLUMN_EMPTY;
            }
            else if (d >= columnSize.lowDistance+halfItem) {
                fillState = DispensingItems::COLUMN_LOW;
            }
            else {
                fillState = DispensingItems::COLUMN_GOOD;
            }
            if (columnSize.canHeight) {
                if (columnSize.emptyDistance > distance.distance_mm) {
                    // add half a can so that the nominal point is in the middle of the can
                    // This prevents jitter from creating false readings.
                    itemCount = (columnSize.emptyDistance-distance.distance_mm+halfItem) / columnSize.canHeight;
                }
                else {
                    itemCount = 0; // womp womp, no more soda in this column
                }
            }
            else {
                itemCount = ITEM_COUNT_UNKNOWN;
            }
        }

        items.itemCount = itemCount;
        items.fillState = fillState;

        if (items.itemCount != prevItems.itemCount || items.fillState != prevItems.fillState) {
            columnStateChanged(prevItems, items);
        }
    }

    /**
     * @brief Adds fields to the body of an event for the state of this column.
     * 
     * @param items The state of the dispensing items
     * @return J* The created event body
     */
    J* buildColumnEvent(const DispensingItems& items) {
        J* body = JCreateObject();
        JAddNumberToObject(body, DATA_FIELD_COL, columnNumber());
        JAddStringToObject(body, DATA_FIELD_NAME, columnName());
        JAddStringToObject(body, DATA_FIELD_STATE, items.fillStateString());
        if (items.isOnline()) {
            JAddNumberToObject(body, DATA_FIELD_DISTANCE, this->distance.distance_mm);
            JAddNumberToObject(body, DATA_FIELD_ITEMS, items.itemCount);
        }
        return body;
    }

    /**
     * @brief Creates and sends and event about the current column state, and what changed
     *  since the last reported event.
     * 
     * @param prev      The previous column state
     * @param current   The current column state
     */
    void columnStateChanged(const DispensingItems& prev, const DispensingItems& current) {
        char changed[128];
        *changed = 0;
        J* body = buildColumnEvent(current);
        if (current.isOnline() && prev.itemCount != current.itemCount) {
            strncat(changed, ",items", sizeof(changed)-1);
        }
        if (prev.fillState != current.fillState) {
            strncat(changed, ",state", sizeof(changed)-1);
        }
        if (*changed) {
            JAddStringToObject(body, DATA_FIELD_CHANGES, changed+1);    // skip the initial comma
        }

        if (prev.isOnline() && current.isOnline()) {
            // add a text field with human-readable change details
            const char* text = nullptr;
            if (prev.fillState != DispensingItems::COLUMN_FULL && current.fillState==DispensingItems::COLUMN_FULL) {
                text = "Column restocked.";
            }
            else if (prev.itemCount < current.itemCount) {
                text = "Item dispensed.";
            }
            if (text) {
                JAddStringToObject(body, DATA_FIELD_TEXT, text);
            }
        }
        sendNotecardEvent(body, DATA_FILE_VENDING, true);
    }

public:
    /**
     * @brief The next column in the chain. This is used to maintain a linked list of columns.
     */
    DispensingColumn* next;

    /**
     * @brief Construct a new Dispensing Column object.
     * 
     * @param i2c           The i2c instance to use for communicating with the ToF sensor.
     * @param i2cAddress    The i2c address of the ToF sensor.
     * @param xshut_pin     The `XSHUT` pin for the sensor, which is used to switch the sensor
     *  on and off.
     * @param column        The 1-based column ID for this column.
     */
    DispensingColumn(TwoWire& i2c, uint8_t i2cAddress, uint8_t xshut_pin, uint8_t column) :
        enabled(false),
        sensorAddress(i2cAddress),
        distanceSensor(&i2c, xshut_pin),
        next(nullptr),
        column(column),
        sensorOfflineAlert(*this, TEXT_SENSOR_OFFLINE, TEXT_SENSOR_ONLINE),
        lastSuccessfulRead(0), sensorOnline(false)
    {
        items.reset();
        setName(nullptr);   // use the default name until configured by the environment
    }

    ~DispensingColumn() {
        updateEnabled(false);
        distanceSensor.end();
    }

    const char* columnName() const { return this->name; }
    void setName(const char* newName) {
        if (newName && *newName) {
            strncpy(name, newName, sizeof(name));
        }
        else {
            // set the default column name
            snprintf(name, sizeof(name), "col%d", column);
        }
    }

    uint8_t columnNumber() const { return this->column; }
    const DispensingItems& currentItems() const { return items; }
    SodaStack& size() { return this->columnSize; }

    bool isEnabled() { return enabled; }
    bool setEnabled(bool enabled) {
        bool changed = (enabled!=this->enabled);
        if (changed) {
            updateEnabled(enabled);
        }
        return changed;
    }

    bool readDistance() {
        if (!enabled) {     // nothing to do
            return false;
        }

        memset(&distance, 0, sizeof(distance));

        if (!sensorOnline) {
            if (!initializeDistanceSensor()) {
                sensorOnline = true;
                debug.printf("col %d: ToF sensor initialized.", columnNumber());
                debug.println();
            }
        }

        int error = 1;
        if (sensorOnline) {
            if (isTOfSensorReady(distanceSensor)) {
                error = readToFSensor(distanceSensor, distance);
            }
        }
        if (!error && distance.distance_mm) {
            lastSuccessfulRead = millis();
        }

        bool offlineTimeout = (millis()-lastSuccessfulRead)>=DISTANCE_SENSOR_OFFLINE_TIMEOUT_MS;
        bool alertChanged = sensorOfflineAlert.setActive(offlineTimeout);
        if (alertChanged && offlineTimeout) {
            sensorOnline = false;
            deinitializeDistanceSensor();
            debug.printf("col %d: alert. ToF sensor offline.", columnNumber());
            debug.println();
        }
        if (sensorOnline || alertChanged) {
            updateColumnState();
        }
        return !error;
    }

    bool sendMonitoringEvent() {
        J* body = buildColumnEvent(items);
        return sendNotecardEvent(body, DATA_FILE_VENDING, SYNC_MONITORING_NOTES);
    }
};

/**
 * @brief Adds the fields for a column alert event.
 * 
 * @param event The JSON body of the event to add to.
 */
void ColumnAlert::buildEvent(J* event) {
    Alert::buildEvent(event);
    JAddNumberToObject(event, DATA_FIELD_COL, col.columnNumber());
    JAddStringToObject(event, DATA_FIELD_NAME, col.columnName());
}


/**
 * @brief AuxSerial notifications are received over TX/RX, aka Serial1
 */
auto& auxSerialStream = Serial1;

/*
 * Create the aux-serial manager over TX/RX
 */
NotecardAuxSerial auxSerial(auxSerialStream);

NotecardTiltSensor tiltSensor(auxSerial);

NotecardSleep notecardSleep(DEFAULT_WAIT_BEFORE_SLEEP_MS, DEFAULT_SLEEP_PERIOD_MS);

/**
 * @brief A linked list of DispensingColumn instances.
 */
DispensingColumn* columns = nullptr;

// Application alerts
Alert batteryLowAlert("battery low", "");
Alert tiltAlert("excessive tilt", "normal tilt");
Alert powerAlert("power failure", "power restored");

uint32_t tiltAlertDegrees = DEFAULT_TILT_ALERT_DEGREES;
double notecardVoltage;
bool usbPower;

uint32_t readSensorsInterval = DEFAULT_POLL_SENSORS_INTERVAL;
uint32_t readSensorsMs;
uint32_t sendReportInterval = DEFAULT_REPORT_INTERVAL;
uint32_t sendReportMs;
uint32_t pollEnvironmentInterval = DEFAULT_POLL_ENVIRONMENT_INTERVAL;
uint32_t pollEnvironmentMs;
int64_t environmentModifiedTime;

/**
 * @brief Get the dispensing column count
 * 
 * @return int THe number of dispensing columns presently configured.
 */
int getDispensingColumnCount() {
    int count = 0;
    for (DispensingColumn* column = columns; column; column = column->next) {
        count++;
    }
    return count;
}

/**
 * @brief Set the Dispensing Column count. Any existing monitored columns are removed.
 * @param count The number of dispensing columns to monitor. When 0, no columns are monitored.
 */
void initializeDispensingColumns(uint8_t count) {
    if (count) {
        debug.printf("initializing dispensing columns (count=%d)", count);
        debug.println();
        // clear out any current columns
        initializeDispensingColumns(0);

        uint8_t i2cAddress = 0x53;      // start with the default address, and increase for subsequent columns
        uint8_t col = 0;

        if (count > COLUMN_COUNT_MAX) {
            count = COLUMN_COUNT_MAX;
        }

        while (count --> 0) {
            DispensingColumn* column = new DispensingColumn(Wire, i2cAddress++, COLUMN_PINS[col], ++col);
            column->next = columns;
            columns = column;
        }
    }
    else {
        for (DispensingColumn* column = columns; column; ) {
            DispensingColumn* next = column->next;
            delete column;
            column = next;
        }
        columns = nullptr;
    }
}

/**
 * @brief Sets the power state of the app. When powered by battery, all dispensing columns
 * are disabled, and tilt sensing is deactivated.
 * 
 * @param powered 
 */
void powerStateChanged(bool powered) {
    // enable/disable aux.serial
    auxSerial.setEnabled(powered);
    tiltSensor.setEnabled(usbPower);

    // disable/enable monitoring of all dispensing columns
    for (DispensingColumn* column = columns; column != nullptr; column = column->next) {
        column->setEnabled(powered);
    }

    powerAlert.setActive(!usbPower);
    notecardSleep.setEnabled(!powered);
}

/**
 * @brief Sends monitoring events for all the columns.
 * 
 */
void sendReport() {
    if (!usbPower)
        return;

    for (DispensingColumn* column = columns; column; column = column->next) {
        column->sendMonitoringEvent();
    }
}

/**
 * @brief Updates notecardVoltage, usbPower values by sending a `card.voltage` request.
 * 
 * @return true If the request was successful 
 * @return false 
 */
bool readNotecardVoltage() {
    J* req = notecard.newRequest("card.voltage");
    J* rsp = notecard.requestAndResponse(req);
    if (rsp && !NoteResponseError(rsp)) {
        double voltage = JGetNumber(rsp, "value");
        if (voltage<0.1) // i.e 0
            return false;
        usbPower = JGetBool(rsp, "usb");
        notecard.deleteResponse(rsp);

        // if (notecardVoltage >= 4.5) {
        //     usbPower = true;
        // }
    }
    return rsp;
}

/**
 * @brief The tilt sensor is read more frequently, so is pulled out to it's own function.
 * 
 */
void readTiltSensor() {
    bool tilted = tiltAlertDegrees && tiltSensor.isTiltSensed() && (tiltSensor.angleFromNormal()*180/PI)>tiltAlertDegrees;
    tiltAlert.setActive(tilted);
}

void readSensors() {
    bool wasPowered = usbPower;
    if (readNotecardVoltage()) {
        if (wasPowered!=usbPower) {
            powerStateChanged(usbPower);
        }
    }

    if (usbPower) {
        batteryLowAlert.setActive(false);

        readTiltSensor();

        for (DispensingColumn* column = columns; column; column = column->next) {
            if (!column->isEnabled())
                continue;
            column->readDistance();
        }
    }
    else {
        if (notecardVoltage < BATTERY_LOW_VOLTAGE) {
            batteryLowAlert.setActive(true);
        }
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

/**
 * @brief Adds a notification to a JSON object about changes to the environment.
 *
 * @param changed   The JSON object that the notification is added to.
 * @param name      The name of the environment variable that changed.
 * @param oldValue  The previous value of the variable.
 * @param newValue  The current value of the variable.
 * @return J*       The notification that was added.
 */
J* addEnvironmentChangedNotification(J* changed, const char* name, J* oldValue, J* newValue) {
    J* item = JCreateObject();
    if (oldValue) {
        JAddItemToObject(item, "old_value", oldValue);
    }
    if (newValue) {
        JAddItemToObject(item, "new_value", newValue);
    }
    JAddItemToObject(changed, name, item);
    return item;
}

J* addEnvironmentChangedNotification(J* changed, const char* name, double oldValue, double newValue) {
    return addEnvironmentChangedNotification(changed, name, JCreateNumber(oldValue), JCreateNumber(newValue));
}

J* notifyEnvironmentError(J* errors, const char* name, const char* text, const char* value) {
    J* item = JCreateObject();
    JAddStringToObject(item, "error", text);
    JAddStringToObject(item, "value", value);
    JAddItemToObject(errors, name, item);
    return item;
}

void setIntValueFromEnvironment(J* env, J* changed, J* error, uint32_t* result, const char* envVar, uint32_t multiplier, uint32_t defaultValue) {
    const char* value_str = JGetString(env, envVar);
    uint32_t value = defaultValue;
    if (value_str && *value_str) {    // variable is defined
        char *endptr;
        long env_value = strtol(value_str, &endptr, 10);
        if (*endptr || env_value<0) {
            notifyEnvironmentError(error, envVar, "not a valid whole positive number.", value_str);
        }
        else {
            value = env_value * multiplier;
            if (*result!=value) {
                addEnvironmentChangedNotification(changed, envVar, *result/multiplier, value);
            }
        }
    }
    *result = value;
}

#define ENV_COL_NAME_FMT "col_%d_name"
#define ENV_COL_EMPTY_FMT "col%sempty_mm"
#define ENV_COL_LOW_FMT "col%slow_mm"
#define ENV_COL_FULL_FMT "col%sfull_mm"
#define ENV_COL_ITEM_FMT "col%sitem_mm"

SodaStack defaultColumnSize;
const SodaStack INITIAL_STACK_SIZE; // zero as the default

void updateSize(J* changed, J* errors, J* env, const char* fmt, const char* colId, uint16_t& newValue, uint16_t defaultValue) {
    // first refresh the common column definition
    char varName[32];
    snprintf(varName, sizeof(varName), fmt, colId);
    uint32_t value = newValue;
    setIntValueFromEnvironment(env, changed, errors, &value, varName, 1, defaultValue);
    if (value>=2000) {
        notifyEnvironmentError(errors, varName, "Distance must be a positive integer less than 2000mm", JGetString(env, varName));
    }
    else {
        newValue = (uint16_t)value;
    }
}

void environmentUpdateColumnSize(J* changed, J* errors, J* env, const char* colId, SodaStack& newSize, const SodaStack& defaultSize) {
    updateSize(changed, errors, env, ENV_COL_EMPTY_FMT, colId, newSize.emptyDistance,defaultSize.emptyDistance);
    updateSize(changed, errors, env, ENV_COL_LOW_FMT, colId, newSize.lowDistance, defaultSize.lowDistance);
    updateSize(changed, errors, env, ENV_COL_FULL_FMT, colId, newSize.fullDistance, defaultSize.fullDistance);
    updateSize(changed, errors, env, ENV_COL_ITEM_FMT, colId, newSize.canHeight, defaultSize.canHeight);
}

void environmentUpdateDispensingColumn(J* changed, J* errors, J* env, DispensingColumn* column) {
    char buf[32];
    snprintf(buf, sizeof(buf), ENV_COL_NAME_FMT, column->columnNumber());
    const char* name = JGetString(env, buf);
    String prevName(column->columnName());
    column->setName(name);
    if (strcmp(prevName.c_str(), column->columnName())) {
        addEnvironmentChangedNotification(changed, buf, JCreateString(prevName.c_str()), JCreateString(column->columnName()));
    }
    snprintf(buf, sizeof(buf), "_%d_", column->columnNumber());
    environmentUpdateColumnSize(changed, errors, env, buf, column->size(), defaultColumnSize);
}

#define ENV_COL_COUNT "col_count"
#define ENV_TILT_ALERT_DEG "tilt_alert_deg"

/**
 * @brief Handles updates to the environment for the app's tasks and report intervals.
 */
void environmentUpdated(J* env) {
    J* changed = JCreateObject();
    J* errors = JCreateObject();

    setIntValueFromEnvironment(env, changed, errors, &pollEnvironmentInterval, "environment_update_mins", 1000*60, DEFAULT_POLL_ENVIRONMENT_INTERVAL);
    setIntValueFromEnvironment(env, changed, errors, &readSensorsInterval, "monitor_secs", 1000, DEFAULT_POLL_SENSORS_INTERVAL);
    setIntValueFromEnvironment(env, changed, errors, &sendReportInterval, "report_mins", 1000*60, DEFAULT_REPORT_INTERVAL);

    uint32_t col_count = getDispensingColumnCount();
    uint32_t new_count = col_count;
    setIntValueFromEnvironment(env, changed, errors, &new_count, ENV_COL_COUNT,1, 1);
    if (new_count != col_count) {
        if (new_count > COLUMN_COUNT_MAX) {
            notifyEnvironmentError(errors, ENV_COL_COUNT, "Too many columns.", JGetString(env, ENV_COL_COUNT));
        }
        else {
            initializeDispensingColumns(new_count);
        }
    }

    setIntValueFromEnvironment(env, changed, errors, &tiltAlertDegrees, ENV_TILT_ALERT_DEG, 1, DEFAULT_TILT_ALERT_DEGREES);

    // update the default column size
    environmentUpdateColumnSize(changed, errors, env, "_", defaultColumnSize, INITIAL_STACK_SIZE);

    for (DispensingColumn* column = columns; column; column = column->next) {
        environmentUpdateDispensingColumn(changed, errors, env, column);
    }

    // publish the updates
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
    pinMode(LED_BUILTIN, OUTPUT);
    while (!debug) {};
    debug.begin(115200);
    debug.println("*** " __DATE__ " " __TIME__ " ***");

    // Initialize I2C
    Wire.begin();

    // Initialize Notecard library (without doing any I/O on this task)
    notecard.setDebugOutputStream(debug);
    notecard.begin();

    // todo - add voltage-variable inbound/outbound
    J* req = notecard.newRequest("card.voltage");
    JAddBoolToObject(req, "usb", true);
    JAddBoolToObject(req, "alert", true);   // add a note to health.qo
    JAddStringToObject(req, "mode", "lipo");
    notecard.sendRequest(req);

    // todo - 
    // "voutbound": "usb:30;high:60;normal:90;low:120;dead:0",
    // "vinbound": "usb:60;high:120;normal:240;low:480;dead:0
    req = notecard.newRequest("hub.set");
    JAddStringToObject(req, "product", PRODUCT_UID);
    JAddStringToObject(req, "mode", "continuous");
    JAddBoolToObject(req, "sync", true);
    notecard.sendRequest(req);

    auxSerialStream.begin(auxSerial.baudRate);

    pollEnvironmentMs = millis();
    readEnvironment();

    if (!readNotecardVoltage()) {
        // assume battery power until we can contact Notecard
        usbPower = false;
        notecardVoltage = 3.3;
    }
    // force initialization according to the power state.
    powerStateChanged(usbPower);
}

bool sync() {
    J* req = notecard.newRequest("hub.sync");
    return notecard.sendRequest(req);
}


void printReport(VL53L4CD_Result_t& results, Stream& output) {
    char report[64];
    snprintf(report, sizeof(report), "Status = %3u, Distance = %5u mm, Signal = %6u kcps/spad",
             results.range_status,
             results.distance_mm,
             results.signal_per_spad_kcps);
    output.println(report);
}

void loop()
{
    if (auxSerial.isEnabled()) {
        auxSerial.poll();
    }

    notecardSleep.poll();

    uint32_t now = millis();
    bool sensorsRead = false;
    if (readSensorsInterval && (now-readSensorsMs)>=readSensorsInterval) {
        digitalWrite(LED_BUILTIN, HIGH);
        readSensorsMs = now;
        sensorsRead = true;
        readSensors();
        digitalWrite(LED_BUILTIN, LOW);
    }

    if (sendReportInterval && (now-sendReportMs)>=sendReportInterval) {
        sendReportMs = now;
        if (!sensorsRead) {
            readSensors();
        }
        sendReport();
    }

    if (pollEnvironmentInterval && (now-pollEnvironmentMs)>=pollEnvironmentInterval) {
        pollEnvironmentMs = now;
        debug.println("checking environment...");
        readEnvironment();
    }

    // // allow manual triggering of tasks
    // int c = Serial.read();
    // switch (c) {
    //     case 's': sync(); break;
    // }
}
