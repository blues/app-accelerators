/*
 * Copyright 2023 Blues Inc.  All rights reserved.
 * Use of this source code is governed by licenses granted by the
 * copyright holder including that found in the LICENSE file.
 */

#include <inttypes.h>
#include <stdlib.h>

#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/util.h>
#include <zephyr/sys/printk.h>
#include <zephyr/usb/usb_device.h>

/* Include Notecard note-c library */
#include "note.h"

/* Notecard node-c helper methods */
#include "notecard.h"

/* 
 * Don't keep valve open for longer than 10 minutes to prevent overheating of
 * its solenoid.
 */
#define MAX_OPEN_S (10 * 60)
/* Check for environment variable changes every 5 seconds. */
#define ENV_POLL_S 5
/* After triggering an alarm, raise the alarm every 30 seconds after that. */
#define ALARM_REFRESH_S 30
/*
 * Calculate the flow rate every 500 ms. We want to allow a little time between
 * measurements for the sensor's signal line to pulse. If we make this interval
 * too small, its possible we won't have enough sensor data (i.e. pulses) to
 * produce an accurate flow rate measurement.
 */
#define FLOW_CALC_INTERVAL_MS 500

#define LEAK_THRESHOLD 6

/*
 * Uncomment this line and replace com.your-company:your-product-name with your
 * ProductUID.
 */
// #define PRODUCT_UID "com.your-company:your-product-name"

#ifndef PRODUCT_UID
#define PRODUCT_UID ""
#pragma message "PRODUCT_UID is not defined in this example. Please ensure your Notecard has a product identifier set before running this example or define it in code here. More details at https://bit.ly/product-uid"
#endif

typedef struct {
    const char *alarmReason;
    uint32_t monitorInterval;
    uint32_t lastFlowRateCalcMs;
    uint32_t envLastModTime;
    volatile uint32_t flowMeterPulseCount;
    uint32_t flowRate;
    uint32_t flowRateAlarmMin;
    uint32_t flowRateAlarmMax;
    uint32_t leakCount;
    volatile bool attnTriggered;
    bool valveOpen;
} AppState;

AppState state = {0};

/* Calculate the flow rate in mL/min. */
uint32_t calculateFlowRate(uint32_t currentMs)
{
    return 60000 * (state.flowMeterPulseCount * 2.25) /
           (currentMs - state.lastFlowRateCalcMs);
}

/* Forward declarations */
void updateEnvVars(void);
void valveToggle(void);
void publishSystemStatus(bool);

/* BEGIN GPIOS ****************************************************************/

/* Valve open pin */
#define VALVE_NODE DT_ALIAS(valve)
#if !DT_NODE_HAS_STATUS(VALVE_NODE, okay)
#error "Unsupported board: valve devicetree alias is not defined"
#endif

static const struct gpio_dt_spec valve = GPIO_DT_SPEC_GET_OR(VALVE_NODE, gpios, {0});

/* Flow meter pin */
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

/* ATTN pin */
#define ATTN_NODE DT_ALIAS(attn)
#if !DT_NODE_HAS_STATUS(ATTN_NODE, okay)
#error "Unsupported board: attn devicetree alias is not defined"
#endif

static const struct gpio_dt_spec attn = GPIO_DT_SPEC_GET_OR(ATTN_NODE, gpios, {0});

static struct gpio_callback attnCbData;
static void attnCb(const struct device *, struct gpio_callback *, uint32_t)
{
    /* 
     * This flag will be read in the main loop. We keep the ISR lean and do all
     * the logic in the main loop.
     */
    state.attnTriggered = true;
}

/* END GPIOS ******************************************************************/

/* BEGIN TIMERS ***************************************************************/

/* Env var update timer */
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

/* Publish system status timer */
static void publishSystemStatusWorkCb(struct k_work *)
{
    publishSystemStatus(false);
}

K_WORK_DEFINE(publishSystemStatusWorkItem, publishSystemStatusWorkCb);

static void publishSystemStatusTimerCb(struct k_timer *)
{
    k_work_submit(&publishSystemStatusWorkItem);
}

K_TIMER_DEFINE(publishSystemStatusTimer, publishSystemStatusTimerCb, NULL);

/* Alarm refresh timer */
static void alarmRefreshWorkCb(struct k_work *)
{
    publishSystemStatus(true);
}

K_WORK_DEFINE(alarmRefreshWorkItem, alarmRefreshWorkCb);

static void alarmRefreshTimerCb(struct k_timer *)
{
    k_work_submit(&alarmRefreshWorkItem);
}

K_TIMER_DEFINE(alarmRefreshTimer, alarmRefreshTimerCb, NULL);

/* Flow rate calculation timer */
static void flowRateCalcTimerCb(struct k_timer *)
{
    uint32_t currentMs = NoteGetMs();
    state.flowRate = calculateFlowRate(currentMs);
    state.lastFlowRateCalcMs = currentMs;
    state.flowMeterPulseCount = 0;

#if USE_VALVE == 1
    if (!state.valveOpen) {
        /*
         * If the valve is closed and flow is detected, increment the leak
         * counter. If a leak is detected on LEAK_THRESHOLD or more consecutive
         * measurements, a leak alarm will be published. This filter prevents
         * spurious leak alarms shortly after the valve is closed while the flow
         * meter's spinner is still slowing down.
         */
        if (state.flowRate > 0) {
            ++state.leakCount;
        }
        else {
            state.leakCount = 0;
        }
    }
#endif /* USE_VALVE == 1 */
}

K_TIMER_DEFINE(flowRateCalcTimer, flowRateCalcTimerCb, NULL);

#if USE_VALVE == 1

/* Valve safety timer (close valve if open too long) */
static void valveSafetyTimerCb(struct k_timer *)
{
    if (state.valveOpen) {
        valveToggle();
    }
}

K_TIMER_DEFINE(valveSafetyTimer, valveSafetyTimerCb, NULL);

#endif /* USE_VALVE == 1 */

/* END TIMERS *****************************************************************/

/* Arm the ATTN interrupt. */
void attnArm(void)
{
    /*
     * Once ATTN has triggered, it stays set until explicitly rearm. Rearm it
     * here. It will trigger again after a change to the watched Notefile.
     */
    state.attnTriggered = false;
    J *req = NoteNewRequest("card.attn");
    JAddStringToObject(req, "mode", "rearm");
    NoteRequest(req);
}

#if USE_VALVE == 1

/* Toggle the valve's state. If open, close. If closed, open. */
void valveToggle(void)
{
    if (state.valveOpen) {
        k_timer_stop(&valveSafetyTimer);
    }
    else {
        k_timer_start(&valveSafetyTimer, K_SECONDS(MAX_OPEN_S), K_FOREVER);
        /*
         * Clear leak counter. By definition, we can't have a leak if the valve
         * is open.
         */
        state.leakCount = 0;
    }

    gpio_pin_toggle_dt(&valve);
    state.valveOpen = !state.valveOpen;
}

/*
 * Handle a valve command from Notehub. Supported commands are "open" and
 * "close".
 */
void handleValveCmd(char *cmd)
{
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

#endif /* USE_VALVE == 1 */

/*
 * Publish the system status (flow rate, valve state). If alarm is true, attach
 * the alarm reason to the outbound note and send it to alarm.qo. Otherwise,
 * send the note to data.qo.
 */
void publishSystemStatus(bool alarm)
{
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
            JAddNumberToObject(body, "flow_rate", state.flowRate);

        #if USE_VALVE == 1
            if (state.valveOpen) {
                JAddStringToObject(body, "valve_state", "open");
            }
            else {
                JAddStringToObject(body, "valve_state", "closed");
            }
        #endif /* USE_VALVE == 1 */

            if (alarm) {
                JAddStringToObject(body, "reason", state.alarmReason);
            }

            JAddStringToObject(body, "app", "nf9");
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

/*
 * Check for environment variable changes. Returns true if there are changes and
 * false otherwise.
 */
bool pollEnvVars(void)
{
    J *rsp = NoteRequestResponse(NoteNewRequest("env.modified"));
    if (rsp == NULL) {
        printk("NULL response to env.modified.\n");
        return false;
    }

    uint32_t modifiedTime = JGetInt(rsp, "time");
    NoteDeleteResponse(rsp);
    /*
     * If the last modified timestamp is the same as the one we've got saved,
     * there have been no changes.
     */
    if (state.envLastModTime == modifiedTime) {
        return false;
    }

    state.envLastModTime = modifiedTime;

    printk("Environment variable changed detected.\n");

    return true;
}

/* Returns true if any variables were updated and false otherwise. */
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

                        /*
                         * If the monitor interval changed, we need to restart
                         * the timer using the new interval value.
                         */
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
                printk("NULL body in response to env.get request."
                    "\n");
            }
        }

        NoteDeleteResponse(rsp);
    }
    else {
        printk("NULL response to env.get request.\n");
    }

    return updated;
}

/* 
 * Used in the main loop to check for and accept environment variable updates.
 * If any variables are updated, this function sends a note to Notehub to
 * acknowledge the update.
 */
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

void checkAlarm(void)
{
    bool flowRateHigh = state.valveOpen && state.flowRate > state.flowRateAlarmMax;
    bool flowRateLow = state.valveOpen && state.flowRate < state.flowRateAlarmMin;
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
        if (state.alarmReason == NULL) {
            state.alarmReason = reason;
            /*
             * Publish the system status with the alarm now and every
             * ALARM_REFRESH_S seconds thereafter until the alarm conditions are
             * cleared.
             */
            k_timer_start(&alarmRefreshTimer,
                          K_SECONDS(0), K_SECONDS(ALARM_REFRESH_S));
        }
    }
    else {
        if (state.alarmReason != NULL) {
            state.alarmReason = NULL;
            k_timer_stop(&alarmRefreshTimer);
        }
    }
}

void main(void)
{
    /* Configure USB Serial for Console output */
    const struct device *usb_dev = DEVICE_DT_GET(DT_CHOSEN(zephyr_console));
    uint32_t dtr = 0;

    if (usb_enable(NULL)) {
        return;
    }

    /* Sleep to wait for a terminal connection. */
    k_sleep(K_MSEC(2500));
    uart_line_ctrl_get(usb_dev, UART_LINE_CTRL_DTR, &dtr);

    /* Initialize note-c references */
    NoteSetFnDefault(malloc, free, platform_delay, platform_millis);
    NoteSetFnDebugOutput(noteLogPrint);

#ifdef USE_SERIAL
    NoteSetFnSerial(noteSerialReset, noteSerialTransmit,
                    noteSerialAvailable, noteSerialReceive);
#else
    NoteSetFnI2C(NOTE_I2C_ADDR_DEFAULT, NOTE_I2C_MAX_DEFAULT, noteI2cReset,
                 noteI2cTransmit, noteI2cReceive);

#endif

    /*
     * Configure the productUID and instruct the Notecard to stay connected to
     * the service.
     */
    J *req = NoteNewRequest("hub.set");
    if (PRODUCT_UID[0]) {
        JAddStringToObject(req, "product", PRODUCT_UID);
    }
    JAddStringToObject(req, "mode", "continuous");
    JAddBoolToObject(req, "sync", true);
    if (NoteRequest(req)) {
        printk("Notecard hub.set successful.\n");
    }
    else {
        printk("Notecard hub.set failed.\n");
    }

    /* Disarm ATTN to clear any previous state before re-arming. */
    req = NoteNewRequest("card.attn");
    JAddStringToObject(req, "mode", "disarm,-files");
    if (NoteRequest(req)) {
        printk("Notecard card.attn disarm successful.\n");
    }
    else {
        printk("Notecard card.attn disarm failed.\n");
    }

    /* Configure ATTN to watch for changes to data.qi. */
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

    /* Valve open pin setup */
    if (!device_is_ready(valve.port)) {
        printk("Error: Valve GPIO device %s is not ready\n", valve.port->name);
        return;
    }

    int ret = gpio_pin_configure_dt(&valve, GPIO_OUTPUT_LOW);
    if (ret != 0) {
        printk("Error %d: failed to configure %s pin %d\n",
               ret, valve.port->name, valve.pin);
        return;
    }

    printk("Set up valve open at %s pin %d\n", valve.port->name, valve.pin);

    /* Flow meter pin setup */
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

    ret = gpio_pin_interrupt_configure_dt(&flowMeter, GPIO_INT_EDGE_FALLING);
    if (ret != 0) {
        printk("Error %d: failed to configure interrupt on %s pin %d\n",
               ret, flowMeter.port->name, flowMeter.pin);
        return;
    }

    gpio_init_callback(&flowMeterCbData, flowMeterCb, BIT(flowMeter.pin));
    gpio_add_callback(flowMeter.port, &flowMeterCbData);

    printk("Set up flow meter at %s pin %d\n", flowMeter.port->name,
           flowMeter.pin);

    /* ATTN pin setup */
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

    /* Default values. */
    state.monitorInterval = 10; /* 10 seconds */
    state.flowRateAlarmMin = 500;
    state.flowRateAlarmMax = 1500;

    fetchEnvVars();

    k_timer_start(&envVarUpdateTimer, K_SECONDS(ENV_POLL_S),
                  K_SECONDS(ENV_POLL_S));
    k_timer_start(&publishSystemStatusTimer, K_SECONDS(state.monitorInterval),
                  K_SECONDS(state.monitorInterval));
    k_timer_start(&flowRateCalcTimer, K_MSEC(FLOW_CALC_INTERVAL_MS),
                  K_MSEC(FLOW_CALC_INTERVAL_MS));

    while (true) {
    #if USE_VALVE == 1
        if (state.attnTriggered) {
            /* Re-arm the ATTN interrupt. */
            attnArm();

            /* Process all pending inbound requests. */
            while (true) {
                /* Pop the next available note from data.qi. */
                J *req = NoteNewRequest("note.get");
                JAddStringToObject(req, "file", "data.qi");
                JAddBoolToObject(req, "delete", true);
                J *rsp = NoteRequestResponse(req);
                if (rsp != NULL) {

                    /*
                     * If an error is returned, this means that no response is
                     * pending. Note that it's expected that this might return
                     * either a "note does not exist" error if there are no
                     * pending inbound notes, or a "file does not exist" error
                     * if the inbound queue hasn't yet been created on the
                     * service.
                     */
                    if (NoteResponseError(rsp)) {
                        NoteDeleteResponse(rsp);
                        break;
                    }

                    /* Get the note's body. */
                    J *body = JGetObject(rsp, "body");
                    if (body != NULL) {
                        char *cmd = JGetString(body, "state");
                        if (cmd != NULL && strlen(cmd) != 0) {
                            handleValveCmd(cmd);
                            /*
                             * If we received a valve command (open or close),
                             * we want to publish the system status immediately
                             * in response, regardless of if it's time to do so
                             * based on the monitor interval. This acts as a
                             * sort of acknowledgment so that the controller 
                             * knows their valve command was received.
                             */
                            publishSystemStatus(false);
                        }
                        else {
                            printk("Unable to get valve command from note."
                                "\n");
                        }
                    }
                    else {
                        printk("Note body was NULL.\n");
                    }
                }

                NoteDeleteResponse(rsp);
            }
        }
    #endif /* USE_VALVE == 1 */
        
        checkAlarm();
    }
}
