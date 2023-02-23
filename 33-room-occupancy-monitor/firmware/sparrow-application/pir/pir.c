// Copyright 2023 Blues Inc.  All rights reserved.
// Use of this source code is governed by licenses granted by the
// copyright holder including that found in the LICENSE file.

#include "pir.h"

// ST Header(s)
#include <main.h>

// Blues Header(s)
#include <framework.h>
#include <note.h>

// Addresses of the BME sensor used to identify
// the Sparrow Reference Sensor Board
#define BME280_I2C_ADDR_PRIM        UINT8_C(0x76)
#define BME280_I2C_ADDR_SEC         UINT8_C(0x77)
#define BME280_I2C_RETRY_COUNT      3
#define BME280_I2C_TIMEOUT_MS       100

// Suppression timer for PIR activity, so that in a high-activity area
// it isn't continuously sending messages
#define PIR_SUPPRESSION_MINS        5
static int64_t lastInterruptMs = 0;

// States for the local state machine
#define STATE_MOTION_CHECK  0

// Special request IDs
#define REQUESTID_TEMPLATE  1

// The dynamic filename of the application specific queue.
// NOTE: The Gateway will replace `*` with the originating node's ID.
#define APPLICATION_NOTEFILE "*#motion.qo"

#ifndef PIR_THRESHOLD_DEFAULT
#define PIR_THRESHOLD_DEFAULT 100
#endif

// TRUE if we've successfully registered the template
static bool templateRegistered = false;

// Number of motion events
static uint32_t motionEvents = 0;
static uint32_t motionEventsTotal = 0;

// Forwards
static void addNote(bool immediate);
static inline bool isSparrowReferenceSensorBoard(void);
static bool registerNotefileTemplate(void);
static void resetInterrupt(void);

typedef struct {
    uint8_t sensorThreshold;
} AppState;

static AppState appState = {
    .sensorThreshold = PIR_THRESHOLD_DEFAULT
};

// Scheduled App One-Time Init
bool pirInit()
{

    // Do not initialize if this isn't a Sparrow Reference Sensor Board
    if (!isSparrowReferenceSensorBoard()) {
        return false;
    }

    // Register the app
    schedAppConfig config = {
        .name = "pir",
        .activationPeriodSecs = 60 * 60,
        .pollPeriodSecs = 15,
        .activateFn = NULL,
        .interruptFn = pirISR,
        .pollFn = pirPoll,
        .responseFn = pirResponse,
        .appContext = &appState,
    };
    if (schedRegisterApp(&config) < 0) {
        return false;
    }

    // Initialize GPIOs as per data sheet 2.6
    GPIO_InitTypeDef init = {0};
    init.Mode = GPIO_MODE_OUTPUT_PP;
    init.Pull = GPIO_NOPULL;
    init.Speed = GPIO_SPEED_FREQ_LOW;
    init.Pin = PIR_SERIAL_IN_Pin;
    HAL_GPIO_Init(PIR_SERIAL_IN_Port, &init);
    HAL_GPIO_WritePin(PIR_SERIAL_IN_Port, PIR_SERIAL_IN_Pin, GPIO_PIN_RESET);

    // Prepare to configure the module
    uint32_t configurationRegister = 0;

    // Threshold [24:17] 8 bits (Detection threshold on BPF value)
    // The pyroelectric signal must exceed that threshold after band-pass filtering in order to be recognized by
    // the pulse counter. The threshold applies to positive as well as negative pulses by the pyroelectric element.
    // The threshold must be configured to a value which meets the application's requirements.
    // Lower threshold means longer detection range, higher threshold means shorter detection range. You want the
    // threshold to be set as high as possible to avoid false triggers, but you want it set low enough so you get
    // the detection range you need to achieve.
    uint32_t threshold = appState.sensorThreshold;
    configurationRegister |= ((threshold & 0xff) << 17);

    // Blind Time [16:13] 4 bits (0.5 s + [Reg Val] * 0.5 s)
    // The purpose of blind time is to avoid immediate re-triggering after a motion event was detected and
    // an interrupt was signalized. The blind time starts counting after pulling the "DIRECT LINK" line from
    // high to low by the host system. The time can be selected between 0.5 s and 8 s in steps of 0.5 s.
    // This parameter is only critical if you want to detect multiple motion events while always staying in
    // the wake up mode. This is typically not the way the sensor is used. The typical sensor used case
    // is: pyro is in wake up mode, detects a motion event, generates an interrupt and the application
    // takes an action. In that case the blind time is irrelevant.
    uint32_t blindTime = 2;
    configurationRegister |= ((blindTime & 0x0f) << 13);

    // Pulse Counter [12:11] 2 bits (1 + [Reg Val])
    // The amount of pulses above the threshold is counted in a specified window time. It triggers
    // the alarm event (DIRECT LINK is pushed by the ASIC from low to high) in wake up operation mode. It can
    // be configured from 1 up to 4 pulses. The amount of pulses is application specific.
    // This is the number of times the threshold must be exceeded to constitute a motion event and for
    // the pyro to generate an interrupt. A low pulse count is more sensitive to small amplitude motion
    // but is more prone to have false triggers due to thermal events.
    uint32_t pulseCounter = 2;
    configurationRegister |= ((pulseCounter & 0x03) << 11);

    // Window Time [10:9] 2 bits (2 s + [Reg Val] * 2 s)
    // The pulse counter is evaluated for pulses above the threshold within a given moving window
    // time. The window time can be set from 2 s up to 8 s in intervals of 2 s. The best setting depends on
    // the application specific motion pattern.
    // This is the window of time in which the threshold must be exceeded the number of times as defined
    // in the pulse counter register, to constitute a motion event for the pyro to generate an interrupt.
    // This also helps filter out motion events from thermal events since both types of events do not
    // have the same temporal signature.
    uint32_t windowTime = 3;
    configurationRegister |= ((windowTime & 0x03) << 9);

    // Operation Modes [8:7] 2 bits (0: Forced Readout 1: Interrupt Readout 2: Wake Up 3: Reserved)
    // In "Forced" and "Interrupt Readout" mode the "DIRECT LINK" interface is used to read raw data and
    // configuration settings. The source is defined by the filter source setting. Please refer to
    // section 2.7 for communication details. In wake up operation mode, the internal alarm event unit is
    // used to generate a low to high transition on the "DIRECT LINK" line once the criteria for motion was
    // met. The host system must pull this line from high to low in order to reset the alarm unit.
    uint32_t operationModes = 2;        // Wake Up mode
    configurationRegister |= ((operationModes & 0x03) << 7);

    // Signal Source [6:5] 2 bits (0: PIR (BPF) 1: PIR (LPF) 2: Reserved 3: Temperature Sensor)
    // The signal of the pyroelectric sensor can be observed after low-pass filtering (LPF). The data on the
    // "DIRECT LINK" line will be an unsigned integer in the range of 0 counts to 16,383 counts.
    // After band pass filtering (BPF) the data will be a signed integer in the range of -8192 counts to +8191 counts.
    // If the source is set to the internal temperature sensor, an unsigned integer in the range of 0 counts to
    // 16,383 counts will be provided which is proportional to the internal temperature of the sensor. This can
    // be used to ignore false triggers due to difficult conditions such as sudden temperature changes above 1 K min^-1.
    // For motion detection this register should always be set to 0 (Band pass filtered Pyro output).
    uint32_t signalSource = 0;
    configurationRegister |= ((signalSource & 0x03) << 5);

    // Reserved1 [4:3] 2 bits (Must be set to the value 2)
    uint32_t reserved1 = 2;
    configurationRegister |= ((reserved1 & 0x03) << 3);

    // HPF Cut-Off [2] 1 bit (0: 0.4 Hz 1: 0.2 Hz)
    // The optimal value depends on the motion pattern and is application specific. Generally
    // speaking, the lower cut-off value is used for long distance motion detection.
    // This setting is to be determined experimentally based on the detection range you want to achieve,
    // lens design and speed of motion you want to detect. However a good starting point is to set
    // that register at 0 (0.4Hz).
    uint32_t hpfCutoff = 0;             // Long-distance
    configurationRegister |= ((hpfCutoff & 0x01) << 2);

    // Reserved2 [1] 1 bit (Must be set to the value 0)
    uint32_t reserved2 = 0;
    configurationRegister |= ((reserved2 & 0x01) << 1);

    // Pulse Detection Mode [0] 1 bit (Count with (0) or without (1) BPF sign change)
    // If the mode is set to 0, pulses above the threshold are only counted when the sign of
    // the signal changed after BPF. If set to 1, no zero crossing is required.
    // This register is to decide if you want the threshold to be exceeded with or without sign change
    // to be counted as a motion event. With sign change makes it more robust against false triggers
    // but makes it more difficult to detect small amplitude motion at long distances.
    uint32_t pulseDetectionMode = 0;
    configurationRegister |= ((pulseDetectionMode & 0x01) << 0);

    // Send the register according to 2.6 timing
    HAL_DelayUs(750);       // tSLT must be at least 580uS to prepare for accepting config
    for (int i=24; i>=0; --i) {
        HAL_GPIO_WritePin(PIR_SERIAL_IN_Port, PIR_SERIAL_IN_Pin, GPIO_PIN_RESET);
        HAL_DelayUs(5);     // tSL can be very short
        HAL_GPIO_WritePin(PIR_SERIAL_IN_Port, PIR_SERIAL_IN_Pin, GPIO_PIN_SET);
        HAL_DelayUs(1);     // between tSL and tSHD
        HAL_GPIO_WritePin(PIR_SERIAL_IN_Port, PIR_SERIAL_IN_Pin,
                          (configurationRegister & (1<<i)) != 0 ? GPIO_PIN_SET : GPIO_PIN_RESET);
        HAL_DelayUs(100);   // tSHD must be at least 72uS
    }
    HAL_GPIO_WritePin(PIR_SERIAL_IN_Port, PIR_SERIAL_IN_Pin, GPIO_PIN_RESET);
    HAL_DelayUs(750);       // tSLT must be at least 580uS for latching

    // Reset the interrupt
    resetInterrupt();

    // Success
    return true;

}

// Reset the interrupt according to datasheet 2.7 "Wake Up Mode"
void resetInterrupt()
{
    // Initialize GPIOs as per data sheet 2.7
    GPIO_InitTypeDef init = {0};
    init.Mode = GPIO_MODE_OUTPUT_PP;
    init.Pull = GPIO_NOPULL;
    init.Speed = GPIO_SPEED_FREQ_LOW;
    init.Pin = PIR_DIRECT_LINK_Pin;
    HAL_GPIO_Init(PIR_DIRECT_LINK_Port, &init);
    HAL_GPIO_WritePin(PIR_DIRECT_LINK_Port, PIR_DIRECT_LINK_Pin, GPIO_PIN_RESET);
    HAL_DelayUs(250);                   // Must be held low for at least 35uS
    // Note that the datasheet suggests that this should be NOPULL, but I have
    // tested PULLDOWN and the PIR's active state is strong enough that it works.
    // This is important so that if the PIR is not mounted on the board we
    // don't have an open input that is generating random interrupts with noise.
    init.Mode = GPIO_MODE_IT_RISING;
    init.Pull = GPIO_PULLDOWN;
    init.Speed = GPIO_SPEED_FREQ_HIGH;
    init.Pin = PIR_DIRECT_LINK_Pin;
    HAL_GPIO_Init(PIR_DIRECT_LINK_Port, &init);
    HAL_NVIC_SetPriority(PIR_DIRECT_LINK_EXTI_IRQn, PIR_DIRECT_LINK_IT_PRIORITY, 0x00);
    HAL_NVIC_EnableIRQ(PIR_DIRECT_LINK_EXTI_IRQn);
}

// Poller
void pirPoll(int appID, int state, void *appContext)
{

    // Unused parameter(s)
    (void)appContext;

    // Switch based upon state
    switch (state) {

    case STATE_ACTIVATED:
        if (!templateRegistered) {
            registerNotefileTemplate();
            schedSetCompletionState(appID, STATE_ACTIVATED, STATE_MOTION_CHECK);
            APP_PRINTF("pir: template registration request\r\n");
            break;
        }
        // fallthrough to do a motion check

    case STATE_MOTION_CHECK:
        if (motionEvents == 0) {
            schedSetState(appID, STATE_DEACTIVATED, "pir: completed");
            break;
        }
        APP_PRINTF("pir: %d motion events sensed\r\n", motionEvents);
        addNote(true);
        schedSetCompletionState(appID, STATE_MOTION_CHECK, STATE_MOTION_CHECK);
        APP_PRINTF("pir: note queued\r\n");
        break;

    }

}

// Register the notefile template for our data
static bool registerNotefileTemplate()
{

    // Create the request
    J *req = NoteNewRequest("note.template");
    if (req == NULL) {
        return false;
    }

    // Create the body
    J *body = JCreateObject();
    if (body == NULL) {
        JDelete(req);
        return false;
    }

    // Add an ID to the request, which will be echo'ed
    // back in the response by the notecard itself.  This
    // helps us to identify the asynchronous response
    // without needing to have an additional state.
    JAddNumberToObject(req, "id", REQUESTID_TEMPLATE);

    // Fill-in request parameters.  Note that in order to minimize
    // the size of the over-the-air JSON we're using a special format
    // for the "file" parameter implemented by the gateway, in which
    // a "file" parameter beginning with * will have that character
    // substituted with the textified application address.
    JAddStringToObject(req, "file", APPLICATION_NOTEFILE);

    // Fill-in the body template
    JAddNumberToObject(body, "count", TINT32);
    JAddNumberToObject(body, "total", TINT32);

    // Attach the body to the request, and send it to the gateway
    JAddItemToObject(req, "body", body);
    noteSendToGatewayAsync(req, true);
    return true;

}

// Gateway Response handler
void pirResponse(int appID, J *rsp, void *appContext)
{

    // Unused parameter(s)
    (void)appID;
    (void)appContext;

    // See if there's an error
    char *err = JGetString(rsp, "err");
    if (err[0] != '\0') {
        APP_PRINTF("pir: gateway returned error: %s\r\n", err);
        return;
    }

    // Flash the LED if this is a response to this specific ping request
    switch (JGetInt(rsp, "id")) {

    case REQUESTID_TEMPLATE:
        templateRegistered = true;
        APP_PRINTF("pir: SUCCESSFUL template registration\r\n");
        break;
    }

}

// Send the sensor data
static void addNote(bool immediate)
{

    // Create the request
    J *req = NoteNewRequest("note.add");
    if (req == NULL) {
        return;
    }

    // Create the body
    J *body = JCreateObject();
    if (body == NULL) {
        JDelete(req);
        return;
    }

    // Set the target notefile
    JAddStringToObject(req, "file", APPLICATION_NOTEFILE);

    // If immediate, sync now
    if (immediate) {
        JAddBoolToObject(req, "sync", true);
    }

    // Fill-in the body
    JAddNumberToObject(body, "total", motionEventsTotal);
    JAddNumberToObject(body, "count", motionEvents);
    motionEvents = 0;

    // Attach the body to the request, and send it to the gateway
    JAddItemToObject(req, "body", body);
    noteSendToGatewayAsync(req, false);

}

// Interrupt handler
void pirISR(int appID, uint16_t pins, void *appContext)
{

    // Unused parameter(s)
    (void)appContext;

    // Set the state to 'motion' and immediately schedule
    if ((pins & PIR_DIRECT_LINK_Pin) != 0) {

        // Record the motion event
        motionEvents++;
        motionEventsTotal++;
        resetInterrupt();

        // See if we're past the suppression interval
        int64_t nowMs = TIMER_IF_GetTimeMs();
        if (lastInterruptMs == 0) {
            lastInterruptMs = nowMs;
        } else {
            uint32_t elapsedSecs = (uint32_t) (nowMs - lastInterruptMs) / 1000;
            lastInterruptMs = nowMs;
            if (elapsedSecs < (PIR_SUPPRESSION_MINS*60)) {
                return;
            }
        }

        // Activate the scheduler
        if (!schedIsActive(appID)) {
            schedActivateNowFromISR(appID, true, STATE_MOTION_CHECK);
        }

        return;
    }

}

// We have no viable way of detecting whether or not the PIR sensor
// hardware is present, so we use the presence of the BME280 as a proxy.
bool isSparrowReferenceSensorBoard (void) {
    bool result;

    // Power on the sensor to see if it's here
    GPIO_InitTypeDef init = {0};
    init.Speed = GPIO_SPEED_FREQ_LOW;
    init.Pin = BME_POWER_Pin;
    init.Mode = GPIO_MODE_OUTPUT_PP;
    init.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(BME_POWER_GPIO_Port, &init);
    HAL_GPIO_WritePin(BME_POWER_GPIO_Port, BME_POWER_Pin, GPIO_PIN_SET);
    MX_I2C2_Init();
    result = (MY_I2C2_Ping(BME280_I2C_ADDR_PRIM, BME280_I2C_TIMEOUT_MS, BME280_I2C_RETRY_COUNT)
           || MY_I2C2_Ping(BME280_I2C_ADDR_SEC, BME280_I2C_TIMEOUT_MS, BME280_I2C_RETRY_COUNT));
    MX_I2C2_DeInit();

    return result;
}
