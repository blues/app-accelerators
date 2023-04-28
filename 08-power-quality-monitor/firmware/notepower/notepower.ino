// Copyright 2022 Blues Inc.  All rights reserved.
// Use of this source code is governed by licenses granted by the
// copyright holder including that found in the LICENSE file.

#include <Wire.h>
#include "app.h"
#include "metadata.h"
#include "Notecard.h"
#include "power_monitor.h"
#include "UpbeatLabs_MCP39F521.h"

//  For ease of seeing the values, use a program like screen to display
//  the Serial output. The serial writes the characters necessary to
//  clear the screen on a regular terminal, which means that the serial
//  output will stay in place and just update over time.
//  In that case, set USING_SCREEN to true
#define USING_SCREEN false

#define ENV_POLL_SECS 10
#define IDLE_UPDATE_PERIOD (1000 * 60 * 5)
#define LIVE_UPDATE_PERIOD (1000 * 60 * 1)

// Variables for Env Var polling
static unsigned long nextPollMs = 0;
static uint32_t lastModifiedTime = 0;
// Variables for sensor reading period when not in live mode
static unsigned long lastUpdateMs;
static unsigned long updatePeriod;

int led = LED_BUILTIN;

applicationState state = {0};
mcpContext mcp = {0};

Notecard notecard;
UpbeatLabs_MCP39F521 wattson = UpbeatLabs_MCP39F521();

// Forward declarations
bool notecardSetup(void);
void fetchEnvironmentVariables(applicationState &state);
bool pollEnvVars(void);
void sendDataToNotecard(mcpContext *data);
void printMCP39F521Data(UpbeatLabs_MCP39F521_FormattedData *data);

void setup()
{
    pinMode(led, OUTPUT);

    Serial.begin(115200); // turn on serial communication
    delay(250);
    Serial.println("Notecard & Dr. Wattson Power Quality Monitoring Example");
    Serial.println("*******************************************************");
    Serial.println("*** " __DATE__ " " __TIME__ " ***");

    Wire.begin();
    wattson.begin(); // Pass in the appropriate address, if needed. Defaults to 0x74

    // Initialize Notecard library (without doing any I/O on this task)
    notecard.setDebugOutputStream(Serial);
    notecard.begin();

    while (!notecardSetup())
    {
        delay(750);
    }

    updatePeriod = IDLE_UPDATE_PERIOD;

    mcp.first = true;

    state.envVoltageUnder = 0;
    state.envVoltageOver = 0;
    state.envVoltageChange = 0;
    state.envCurrentUnder = 0;
    state.envCurrentOver = 0;
    state.envCurrentChange = 0;
    state.envPowerUnder = 0;
    state.envPowerOver = 0;
    state.envPowerChange = 0;

    // Load the environment vars for the first time
    fetchEnvironmentVariables(state);

    lastUpdateMs = millis();
}

void loop()
{
    UpbeatLabs_MCP39F521_Data data;
    UpbeatLabs_MCP39F521_FormattedData fData;

    if (pollEnvVars())
    {
        fetchEnvironmentVariables(state);
    }

    // Read from MCP
    int readMCPretval = wattson.read(&data, NULL);
    if (readMCPretval == UpbeatLabs_MCP39F521::SUCCESS)
    {
        wattson.convertRawData(&data, &fData);

        if (mcp.first) {
            mcp.first = false;
            mcp.maxVoltage = mcp.lastVoltage = fData.voltageRMS;
            mcp.maxCurrent = mcp.lastCurrent = fData.currentRMS;
            mcp.maxPower = mcp.lastPower = fData.activePower;
        }

        // Check for Alarms based on last readings
        if (state.envVoltageUnder != 0 && fData.voltageRMS < state.envVoltageUnder) {
            sendAlert("Under voltage detected!");
        }
        if (state.envVoltageOver != 0 && fData.voltageRMS > state.envVoltageOver) {
            sendAlert("Over voltage detected!");
        }

        float d = mcp.maxVoltage * (state.envVoltageChange * 0.01) < 1 ? 1 : mcp.maxVoltage * (state.envVoltageChange * 0.01);
        if (state.envVoltageChange != 0 && ((fData.voltageRMS < mcp.lastVoltage-d) || fData.voltageRMS > mcp.lastVoltage+d)) {
            sendAlert("Voltage changed outside of set threshold!");
        }
        if (fData.voltageRMS <= ZERO_VOLTS) {
            sendAlert("No voltage detected!");
        }

        if (state.envCurrentUnder != 0 && fData.currentRMS < state.envCurrentUnder) {
            sendAlert("Under current detected!");
        }
        if (state.envCurrentOver != 0 && fData.currentRMS > state.envCurrentOver) {
            sendAlert("Over current detected!");
        }
        d = mcp.maxCurrent * (state.envCurrentChange * 0.01) < 1 ? 1 : mcp.maxCurrent * (state.envCurrentChange * 0.01);
        if (state.envCurrentChange != 0 && ((fData.currentRMS < mcp.lastCurrent-d) || fData.currentRMS > mcp.lastCurrent+d)) {
            sendAlert("Current changed outside of set threshold!");
        }

        if (state.envSendPowerAlarms) {
            if (state.envPowerUnder != 0 && fData.activePower < state.envPowerUnder) {
                sendAlert("Under power detected!");
            }
            if (state.envPowerOver != 0 && fData.activePower > state.envPowerOver) {
                sendAlert("Over power detected!");
            }

            d = mcp.maxPower * (state.envPowerChange * 0.01) < 1 ? 1 : mcp.maxPower * (state.envPowerChange * 0.01);
            if (state.envPowerChange != 0 && ((fData.activePower < mcp.lastPower-d) || fData.activePower > mcp.lastPower+d)) {
                sendAlert("Power changed outside of set threshold!");
            }
            if (fData.currentRMS <= ZERO_AMPS) {
                sendAlert("No power detected!");
            }
        }

        // Update mcpContext with latest readings
        mcp.lastVoltage = fData.voltageRMS;
        if (mcp.lastVoltage > mcp.maxVoltage) {
            mcp.maxVoltage = mcp.lastVoltage;
        }
        mcp.lastCurrent = fData.currentRMS;
        if (mcp.lastCurrent > mcp.maxCurrent) {
            mcp.maxCurrent = mcp.lastCurrent;
        }
        mcp.lastPower = fData.activePower;
        if (mcp.lastPower > mcp.maxPower) {
            mcp.maxPower = mcp.lastPower;
        }
        mcp.lastReactivePower = fData.reactivePower;
        mcp.lastApparentPower = fData.apparentPower;
        mcp.lastPowerFactor = fData.powerFactor;
        mcp.lastFrequency = fData.lineFrequency;

        // Send Heartbeat messages
        const uint32_t currentMillis = millis();
        if (currentMillis - lastUpdateMs >= updatePeriod)
        {
            printMCP39F521Data(&fData);
            // Change to use mcpContext
            sendDataToNotecard(&mcp);
            lastUpdateMs = currentMillis;
        }
    }
    else
    {
        Serial.print("Error returned! ");
        Serial.println(readMCPretval);
    }
}

void sendDataToNotecard(mcpContext *data)
{
    J *body = NoteNewBody();
    JAddNumberToObject(body, DATA_FIELD_VOLTAGE, data->lastVoltage);
    JAddNumberToObject(body, DATA_FIELD_CURRENT, data->lastCurrent);
    JAddNumberToObject(body, DATA_FIELD_POWER, data->lastPower);
    JAddNumberToObject(body, DATA_FIELD_MAX_VOLTAGE, data->maxVoltage);
    JAddNumberToObject(body, DATA_FIELD_MAX_CURRENT, data->maxCurrent);
    JAddNumberToObject(body, DATA_FIELD_MAX_POWER, data->maxPower);
    JAddNumberToObject(body, DATA_FIELD_FREQUENCY, data->lastFrequency);
    JAddNumberToObject(body, DATA_FIELD_POWERFACTOR, data->lastPowerFactor);
    JAddNumberToObject(body, DATA_FIELD_APPARENT, data->lastApparentPower);
    JAddNumberToObject(body, DATA_FIELD_REACTIVE, data->lastReactivePower);
    JAddStringToObject(body, DATA_FIELD_APP, DATA_FIELD_APP_NAME);

    J *req = notecard.newCommand("note.add");
    JAddStringToObject(req, "file", DATA_FILENAME);
    // JAddBoolToObject(req, "sync", true);
    NoteAddBodyToObject(req, body);
    notecard.sendRequest(req);
}

void sendAlert(const char *reason)
{
    J *body = NoteNewBody();
    JAddStringToObject(body, ALERT_FIELD_REASON, reason);

    J *req = notecard.newCommand("note.add");
    JAddStringToObject(req, "file", ALERT_FILENAME);
    JAddBoolToObject(req, "sync", true);
    NoteAddBodyToObject(req, body);
    notecard.sendRequest(req);
}

void printMCP39F521Data(UpbeatLabs_MCP39F521_FormattedData *data)
{
    Serial.println();
    Serial.print("New reading @ ");
    Serial.println(millis());
    Serial.print(F("Voltage = "));
    Serial.println(data->voltageRMS, 4);
    Serial.print(F("Current (Amps) = "));
    Serial.println(data->currentRMS, 4);
    Serial.print(F("Line Frequency (Hz) = "));
    Serial.println(data->lineFrequency, 4);
    Serial.print("Analog Input Voltage = ");
    Serial.println(data->analogInputVoltage, 4);
    Serial.print(F("Power Factor (active / apparent) = "));
    Serial.println(data->powerFactor, 4);
    Serial.print(F("Active Power (Watts) = "));
    Serial.println(data->activePower, 4);
    Serial.print(F("Reactive Power  (VAR) = "));
    Serial.println(data->reactivePower, 4);
    Serial.print(F("Apparent Power (VA) = "));
    Serial.println(data->apparentPower, 4);
    Serial.println();
}

// Set up the Notecard in preparation for the mcp task
bool notecardSetup(void)
{
    // Initialize the Notecard for I2C
    J *req = notecard.newRequest("hub.set");
    JAddStringToObject(req, "product", PRODUCT_UID);
    JAddStringToObject(req, "mode", "periodic");
    JAddNumberToObject(req, "inbound", 60*24);
    JAddNumberToObject(req, "outbound", 60);
    if (!notecard.sendRequest(req)) {
        Serial.printf("notecard not responding\n");
        return false;
    }

    // Add the notefile template
    J *body = JCreateObject();
    JAddStringToObject(body, DATA_FIELD_APP, TSTRINGV);
    JAddNumberToObject(body, DATA_FIELD_VOLTAGE, TFLOAT32);
    JAddNumberToObject(body, DATA_FIELD_CURRENT, TFLOAT32);
    JAddNumberToObject(body, DATA_FIELD_POWER, TFLOAT32);
    JAddNumberToObject(body, DATA_FIELD_MAX_VOLTAGE, TFLOAT32);
    JAddNumberToObject(body, DATA_FIELD_MAX_CURRENT, TFLOAT32);
    JAddNumberToObject(body, DATA_FIELD_MAX_POWER, TFLOAT32);
    JAddNumberToObject(body, DATA_FIELD_FREQUENCY, TFLOAT16);
    JAddNumberToObject(body, DATA_FIELD_APPARENT, TFLOAT32);
    JAddNumberToObject(body, DATA_FIELD_REACTIVE, TFLOAT32);
    JAddNumberToObject(body, DATA_FIELD_POWERFACTOR, TFLOAT16);

    req = notecard.newCommand("note.template");
    JAddStringToObject(req, "file", DATA_FILENAME);
    JAddItemToObject(req, "body", body);
    notecard.sendRequest(req);

    // Notify Notehub of the current firmware version
    req = notecard.newRequest("dfu.status");
    if (req != NULL)
    {
        JAddStringToObject(req, "version", firmwareVersion());
        notecard.sendRequest(req);
    }

    // Enable Outboard DFU
    req = notecard.newRequest("card.dfu");
    if (req != NULL)
    {
        JAddStringToObject(req, "name", "stm32");
        JAddBoolToObject(req, "on", true);
        notecard.sendRequest(req);
    }

    // Done
    return true;

}

void fetchEnvironmentVariables(applicationState &vars)
{
  J *req = notecard.newRequest("env.get");

  J *names = JAddArrayToObject(req, "names");
  JAddItemToArray(names, JCreateString("heartbeat_mins"));
  JAddItemToArray(names, JCreateString("alert_under_voltage"));
  JAddItemToArray(names, JCreateString("alert_over_voltage"));
  JAddItemToArray(names, JCreateString("alert_change_voltage_percent"));
  JAddItemToArray(names, JCreateString("alert_under_current_amps"));
  JAddItemToArray(names, JCreateString("alert_over_current_amps"));
  JAddItemToArray(names, JCreateString("alert_change_current_percent"));
  JAddItemToArray(names, JCreateString("send_power_alarms"));
  JAddItemToArray(names, JCreateString("alert_under_power_watts"));
  JAddItemToArray(names, JCreateString("alert_over_power_watts"));
  JAddItemToArray(names, JCreateString("alert_change_power_percent"));

  J *rsp = notecard.requestAndResponse(req);
  if (rsp != NULL)
  {
    if (notecard.responseError(rsp))
    {
      notecard.deleteResponse(rsp);
      return;
    }

    // Get the note's body
    J *body = JGetObject(rsp, "body");
    if (body != NULL)
    {
        // Update heartbeat period
        vars.envHeartbeatMins = JAtoN(JGetString(body, "heartbeat_mins"), NULL);

        // Update the voltage monitoring-related env vars
        vars.envVoltageUnder = JAtoN(JGetString(body, "alert_under_voltage"), NULL);
        vars.envVoltageOver = JAtoN(JGetString(body, "alert_over_voltage"), NULL);
        vars.envVoltageChange = JAtoN(JGetString(body, "alert_change_voltage_percent"), NULL);
        if (vars.envVoltageUnder == 0.0 && vars.envVoltageOver == 0.0 && vars.envVoltageChange == 0) {
            vars.envVoltageChange = 15.0;
        }

        // Update the current monitoring-related env vars
        vars.envCurrentUnder = JAtoN(JGetString(body, "alert_under_current_amps"), NULL);
        vars.envCurrentOver = JAtoN(JGetString(body, "alert_over_current_amps"), NULL);
        vars.envCurrentChange = JAtoN(JGetString(body, "alert_change_current_percent"), NULL);
        if (vars.envCurrentUnder == 0.0 && vars.envCurrentOver == 0.0 && vars.envCurrentChange == 0) {
            vars.envCurrentChange = 15.0;
        }

        // Update the power monitoring-related env vars
        vars.envPowerUnder = JAtoN(JGetString(body, "alert_under_power_watts"), NULL);
        vars.envPowerOver = JAtoN(JGetString(body, "alert_over_power_watts"), NULL);
        vars.envPowerChange = JAtoN(JGetString(body, "alert_change_power_percent"), NULL);
        if (vars.envPowerUnder == 0.0 && vars.envPowerOver == 0.0 && vars.envPowerChange == 0) {
            vars.envPowerChange = 15.0;
        }

        char *sendAlarms = JGetString(body, "send_power_alarms");
        vars.envSendPowerAlarms = (strcmp(sendAlarms, "true") == 0 || strcmp(sendAlarms, "1") == 0);

        updatePeriod = vars.envHeartbeatMins > 0 ? (vars.envHeartbeatMins * 1000 * 60) : IDLE_UPDATE_PERIOD;
    }
  }
  notecard.deleteResponse(rsp);
}

bool pollEnvVars()
{
  if (millis() < nextPollMs)
  {
    return false;
  }

  nextPollMs = millis() + (ENV_POLL_SECS * 1000);

  J *rsp = notecard.requestAndResponse(notecard.newRequest("env.modified"));

  if (rsp == NULL)
  {
    return false;
  }

  uint32_t modifiedTime = JGetInt(rsp, "time");
  notecard.deleteResponse(rsp);
  if (lastModifiedTime == modifiedTime)
  {
    return false;
  }

  lastModifiedTime = modifiedTime;

  return true;
}
