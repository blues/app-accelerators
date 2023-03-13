#include "mq2.h"

// Standard Header(s)
#include <math.h>

// ST Header(s)
#include <main.h>

// Blues Header(s)
#include <framework.h>
#include <note.h>

// read temperature from the BME sensor
#include "bme.h"


/**
 * @brief Configures how often an air quality reading is taken and reported.
 */
#define MQ2_MONITOR_PERIOD       60     // seconds

/**
 * @brief When set to true, only alerts are sent to notehub. Specifically, events with alert:1 or alert:3.
 * When false, regular air quality events and alerts are sent.
 */
#define ALERTS_ONLY              (false)

/**
 * @brief Signal an alert when the measured temperature is above this value
 */
#define ALERT_TEMPERATURE        50.0    // Celcius
/**
 * @brief  Signal an alert when measured value from the MQ2 sensor goes above this value.
 * You will need to calibrate the sensor in free air and ideally in a smokey environment to determine
 * a suitable threshold for alerts. The value here can very considerably depending upon the setting of the
 * potentiometer, so experimentation is needed.
 */
#define ALERT_GAS_LEVEL          40000  // max is 65535


/**
 * @brief When set to true all notes are immediately synced to notehub, otherwise only
 * alert:1 and alert:3 notes are synced.
 */
#define SYNC_ALL_NOTES           (false)


#if !defined(A1_ENABLED) || (!A1_ENABLED)
#error The mq2 gas sensor app requires A1_ENABLED to be defined.
#endif

#define APPLICATION_NAME "nf31"

// Forward Declaration(s)
typedef struct J J;
typedef struct MQ2SensorReading MQ2SensorReading;
static void mq2Poll(int appID, int state, void *appContext);
static void mq2Response(int appID, J *rsp, void *appContext);
static bool mq2Read(uint16_t* result);
static bool registerNotefileTemplate();
static void addNote(const MQ2SensorReading* reading, bool immediate, uint8_t alert);


// Special request IDs
#define REQUESTID_TEMPLATE          1

// The dynamic filename of the application specific queue.
// NOTE: The Gateway will replace `*` with the originating node's ID.
#define APPLICATION_NOTEFILE "*#mq2.qo"

// TRUE if we've successfully registered the template
static bool templateRegistered = false;

/**
 * @brief Was the BME280 sensor detected. When present, temperature readings are taken and checked
 * against the threshold. When not present, temperature is not checked nor reported.
 */
static bool bmeDetected = false;

// Scheduled App One-Time Init
bool mq2Init()
{
    bmeDetected = bmePresent();
    if (!bmeDetected) {
        APP_PRINTF("BME280 not found, temperature readings will not be reported\r\n");
    }

    // Register the app
    schedAppConfig config = {
        .name = "MQ2 Gas Sensor",
        .activationPeriodSecs = 60*60,
        .pollPeriodSecs = MQ2_MONITOR_PERIOD,
        .activateFn = NULL,
        .interruptFn = NULL,
        .pollFn = mq2Poll,
        .responseFn = mq2Response,
        .appContext = NULL,
    };
    if (schedRegisterApp(&config) < 0) {
        return false;
    }
    return true;
}

typedef enum MQ2States {
    STATE_MQ2_MONITORING,
    STATE_MQ2_ALERTING,
} MQ2States;

typedef struct MQ2SensorReading {
    bool bmeValid;
    BMESample bme;
    uint16_t gas;
    bool alert;
} MQ2SensorReading;

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


static bool determineIfAlert(MQ2SensorReading* reading) {
    return
    (reading->bmeValid && (ALERT_TEMPERATURE && reading->bme.temperature >= ALERT_TEMPERATURE))
    || (reading->gas && ALERT_GAS_LEVEL && reading->gas >= ALERT_GAS_LEVEL);
}

bool takeReading(MQ2SensorReading* reading) {
    reading->bmeValid = bmeDetected;
    if (bmeDetected) {
        reading->bmeValid = bmeUpdate(&reading->bme);
    }

    reading->gas = 0;
    mq2Read(&reading->gas);

    reading->alert = determineIfAlert(reading);
    return true;
}

bool shouldSendMonitoringEvent() {
    // this could be used to send monitoring events less frequently than how often the app takes a sensor reading.
    return !ALERTS_ONLY;
}

// Poller
void mq2Poll(int appID, int state, void *appContext)
{
    // Unused parameter(s)
    (void)appContext;

    MQ2SensorReading sensorReading;

    // Switch based upon state
    switch (state) {
    case STATE_ACTIVATED:
        if (!templateRegistered) {
            registerNotefileTemplate();
            schedSetCompletionState(appID, STATE_ACTIVATED, STATE_ACTIVATED);
            APP_PRINTF("mq2: template registration request\r\n");
            break;
        }
        // fall through to begin monitoring

    case STATE_MQ2_MONITORING:
         if (takeReading(&sensorReading)) {
            if (sensorReading.alert) {
                addNote(&sensorReading, true, ALERT_FIRST);
                schedSetCompletionState(appID, STATE_MQ2_ALERTING, STATE_MQ2_MONITORING);
            }
            else if (shouldSendMonitoringEvent()) {
                addNote(&sensorReading, false, ALERT_NONE);
                schedSetCompletionState(appID, STATE_MQ2_MONITORING, STATE_MQ2_MONITORING);
            }
        }
        break;

    case STATE_MQ2_ALERTING:
        if (takeReading(&sensorReading)) {
            if (sensorReading.alert) {
                if (shouldSendMonitoringEvent()) {
                    addNote(&sensorReading, false, ALERT_ONGOING);
                    schedSetCompletionState(appID, STATE_MQ2_ALERTING, STATE_MQ2_ALERTING);
                }
            }
            else {
                addNote(&sensorReading, true, ALERT_STANDDOWN);
                schedSetCompletionState(appID, STATE_MQ2_MONITORING, STATE_MQ2_ALERTING);
            }
        }
        break;
    }
}

bool mq2Read(uint16_t* result) {
    uint16_t wordValue[ADC_COUNT];
    bool success = MX_ADC_Values(wordValue, NULL, NULL);
    if (success) {
        *result = wordValue[1];
    }
    return success;
}

#define DATA_FIELD_ALERT        "alert"
#define DATA_FIELD_APP          "app"
#define DATA_FIELD_TEMPERATURE  "temperature"
#define DATA_FIELD_HUMIDITY     "humidity"
#define DATA_FIELD_PRESSURE     "pressure"
#define DATA_FIELD_VOLTAGE      "voltage"
#define DATA_FIELD_GAS          "gas"

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
    JAddNumberToObject(body, DATA_FIELD_TEMPERATURE, TFLOAT16);
    JAddNumberToObject(body, DATA_FIELD_HUMIDITY, TFLOAT16);
    JAddNumberToObject(body, DATA_FIELD_PRESSURE, TFLOAT32);
    JAddNumberToObject(body, DATA_FIELD_VOLTAGE, TFLOAT32);
    JAddNumberToObject(body, DATA_FIELD_GAS, TUINT16);

    // Attach the body to the request, and send it to the gateway
    JAddItemToObject(req, "body", body);
    noteSendToGatewayAsync(req, true);
    return true;
}

// Gateway Response handler
void mq2Response(int appID, J *rsp, void *appContext)
{

    // Unused parameter(s)
    (void)appID;
    (void)appContext;

    // See if there's an error
    char *err = JGetString(rsp, "err");
    if (err[0] != '\0') {
        APP_PRINTF("mq2: gateway returned error: %s\r\n", err);
        return;
    }
    switch (JGetInt(rsp, "id")) {

    case REQUESTID_TEMPLATE:
        templateRegistered = true;
        APP_PRINTF("mq2: SUCCESSFUL template registration\r\n");
        break;
    }
}

// Send the sensor data
static void addNote(const MQ2SensorReading* reading, bool immediate, uint8_t alert)
{
    // Create the request
    J *req = NoteNewRequest("note.add");
    if (req == NULL) {
        APP_PRINTF("mq2: out of memory creating note\r\n");
        return;
    }

    // Create the body
    J *body = JCreateObject();
    if (body == NULL) {
        JDelete(req);
        APP_PRINTF("mq2: out of memory creating note\r\n");
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
    JAddNumberToObject(body, DATA_FIELD_TEMPERATURE, reading->bme.temperature);
    JAddNumberToObject(body, DATA_FIELD_HUMIDITY, reading->bme.humidity);
    //JAddNumberToObject(body, DATA_FIELD_PRESSURE, reading->bme.pressure);
    // Add the voltage, just for convenient reference
#ifdef USE_SPARROW
    JAddNumberToObject(body, DATA_FIELD_VOLTAGE, MX_ADC_A0_Voltage());
#endif

    // Attach the body to the request, and send it to the gateway
    JAddItemToObject(req, "body", body);
    noteSendToGatewayAsync(req, false);
    APP_PRINTF("mq2: note sent. gas: %d", reading->gas);
    if (reading->bmeValid) {
        APP_PRINTF(", temperature: %d.%dC humidity:%d.%d%%",
                (int) reading->bme.temperature, (int) (fabs(reading->bme.temperature*100)) % 100,
                (int) reading->bme.humidity, (int) (fabs(reading->bme.humidity*100)) % 100);
    }
    APP_PRINTF("\r\n");
}
