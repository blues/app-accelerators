// Copyright 2023 Blues Inc.  All rights reserved.
// Use of this source code is governed by licenses granted by the
// copyright holder including that found in the LICENSE file.

#include <inttypes.h>
#include <stdlib.h>

#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/util.h>
#include <zephyr/sys/printk.h>
#include <zephyr/usb/usb_device.h>

// Include Notecard note-c library
#include "note.h"

// Notecard node-c helper methods
#include "note_c_hooks.h"

// Don't keep valve open for longer than 10 minutes to prevent overheating of
// its solenoid.
#define MAX_OPEN_S (10 * 60)
// Check for environment variable changes every 5 seconds.
#define ENV_POLL_S 5
// This value specifies how frequently the alarm conditions should be checked.
#define ALARM_CHECK_S 1
// After triggering an alarm, we won't publish another until this cooldown has
// expired.
#define ALARM_COOLDOWN_S 30
// Calculate the flow rate every 500 ms. We want to allow a little time between
// measurements for the sensor's signal line to pulse. If we make this interval
// too small, its possible we won't have enough sensor data (i.e. pulses) to
// produce an accurate flow rate measurement.
#define FLOW_CALC_INTERVAL_MS 500
// The `lockNotecard` function is a spin lock, and this value is responsible for
// controlling the polling frequency.
#define NOTECARD_LOCK_POLLS_MS 10
// This value serves as a low-pass filter to protect against spurious
// leak warnings.
#define LEAK_THRESHOLD 6

// Uncomment the define below and replace com.your-company:your-product-name
// with your ProductUID.
#define PRODUCT_UID "com.blues.valve_monitor_nf9"

#ifndef PRODUCT_UID
#define PRODUCT_UID ""
#pragma message "PRODUCT_UID is not defined in this example. Please ensure your Notecard has a product identifier set before running this example or define it in code here. More details at https://bit.ly/product-uid"
#endif

typedef struct {
    volatile const char *alarmReason;
    uint32_t monitorInterval;
    volatile uint32_t lastFlowRateCalcMs;
    uint32_t envLastModTime;
    volatile uint32_t flowMeterPulseCount;
    volatile uint32_t flowRate;
    volatile uint32_t alarmFlowRate;
    uint32_t flowRateAlarmMin;
    uint32_t flowRateAlarmMax;
    volatile uint32_t leakCount;
    volatile bool notecardLocked;
    volatile bool attnTriggered;
    volatile bool publishRequired;
    bool valveOpen;
} AppState;

AppState state = {0};

// Calculate the flow rate (Q) in mL/min:
// Datasheet: F=(38*Q)±3%, Q=L/Min, error: ±3%
// pulse_count = ~0.50mL
// volume (mL) => (pulse_count * (1/2)(mL))
// duration (ms) => (now_ms - last_sample_ms)
// duration (s) => (now_ms - last_sample_ms) / 1000
// duration (min) => (now_ms - last_sample_ms) / (1000 * 60)
#define MS_IN_MIN 60000

static uint32_t calculateFlowRate(uint32_t currentMs)
{
    return (state.flowMeterPulseCount * MS_IN_MIN * 1) /
           ((currentMs - state.lastFlowRateCalcMs) * 2);
}

// Forward declarations
void updateEnvVars(void);
void valveToggle(void);
void publishSystemStatus(bool, uint32_t);

// BEGIN GPIOS

// Valve open pin
#define VALVE_NODE DT_ALIAS(valve)
#if !DT_NODE_HAS_STATUS(VALVE_NODE, okay)
#error "Unsupported board: valve devicetree alias is not defined"
#endif

static const struct gpio_dt_spec valve = GPIO_DT_SPEC_GET_OR(VALVE_NODE, gpios, {0});

// Flow meter pin
#define FLOW_METER_NODE DT_ALIAS(flow_meter)
#if !DT_NODE_HAS_STATUS(FLOW_METER_NODE, okay)
#error "Unsupported board: flow meter devicetree alias is not defined"
#endif

static const struct gpio_dt_spec flowMeter = GPIO_DT_SPEC_GET_OR(FLOW_METER_NODE, gpios, {0});

static struct gpio_callback flowMeterCbData;
static void flowMeterCb(const struct device *, struct gpio_callback *, uint32_t)
{
    ++state.flowMeterPulseCount;
}

// ATTN pin
#define ATTN_NODE DT_ALIAS(attn)
#if !DT_NODE_HAS_STATUS(ATTN_NODE, okay)
#error "Unsupported board: attn devicetree alias is not defined"
#endif

static const struct gpio_dt_spec attn = GPIO_DT_SPEC_GET_OR(ATTN_NODE, gpios, {0});

static struct gpio_callback attnCbData;
static void attnCb(const struct device *, struct gpio_callback *, uint32_t)
{
    // This flag will be read in the main loop. We keep the ISR lean and do all
    // the logic in the main loop.
    state.attnTriggered = true;
}

// END GPIOS

// BEGIN TIMERS

// Env var update timer
static void envVarUpdateWorkCb(struct k_work *)
{
    updateEnvVars();
}

K_WORK_DEFINE(envVarUpdateWorkItem, envVarUpdateWorkCb);

static void envVarUpdateTimerCb(struct k_timer *)
{
    k_work_submit(&envVarUpdateWorkItem);
}

K_TIMER_DEFINE(envVarUpdateTimer, envVarUpdateTimerCb, NULL);

// Publish system status timer
static void publishSystemStatusWorkCb(struct k_work *)
{
    publishSystemStatus(false, state.flowRate);
}

K_WORK_DEFINE(publishSystemStatusWorkItem, publishSystemStatusWorkCb);

static void publishSystemStatusTimerCb(struct k_timer *)
{
    k_work_submit(&publishSystemStatusWorkItem);
}

K_TIMER_DEFINE(publishSystemStatusTimer, publishSystemStatusTimerCb, NULL);

// Flow rate calculation timer
static void flowRateCalcTimerCb(struct k_timer *)
{
    uint32_t currentMs = NoteGetMs();
    uint32_t flowRate = calculateFlowRate(currentMs);
    if (state.flowRate != flowRate) {
        state.flowRate = flowRate;
        state.publishRequired = true;
    }
    state.lastFlowRateCalcMs = currentMs;
    state.flowMeterPulseCount = 0;

#if USE_VALVE == 1
    if (!state.valveOpen) {
         // If the valve is closed and flow is detected, increment the leak
         // counter. If a leak is detected on LEAK_THRESHOLD or more consecutive
         // measurements, a leak alarm will be published. This filter prevents
         // spurious leak alarms shortly after the valve is closed while the
         // flow meter's spinner is still slowing down.
        if (state.flowRate > 0) {
            ++state.leakCount;
        }
        else {
            state.leakCount = 0;
        }
    }
#endif // USE_VALVE == 1
}

K_TIMER_DEFINE(flowRateCalcTimer, flowRateCalcTimerCb, NULL);

// Alarm cooldown timer
static void alarmCooldownTimerCb(struct k_timer *)
{
    // Clear the alarm reason so that a new alarm can be published, if needed.
    state.alarmReason = NULL;
}

K_TIMER_DEFINE(alarmCooldownTimer, alarmCooldownTimerCb, NULL);

// Work item for publishing an alarm.
static void alarmPublishWorkCb(struct k_work *)
{
    publishSystemStatus(true, state.alarmFlowRate);
    // This timer limits the frequency of alarms we publish to one every
    // ALARM_COOLDOWN_S seconds. Specifying K_FOREVER for the period means
    // this timer fires only once, until it's started again (i.e. it's not
    // periodic).
    k_timer_start(&alarmCooldownTimer,
                  K_SECONDS(ALARM_COOLDOWN_S), K_FOREVER);
}

K_WORK_DEFINE(alarmPublishWorkItem, alarmPublishWorkCb);

// Check alarm timer
static void checkAlarmTimerCb(struct k_timer *)
{
    // Don't do anything if there's an active alarm.
    if (state.alarmReason != NULL) {
        return;
    }

    // state.alarmFlowRate is a snapshot of state.flowRate. This way, if there
    // is an alarm, we have the flow rate value that caused it. We can't just
    // use state.flowRate because it may change between the time of alarm
    // detection and alarm publication.
    state.alarmFlowRate = state.flowRate;
    bool flowRateHigh = state.valveOpen &&
                        state.alarmFlowRate > state.flowRateAlarmMax;
    bool flowRateLow = state.valveOpen &&
                       state.alarmFlowRate < state.flowRateAlarmMin;
    bool leaking = state.leakCount >= LEAK_THRESHOLD;

    const char* reason = NULL;
    if (flowRateHigh) {
        reason = "high";
    }
    else if (flowRateLow) {
        reason = "low";
    }
    else if (leaking) {
        reason = "leak";
    }

    if (reason != NULL) {
        state.alarmReason = reason;
        // Add an item to the work queue to publish the alarm to Notehub.
        k_work_submit(&alarmPublishWorkItem);
    }
}

K_TIMER_DEFINE(checkAlarmTimer, checkAlarmTimerCb, NULL);

#if USE_VALVE == 1

// Valve safety timer (close valve if open too long)
static void valveSafetyTimerCb(struct k_timer *)
{
    if (state.valveOpen) {
        valveToggle();
    }
}

K_TIMER_DEFINE(valveSafetyTimer, valveSafetyTimerCb, NULL);

#endif // USE_VALVE == 1

// END TIMERS

// Attempt to get a lock on the I2C bus. This will spin forever until the lock
// is released.
void lockNotecard(void)
{
    while (state.notecardLocked) {
        NoteDelayMs(NOTECARD_LOCK_POLLS_MS);
    }

    state.notecardLocked = true;
}

void unlockNotecard(void)
{
    state.notecardLocked = false;
}

// Arm the ATTN interrupt.
void attnArm(void)
{
    // Once ATTN has triggered, it stays set until explicitly rearmed. Rearm it
    // here. It will trigger again after a change to the watched Notefile.
    state.attnTriggered = false;
    J *req = NoteNewRequest("card.attn");
    JAddStringToObject(req, "mode", "rearm");
    NoteRequest(req);
}

#if USE_VALVE == 1

// Toggle the valve's state. If open, close. If closed, open.
void valveToggle(void)
{
    // Halt timers related to flow rate, publish and alarms
    k_timer_stop(&checkAlarmTimer);
    k_timer_stop(&publishSystemStatusTimer);
    k_timer_stop(&flowRateCalcTimer);

    // Update safety timer (prevents overheating of solenoid)
    if (state.valveOpen) {
        k_timer_stop(&valveSafetyTimer);
    }
    else {
        k_timer_start(&valveSafetyTimer, K_SECONDS(MAX_OPEN_S), K_FOREVER);
        // Clear leak counter. By definition, we can't have a leak if the valve
        // is open.
        state.leakCount = 0;
    }

    // Adjust valve position
    gpio_pin_toggle_dt(&valve);
    state.valveOpen = !state.valveOpen;

    // Begin flow rate calculation for new state
    k_sleep(K_MSEC(250));  // valve state change cool-down

    // Reset variables used to calculate flow rate
    state.flowRate = 0;
    state.lastFlowRateCalcMs = NoteGetMs();
    state.flowMeterPulseCount = 0;

    // Restart flow rate, publish, alarm timers
    k_timer_start(&flowRateCalcTimer, K_MSEC(FLOW_CALC_INTERVAL_MS),
                  K_MSEC(FLOW_CALC_INTERVAL_MS));
    k_timer_start(&publishSystemStatusTimer,
                    K_SECONDS(state.monitorInterval),
                    K_SECONDS(state.monitorInterval));
    k_timer_start(&checkAlarmTimer, K_SECONDS(ALARM_CHECK_S),
                  K_SECONDS(ALARM_CHECK_S));
}

// Handle a valve command from Notehub. Supported commands are "open" and
// "close".
void handleValveCmd(char *cmd)
{
    // We must publish to acknowledge the controller
    // so it knows the valve command was received.
    state.publishRequired = true;

    if (!strcmp(cmd, "open")) {
        printk("Received valve open cmd.\n");
        if (state.valveOpen) {
            printk("Valve already open.\n");
        }
        else {
            valveToggle();
        }
    }
    else if (!strcmp(cmd, "close")) {
        printk("Received valve close cmd.\n");
        if (state.valveOpen) {
            valveToggle();
        }
        else {
            printk("Valve already closed.\n");
        }
    }
    else {
        printk("Unrecognized valve cmd: %s.\n", cmd);
    }
}

#endif // USE_VALVE == 1


// Publish the system status (flow rate, valve state). If alarm is true, attach
// the alarm reason to the outbound note and send it to alarm.qo. Otherwise,
// send the note to data.qo.
void publishSystemStatus(bool alarm, uint32_t flowRate)
{
    // Only publish when necessary to preserve prepaid data
    if (!state.publishRequired && !alarm) { return; }

    const char *file;

    if (alarm) {
        file = "alarm.qo";
    }
    else {
        file = "data.qo";
    }

    J *req = NoteNewRequest("note.add");
    if (req != NULL) {
        JAddStringToObject(req, "file", file);
        JAddBoolToObject(req, "sync", true);

        J *body = JCreateObject();
        if (body != NULL) {
            JAddNumberToObject(body, "flow_rate", flowRate);

        #if USE_VALVE == 1
            if (state.valveOpen) {
                JAddStringToObject(body, "valve_state", "open");
            }
            else {
                JAddStringToObject(body, "valve_state", "closed");
            }
        #endif // USE_VALVE == 1

            if (alarm) {
                JAddStringToObject(body, "reason",
                                   (const char * const)state.alarmReason);
            }

            JAddStringToObject(body, "app", "nf9");
            JAddItemToObject(req, "body", body);

            const bool success = NoteRequest(req);
            if (success && !alarm) {
                state.publishRequired = false;
            }
        }
        else {
            printk("Failed to create body for system status update.\n");
        }
    }
    else {
        printk("Failed to create note.add request for system status update.\n");
    }
}

// Check for environment variable changes. Returns true if there are changes and
// false otherwise.
bool pollEnvVars(void)
{
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

// Returns true if any variables were updated and false otherwise.
bool fetchEnvVars(void)
{
    bool updated = false;
    J *req = NoteNewRequest("env.get");
    J *names = JAddArrayToObject(req, "names");
    JAddItemToArray(names, JCreateString("monitor_interval"));
    JAddItemToArray(names, JCreateString("flow_rate_alarm_threshold_min"));
    JAddItemToArray(names, JCreateString("flow_rate_alarm_threshold_max"));

    J *rsp = NoteRequestResponse(req);
    if (rsp != NULL) {
        if (NoteResponseError(rsp)) {
            printk("Error in env.get response.\n");
        }
        else {
            J *body = JGetObject(rsp, "body");
            if (body != NULL) {
                char *valueStr = JGetString(body, "monitor_interval");
                float value;
                char *endPtr;

                if (valueStr != NULL) {
                    value = strtof(valueStr, &endPtr);
                    if ((value == 0 && valueStr == endPtr) || value < 0) {
                        printk("Failed to convert %s to positive "
                            "float for monitor interval.\n", valueStr);
                    }
                    else {
                        state.monitorInterval = (uint32_t)value;
                        updated = true;
                        printk("Monitor interval set to %u s.\n",
                            state.monitorInterval);

                        // If the monitor interval changed, we need to restart
                        // the timer using the new interval value.
                        k_timer_stop(&publishSystemStatusTimer);
                        k_timer_start(&publishSystemStatusTimer,
                                      K_SECONDS(state.monitorInterval),
                                      K_SECONDS(state.monitorInterval));
                    }
                }

                valueStr = JGetString(body, "flow_rate_alarm_threshold_min");
                if (valueStr != NULL) {
                    value = strtof(valueStr, &endPtr);
                    if ((value == 0 && valueStr == endPtr) || value < 0) {
                        printk("Failed to convert %s to positive "
                            "float for flow rate min.\n", valueStr);
                    }
                    else {
                        state.flowRateAlarmMin = value;
                        updated = true;
                        printk("Flow rate min set to %u mL/min.\n",
                            state.flowRateAlarmMin);
                    }
                }

                valueStr = JGetString(body, "flow_rate_alarm_threshold_max");
                if (valueStr != NULL) {
                    value = strtof(valueStr, &endPtr);
                    if ((value == 0 && valueStr == endPtr) || value < 0) {
                        printk("Failed to convert %s to positive "
                            "float for flow rate max.\n", valueStr);
                    }
                    else {
                        state.flowRateAlarmMax = value;
                        updated = true;
                        printk("Flow rate max set to %u mL/min.\n",
                            state.flowRateAlarmMax);
                    }
                }
            }
            else {
                printk("NULL body in response to env.get request.\n");
            }
        }

        NoteDeleteResponse(rsp);
    }
    else {
        printk("NULL response to env.get request.\n");
    }

    return updated;
}


// Used in the main loop to check for and accept environment variable updates.
// If any variables are updated, this function sends a note to Notehub to
// acknowledge the update.
void updateEnvVars(void)
{
    if (pollEnvVars()) {
        if (fetchEnvVars()) {
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
}

void main(void)
{
    // Configure USB Serial for Console output.
    const struct device *usb_dev = DEVICE_DT_GET(DT_CHOSEN(zephyr_console));
    uint32_t dtr = 0;

    if (usb_enable(NULL)) {
        return;
    }

    // Sleep to wait for a terminal connection.
    k_sleep(K_MSEC(2500));
    uart_line_ctrl_get(usb_dev, UART_LINE_CTRL_DTR, &dtr);

    // Initialize note-c references.
    NoteSetFnDefault(malloc, free, platform_delay, platform_millis);
    NoteSetFnDebugOutput(noteLogPrint);

    NoteSetFnMutex(NULL, NULL, lockNotecard, unlockNotecard);
#ifdef USE_SERIAL
    NoteSetFnSerial(noteSerialReset, noteSerialTransmit,
                    noteSerialAvailable, noteSerialReceive);
#else
    NoteSetFnI2C(NOTE_I2C_ADDR_DEFAULT, NOTE_I2C_MAX_DEFAULT, noteI2cReset,
                 noteI2cTransmit, noteI2cReceive);
#endif

    // Configure the productUID and instruct the Notecard to stay connected to
    // the service.
    J *req = NoteNewRequest("hub.set");
    if (PRODUCT_UID[0]) {
        JAddStringToObject(req, "product", PRODUCT_UID);
    }
    JAddStringToObject(req, "sn", "NF09_Sink");
    JAddStringToObject(req, "mode", "continuous");
    JAddBoolToObject(req, "sync", true);
    if (NoteRequest(req)) {
        printk("Notecard hub.set successful.\n");
    }
    else {
        printk("Notecard hub.set failed.\n");
    }

    // Disarm ATTN to clear any previous state before re-arming.
    req = NoteNewRequest("card.attn");
    JAddStringToObject(req, "mode", "disarm,-files");
    if (NoteRequest(req)) {
        printk("Notecard card.attn disarm successful.\n");
    }
    else {
        printk("Notecard card.attn disarm failed.\n");
    }

    // Configure ATTN to watch for changes to data.qi.
    req = NoteNewRequest("card.attn");
    const char *filesToWatch[] = {"data.qi"};
    int numFilesToWatch = sizeof(filesToWatch) / sizeof(const char *);
    J *filesArray = JCreateStringArray(filesToWatch, numFilesToWatch);
    JAddItemToObject(req, "files", filesArray);
    JAddStringToObject(req, "mode", "files");
    if (NoteRequest(req)) {
        printk("Notecard card.attn config successful.\n");
    }
    else {
        printk("Notecard card.attn config failed.\n");
    }

    // Valve open pin setup
    if (!device_is_ready(valve.port)) {
        printk("Error: Valve GPIO device %s is not ready\n", valve.port->name);
        return;
    }

    int ret = gpio_pin_configure_dt(&valve, GPIO_OUTPUT_INACTIVE);
    if (ret != 0) {
        printk("Error %d: failed to configure %s pin %d\n",
               ret, valve.port->name, valve.pin);
        return;
    }

    state.valveOpen = false;
    printk("Set up valve control at %s pin %d\n", valve.port->name, valve.pin);

    // Flow meter pin setup
    if (!device_is_ready(flowMeter.port)) {
        printk("Error: Flow meter GPIO device %s is not ready\n",
               flowMeter.port->name);
        return;
    }

    ret = gpio_pin_configure_dt(&flowMeter, GPIO_INPUT | GPIO_PULL_UP);
    if (ret != 0) {
        printk("Error %d: failed to configure %s pin %d\n",
               ret, flowMeter.port->name, flowMeter.pin);
        return;
    }

    ret = gpio_pin_interrupt_configure_dt(&flowMeter, GPIO_INT_EDGE_TO_ACTIVE);
    if (ret != 0) {
        printk("Error %d: failed to configure interrupt on %s pin %d\n",
               ret, flowMeter.port->name, flowMeter.pin);
        return;
    }

    gpio_init_callback(&flowMeterCbData, flowMeterCb, BIT(flowMeter.pin));
    gpio_add_callback(flowMeter.port, &flowMeterCbData);

    state.flowRate = 0;
    state.lastFlowRateCalcMs = NoteGetMs();
    state.flowMeterPulseCount = 0;
    printk("Set up flow meter at %s pin %d\n", flowMeter.port->name,
           flowMeter.pin);

    // ATTN pin setup
    if (!device_is_ready(attn.port)) {
        printk("Error: ATTN GPIO device %s is not ready\n", attn.port->name);
        return;
    }

    ret = gpio_pin_configure_dt(&attn, GPIO_INPUT);
    if (ret != 0) {
        printk("Error %d: failed to configure %s pin %d\n",
               ret, attn.port->name, attn.pin);
        return;
    }

    ret = gpio_pin_interrupt_configure_dt(&attn, GPIO_INT_EDGE_TO_ACTIVE);
    if (ret != 0) {
        printk("Error %d: failed to configure interrupt on %s pin %d\n",
               ret, attn.port->name, attn.pin);
        return;
    }

    gpio_init_callback(&attnCbData, attnCb, BIT(attn.pin));
    gpio_add_callback(attn.port, &attnCbData);

    printk("Set up ATTN at %s pin %d\n", attn.port->name, attn.pin);

    attnArm();

    // Default values.
    state.monitorInterval = 10; // 10 seconds
    state.flowRateAlarmMin = 500;
    state.flowRateAlarmMax = 1500;

    fetchEnvVars();

    k_timer_start(&envVarUpdateTimer, K_SECONDS(ENV_POLL_S),
                  K_SECONDS(ENV_POLL_S));
    k_timer_start(&publishSystemStatusTimer, K_SECONDS(state.monitorInterval),
                  K_SECONDS(state.monitorInterval));
    k_timer_start(&flowRateCalcTimer, K_MSEC(FLOW_CALC_INTERVAL_MS),
                  K_MSEC(FLOW_CALC_INTERVAL_MS));
    k_timer_start(&checkAlarmTimer, K_SECONDS(ALARM_CHECK_S),
                  K_SECONDS(ALARM_CHECK_S));

    while (true) {
    #if USE_VALVE == 1
        if (state.attnTriggered) {
            // Re-arm the ATTN interrupt.
            attnArm();

            // Process all pending inbound requests.
            while (true) {
                // Pop the next available note from data.qi.
                J *req = NoteNewRequest("note.get");
                JAddStringToObject(req, "file", "data.qi");
                JAddBoolToObject(req, "delete", true);
                J *rsp = NoteRequestResponse(req);
                if (rsp != NULL) {

                    // If an error is returned, this means that no response is
                    // pending. Note that it's expected that this might return
                    // either a "note does not exist" error if there are no
                    // pending inbound notes, or a "file does not exist" error
                    // if the inbound queue hasn't yet been created on the
                    // service.
                    if (NoteResponseError(rsp)) {
                        NoteDeleteResponse(rsp);
                        break;
                    }

                    // Get the note's body.
                    J *body = JGetObject(rsp, "body");
                    if (body != NULL) {
                        char *cmd = JGetString(body, "state");
                        if (cmd != NULL && strlen(cmd) != 0) {
                            handleValveCmd(cmd);
                            // If we received a valve command (open or close),
                            // we want to publish the system status immediately
                            // in response, regardless of if it's time to do so
                            // based on the monitor interval. This acts as a
                            // sort of acknowledgment so that the controller
                            // knows their valve command was received.
                            publishSystemStatus(false, state.flowRate);
                        }
                        else {
                            printk("Unable to get valve command from note.\n");
                        }
                    }
                    else {
                        printk("Note body was NULL.\n");
                    }
                }

                NoteDeleteResponse(rsp);
            }
        }
    #endif // USE_VALVE == 1
    }
}
