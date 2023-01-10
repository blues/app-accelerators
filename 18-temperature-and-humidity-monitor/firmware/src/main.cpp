//
// Copyright 2022 Blues Inc.  All rights reserved.
// Use of this source code is governed by licenses granted by the
// copyright holder including that found in the LICENSE file.
//

#include <Notecard.h>
#include <Wire.h>
#include <SparkFunBME280.h>

// Uncomment this line and replace com.your-company:your-product-name with your
// ProductUID.
// #define PRODUCT_UID "com.your-company:your-product-name"

#ifndef PRODUCT_UID
#define PRODUCT_UID "" // "com.my-company.my-name:my-project"
#pragma message "PRODUCT_UID is not defined in this example. Please ensure your Notecard has a product identifier set before running this example or define it in code here. More details at https://bit.ly/product-uid"
#endif

// Sync outbound notes to Notehub a minimum of every 3 minutes.
#define OUTBOUND_SYNC_MINS 3
// Check for environment variable changes every 30 seconds.
#define ENV_POLL_MS (30 * 1000)
// After triggering an alarm, raise the alarm every minute after that.
#define ALARM_REFRESH_MS (60 * 1000)
// Read the sensor every minute.
#define SENSOR_READ_INTERVAL_MS (60 * 1000)
// Publish temperature and humidity every 3 minutes.
#define MONITOR_INTERVAL_DEFAULT_MS (180 * 1000)
// Temperature in C.
#define TEMPERATURE_MIN_DEFAULT 5
#define TEMPERATURE_MAX_DEFAULT 33
// Relative humidity.
#define HUMIDITY_MIN_DEFAULT 20
#define HUMIDITY_MAX_DEFAULT 70

struct AppState {
    uint32_t lastEnvVarPollMs;
    uint32_t envLastModTime;
    uint32_t monitorIntervalMs;
    uint32_t monitorLastUpdateMs;
    uint32_t sensorLastReadMs;
    uint32_t lastAlarmMs;
    float temperatureMin;
    float temperatureMax;
    float humidityMin;
    float humidityMax;
    float temperature;
    float humidity;
};

AppState state = {0};
Notecard notecard;
BME280 bmeSensor;

// Add a note with temperature and humidity data to data.qo. The note will be
// synced to Notehub according to the interval set with hub.set's outbound
// parameter in the setup function (see OUTBOUND_SYNC_MINS).
void publishSystemStatus()
{
    J *req = notecard.newRequest("note.add");
    if (req != NULL) {
        JAddStringToObject(req, "file", "data.qo");

        J *body = JCreateObject();
        if (body != NULL) {
            JAddNumberToObject(body, "temperature", state.temperature);
            JAddNumberToObject(body, "humidity", state.humidity);            
            JAddItemToObject(req, "body", body);

            notecard.sendRequest(req);
        }
        else {
            notecard.logDebug("Failed to create body for system status "
                "update.\n");
        }
    }
    else {
        notecard.logDebug("Failed to create note.add request for system status "
            "update.\n");        
    }
}

// Check for environment variable changes. Returns true if there are changes and
// false otherwise.
bool pollEnvVars()
{
    uint32_t currentMs = millis();

    // Don't check for changes unless its time to do so.
    if (currentMs - state.lastEnvVarPollMs < ENV_POLL_MS) {
        return false;
    }

    state.lastEnvVarPollMs = currentMs;

    J *rsp = notecard.requestAndResponse(notecard.newRequest("env.modified"));
    if (rsp == NULL) {
        notecard.logDebug("NULL response to env.modified.\n");
        return false;
    }

    uint32_t modifiedTime = JGetInt(rsp, "time");
    notecard.deleteResponse(rsp);
    // If the last modified timestamp is the same as the one we've got saved,
    // there have been no changes.
    if (state.envLastModTime == modifiedTime) {
        return false;
    }

    state.envLastModTime = modifiedTime;

    notecard.logDebug("Environment variable changed detected.\n");

    return true;
}

// Returns true if any variables were updated and false otherwise.
bool fetchEnvVars()
{
    bool updated = false;
    J *req = notecard.newRequest("env.get");
    J *names = JAddArrayToObject(req, "names");
    JAddItemToArray(names, JCreateString("monitor_interval"));
    JAddItemToArray(names, JCreateString("temperature_threshold_min"));
    JAddItemToArray(names, JCreateString("temperature_threshold_max"));
    JAddItemToArray(names, JCreateString("humidity_threshold_min"));
    JAddItemToArray(names, JCreateString("humidity_threshold_max"));

    J *rsp = notecard.requestAndResponse(req);
    if (rsp != NULL) {
        if (notecard.responseError(rsp)) {
            notecard.logDebug("Error in env.get response.\n");
        }
        else {
            J *body = JGetObject(rsp, "body");
            if (body != NULL) {
                char *valueStr = JGetString(body, "monitor_interval");
                float value;
                char *endPtr;

                if (valueStr != NULL && strlen(valueStr) > 0) {
                    value = strtof(valueStr, &endPtr);
                    if ((value == 0 && valueStr == endPtr) || value < 0) {
                        notecard.logDebugf("Failed to convert %s to positive "
                            "float for monitor interval.\n", valueStr);
                    }
                    else {
                        state.monitorIntervalMs = (uint32_t)(value * 1000);
                        updated = true;
                        notecard.logDebugf("Monitor interval set to %u ms.\n",
                            state.monitorIntervalMs);
                    }
                }

                valueStr = JGetString(body, "temperature_threshold_min");
                if (valueStr != NULL && strlen(valueStr) > 0) {
                    value = strtof(valueStr, &endPtr);
                    if ((value == 0 && valueStr == endPtr) || value < 0) {
                        notecard.logDebugf("Failed to convert %s to positive "
                            "float for temperature min.\n", valueStr);
                    }
                    else {
                        state.temperatureMin = value;
                        updated = true;
                        notecard.logDebugf("Temperature min set to %sC.\n",
                            valueStr);
                    }
                }

                valueStr = JGetString(body, "temperature_threshold_max");
                if (valueStr != NULL && strlen(valueStr) > 0) {
                    value = strtof(valueStr, &endPtr);
                    if ((value == 0 && valueStr == endPtr) || value < 0) {
                        notecard.logDebugf("Failed to convert %s to positive "
                            "float for temperature max.\n", valueStr);
                    }
                    else {
                        state.temperatureMax = value;
                        updated = true;
                        notecard.logDebugf("Temperature max set to %sC.\n",
                            valueStr);
                    }
                }

                valueStr = JGetString(body, "humidity_threshold_min");
                if (valueStr != NULL && strlen(valueStr) > 0) {
                    value = strtof(valueStr, &endPtr);
                    if ((value == 0 && valueStr == endPtr) || value < 0) {
                        notecard.logDebugf("Failed to convert %s to positive "
                            "float for humidity min.\n", valueStr);
                    }
                    else {
                        state.humidityMin = value;
                        updated = true;
                        notecard.logDebugf("Humidity min set to %s%%.\n",
                            valueStr);
                    }
                }

                valueStr = JGetString(body, "humidity_threshold_max");
                if (valueStr != NULL && strlen(valueStr) > 0) {
                    value = strtof(valueStr, &endPtr);
                    if ((value == 0 && valueStr == endPtr) || value < 0) {
                        notecard.logDebugf("Failed to convert %s to positive "
                            "float for humidity max.\n", valueStr);
                    }
                    else {
                        state.humidityMax = value;
                        updated = true;
                        notecard.logDebugf("Humidity max set to %s%%.\n",
                            valueStr);
                    }
                }
            }
            else {
                notecard.logDebug("NULL body in response to env.get request."
                    "\n");
            }
        }

        notecard.deleteResponse(rsp);
    }
    else {
        notecard.logDebug("NULL response to env.get request.\n");
    }

    return updated;
}

// Used in the main loop to check for and accept environment variable updates.
// If any variables are updated, this function sends a note to Notehub to
// acknowledge the update.
void updateEnvVars()
{
    if (pollEnvVars()) {
        if (fetchEnvVars()) {
            J *req = notecard.newRequest("note.add");
            if (req != NULL) {
                JAddStringToObject(req, "file", "notify.qo");
                JAddBoolToObject(req, "sync", true);

                J *body = JCreateObject();
                if (body != NULL) {
                    JAddStringToObject(body, "message", "environment variable "
                        "update received");
                    JAddItemToObject(req, "body", body);
                    notecard.sendRequest(req);
                }
                else {
                    notecard.logDebug("Failed to create note body for env var "
                        "update ack.\n");
                }
            }
            else {
                notecard.logDebug("Failed to create note.add request for env "
                    "var update ack.\n");
            }
        }
    }
}

// One-time Arduino initialization.
void setup()
{
    // Set up debug output via serial connection.
    delay(2500);
    Serial.begin(115200);
    notecard.setDebugOutputStream(Serial);

    // Initialize the physical I/O channel to the Notecard.
    Wire.begin();
    notecard.begin();

    if (!bmeSensor.beginI2C()) {
        notecard.logDebug("bmeSensor.beginI2C() failed.\n");
    } 
    else {
        notecard.logDebug("Connected to BME280.\n");
    }

    // Set defaults.
    state.monitorIntervalMs = MONITOR_INTERVAL_DEFAULT_MS;
    state.temperatureMin = TEMPERATURE_MIN_DEFAULT;
    state.temperatureMax = TEMPERATURE_MAX_DEFAULT;
    state.humidityMin = HUMIDITY_MIN_DEFAULT;
    state.humidityMax = HUMIDITY_MAX_DEFAULT;

    // Configure the productUID and instruct the Notecard to sync outbound notes
    // a minimum of every OUTBOUND_SYNC_MINS minutes.
    J *req = notecard.newRequest("hub.set");
    if (PRODUCT_UID[0]) {
       JAddStringToObject(req, "product", PRODUCT_UID);
    }
    JAddStringToObject(req, "mode", "periodic");
    JAddNumberToObject(req, "outbound", OUTBOUND_SYNC_MINS);
    JAddBoolToObject(req, "sync", true);
    notecard.sendRequest(req);

    // Send a note to _health.qo when USB power is lost or restored.
    req = notecard.newRequest("card.voltage");
    JAddBoolToObject(req, "usb", true);
    JAddBoolToObject(req, "alert", true);
    JAddBoolToObject(req, "sync", true);
    notecard.sendRequest(req);

    fetchEnvVars();
}

void readSensor()
{
    state.temperature = bmeSensor.readTempC();
    state.humidity = bmeSensor.readFloatHumidity();
}

// Construct an alarm note and add it to alarm.qo, syncing it to Notehub
// immediately.
void publishAlarm(const char *temperatureStatus, const char *humidityStatus)
{
    if (temperatureStatus == NULL || humidityStatus == NULL) {
        notecard.logDebug("publishAlarm called with a NULL status.\n");
        return;
    }

    J *req = notecard.newRequest("note.add");
    if (req != NULL) {
        JAddStringToObject(req, "file", "alarm.qo");
        JAddBoolToObject(req, "sync", true);

        J *body = JCreateObject();
        if (body != NULL) {
            J *temperatureBody = JCreateObject();
            if (temperatureBody != NULL){
                JAddNumberToObject(temperatureBody, "value", state.temperature);
                JAddStringToObject(temperatureBody, "status",
                    temperatureStatus);
                JAddItemToObject(body, "temperature", temperatureBody);

                J *humidityBody = JCreateObject();
                if (humidityBody != NULL){
                    JAddNumberToObject(humidityBody, "value", state.humidity);
                    JAddStringToObject(humidityBody, "status", humidityStatus);
                    JAddItemToObject(body, "humidity", humidityBody);
                }
                else {
                    notecard.logDebug("Failed to create humidity body for "
                        "alarm.\n");
                }
            }
            else {
                notecard.logDebug("Failed to create temperature body for "
                    "alarm.\n");
            }

            JAddItemToObject(req, "body", body);

            notecard.sendRequest(req);
        }
        else {
            notecard.logDebug("Failed to create body for alarm.\n");
        }
    }
    else {
        notecard.logDebug("Failed to create note.add request for alarm.\n");        
    }
}

static const char statusOk[] = "ok";
static const char statusLow[] = "low";
static const char statusHigh[] = "high";

// Check if the temperature or humidity is outside the configured range for each
// value. If both are in range, do nothing. If either is out of range, send an
// alarm note to Notehub.
void checkAlarm()
{
    const char *temperatureStatus = statusOk;
    const char *humidityStatus = statusOk;

    if (state.temperature < state.temperatureMin) {
        temperatureStatus = statusLow;
    }
    else if (state.temperature > state.temperatureMax) {
        temperatureStatus = statusHigh;
    }

    if (state.humidity < state.humidityMin) {
        humidityStatus = statusLow;
    }
    else if (state.humidity > state.humidityMax) {
        humidityStatus = statusHigh;
    }

    // If either status is not "ok".
    if (strcmp(temperatureStatus, statusOk) ||
        strcmp(humidityStatus, statusOk)) {
        uint32_t currentMs = millis();
        if (state.lastAlarmMs == 0 || (currentMs - state.lastAlarmMs) >=
            ALARM_REFRESH_MS) {
            state.lastAlarmMs = currentMs;
            publishAlarm(temperatureStatus, humidityStatus);
        }
    }
    else {
        state.lastAlarmMs = 0;
    }
}

void loop()
{
    updateEnvVars();

    uint32_t currentMs = millis();
    if (currentMs - state.sensorLastReadMs >= SENSOR_READ_INTERVAL_MS) {
        state.sensorLastReadMs = currentMs;
        readSensor();
    }

    checkAlarm();

    currentMs = millis();
    if (currentMs - state.monitorLastUpdateMs >= state.monitorIntervalMs) {
        state.monitorLastUpdateMs = currentMs;
        publishSystemStatus();
    }
}
