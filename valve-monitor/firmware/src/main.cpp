//
// Copyright 2022 Blues Inc.  All rights reserved.
// Use of this source code is governed by licenses granted by the
// copyright holder including that found in the LICENSE file.
//

#include <Notecard.h>
#include <Wire.h>

// This is the GPIO pin that will be connected to the Notecard's ATTN pin.
#define ATTN_INPUT_PIN  PA5
// This is the GPIO pin that will open the valve.
#define VALVE_OPEN_PIN  PA4
// This is the GPIO that will be connected to the flow rate meter.
#define FLOW_RATE_METER_PIN PE9

// Don't keep valve open for longer than 10 minutes to prevent overheating of
// its solenoid.
#define MAX_OPEN_MS (10 * 60 * 1000)
// Check for environment variable changes every 5 seconds.
#define ENV_POLL_MS (5 * 1000)
// After triggering an alarm, raise the alarm every 30 seconds after that.
#define ALARM_REFRESH_MS (30 * 1000)
// Calculate the flow rate every 200 ms. We want to allow a little time between
// measurements for the sensor's signal line to pulse. If we make this interval
// too small, its possible we won't have enough sensor data (i.e. pulses) to
// produce an accurate flow rate measurement.
#define FLOW_CALC_INTERVAL_MS 200

// Define USE_VALVE to compile in valve control support. If not defined, it's
// turned on by default.
#ifndef USE_VALVE
#define USE_VALVE 1
#endif

// This is the unique Product Identifier for your device
#define PRODUCT_UID "com.blues.hroche:quickstart"   // "com.my-company.my-name:my-project"

#ifndef PRODUCT_UID
#error "PRODUCT_UID is not defined in this example. Please ensure your Notecard has a product identifier set before running this example or define it in code here. More details at https://dev.blues.io/tools-and-sdks/samples/product-uid"
#endif

struct AppState {
    uint32_t lastEnvVarPollMs;
    uint32_t monitorIntervalMs;
    uint32_t monitorLastUpdateMs;
    uint32_t lastFlowRateCalcMs;
    uint32_t valveOpenedMs;
    uint32_t flowLastPulseMs;
    uint32_t lastAlarmMs;
    uint32_t envLastModTime;
    volatile uint32_t flowMeterPulseCount;
    uint32_t flowRate;
    uint32_t flowRateAlarmMin;
    uint32_t flowRateAlarmMax;
    volatile bool attnTriggered;
    bool valveOpen;
    bool ackValveCmd;
};

AppState state = {0};
Notecard notecard;

void attnISR()
{
    // This flag will be read in the main loop. We keep the ISR lean and do all
    // the logic in the main loop.
    state.attnTriggered = true;
}

void flowMeterISR()
{
    ++state.flowMeterPulseCount;
}

// Arm the ATTN interrupt.
void attnArm()
{
    // Once ATTN has triggered, it stays set until explicitly reset. Reset it
    // here. It will trigger again after a change to the watched Notefile.
    state.attnTriggered = false;
    J *req = notecard.newRequest("card.attn");
    JAddStringToObject(req, "mode", "reset");
    notecard.sendRequest(req);
}

// Toggle the valve's state. If open, close, If closed, open.
void valveToggle()
{
    if (state.valveOpen) {
        notecard.logDebug("Closing valve.\n");
        digitalWrite(VALVE_OPEN_PIN, LOW);
    }
    else {
        notecard.logDebug("Opening valve.\n");
        // Save the time the valve was opened. This is used to determine if the
        // valve has been open too long and should be shut off to prevent
        // overheating. This is a problem with the particular solenoid used in
        // this example. Ideally, a better solenoid is used and this timeout
        // mechanism isn't needed.
        state.valveOpenedMs = millis();
        digitalWrite(VALVE_OPEN_PIN, HIGH);
    }

    state.valveOpen = !state.valveOpen;
}

// Handle a valve command from Notehub. Supported commands are "open" and
// "close".
void handleValveCmd(char *cmd)
{
    if (!strcmp(cmd, "open")) {
        notecard.logDebug("Received valve open cmd.\n");
        if (state.valveOpen) {
            notecard.logDebug("Valve already open.\n");
        }
        else {
            valveToggle();
        }
    }
    else if (!strcmp(cmd, "close")) {
        notecard.logDebug("Received valve close cmd.\n");
        if (state.valveOpen) {
            state.flowMeterPulseCount = 0;
            valveToggle();
        }
        else {
            notecard.logDebug("Valve already closed.\n");
        }
    }
    else {
        notecard.logDebugf("Unrecognized valve cmd: %s.\n", cmd);
    }

    state.ackValveCmd = 1;
}

void publishSystemStatus(uint32_t flowRate)
{
    J *req = notecard.newRequest("note.add");

    if (req != NULL) {
        JAddStringToObject(req, "file", "data.qo");
        JAddBoolToObject(req, "sync", true);

        J *body = JCreateObject();
        if (body != NULL) {
            JAddNumberToObject(body, "flow_rate", flowRate);

        #ifdef USE_VALVE
            if (state.valveOpen) {
                JAddStringToObject(body, "valve_state", "open");
            }
            else {
                JAddStringToObject(body, "valve_state", "closed");
            }
        #endif // USE_VALVE

            JAddStringToObject(body, "app", "nf9");
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
    JAddItemToArray(names, JCreateString("flow_rate_alarm_threshold_min"));
    JAddItemToArray(names, JCreateString("flow_rate_alarm_threshold_max"));

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

                if (valueStr != NULL) {
                    value = atof(valueStr);
                    if (value != 0.0) {
                        state.monitorIntervalMs = (uint32_t)(value * 1000);
                        updated = true;
                        notecard.logDebugf("Monitor interval set to %u ms.\n",
                            state.monitorIntervalMs);
                    }
                }

                valueStr = JGetString(body, "flow_rate_alarm_threshold_min");
                if (valueStr != NULL) {
                    value = atof(valueStr);
                    if (value != 0.0) {
                        state.flowRateAlarmMin = value;
                        updated = true;
                        notecard.logDebugf("Flow rate min set to %u mL/min.\n",
                            state.flowRateAlarmMin);
                    }
                }

                valueStr = JGetString(body, "flow_rate_alarm_threshold_max");
                if (valueStr != NULL) {
                    value = atof(valueStr);
                    if (value != 0.0) {
                        state.flowRateAlarmMax = value;
                        updated = true;
                        notecard.logDebugf("Flow rate max set to %u mL/min.\n",
                            state.flowRateAlarmMax);
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

    // Configure the productUID and instruct the Notecard to stay connected to
    // the service.
    J *req = notecard.newRequest("hub.set");
    JAddStringToObject(req, "product", PRODUCT_UID);
    JAddStringToObject(req, "mode", "continuous");
    JAddBoolToObject(req, "sync", true);
    notecard.sendRequest(req);

    // Disarm ATTN to clear any previous state before re-arming.
    req = notecard.newRequest("card.attn");
    JAddStringToObject(req, "mode", "disarm,-files");
    notecard.sendRequest(req);

    // Configure ATTN to watch for changes to data.qi.
    req = notecard.newRequest("card.attn");
    const char *filesToWatch[] = {"data.qi"};
    int numFilesToWatch = sizeof(filesToWatch) / sizeof(const char *);
    J *filesArray = JCreateStringArray(filesToWatch, numFilesToWatch);
    JAddItemToObject(req, "files", filesArray);
    JAddStringToObject(req, "mode", "files");
    notecard.sendRequest(req);

    // Set up the GPIO pin connected to ATTN as an input.
    pinMode(ATTN_INPUT_PIN, INPUT);
    // When that pin detects a rising edge, jump to the attnISR interrupt
    // handler.
    attachInterrupt(digitalPinToInterrupt(ATTN_INPUT_PIN), attnISR, RISING);

    // Configure valve open pin. Will default to LOW (closed).
    pinMode(VALVE_OPEN_PIN, OUTPUT);

    pinMode(FLOW_RATE_METER_PIN, INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(FLOW_RATE_METER_PIN), flowMeterISR, FALLING);

    // Arm the interrupt, so that we are notified whenever ATTN rises
    attnArm();

    // Default values.
    state.monitorIntervalMs = 10000; // 10 seconds
    state.flowRateAlarmMin = 10;
    state.flowRateAlarmMax = 100;

    fetchEnvVars();
}

// Calculate the flow rate in mL/min.
uint32_t calculateFlowRate(uint32_t currentMs)
{
    return 60000 * (state.flowMeterPulseCount * 2.25) /
           (currentMs - state.lastFlowRateCalcMs);
}

void alarm(float flowRate)
{
    J *req = notecard.newRequest("note.add");

    if (req != NULL) {
        JAddBoolToObject(req, "sync", true);
        JAddStringToObject(req, "file", "alarm.qo");

        J *body = JCreateObject();
        if (body != NULL) {
            JAddNumberToObject(body, "flow_rate", flowRate);
            JAddBoolToObject(body, "alarm", true);
            JAddStringToObject(body, "app", "nf9");
            JAddItemToObject(req, "body", body);

            notecard.sendRequest(req);
        }
        else {
             notecard.logDebug("Failed to create body for alarm note.\n");
        }
    }
    else {
        notecard.logDebug("Failed to create note.add request alarm note.\n");
    }
}

void checkAlarm(float flowRate)
{
    // TODO: Add leak detection. If the valve is closed and we detect any flow
    //       sound the alarm.

    // If the valve's open and the flow rate falls outside our configured
    // range.
    if (state.valveOpen && (flowRate < state.flowRateAlarmMin ||
        flowRate > state.flowRateAlarmMax)) {
        uint32_t currentMs = millis();

        // After the initial alarm for an event, only resend the alarm note
        // every ALARM_REFRESH_MS, to avoid flooding Notehub.
        if (state.lastAlarmMs == 0 || (currentMs - state.lastAlarmMs) >=
            ALARM_REFRESH_MS) {
            state.lastAlarmMs = currentMs;
            alarm(flowRate);
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
    if (currentMs - state.lastFlowRateCalcMs >= FLOW_CALC_INTERVAL_MS) {
        state.flowRate = calculateFlowRate(currentMs);
        state.lastFlowRateCalcMs = currentMs;
        state.flowMeterPulseCount = 0;
    }
    
    checkAlarm(state.flowRate);

    currentMs = millis();
    // If we received a valve command (open or close), we want to publish the
    // system status immediately in response, regardless of if it's time to do
    // so based on the monitor interval. This acts as a sort of acknowledgment
    // so that the controller knows their valve command was received.
    if (state.ackValveCmd ||
        (currentMs - state.monitorLastUpdateMs >= state.monitorIntervalMs)) {
        state.monitorLastUpdateMs = currentMs;
        publishSystemStatus(state.flowRate);

        if (state.ackValveCmd) {
            state.ackValveCmd = 0;
        }
    }

// All the code below is for valve control, so only compile it in if USE_VALVE
// is defined.
#ifdef USE_VALVE
    if (state.valveOpen) {
        currentMs = millis();
        if (currentMs - state.valveOpenedMs >= MAX_OPEN_MS) {
            notecard.logDebugf("Valve open for max %u ms, closing to "
                "prevent overheating.\n", MAX_OPEN_MS);
            valveToggle();
        }
    }

    // If the ATTN interrupt hasn't occurred, nothing to do.
    if (!state.attnTriggered) {
        return;
    }

    // Re-arm the ATTN interrupt.
    attnArm();

    // TODO: Should we drain the queue and only take the most recent valve
    //       command?
    // Process all pending inbound requests.
    while (true) {
        // Pop the next available note from data.qi.
        J *req = notecard.newRequest("note.get");
        JAddStringToObject(req, "file", "data.qi");
        JAddBoolToObject(req, "delete", true);
        J *rsp = notecard.requestAndResponse(req);
        if (rsp != NULL) {

            // If an error is returned, this means that no response is pending.
            // Note that it's expected that this might return either a "note
            // does not exist" error if there are no pending inbound notes, or a
            // "file does not exist" error if the inbound queue hasn't yet been
            // created on the service.
            if (notecard.responseError(rsp)) {
                notecard.deleteResponse(rsp);
                break;
            }

            // Get the note's body
            J *body = JGetObject(rsp, "body");
            if (body != NULL) {
                char *cmd = JGetString(body, "state");
                if (cmd != NULL && strlen(cmd) != 0) {
                    handleValveCmd(cmd);
                }
                else {
                    notecard.logDebug("Unable to get valve command from note."
                        "\n");
                }
            }
            else {
                notecard.logDebug("Note body was NULL.\n");
            }
        }

        notecard.deleteResponse(rsp);
    }
#endif // USE_VALVE
}
