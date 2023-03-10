// Copyright 2023 Blues Inc.  All rights reserved.
// Use of this source code is governed by licenses granted by the
// copyright holder including that found in the LICENSE file.

extern "C" {
#include "aqi.h"

// ST Header(s)
#include <main.h>

// Blues Header(s)
#include <framework.h>
#include <note.h>
}

#include "sfe_ens160.h"
#include "sfe_bus.h"

//
// Configuration for the AQI app.
//

/**
 * @brief Configures how often an air quality reading is taken and reported.
 */
#define AQI_MONITOR_PERIOD       60     // seconds

/**
 * @brief When set to true, only alerts are sent to notehub. Specifically, events with alert:1 or alert:3.
 * When false, regular air quality events and alerts are sent.
 */
#define ALERTS_ONLY              (false)

/**
 * @brief Signal an alert when the AQI is equal or above this value.
 * Set to 0 to disable alerts based on the AQI value.
 */
#define ALERT_AQI_LEVEL          3
/**
 * @brief  Signal an alert when the CO2 concentration (in ppm) is equal or above this value.
 * Set to 0 to disable alerts based on the CO2 value.
 */
#define ALERT_ECO2_LEVEL         500    // ppm (atmospheric CO2 is ~420 ppm)

/**
 * @brief Signal an alert when the concentration of TVOCs (total volatile organic compounds)
 * is at or above this level.
 * Set to 0 to disable alerts based on the TVOC value.
 */
#define ALERT_TVOC_LEVEL         200    // ppb

/**
 * @brief How many times to try an I2C operation before considering it failed.
 */
#define AQI_I2C_RETRY_COUNT      3

/**
 * @brief How long to wait for an I2C response from the air quality sensor before 
 * that attempt is considered unsuccessful.
 * 
 */
#define AQI_I2C_TIMEOUT_MS       100


// When set to `true` all notes are immediately synched to notehub. Useful for testing but not intended for production use.
#define SYNC_ALL_NOTES           (false)


// States for the local state machine

/**
 * @brief That the app is not in an alert state. The AQI and other quality readings
 * are read at the poll interval and checked if the readings should trigger an alert.
 * If an alert is triggered the message is sent immediately. When the message is successfully sent,
 * transitions to STATE_AQI_ALERT. Otherwise regular AQI monitoring events and failed alerts remain in this state.
 */
#define STATE_AQI_MONITORING           0

/**
 * @brief An alert has been sent. Subsequent alerts are suppressed although AQI events continue to be sent
 * for monitoring purposes. When an AQI reading is not an alert, an "alert stand down" event is sent (alert=2)
 * to indicate the gas levels are within normal parameters again. When the event is successfully sent,
 * transitions back to STATE_AQI_MONITORING, otherwise remain in teh STATE_AQI_ALERTING.
 */
#define STATE_AQI_ALERTING             1

/**
 * @brief The request ID used when registering the template.
 */
#define REQUESTID_TEMPLATE  1

// The dynamic filename of the application specific queue.
// NOTE: The Gateway will replace `*` with the originating node's ID.
#define APPLICATION_NOTEFILE        "*#aqi.qo"
#define APPLICATION_NAME            "nf30"
#define DATA_FIELD_ALERT            "alert"
#define DATA_FIELD_APP              "app"
#define DATA_FIELD_AQI              "aqi"
#define DATA_FIELD_TVOC             "tvoc"
#define DATA_FIELD_ECO2             "eco2"
#define DATA_FIELD_ETOH             "etoh"
#define DATA_FIELD_TEMP             "temp"
#define DATA_FIELD_HUMIDITY         "rh"

#define ALERT_NONE                  0
/**
 * @brief The first alert is sent with alert=1.
 */
#define ALERT_FIRST                 1

/**
 * @brief Subsequent events have alert=2 until the alert condition clears.
 */
#define ALERT_ONGOING               2

/**
 * @brief When the alert clears alert=3 is sent on the first normal air quality event.
 */
#define ALERT_STANDDOWN             3

// TRUE if we've successfully registered the template
static bool templateRegistered = false;


/**
 * @brief Collects together the individual readings from the gas sensor.
 */
struct AQISensorReading {
    uint16_t tvoc;
    uint16_t eco2;
    uint16_t etoh;
    uint8_t aqi;

    /**
     * @brief Set to true when the values measured should signal an alert.
     */
    bool alert;
    float temp;
    float rh;

    inline bool isAlert() const {
        return alert;
    }
};


// Forwards
static void addNote(const AQISensorReading& reading, bool immediate, uint8_t alert);
static bool registerNotefileTemplate(void);

class STM32HALI2CBus : public sfe_ENS160::QwIDeviceBus {

    uint16_t timeout_ms;
    uint16_t retries;

public:
    STM32HALI2CBus(uint16_t timeout_ms, uint16_t retries)
    : timeout_ms(timeout_ms), retries(retries) 
{}
    
    //////////////////////////////////////////////////////////////////////////////////////////////////
    // I2C init()
    //
    // Methods to init/setup this device. The caller can provide a Wire Port, or this class
    // will use the default

    bool init()
    {
        MX_I2C2_Init();
        return true;
    }

    void deinit()
    {
        MX_I2C2_DeInit();
    }

    bool ping(uint8_t i2c_address)
    {
        return MY_I2C2_Ping(i2c_address, timeout_ms, retries);
    }

    //////////////////////////////////////////////////////////////////////////////////////////////////
    // writeRegisterByte()
    //
    // Write a byte to a register

    bool writeRegisterByte(uint8_t i2c_address, uint8_t offset, uint8_t dataToWrite)
    {
        return MY_I2C2_WriteRegister(i2c_address, offset, &dataToWrite, 1, timeout_ms);
    }

    int writeRegisterRegion(uint8_t i2c_address, uint8_t offset, const uint8_t *data, uint16_t length)
    {
        return MY_I2C2_WriteRegister(i2c_address, offset, (void*)data, length, timeout_ms) ? 0 : -1;
    }

    ////////////////////////////////////////////////////////////////////////////////////////////////////////////
    // readRegisterRegion()
    //
    // Reads a block of data from an i2c register on the devices.
    //
    // For large buffers, the data is chuncked over KMaxI2CBufferLength at a time
    //
    //
    int readRegisterRegion(uint8_t addr, uint8_t reg, uint8_t *data, uint16_t numBytes)
    {
        return MY_I2C2_ReadRegister(addr, reg, data, numBytes, timeout_ms) ? 0 : -1;
    }
};

class AQISensor :  public QwDevENS160 {

    STM32HALI2CBus bus;
    
public:
    AQISensor():
        bus(AQI_I2C_TIMEOUT_MS, AQI_I2C_RETRY_COUNT)
    {
        setCommunicationBus(bus, ENS160_ADDRESS_HIGH);
    }

    bool read(AQISensorReading& reading) {
        bool success = false;
        if (bus.init()) {
            if (!this->setOperatingMode(SFE_ENS160_STANDARD)) {
                APP_PRINTF("aqi: ERROR setting operating mode to standard\r\n");
            }
            else if (!this->checkDataStatus()) {
                APP_PRINTF("aqi: ERROR checkDataStatus\r\n");
            }
            else {
                reading.aqi = this->getAQI();
                reading.eco2 = this->getECO2();
                reading.tvoc = this->getTVOC();
                reading.etoh = this->getETOH();
                if (reading.aqi && reading.eco2) {
                    success = true;
                }
                else {
                    APP_PRINTF("aqi: ERROR reading is 0.\r\n");
                }
            }
            //this->setOperatingMode(SFE_ENS160_IDLE);
            bus.deinit();
        }
        else {

        }
        return success;
    }
};

AQISensor aqiSensor;


// Addresses of the BME sensor used to identify
// the Sparrow Reference Sensor Board
#define BME280_I2C_ADDR_PRIM        UINT8_C(0x76)
#define BME280_I2C_ADDR_SEC         UINT8_C(0x77)
#define BME280_I2C_RETRY_COUNT      3
#define BME280_I2C_TIMEOUT_MS       100



// We have no viable way of detecting whether or not the PIR sensor
// hardware is present, so we use the presence of the BME280 as a proxy.
bool isSparrowReferenceSensorBoard (void) {
    bool result;

    // Power on the sensor to see if it's here
    GPIO_InitTypeDef init;
    memset(&init, 0, sizeof(init));
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

// Scheduled App One-Time Init
bool aqiInit()
{
    // Register the app
    schedAppConfig config = {
        .name = "aqi",
        .activationPeriodSecs = 60 * 60,    // 1 hour
        .pollPeriodSecs = 15,
        .activateFn = NULL,
        .interruptFn = NULL,
        .pollFn = aqiPoll,
        .responseFn = aqiResponse,
        .appContext = &aqiSensor,
    };
    if (schedRegisterApp(&config) < 0) {
        return false;
    }

    // Success
    return true;

}

bool isAlert(AQISensorReading& reading) {
    return (ALERT_AQI_LEVEL && reading.aqi >= ALERT_AQI_LEVEL)
    || (ALERT_ECO2_LEVEL && reading.eco2 >= ALERT_ECO2_LEVEL)
    || (ALERT_TVOC_LEVEL && reading.tvoc >= ALERT_TVOC_LEVEL);
}

bool takeReading(AQISensorReading& reading) {
    if (!aqiSensor.read(reading)) {
        return false;
    }

    reading.alert = isAlert(reading);
    return true;
}

bool shouldSendMonitoringEvent() {
    // this could be used to send monitoring events less frequently than how often the app takes a sensor reading.
    return !ALERTS_ONLY;
}


// Poller
void aqiPoll(int appID, int state, void *appContext)
{
    // Unused parameter(s)
    (void)appContext;

    AQISensorReading sensorReading;

    // Switch based upon state
    switch (state) {

    case STATE_ACTIVATED:
        if (!templateRegistered) {
            registerNotefileTemplate();
            schedSetCompletionState(appID, STATE_AQI_MONITORING, STATE_ACTIVATED);
            APP_PRINTF("aqi: template registration request\r\n");
            break;
        }
        // fallthrough to monitoring immediately

    case STATE_AQI_MONITORING:
         if (takeReading(sensorReading)) {
            if (sensorReading.isAlert()) {
                addNote(sensorReading, true, ALERT_FIRST);
                schedSetCompletionState(appID, STATE_AQI_ALERTING, STATE_AQI_MONITORING);
            }
            else if (shouldSendMonitoringEvent()) {
                addNote(sensorReading, false, ALERT_NONE);
                schedSetCompletionState(appID, STATE_AQI_MONITORING, STATE_AQI_MONITORING);
            }
        }
        break;

    case STATE_AQI_ALERTING:
        if (takeReading(sensorReading)) {
            if (sensorReading.isAlert()) {
                if (shouldSendMonitoringEvent()) {
                    addNote(sensorReading, false, ALERT_ONGOING);
                    schedSetCompletionState(appID, STATE_AQI_ALERTING, STATE_AQI_ALERTING);
                }
            }
            else {
                addNote(sensorReading, true, ALERT_STANDDOWN);
                schedSetCompletionState(appID, STATE_AQI_MONITORING, STATE_AQI_ALERTING);
            }
        }
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
    JAddStringToObject(body, DATA_FIELD_APP, TSTRINGV);
    JAddNumberToObject(body, DATA_FIELD_ALERT, TUINT8);
    JAddNumberToObject(body, DATA_FIELD_AQI, TUINT8);
    JAddNumberToObject(body, DATA_FIELD_TVOC, TUINT16);
    JAddNumberToObject(body, DATA_FIELD_ECO2, TUINT16);
    JAddNumberToObject(body, DATA_FIELD_ETOH, TUINT16);
    JAddNumberToObject(body, DATA_FIELD_TEMP, TFLOAT16);
    JAddNumberToObject(body, DATA_FIELD_HUMIDITY, TFLOAT16);

    // Attach the body to the request, and send it to the gateway
    JAddItemToObject(req, "body", body);
    noteSendToGatewayAsync(req, true);
    return true;
}

// Gateway Response handler
void aqiResponse(int appID, J *rsp, void *appContext)
{

    // Unused parameter(s)
    (void)appID;
    (void)appContext;

    // See if there's an error
    char *err = JGetString(rsp, "err");
    if (err[0] != '\0') {
        APP_PRINTF("aqi: gateway returned error: %s\r\n", err);
        return;
    }

    switch (JGetInt(rsp, "id")) {

    case REQUESTID_TEMPLATE:
        templateRegistered = true;
        APP_PRINTF("aqi: SUCCESSFUL template registration\r\n");
        break;
    }

}

// Send the sensor data
static void addNote(const AQISensorReading& reading, bool immediate, uint8_t alert)
{

    // Create the request
    J *req = NoteNewRequest("note.add");
    if (req == NULL) {
        APP_PRINTF("aqi: out of memory creating note\r\n");
        return;
    }

    // Create the body
    J *body = JCreateObject();
    if (body == NULL) {
        JDelete(req);
        APP_PRINTF("aqi: out of memory creating note\r\n");
        return;
    }

    // Set the target notefile
    JAddStringToObject(req, "file", APPLICATION_NOTEFILE);

    // If immediate, sync now
    if (SYNC_ALL_NOTES || immediate) {
        JAddBoolToObject(req, "sync", true);
    }

    // Fill-in the body
    JAddNumberToObject(body, DATA_FIELD_ALERT, alert);
    JAddStringToObject(body, DATA_FIELD_APP, APPLICATION_NAME);
    JAddNumberToObject(body, DATA_FIELD_AQI, reading.aqi);
    JAddNumberToObject(body, DATA_FIELD_ECO2, reading.eco2);
// etoh is a copy of tvoc
//    JAddNumberToObject(body, DATA_FIELD_ETOH, reading.etoh);
//    JAddNumberToObject(body, DATA_FIELD_HUMIDITY, reading.rh);
//    JAddNumberToObject(body, DATA_FIELD_TEMP, reading.temp);
    JAddNumberToObject(body, DATA_FIELD_TVOC, reading.tvoc);

    // Attach the body to the request, and send it to the gateway
    JAddItemToObject(req, "body", body);
    noteSendToGatewayAsync(req, false);
    APP_PRINTF("aqi: note sent. aqi %d, eco2 %d, tvoc: %d\r\n", reading.aqi, reading.eco2, reading.tvoc);

}
