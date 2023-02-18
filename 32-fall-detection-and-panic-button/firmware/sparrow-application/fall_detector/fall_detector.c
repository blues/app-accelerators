// Copyright 2023 Blues Inc. All rights reserved.
// Use of this source code is governed by licenses granted by the
// copyright holder including that found in the LICENSE file.

// Firmware for a fall detector. This app sends a note to Notehub when the
// BMA400 accelerometer detects a fall. The accelerometer configuration
// parameters were based on this technical document from Bosch, with some
// tweaking: https://community.bosch-sensortec.com/varuj77995/attachments/varuj77995/bst_community-mems-forum/4402/1/How%20to%20generate%20freefall%20interrupt%20using%20BMA400.pdf

#include "fall_detector.h"
#include "bma400.h"

#include <framework.h>
#include <main.h>
#include <note.h>

static void fallDetectorPoll(int appID, int state, void *appCtx);
static void fallDetectorISR(int appID, uint16_t pins, void *appCtx);
static void sendNotification(int appID);

// The interrupt pin of the BMA400 should be connected to pin A1 on the Sparrow
// reference node.
#define ACCEL_INT_PORT  A1_GPIO_Port
#define ACCEL_INT_PIN   A1_Pin
#define ACCEL_INT_IRQN  EXTI2_IRQn

// This app is interrupt driven, so we really don't ever need to activate. Set
// the activation interval to a large value (1 day).
#define ACTIVATION_PERIOD_SECS (60 * 60 * 24)
#define POLL_PERIOD_SECS 15

static struct bma400_dev accelerometer = {0};
// The BMA400 API requires the user to set an "interface pointer", which is
// basically a pointer to some user data that will be made available in various
// callbacks (e.g. readRegister below). We don't use it in this app, but it has
// to be set for the API to not return an error.
static uint8_t intfData = 0;

#define BMA400_I2C_TIMEOUT_MS 100

#ifndef BMA400_INTERRUPT_DURATION_DEFAULT
#define BMA400_INTERRUPT_DURATION_DEFAULT 30
#endif

#define STATE_FALL_DETECTED 0

typedef struct {
    uint16_t interruptDuration;
} AppState;

static AppState appState = {
    .interruptDuration = BMA400_INTERRUPT_DURATION_DEFAULT
};

// The BMA400 API needs us to set three callbacks: one for reading registers
// over I2C, one for writing registers over I2C, and one for delaying a given
// number of microseconds.
static BMA400_INTF_RET_TYPE readRegister(uint8_t addr, uint8_t *data,
                                         uint32_t len, void *intfData)
{
    (void)intfData;

    if (MY_I2C2_ReadRegister(BMA400_I2C_ADDRESS_SDO_LOW, addr, data, len,
                             BMA400_I2C_TIMEOUT_MS)) {
        return BMA400_OK;
    }
    else {
        return BMA400_E_COM_FAIL;
    }
}

static BMA400_INTF_RET_TYPE writeRegister(uint8_t addr, const uint8_t *data,
                                          uint32_t len, void *intfData)
{
    (void)intfData;

    if (MY_I2C2_WriteRegister(BMA400_I2C_ADDRESS_SDO_LOW, addr, (void *)data,
                              len, BMA400_I2C_TIMEOUT_MS)) {
        return BMA400_OK;
    }
    else {
        return BMA400_E_COM_FAIL;
    }
}


static void delayUs(uint32_t period, void *intfData)
{
    (void)intfData;

    MX_TIM17_DelayUs(period);
}

bool initAccel(void)
{
    int8_t ret = BMA400_OK;

    accelerometer.read = readRegister;
    accelerometer.write = writeRegister;
    accelerometer.delay_us = delayUs;
    accelerometer.intf = BMA400_I2C_INTF;
    accelerometer.intf_ptr = &intfData;

    if ((ret = bma400_soft_reset(&accelerometer)) != BMA400_OK) {
        APP_PRINTF("fall_detector: initAccel: bma400_soft_reset failed!\r\n");
    }
    else if ((ret = bma400_init(&accelerometer)) != BMA400_OK) {
        APP_PRINTF("fall_detector: initAccel: bma400_init failed!\r\n");
    }
    else if ((ret = bma400_set_power_mode(BMA400_MODE_NORMAL, &accelerometer))
             != BMA400_OK) {
        APP_PRINTF("fall_detector: initAccel: bma400_set_power_mode failed!"
                   "\r\n");
    }

    return ret == BMA400_OK;
}

bool setAccelParams(void)
{
    int8_t ret = BMA400_OK;
    struct bma400_sensor_conf config = {0};

    config.type = BMA400_ACCEL;
    config.param.accel.odr = BMA400_ODR_200HZ;
    config.param.accel.range = BMA400_RANGE_4G;
    config.param.accel.data_src = BMA400_DATA_SRC_ACCEL_FILT_1;

    if ((ret = bma400_set_sensor_conf(&config, 1, &accelerometer))
        != BMA400_OK) {
        APP_PRINTF("fall_detector: setAccelParams: bma400_set_sensor_conf "
                    "failed!\r\n");
    }

    return ret == BMA400_OK;
}

bool setInterruptParams(void)
{
    int8_t ret = BMA400_OK;
    struct bma400_gen_int_conf intConfig = {0};
    // The threshold is in units of 8mg (milli g's). 63 * 8mg = 504 mg.
    intConfig.gen_int_thres = 63;
    // The duration is in units of 10ms. Using the default value of 30:
    //
    //     30 * 10ms = 300ms.
    //
    // The fall height (H) implied by this duration is:
    //
    //     H = 0.5 * g * t^2 = 0.5 * 9.81m/s^2 * (0.30s)^2 = 0.708m.
    //
    // The accerlometer needs to be in free fall for 300 ms (or 0.708 meters) to
    // trigger the interrupt.
    intConfig.gen_int_dur = appState.interruptDuration;
    // Use all three axes.
    intConfig.axes_sel = BMA400_AXIS_XYZ_EN;
    // The acceleration readings on all 3 axes must be below the threshold to
    // cause an interrupt.
    intConfig.evaluate_axes = BMA400_ALL_AXES_INT;
    // From the datasheet: "Using acc_filt2 is recommended."
    intConfig.data_src = BMA400_DATA_SRC_ACCEL_FILT_2;
    // An accelerometer will read ~0 acceleration on all axes when in free fall.
    // Thus, we want to configure our interrupt threshold range to be a small
    // interval around 0. Setting the interrupt criterion to inactivity means
    // the interrupt condition is acceleration < threshold, rather than
    // > threshold. With an inactivity threshold of 504mg on all axes, the
    // interrupt condition is satisfied when all three acceleration values are
    // in the range (-504mg, 504mg). If that condition persists for >= 300ms,
    // the interrupt line will pulse.
    intConfig.criterion_sel = BMA400_INACTIVITY_INT;
    // All acceleration values are referenced with respect to 0. Practically,
    // this means the values will be compared directly with the threshold,
    // rather than being subtracted from some reference value.
    intConfig.int_thres_ref_x = 0;
    intConfig.int_thres_ref_y = 0;
    intConfig.int_thres_ref_z = 0;
    // Reference acceleration values are manually set once and never updated.
    intConfig.ref_update = BMA400_UPDATE_MANUAL;
    // No hysteresis.
    intConfig.hysteresis = BMA400_HYST_0_MG;
    // There are two generic interrupts. We're using number 1.
    intConfig.int_chan = BMA400_INT_CHANNEL_1;

    struct bma400_sensor_conf sensorConfig = {0};
    sensorConfig.type = BMA400_GEN1_INT;
    sensorConfig.param.gen_int = intConfig;

    if ((ret = bma400_set_sensor_conf(&sensorConfig, 1, &accelerometer))
        != BMA400_OK) {
        APP_PRINTF("fall_detector: setInterruptParams: bma400_set_sensor_conf "
                   "failed!\r\n");
    }
    if (ret == BMA400_OK) {
        struct bma400_device_conf devConfig = {0};
        devConfig.type = BMA400_INT_PIN_CONF;
        devConfig.param.int_conf.int_chan = BMA400_INT_CHANNEL_1;
        devConfig.param.int_conf.pin_conf = BMA400_INT_PUSH_PULL_ACTIVE_1;
        if ((ret = bma400_set_device_conf(&devConfig, 1, &accelerometer))
            != BMA400_OK) {
            APP_PRINTF("fall_detector: setInterruptParams: "
                       "bma400_set_device_conf failed!\r\n");
        }
    }
    if (ret == BMA400_OK) {
        struct bma400_int_enable intEnableConfig = {0};
        intEnableConfig.type = BMA400_GEN1_INT_EN;
        intEnableConfig.conf = BMA400_ENABLE;
        if ((ret = bma400_enable_interrupt(&intEnableConfig, 1, &accelerometer))
            != BMA400_OK) {
            APP_PRINTF("fall_detector: setInterruptParams: "
                       "bma400_enable_interrupt failed!\r\n");
        }
    }

    return ret == BMA400_OK;
}

bool fallDetectorInit(void)
{
    bool success = true;

    schedAppConfig config = {
        .name = "fall_detector",
        .activationPeriodSecs = ACTIVATION_PERIOD_SECS,
        .pollPeriodSecs = POLL_PERIOD_SECS,
        .activateFn = NULL,
        .interruptFn = fallDetectorISR,
        .pollFn = fallDetectorPoll,
        .responseFn = NULL,
        .appContext = &appState,
    };
    if (schedRegisterApp(&config) < 0) {
        success = false;
    }
    if (success) {
        // Configure ACCEL_INT_PIN so that a rising edge kicks off the fall
        // notification logic.
        HAL_GPIO_DeInit(ACCEL_INT_PORT, ACCEL_INT_PIN);
        GPIO_InitTypeDef gpioInit = {0};
        gpioInit.Pin = ACCEL_INT_PIN;
        gpioInit.Speed = GPIO_SPEED_FREQ_LOW;
        gpioInit.Pull = GPIO_NOPULL;
        gpioInit.Mode = GPIO_MODE_IT_RISING;
        HAL_GPIO_Init(ACCEL_INT_PORT, &gpioInit);
        HAL_NVIC_SetPriority(ACCEL_INT_IRQN, 15, 0);
        HAL_NVIC_EnableIRQ(ACCEL_INT_IRQN);

        MX_I2C2_Init();

        success = initAccel();
    }
    if (success) {
        success = setAccelParams();
    }
    if (success) {
        success = setInterruptParams();
    }

    return success;
}

static void fallDetectorPoll(int appID, int state, void *appCtx)
{
    (void)appCtx;

    switch (state) {
        case STATE_ACTIVATED:
            schedSetState(appID, STATE_DEACTIVATED, "Nothing to do.");
            break;
        case STATE_FALL_DETECTED:
            sendNotification(appID);
            break;
    }
}

static void fallDetectorISR(int appID, uint16_t pins, void *appCtx)
{
    (void)appCtx;

    if ((pins & ACCEL_INT_PIN) != 0) {
        schedActivateNowFromISR(appID, true, STATE_FALL_DETECTED);
        return;
    }
}

static void sendNotification(int appID)
{
    J *req = NoteNewRequest("note.add");
    if (req == NULL) {
        return;
    }
    JAddStringToObject(req, "file", "*#fall.qo");
    JAddBoolToObject(req, "sync", true);

    J *body = JCreateObject();
    if (body == NULL) {
        JDelete(req);
        return;
    }
    JAddBoolToObject(body, "fall", true);
    JAddItemToObject(req, "body", body);

    atpMaximizePowerLevel();
    ledIndicateAck(1);
    noteSendToGatewayAsync(req, false);

    APP_PRINTF("fall_detector: sendNotification: Sent to gateway.\r\n");

    schedSetCompletionState(appID, STATE_DEACTIVATED, STATE_DEACTIVATED);
}
