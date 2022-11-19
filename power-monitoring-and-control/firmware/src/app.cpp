// Copyright 2022 Blues Inc.  All rights reserved.
// Use of this source code is governed by licenses granted by the
// copyright holder including that found in the LICENSE file.

#include "app.h"
#include "UpbeatLabs_MCP39F521.h"

// MCP Hardware definitions
#define	MCP_I2C_ADDRESS_BASE	0x74
#define	MCP_I2C_INSTANCES		4

// Switched outputs
typedef struct {
    const char *var;
    int pin;
    bool on;
    bool init;
} pindef;
pindef ioPin[] = {
    {.var = "switch1", .pin = D10, .on = false, .init = false},
    {.var = "switch2", .pin = D11, .on = false, .init = false},
    {.var = "switch3", .pin = D12, .on = false, .init = false},
    {.var = "switch4", .pin = D13, .on = false, .init = false},
};
#define ioPins ((int)(sizeof(ioPin)/sizeof(ioPin[0])))

// Notefile/Note definitions
#define	DATA_FILENAME			"power.qo"
#define DATA_FIELD_EVENT		"event"
#define DATA_FIELD_INSTANCE		"instance"
#define DATA_FIELD_VOLTAGE		"voltage"
#define DATA_FIELD_CURRENT		"current"
#define DATA_FIELD_POWER		"power"
#define DATA_FIELD_FREQUENCY	"frequency"

// Cached copies of environment variables
uint32_t envHeartbeatMins = 0;
float envVoltageUnder = 0;
float envVoltageOver = 0;
float envVoltageChange = 0;
float envCurrentUnder = 0;
float envCurrentOver = 0;
float envCurrentChange = 0;
float envPowerUnder = 0;
float envPowerOver = 0;
float envPowerChange = 0;

// Time used to determine whether or not we should refresh the environment vars
int64_t environmentModifiedTime = 0;

// MCP-specific context used to manage instances.
typedef struct {
    uint8_t taskID;
    uint8_t i2cAddress;
    UpbeatLabs_MCP39F521 wattson;
    float lastVoltage;
    float lastCurrent;
    float lastPower;
    float maxVoltage;
    float maxCurrent;
    float maxPower;
    JTIME heartbeatDue;
    uint32_t heartbeatMins;
    bool first;
} mcpContext;
mcpContext mcp[MCP_I2C_INSTANCES];
uint32_t mcpSchedMs[MCP_I2C_INSTANCES];

// Forwards
bool refreshEnvironmentVars(void);
void updateEnvironment(J *body);

// Dynamically sense the instance(s) of the device that are present
uint32_t appTasks(uint32_t **taskSchedMs, uint8_t **contextBase, uint32_t *contextSize)
{

    // Detect MCP instances
    uint8_t tasks = 0;
    for (;;) {
        _lock_wire();
        for (uint8_t i=0; i<=MCP_I2C_INSTANCES; i++) {
            Wire.beginTransmission(MCP_I2C_ADDRESS_BASE + i);
            if (Wire.endTransmission() == 0) {
                mcp[tasks].taskID = i;
                mcp[tasks].i2cAddress = MCP_I2C_ADDRESS_BASE + i;
                debug.printf("mcp %d found at i2c 0x%02x\n", mcp[tasks].taskID, mcp[tasks].i2cAddress);
                _delay(100);
                tasks++;
            }
        }
        _unlock_wire();
        if (tasks > 0) {
            break;
        }
        debug.printf("waiting for connection to an MCP instance\n");
        _delay(1000);
        Wire.end();
        Wire.begin();
    }

    // Done
    *taskSchedMs = mcpSchedMs;
    *contextBase = (uint8_t *) mcp;
    *contextSize = sizeof(mcpContext);
    return tasks;

}

// Set up the Notecard in preparation for the mcp task
bool appSetup(void)
{

    // Inititialize digital I/O's for switches
    for (int i=0; i<ioPins; i++) {
        pinMode(ioPin[i].pin, OUTPUT);
    }

    // Initialize the Notecard for I2C and for the rtos
    J *req = notecard.newRequest("hub.set");
    JAddStringToObject(req, "product", NOTEHUB_PRODUCT_UID);
    JAddStringToObject(req, "mode", "continuous");
    JAddBoolToObject(req, "sync", true);
    JAddNumberToObject(req, "inbound", 60*24);
    JAddNumberToObject(req, "outbound", 60);
    if (!notecard.sendRequest(req)) {
        debug.printf("notecard not responding\n");
        return false;
    }

    // Add the notefile template
    J *body = JCreateObject();
    JAddStringToObject(body, DATA_FIELD_EVENT, TSTRINGV);
    JAddNumberToObject(body, DATA_FIELD_INSTANCE, TUINT8);
    JAddNumberToObject(body, DATA_FIELD_VOLTAGE, TFLOAT32);
    JAddNumberToObject(body, DATA_FIELD_CURRENT, TFLOAT32);
    JAddNumberToObject(body, DATA_FIELD_POWER, TFLOAT32);
    JAddNumberToObject(body, DATA_FIELD_FREQUENCY, TFLOAT16);
    req = notecard.newCommand("note.template");
    JAddStringToObject(req, "file", DATA_FILENAME);
    JAddItemToObject(req, "body", body);
    notecard.sendRequest(req);

    // Set the AUX port into "receive notifications" mode
    if (serialIsAvailable()) {
#if serialIsAvailable() && SERIAL_RX_BUFFER_SIZE < 4096
#error INSUFFICIENT SERIAL BUFFER RX SIZE (SEE ARDUINO.txt)
#endif
        Serial.begin(115200);
        req = notecard.newCommand("card.aux.serial");
        JAddStringToObject(req, "mode", "notify,env");
        notecard.sendRequest(req);
    }

    // Load the environment vars for the first time
    refreshEnvironmentVars();

    // Done
    return true;

}

// Perform repetitive tasks
uint32_t appLoop(void)
{

    // If we're not updating via serial notify, poll to see if the environment vars have been modified
    if (!serialIsAvailable()) {

        J *rsp = notecard.requestAndResponse(notecard.newRequest("env.modified"));
        if (rsp != NULL) {
            if (!notecard.responseError(rsp) && environmentModifiedTime != (int64_t) JGetNumber(rsp, "time")) {
                refreshEnvironmentVars();
            }
            notecard.deleteResponse(rsp);
        }

        // Poll only occasionally
        return 15000;

    }

    // See if we've got any data available on the serial port
    if (!Serial.available()) {
        return 0;
    }

    // Define the scope of the string received from Serial so it's freed immediately after parsing
    J *notification;
    {
        String receivedString;

        // Receive a JSON object over the serial line
        receivedString = Serial.readStringUntil('\n');
        if (receivedString == NULL) {
            return 0;
        }

        // Parse the JSON object
        const char *JSON = receivedString.c_str();
        debug.printf("notification: %s\n", JSON);
        notification = JParse(JSON);
        if (notification == NULL) {
            debug.printf("notify: not a JSON object\n");
            return 0;
        }
    }

    // Get notification type, and ignore if not an "env" notification
    const char *notificationType = JGetString(notification, "type");
    if (0 != strcmp(notificationType, "env")) {
        debug.printf("notify: ignoring '%s'\n", notificationType);
        JDelete(notification);
        return 0;
    }

    // Update the env modified time
    environmentModifiedTime = JGetNumber(notification, "modified");

    // Update the environment
    J *body = JGetObject(notification, "body");
    if (body != NULL) {
        updateEnvironment(body);
    }

    // Done
    JDelete(notification);
    return 0;

}

// Per-task setup
bool taskSetup(void *vmcp)
{
    mcpContext *mcp = (mcpContext *) vmcp;

    // Initialize state
    mcp->first = true;
    mcp->heartbeatDue = 0;
    mcp->heartbeatMins = 0;

    // Initialize communications with the card
    _lock_wire();
    mcp->wattson = UpbeatLabs_MCP39F521();
    mcp->wattson.begin(mcp->i2cAddress);
    _unlock_wire();

    // Done
    return true;

}

// Per-task loop
uint32_t taskLoop(void *vmcp)
{
    mcpContext *mcp = (mcpContext *) vmcp;

    // This is how quickly we should come back in normal circumstances.  We choose
    // this value because it tends to be good enough to debounce the 'rise' or 'fall'
    // when something is powering on and off.
    uint32_t quickly = 250;

    // If it's time to send a heartbeat, do so
    bool reportHeartbeat = false;
    if (mcp->heartbeatMins != envHeartbeatMins) {
        mcp->heartbeatDue = 0;
        mcp->heartbeatMins = envHeartbeatMins;
    }
    if (mcp->heartbeatMins != 0 && NoteTimeValidST()) {
	    JTIME now = NoteTimeST();
		if (now > mcp->heartbeatDue) {
	        mcp->heartbeatDue = now + (mcp->heartbeatMins * 60);
		    reportHeartbeat = true;
		}
    }

    // Read data
    _lock_wire();
    UpbeatLabs_MCP39F521_Data rawData;
    int ret = mcp->wattson.read(&rawData, NULL);
    _unlock_wire();
    if (ret != UpbeatLabs_MCP39F521::SUCCESS) {
        Wire.end();
        Wire.begin();
        debug.printf("*** error reading sensor data: %d\n", ret);
        return 2500;
    }

    // Convert data to something usable
    UpbeatLabs_MCP39F521_FormattedData data;
    mcp->wattson.convertRawData(&rawData, &data);
    if (mcp->first) {
        mcp->first = false;
        mcp->maxVoltage = mcp->lastVoltage = data.voltageRMS;
        mcp->maxCurrent = mcp->lastCurrent = data.currentRMS;
        mcp->maxPower = mcp->lastPower = data.activePower;
    }

    // Determine if anything requires an alert
    char reportReasons[256] = {'\0'};
    if (envVoltageUnder != 0 && data.voltageRMS < envVoltageUnder) {
        strlcat(reportReasons, ",undervoltage", sizeof(reportReasons));
    }
    if (envVoltageOver != 0 && data.voltageRMS > envVoltageOver) {
        strlcat(reportReasons, ",overvoltage", sizeof(reportReasons));
    }
    float d = mcp->maxVoltage * (envVoltageChange * 0.01) < 1 ? 1 : mcp->maxVoltage * (envVoltageChange * 0.01);
    if (envVoltageChange != 0 && ((data.voltageRMS < mcp->lastVoltage-d) || data.voltageRMS > mcp->lastVoltage+d)) {
        strlcat(reportReasons, ",voltage", sizeof(reportReasons));
    }
    if (envCurrentUnder != 0 && data.currentRMS < envCurrentUnder) {
        strlcat(reportReasons, ",undercurrent", sizeof(reportReasons));
    }
    if (envCurrentOver != 0 && data.currentRMS > envCurrentOver) {
        strlcat(reportReasons, ",overcurrent", sizeof(reportReasons));
    }
    d = mcp->maxCurrent * (envCurrentChange * 0.01) < 1 ? 1 : mcp->maxCurrent * (envCurrentChange * 0.01);
    if (envCurrentChange != 0 && ((data.currentRMS < mcp->lastCurrent-d) || data.currentRMS > mcp->lastCurrent+d)) {
        strlcat(reportReasons, ",current", sizeof(reportReasons));
    }
    if (envPowerUnder != 0 && data.activePower < envPowerUnder) {
        strlcat(reportReasons, ",underpower", sizeof(reportReasons));
    }
    if (envPowerOver != 0 && data.activePower > envPowerOver) {
        strlcat(reportReasons, ",overpower", sizeof(reportReasons));
    }
    d = mcp->maxPower * (envPowerChange * 0.01) < 1 ? 1 : mcp->maxPower * (envPowerChange * 0.01);
    if (envPowerChange != 0 && ((data.activePower < mcp->lastPower-d) || data.activePower > mcp->lastPower+d)) {
        strlcat(reportReasons, ",power", sizeof(reportReasons));
    }

    // Remember state for next time
    mcp->lastVoltage = data.voltageRMS;
    if (mcp->lastVoltage > mcp->maxVoltage) {
        mcp->maxVoltage = mcp->lastVoltage;
    }
    mcp->lastCurrent = data.currentRMS;
    if (mcp->lastCurrent > mcp->maxCurrent) {
        mcp->maxCurrent = mcp->lastCurrent;
    }
    mcp->lastPower = data.activePower;
    if (mcp->lastPower > mcp->maxPower) {
        mcp->maxPower = mcp->lastPower;
    }

    // Exit and come back immediately if nothing to report
    if (!reportHeartbeat && reportReasons[0] == '\0') {
        return quickly;
    }

    // Generate a report
    J *body = NoteNewBody();
    JAddNumberToObject(body, DATA_FIELD_INSTANCE, mcp->taskID);
    if (reportReasons[0] != '\0') {
        // skip the first comma
        JAddStringToObject(body, DATA_FIELD_EVENT, &reportReasons[1]);
    }
    JAddNumberToObject(body, DATA_FIELD_VOLTAGE, data.voltageRMS);
    JAddNumberToObject(body, DATA_FIELD_CURRENT, data.currentRMS);
    JAddNumberToObject(body, DATA_FIELD_POWER, data.activePower);
    JAddNumberToObject(body, DATA_FIELD_FREQUENCY, data.lineFrequency);
    J *req = notecard.newCommand("note.add");
    JAddStringToObject(req, "file", DATA_FILENAME);
    if (reportReasons[0] != '\0') {
        JAddBoolToObject(req, "sync", true);
    }
    NoteAddBodyToObject(req, body);
    notecard.sendRequest(req);

    // Come back immediately
    return quickly;
}

// Re-load all env vars, returning the modified time
bool refreshEnvironmentVars()
{

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
        updateEnvironment(body);
    }

    // Done
    notecard.deleteResponse(rsp);
    return true;

}

// Update the environment from the body
void updateEnvironment(J *body)
{

	// Update heartbeat period
	envHeartbeatMins = JAtoN(JGetString(body, "heartbeat_mins"), NULL);

    // Update the voltage monitoring-related env vars
	envVoltageUnder = JAtoN(JGetString(body, "alert_under_voltage"), NULL);
	envVoltageOver = JAtoN(JGetString(body, "alert_over_voltage"), NULL);
	envVoltageChange = JAtoN(JGetString(body, "alert_change_voltage_percent"), NULL);
    if (envVoltageUnder == 0.0 && envVoltageOver == 0.0 && envVoltageChange == 0) {
        envVoltageChange = 15.0;
    }

    // Update the current monitoring-related env vars
	envCurrentUnder = JAtoN(JGetString(body, "alert_under_current_amps"), NULL);
	envCurrentOver = JAtoN(JGetString(body, "alert_over_current_amps"), NULL);
	envCurrentChange = JAtoN(JGetString(body, "alert_change_current_percent"), NULL);
    if (envCurrentUnder == 0.0 && envCurrentOver == 0.0 && envCurrentChange == 0) {
        envCurrentChange = 15.0;
    }

	// Update the power monitoring-related env vars
    envPowerUnder = JAtoN(JGetString(body, "alert_under_power_watts"), NULL);
    envPowerOver = JAtoN(JGetString(body, "alert_over_power_watts"), NULL);
    envPowerChange = JAtoN(JGetString(body, "alert_change_power_percent"), NULL);
    if (envPowerUnder == 0.0 && envPowerOver == 0.0 && envPowerChange == 0) {
        envPowerChange = 15.0;
    }

    // Turn on/off each switch as it changes
    for (int i=0; i<ioPins; i++) {
        const char *v = JGetString(body, ioPin[i].var);
        if (0 == strcmp(v, "1") || 0 == strcmp(v, "on")) {
            if (!ioPin[i].on || !ioPin[i].init) {
                digitalWrite(ioPin[i].pin, HIGH);
                ioPin[i].on = true;
                ioPin[i].init = true;
            }
        } else if (0 == strcmp(v, "0")|| 0 == strcmp(v, "off")) {
            if (ioPin[i].on || !ioPin[i].init) {
                digitalWrite(ioPin[i].pin, LOW);
                ioPin[i].on = false;
                ioPin[i].init = true;
            }
        } else {
            ioPin[i].init = false;
        }
    }

}
