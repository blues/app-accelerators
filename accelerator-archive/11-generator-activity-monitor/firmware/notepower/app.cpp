// Copyright 2022 Blues Inc.  All rights reserved.
// Use of this source code is governed by licenses granted by the
// copyright holder including that found in the LICENSE file.

#include "app.h"
#include "UpbeatLabs_MCP39F521.h"
#include "TicksTimer.h"

// MCP (Dr Wattson) Hardware definitions
#define	MCP_I2C_ADDRESS_BASE	0x74
#define	MCP_I2C_INSTANCES		4
#define REPORT_REASONS_LENGTH   (256)

// Switched outputs or sensed inputs
typedef struct {
    const char *ovar;   // output variable name
    const char *ivar;   // input variable name
    int pin;
    bool on;
    bool output;        // pin mode is output when true, input when false.
    bool init;
} pindef;

pindef ioPin[] = {
    {.ovar = "switch1", .ivar = "input1", .pin = D10, .on = false, .init = false},
    {.ovar = "switch2", .ivar = "input2", .pin = D11, .on = false, .init = false},
    {.ovar = "switch3", .ivar = "input3", .pin = D12, .on = false, .init = false},
    {.ovar = "switch4", .ivar = "input4", .pin = D13, .on = false, .init = false},
};
#define ioPins ((int)(sizeof(ioPin)/sizeof(ioPin[0])))


// Notefile/Note definitions
#define	DATA_FILENAME			"power.qo"
#define DATA_FIELD_APP			"app"
#define DATA_FIELD_ALERT		"alert"
#define DATA_FIELD_INSTANCE		"instance"
#define DATA_FIELD_VOLTAGE		"voltage"
#define DATA_FIELD_CURRENT		"current"
#define DATA_FIELD_POWER		"power"
#define DATA_FIELD_FREQUENCY	"frequency"
#define DATA_FIELD_REACTIVE     "reactivePower"
#define DATA_FIELD_APPARENT     "apparentPower"
#define DATA_FIELD_POWERFACTOR  "powerFactor"
#define DATA_FIELD_PIN_ACTIVE   "pinActive"
#define DATA_FIELD_VIBRATION    "vibration"
#define DATA_FIELD_VIBRATION_RAW "vibration_raw"
#define DATA_FIELD_EVENT_COUNTER "counter"

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
float envVibrationOff = 0;
float envVibrationUnder = 0;
float envVibrationOver = 0;
int8_t envVibrationActiveLine = 0;

// Time used to determine whether or not we should refresh the environment vars
int64_t environmentModifiedTime = 0;
float vibrationAccumulator = 0;
int32_t vibrationCount = 0;
const int32_t vibrationSamples = 5;
float vibration = 0;
int32_t eventCounter = 0;

/**
 * @brief Describes the type of power for an MCP, which is correlated with the activity state of
 * the corresponding GPIO pin.
 */
enum class PowerActivityAlert {
    NONE,   /* no alerts related to activity of the line pin */
    LOAD,   /* power is supplied to a load, should be 0 power consumed when inactive */
    SUPPLY  /* power is provided by a supply, current and voltage should be 0 when off. */
};

// MCP-specific context used to manage instances.
typedef struct {
    uint8_t taskID;
    uint8_t i2cAddress;
    UpbeatLabs_MCP39F521 wattson;
    float lastApparentPower;
    float lastReactivePower;
    float lastPowerFactor;
    float lastVoltage;
    float lastCurrent;
    float lastPower;
    float maxVoltage;
    float maxCurrent;
    float maxPower;
    JTIME heartbeatDue;
    uint32_t heartbeatMins;
    PowerActivityAlert activityAlert;
    float startup;              // duration in seconds
    float shutdown;             // duration in seconds
    ArduinoTicksTimer suppressActivityAlarmUntil;    // when alarm suppression begins
    bool first;
    char lastReasons[REPORT_REASONS_LENGTH];
} mcpContext;
mcpContext mcp[MCP_I2C_INSTANCES];
uint32_t mcpSchedMs[MCP_I2C_INSTANCES];

static_assert(MCP_I2C_INSTANCES==ioPins, "Each MCP instance should have a dedicated IO pin.");

// Forwards
bool refreshEnvironmentVars(void);
void updateEnvironment(J *body);
float computeVibrationFromAccelerometer(int x, int y, int z);

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
                mcp[tasks].lastReasons[0] = 0;
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

    // Initialize the Notecard for I2C and for the rtos
    J *req = notecard.newRequest("hub.set");
    JAddStringToObject(req, "product", PRODUCT_UID);
    JAddStringToObject(req, "mode", "periodic");
    JAddNumberToObject(req, "inbound", 60*24);
    JAddNumberToObject(req, "outbound", 60);
    if (!notecard.sendRequest(req)) {
        debug.printf("notecard not responding\n");
        return false;
    }

    // Add the notefile template
    J *body = JCreateObject();
    JAddStringToObject(body, DATA_FIELD_APP, TSTRINGV);
    JAddStringToObject(body, DATA_FIELD_ALERT, TSTRINGV);
    JAddNumberToObject(body, DATA_FIELD_INSTANCE, TUINT8);
    JAddNumberToObject(body, DATA_FIELD_VOLTAGE, TFLOAT32);
    JAddNumberToObject(body, DATA_FIELD_CURRENT, TFLOAT32);
    JAddNumberToObject(body, DATA_FIELD_POWER, TFLOAT32);
    JAddNumberToObject(body, DATA_FIELD_FREQUENCY, TFLOAT16);
    JAddNumberToObject(body, DATA_FIELD_APPARENT, TFLOAT32);
    JAddNumberToObject(body, DATA_FIELD_REACTIVE, TFLOAT32);
    JAddNumberToObject(body, DATA_FIELD_POWERFACTOR, TFLOAT16);
    JAddNumberToObject(body, DATA_FIELD_VIBRATION_RAW, TFLOAT16);
    JAddStringToObject(body, DATA_FIELD_VIBRATION, TSTRINGV);
    JAddBoolToObject(body, DATA_FIELD_PIN_ACTIVE, TBOOL);
    JAddNumberToObject(body, DATA_FIELD_EVENT_COUNTER, TUINT32);

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
        JAddStringToObject(req, "mode", "notify,env,accel");
        JAddNumberToObject(req, "duration", 500);
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
    if (!strcmp(notificationType, "env")) {
        // Update the env modified time
        environmentModifiedTime = JGetNumber(notification, "modified");

        // Update the environment
        J *body = JGetObject(notification, "body");
        if (body != NULL) {
            updateEnvironment(body);
        }
    }
    else if (!strcmp(notificationType, "accel")) {
        int x = JGetNumber(notification, "x");
        int y = JGetNumber(notification, "y");
        int z = JGetNumber(notification, "z");
        vibrationAccumulator += computeVibrationFromAccelerometer(x, y, z);
        vibrationCount++;
        if (vibrationCount>=vibrationSamples) {
            vibration = vibrationAccumulator/vibrationCount;
            vibrationAccumulator = 0;
            vibrationCount = 0;
        }
    }
    else {
        debug.printf("notify: ignoring '%s'\n", notificationType);
    }
    // Done
    JDelete(notification);
    return 0;
}

// Per-task setup - each task handles a distinct Dr Wattson instance.
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

float computeVibrationFromAccelerometer(int x, int y, int z)
{
    // remove acceleration due to gravity
    z -= 1024;
    return sqrt(pow(x, 2) + pow(y, 2) + pow(z, 2));
}

void updatePinState(pindef& ioPin) {
    if (ioPin.init && !ioPin.output) {
        ioPin.on = digitalRead(ioPin.pin)==HIGH;
    }
}

// Nominal voltage/current for 0.  Detection is not totally precise and there is some inherent noise/drift so these are 
// set to be just above 0.
#define ZERO_VOLTS (5)
#define ZERO_AMPS (0.3)

/**
 * @brief Describes the types of check that is performed in relation to 
 * 
 */
enum class PowerCheck {
    NORMAL,  /* no additional checks for power being present are done beyond the under/over checks */
    PRESENT, /* Power is expected since the activation pin indicates the device is active */
    NOT_PRESENT, /* Power is not expected since the activation pin indicates the device is not active */
};

/**
 * @brief Determines the type of voltage check that is applicable to a monitored machine based on whether
 * the activity line is configured, and the nature of the power source.
 *
 * @param mcp 
 * @param pin 
 * @return PowerCheck 
 */
PowerCheck voltageActivityCheckRequired(mcpContext* mcp, pindef& pin) {
    if (!pin.init) {
        // activity monitoring requires the corresponding pin to be configured
        return PowerCheck::NORMAL;
    }
    PowerCheck result;
    switch (mcp->activityAlert) {
        case PowerActivityAlert::NONE: result = PowerCheck::NORMAL; break;       // activity checks not configured for this line
        case PowerActivityAlert::LOAD: result = PowerCheck::PRESENT; break;      // load is always supplied with voltage
        case PowerActivityAlert::SUPPLY: result = pin.on ? PowerCheck::PRESENT : PowerCheck::NOT_PRESENT; break;   // supply provides voltage when on
    }
    return result;
}

/**
 * @brief Determines the type of check for current required for a given line.
 * 
 * @param mcp 
 * @param pin 
 * @return PowerCheck 
 */
PowerCheck currentActivityCheckRequired(mcpContext* mcp, pindef& pin) {
    if (!pin.init || mcp->activityAlert==PowerActivityAlert::NONE) {
        // activity monitoring requires the corresponding pin to be configured
        return PowerCheck::NORMAL;
    }
    PowerCheck result;
    switch (mcp->activityAlert) {
        case PowerActivityAlert::NONE: result = PowerCheck::NORMAL; break;
        // load is expected to draw current while it's on
        case PowerActivityAlert::LOAD: result = pin.on ? PowerCheck::PRESENT : PowerCheck::NOT_PRESENT; break;       
        // supply may or may not be showing a current measurement depending on the activity of the load
        case PowerActivityAlert::SUPPLY: result = PowerCheck::NORMAL; break;
    }
    return result;
}

/**
 * @brief Describes the level of vibration detected.
 */
enum VibrationCategory {
    VIBRATION_NOT_MEASURED = 0,
    VIBRATION_NONE,
    VIBRATION_LOW,        // can't call this LOW because of Arduino #defines.
    VIBRATION_NORMAL,
    VIBRATION_HIGH
};

/**
 * @brief Determines if vibration monitoring is required.
 * @return true     Vibration monitoring is needed.
 * @return false    Vibration monitoring is not needed.
 */
inline bool isMonitoringVibration() {
    return vibration!=0.0 && (envVibrationOff != 0.0 || envVibrationUnder != 0.0 || envVibrationOver != 0.0
        && (envVibrationOff <= envVibrationUnder) && (envVibrationUnder < envVibrationOver));
}

/**
 * @brief Determines the vibration category corresponding to the vibration sensed.
 * @return VibrationCategory 
 */
VibrationCategory determineVibrationCategory() {
    if (!isMonitoringVibration())
        return VIBRATION_NOT_MEASURED;
    if (vibration <= envVibrationOff) {
        return VIBRATION_NONE;
    }
    if (vibration <= envVibrationUnder) {
        return VIBRATION_LOW;
    }
    if (vibration < envVibrationOver) {
        return VIBRATION_NORMAL;
    }
    return VIBRATION_HIGH;
}

const char* vibrationCategoryString(VibrationCategory category) {
    switch (category) {
        case VIBRATION_NOT_MEASURED: return "N/A";
        case VIBRATION_NONE: return "none";
        case VIBRATION_LOW: return "low";
        case VIBRATION_NORMAL: return "normal";
        case VIBRATION_HIGH: return "high";
    }
    return "";
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

    for (int i=0; i<ioPins; i++) {
        updatePinState(ioPin[i]);
    }

    VibrationCategory vibrationCategory = determineVibrationCategory();

    pindef& pin = ioPin[mcp->taskID];
    PowerCheck currentActivityCheck = currentActivityCheckRequired(mcp, pin);
    PowerCheck voltageActivityCheck = voltageActivityCheckRequired(mcp, pin);

    // Determine if anything requires an alert. When non-zero length, the first character is a comma, and can be ignored
    char reportReasons[REPORT_REASONS_LENGTH] = {'\0'};
    if (voltageActivityCheck != PowerCheck::NOT_PRESENT) {
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
        if (voltageActivityCheck==PowerCheck::PRESENT && data.voltageRMS <= ZERO_VOLTS) {
            strlcat(reportReasons, ",novoltage", sizeof(reportReasons));
        }
    }
    else {
        if (data.voltageRMS > ZERO_VOLTS) {
            strlcat(reportReasons, ",inactivevoltage", sizeof(reportReasons));
        }
    }

    if (currentActivityCheck != PowerCheck::NOT_PRESENT) {
        if (envCurrentUnder != 0 && data.currentRMS < envCurrentUnder) {
            strlcat(reportReasons, ",undercurrent", sizeof(reportReasons));
        }
        if (envCurrentOver != 0 && data.currentRMS > envCurrentOver) {
            strlcat(reportReasons, ",overcurrent", sizeof(reportReasons));
        }
        float d = mcp->maxCurrent * (envCurrentChange * 0.01) < 1 ? 1 : mcp->maxCurrent * (envCurrentChange * 0.01);
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
        if (currentActivityCheck == PowerCheck::PRESENT && data.currentRMS <= ZERO_AMPS) {
            strlcat(reportReasons, ",nocurrent", sizeof(reportReasons));
        }
    }
    else {
        if (data.currentRMS > ZERO_AMPS) {
            strlcat(reportReasons, ",inactivecurrent", sizeof(reportReasons));
        }
    }

    bool vibrationAlert = vibrationCategory &&
        (((envVibrationActiveLine==0 || (envVibrationActiveLine==mcp->taskID+1 && !pin.init)) && (vibrationCategory==VIBRATION_LOW || vibrationCategory==VIBRATION_HIGH)) ||
        (envVibrationActiveLine==mcp->taskID+1 && pin.init && (vibrationCategory!=(pin.on ? VIBRATION_NORMAL : VIBRATION_NONE))));
    if (vibrationAlert) {
        strlcat(reportReasons, ",vibration", sizeof(reportReasons));
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
    mcp->lastReactivePower = data.reactivePower;
    mcp->lastApparentPower = data.apparentPower;
    mcp->lastPowerFactor = data.powerFactor;

    // Exit and come back immediately if nothing to report
    if (!reportHeartbeat) {
        // when the reasons for the non-heartbeat report haven't changed, skip
        if (!strcmp(reportReasons+1, mcp->lastReasons)) {
            return quickly;
        }
        else if (!mcp->suppressActivityAlarmUntil.hasElapsed()) {
            debug.printf("mcp %d: suppressing alarms %s\n", mcp->taskID, reportReasons[1]);
            return quickly;
        }
    }
    strlcpy(mcp->lastReasons, reportReasons+1, REPORT_REASONS_LENGTH);

    // Generate a report
    J *body = NoteNewBody();
    JAddNumberToObject(body, DATA_FIELD_INSTANCE, mcp->taskID+1);
    JAddNumberToObject(body, DATA_FIELD_EVENT_COUNTER, ++eventCounter);
    if (reportReasons[0] != '\0') {
        JAddStringToObject(body, DATA_FIELD_ALERT, &reportReasons[1]);         // [1] skip the first comma
    }
    JAddNumberToObject(body, DATA_FIELD_VOLTAGE, data.voltageRMS);
    JAddNumberToObject(body, DATA_FIELD_CURRENT, data.currentRMS);
    JAddNumberToObject(body, DATA_FIELD_POWER, data.activePower);
    JAddNumberToObject(body, DATA_FIELD_FREQUENCY, data.lineFrequency);
    JAddNumberToObject(body, DATA_FIELD_POWERFACTOR, data.powerFactor);
    JAddNumberToObject(body, DATA_FIELD_APPARENT, data.apparentPower);
    JAddNumberToObject(body, DATA_FIELD_REACTIVE, data.reactivePower);
    JAddStringToObject(body, DATA_FIELD_APP, APP_NAME);

    if (pin.init) {
        JAddBoolToObject(body, DATA_FIELD_PIN_ACTIVE, pin.on);
    }

    if (vibrationCategory && (envVibrationActiveLine==0 || envVibrationActiveLine==mcp->taskID+1)) {
        JAddStringToObject(body, DATA_FIELD_VIBRATION, vibrationCategoryString(vibrationCategory));
        JAddNumberToObject(body, DATA_FIELD_VIBRATION_RAW, vibration);
    }

    J *req = notecard.newCommand("note.add");
    JAddStringToObject(req, "file", DATA_FILENAME);
    // alerts are always synced immediately. Optionally, all notes can be synced when SYNC_POWER_NOTES is true
    if (reportReasons[0] != '\0' || SYNC_POWER_MONITORING_NOTES) {
        JAddBoolToObject(req, "sync", true);
    }
    NoteAddBodyToObject(req, body);
    notecard.sendRequest(req);

    // Come back immediately
    return quickly;
}

/**
 * @brief Handles a change in activity for a given pin. This supresses alarms for the configured ramp up or ramp down time
 * to avoid spurious alarms being sent.
 * 
 * @param active        The new activity state. True for active, False for inactive.
 * @param pinIndex      The MCP that changed activity.
 */
void activityChanged(bool active, int pinIndex) {
    mcpContext& mcp = ::mcp[pinIndex];
    if (mcp.taskID) {
        float duration = active ? mcp.startup : mcp.shutdown;
        mcp.suppressActivityAlarmUntil.set(duration*1000);
    }
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

/**
 * @brief Determines if the value of an environment variable corresponds to active, inactive or undefined.
 * @param v     The environment variable value
 * @return int8_t   1 when the value is "1", "on" or "true"
 *                  0 when the value is "0", "off" or "false"
 *                  -1 for anything else
 */
int8_t parsePinSetting(const char* v) {
    if (0 == strcmp(v, "1") || 0 == strcmp(v, "on") || 0 == strcmp(v, "true")) {
        return 1;
    }
    if (0 == strcmp(v, "0") || 0 == strcmp(v, "off") || 0 == strcmp(v, "false")) {
        return 0;
    }
    return -1;
}

/**
 * @brief Updates the direction and state of an activity line from the environment variables.
 * 
 * @param body  The JSON object encoding environment variable names and values.
 * @param ioPin The pin defintion to update.
 */
bool updatePinFromEnvironment(J *body, pindef& ioPin)
{
    bool changed = false;
    const char *output = JGetString(body, ioPin.ovar);
    const char *input = JGetString(body, ioPin.ivar);

    int8_t outputSetting = parsePinSetting(output);
    int8_t inputSetting = parsePinSetting(input);

    if (outputSetting>=0) {
        if (!ioPin.output || !ioPin.init) {
            pinMode(ioPin.pin, OUTPUT);
            ioPin.output = true;
            ioPin.on = !outputSetting;    // enure pin state is set below
            ioPin.init = true;
            changed = true;
        }
        if (outputSetting != ioPin.on) { // output state has changed
            changed = true;
            digitalWrite(ioPin.pin, outputSetting==1 ? HIGH : LOW);
            ioPin.on = outputSetting;
        }
    }
    else if (inputSetting==1) {
        if (ioPin.output || !ioPin.init) {
            pinMode(ioPin.pin, INPUT);
            ioPin.output = false;
            ioPin.init = true;
            changed = true;
        }
        bool oldState = ioPin.on;
        updatePinState(ioPin);
        changed = oldState != ioPin.on;
    }
    else {
        ioPin.init = false;
        pinMode(ioPin.pin, INPUT);
        // ideally we would uninitialize the pin here, but for now, set it to input
    }
    return changed;
}

/**
 * @brief Retrieves the value of an environment variable for a specific line, or when that is not defined,
 * the general value. For a given environment variable name, the line variable name is `name_`, while the general value
 * is `name`.
 * 
 * @param mcp       The line number.
 * @param body      The body of environment variable data, providing names and values.
 * @param name      The base name of the environment variable.
 * @return const char*  The value of the environment variable defined, either for the specific line or more generally for all lines.
 */
const char* getLineEnvironmentVariable(int8_t mcp, J* body, const char* name) {
    char varName[strlen(name)+3];
    const char* result = nullptr;
    if (mcp>=0) {
        sprintf(varName, "%s_%d", name, mcp+1);
        result = JGetString(body, varName);
    }
    if (!result || strlen(result)==0) {
        result = JGetString(body, name);
    }
    return result;
}

/**
 * @brief Updates the environment for a specific monitored instance (line).
 * 
 * @param body  The body of environment variable data providing defined names and values.
 * @param mcp   The instance to update.
 */
void updateMCPEnvironment(J* body, mcpContext& mcp)
{
    const char* alert = getLineEnvironmentVariable(mcp.taskID, body, "alert_power_activity");
    PowerActivityAlert alertType = PowerActivityAlert::NONE;
    if (!strcmp(alert, "load")) {
        alertType = PowerActivityAlert::LOAD;
    }
    else if (!strcmp(alert, "supply")) {
        alertType = PowerActivityAlert::SUPPLY;
    }
    mcp.activityAlert = alertType;

    mcp.startup = JAtoN(getLineEnvironmentVariable(mcp.taskID, body, "power_activity_startup_secs"), nullptr);
    mcp.shutdown = JAtoN(getLineEnvironmentVariable(mcp.taskID, body, "power_activity_shutdown_secs"), nullptr);
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

    envVibrationOff = JAtoN(JGetString(body, "alert_off_vibration"), NULL);
    envVibrationUnder = JAtoN(JGetString(body, "alert_under_vibration"), NULL);
    envVibrationOver = JAtoN(JGetString(body, "alert_over_vibration"), NULL);
    envVibrationActiveLine = JAtoN(JGetString(body, "alert_vibration_activity_line"), NULL);

    // disable vibration activity alerts when there are no vibration bounds configured
    if (!isMonitoringVibration()) {
        envVibrationActiveLine = 0;
    }

    for (int i=0; i<MCP_I2C_INSTANCES; i++) {
        if (mcp[i].i2cAddress) {
            updateMCPEnvironment(body, mcp[i]);
        }
    }

    // Turn on/off each switch as it changes and handle changes in activity for a given line
    for (int i=0; i<ioPins; i++) {
        if (updatePinFromEnvironment(body, ioPin[i])) {
            activityChanged(ioPin[i].on, i);
        }
    }
}

void NoteUserAgentUpdate(J *ua) {
    JAddStringToObject(ua, DATA_FIELD_APP, APP_NAME);
}

