//
// Copyright 2022 Blues Inc.  All rights reserved.
// Use of this source code is governed by licenses granted by the
// copyright holder including that found in the LICENSE file.
//

#include <stdlib.h>
#include <string.h>

#include <zephyr/drivers/sensor.h>
#include <zephyr/sys/printk.h>
#include <zephyr/device.h>

#include <note.h>
#include <NotecardEnvVarManager.h>

#include "actuator.h"
#include "note_c_hooks.h"

// Uncomment this line and replace com.your-company:your-product-name with your
// ProductUID.
#define PRODUCT_UID "com.blues.eoss"

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
#define REPORT_INTERVAL_DEFAULT_MS (180 * 1000)
// Temperature in C.
#define TEMPERATURE_MIN_DEFAULT 5
#define TEMPERATURE_MAX_DEFAULT 33
// Relative humidity.
#define HUMIDITY_MIN_DEFAULT 20
#define HUMIDITY_MAX_DEFAULT 70

typedef struct AppState {
    uint32_t lastEnvVarPollMs;
    uint32_t envLastModTime;
    uint32_t reportIntervalMs;
    uint32_t reportLastUpdateMs;
    uint32_t sensorLastReadMs;
    uint32_t lastAlarmMs;
    float temperatureMin;
    float temperatureMax;
    float humidityMin;
    float humidityMax;
    float temperature;
    float humidity;
} AppState;

volatile static bool notecard_lock = false;
static const char *envVarNames[] = {
    "report_interval",
    "temperature_threshold_min",
    "temperature_threshold_max",
    "humidity_threshold_min",
    "humidity_threshold_max"
};
AppState state = {0};
NotecardEnvVarManager *envVarManager;
const struct device *const bme280 = DEVICE_DT_GET_ANY(bosch_bme280);

// The environment variable manager callback is called once for each variable
// that has changed.
void envVarManagerCb(const char *var, const char *val, void *ctx) {
    AppState *state = (AppState *)ctx;

    // Convert value string to positive float values
    char *endPtr;
    float value = strtof(val, &endPtr);
    if ((value == 0 && val == endPtr) || value < 0) {
        printk("ERROR: Failed to convert %s to positive "
            "float for %s variable.\n", val, var);
        return;
    }

    // Cache the values for each variable.
    if (strcmp(var, "report_interval") == 0)
    {
        state->reportIntervalMs = (uint32_t)(value * 1000);
        printk("Report interval set to %u ms.\n",
            state->reportIntervalMs);
    }
    else if (strcmp(var, "temperature_threshold_min") == 0)
    {
        state->temperatureMin = value;
        printk("Temperature min set to %sC.\n", val);
    }
    else if (strcmp(var, "temperature_threshold_max") == 0)
    {
        state->temperatureMax = value;
        printk("Temperature max set to %sC.\n", val);
    }
    else if (strcmp(var, "humidity_threshold_min") == 0)
    {
        state->humidityMin = value;
        printk("Humidity min set to %s%%.\n", val);
    }
    else if (strcmp(var, "humidity_threshold_max") == 0)
    {
        state->humidityMax = value;
        printk("Humidity max set to %s%%.\n", val);
    }
    else
    {
        printk("Unknown environment variable %s.\n", var);
    }
}

// Simple mutex for the I2C bus. This will spin forever until the lock is released.
void lockNotecard(void)
{
    while (notecard_lock) {
        NoteDelayMs(1);
    }

    notecard_lock = true;
}

void unlockNotecard(void)
{
    notecard_lock = false;
}


// Check for environment variable changes. Returns true if there are changes and
// false otherwise.
bool pollEnvVars()
{
    uint32_t currentMs = platform_millis();

    // Don't check for changes unless its time to do so.
    if (currentMs - state.lastEnvVarPollMs < ENV_POLL_MS) {
        return false;
    }

    state.lastEnvVarPollMs = currentMs;

    J *rsp = NoteRequestResponse(NoteNewRequest("env.modified"));
    if (rsp == NULL) {
        printk("NULL response to env.modified.\n");
        return false;
    }

    uint32_t modifiedTime = JGetInt(rsp, "time");
    NoteDeleteResponse(rsp);
    // If the last modified timestamp is the same as the one we've got saved,
    // there have been no changes.
    if (state.envLastModTime == modifiedTime) {
        return false;
    }

    state.envLastModTime = modifiedTime;

    printk("Environment variable changed detected.\n");

    return true;
}

// Add a note with temperature and humidity data to data.qo. The note will be
// synced to Notehub according to the interval set with hub.set's outbound
// parameter in the setup function (see OUTBOUND_SYNC_MINS).
void publishSystemStatus()
{
    J *req = NoteNewRequest("note.add");
    if (req != NULL) {
        JAddStringToObject(req, "file", "data.qo");

        J *body = JCreateObject();
        if (body != NULL) {
            JAddNumberToObject(body, "temperature", state.temperature);
            JAddNumberToObject(body, "humidity", state.humidity);
            JAddStringToObject(body, "app", "nf18");
            JAddItemToObject(req, "body", body);

            NoteRequest(req);
        }
        else {
            printk("Failed to create body for system status "
                "update.\n");
        }
    }
    else {
        printk("Failed to create note.add request for system status "
            "update.\n");
    }
}

// Used in the main loop to check for and accept environment variable updates.
// If any variables are updated, this function sends a note to Notehub to
// acknowledge the update.
void updateEnvVars()
{
    if (pollEnvVars()) {
        NotecardEnvVarManager_fetch(envVarManager, envVarNames, (sizeof(envVarNames) / sizeof(envVarNames[0])));

        // Generate a note to Notehub to acknowledge the update.
        J *req = NoteNewRequest("note.add");
        if (req != NULL) {
            JAddStringToObject(req, "file", "notify.qo");
            JAddBoolToObject(req, "sync", true);

            J *body = JCreateObject();
            if (body != NULL) {
                JAddStringToObject(body, "message", "environment variable "
                    "update received");
                JAddItemToObject(req, "body", body);
                NoteRequest(req);
            }
            else {
                free(req);
                printk("Failed to create note body for env var "
                    "update ack.\n");
            }
        }
        else {
            printk("Failed to create note.add request for env "
                "var update ack.\n");
        }
    }
}

// One-time Arduino initialization.
void setup()
{
    // Set up debug output via serial connection.
#define RELEASE
#ifndef RELEASE
#warning "Debug mode is enabled. Define RELEASE to disable debug."
    NoteSetFnDebugOutput(noteLogPrint);
#endif

    // Initialize the physical I/O channel to the Notecard.
    NoteSetFnMutex(NULL, NULL, lockNotecard, unlockNotecard);
    NoteSetFnDefault(malloc, free, platform_delay, platform_millis);
    NoteSetFnI2C(NOTE_I2C_ADDR_DEFAULT, NOTE_I2C_MAX_DEFAULT, noteI2cReset,
                 noteI2cTransmit, noteI2cReceive);

    // Initialize the Notecard Environment Variable Manager.
    envVarManager = NotecardEnvVarManager_alloc();
    if (envVarManager == NULL) {
        printk("Failed to allocate NotecardEnvVarManager.\n");
    }
    else {
        NotecardEnvVarManager_setEnvVarCb(envVarManager, envVarManagerCb, &state);
    }

    // Set defaults.
    state.reportIntervalMs = REPORT_INTERVAL_DEFAULT_MS;
    state.temperatureMin = TEMPERATURE_MIN_DEFAULT;
    state.temperatureMax = TEMPERATURE_MAX_DEFAULT;
    state.humidityMin = HUMIDITY_MIN_DEFAULT;
    state.humidityMax = HUMIDITY_MAX_DEFAULT;

    // Configure the productUID and instruct the Notecard to sync outbound notes
    // a minimum of every OUTBOUND_SYNC_MINS minutes.
    J *req = NoteNewRequest("hub.set");
    if (PRODUCT_UID[0]) {
       JAddStringToObject(req, "product", PRODUCT_UID);
    }
    JAddStringToObject(req, "mode", "continuous");
    JAddBoolToObject(req, "sync", true);
    JAddNumberToObject(req, "outbound", OUTBOUND_SYNC_MINS);
    NoteRequest(req);

    // Send a note to _health.qo when USB power is lost or restored.
    req = NoteNewRequest("card.voltage");
    JAddBoolToObject(req, "usb", true);
    JAddBoolToObject(req, "alert", true);
    JAddBoolToObject(req, "sync", true);
    NoteRequest(req);

    updateEnvVars();
    initActuator();

    // Initialize the BME280 sensor
    if (!device_is_ready(bme280)) {
        printk("getDevice: error: Device \"%s\" is not ready; "
               "check the driver initialization logs for errors.\n",
               bme280->name);
    }
    else {
        printk("Connected to BME280.\n");
    }
}

void readSensor()
{
    struct sensor_value temperature, humidity;
    sensor_sample_fetch(bme280);
    sensor_channel_get(bme280, SENSOR_CHAN_AMBIENT_TEMP, &temperature);
    sensor_channel_get(bme280, SENSOR_CHAN_HUMIDITY, &humidity);
    state.temperature = sensor_value_to_double(&temperature);
    state.humidity = sensor_value_to_double(&humidity);
}

// Construct an alarm note and add it to alarm.qo, syncing it to Notehub
// immediately.
void publishAlarm(const char *temperatureStatus, const char *humidityStatus)
{
    if (temperatureStatus == NULL || humidityStatus == NULL) {
        printk("publishAlarm called with a NULL status.\n");
        return;
    }

    J *req = NoteNewRequest("note.add");
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
                    printk("Failed to create humidity body for "
                        "alarm.\n");
                }
            }
            else {
                printk("Failed to create temperature body for "
                    "alarm.\n");
            }

            JAddItemToObject(req, "body", body);

            NoteRequest(req);
        }
        else {
            printk("Failed to create body for alarm.\n");
        }
    }
    else {
        printk("Failed to create note.add request for alarm.\n");
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
        uint32_t currentMs = platform_millis();
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

    uint32_t currentMs = platform_millis();
    if (currentMs - state.sensorLastReadMs >= SENSOR_READ_INTERVAL_MS) {
        state.sensorLastReadMs = currentMs;
        readSensor();
    }

    checkAlarm();

    currentMs = platform_millis();
    if (currentMs - state.reportLastUpdateMs >= state.reportIntervalMs) {
        state.reportLastUpdateMs = currentMs;
        publishSystemStatus();
    }
}

void NoteUserAgentUpdate(J *ua) {
    JAddStringToObject(ua, "app", "nf18");
}

// Simulate Arduino Runtime
int main (void) {
    setup();
    while(1) {
        loop();
    }
}
