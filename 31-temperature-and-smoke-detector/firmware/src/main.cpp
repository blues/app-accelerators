#include <Arduino.h>
#include <Notecard.h>
#include "SparkFunBME280.h"

#include "STM32LowPower.h"

#define APPLICATION_NAME        "nf31"
#define DATA_FIELD_ALERT        "alert"
#define DATA_FIELD_APP          "app"
#define DATA_FIELD_TEMPERATURE  "temperature"
#define DATA_FIELD_HUMIDITY     "humidity"
#define DATA_FIELD_PRESSURE     "pressure"
#define DATA_FIELD_GAS          "gas"
#define APPLICATION_NOTEFILE    "mq2.qo"

typedef struct {
    double temperature;
    double pressure;
    double humidity;
} BMESample;


typedef struct MQ2SensorReading {
    BMESample bme;
    uint16_t gas;
} MQ2SensorReading;

static bool isAlert(MQ2SensorReading& reading);
static bool registerNotefileTemplate();
static void addNote(const MQ2SensorReading& reading, bool immediate, uint8_t alert);


Notecard notecard;
BME280 bmeSensor;
// Uncomment this line and replace com.your-company:your-product-name with your
// ProductUID.
// #define PRODUCT_UID "com.your-company:your-product-name"

#ifndef PRODUCT_UID
#define PRODUCT_UID "" // "com.my-company.my-name:my-project"
#pragma message "PRODUCT_UID is not defined in this example. Please ensure your Notecard has a product identifier set before running this example or define it in code here. More details at https://bit.ly/product-uid"
#endif

#define usbSerial Serial

bool activeAlert;

/**
 * @brief Configures how often the note card periodicly syncs when there are no alerts.
 */
#define MQ2_SYNC_PERIOD       60     // minutes

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



void setup() {
    LowPower.begin();
    usbSerial.begin(115200);
    usbSerial.println("Starting....");
    notecard.begin();

    J *req = notecard.newRequest("hub.set");
    JAddStringToObject(req, "product", PRODUCT_UID);
    notecard.sendRequest(req);

    req = notecard.newRequest("hub.set");
    JAddStringToObject(req, "mode", "periodic");
    JAddNumberToObject(req, "outbound", MQ2_SYNC_PERIOD);
    JAddNumberToObject(req, "inbound", 240);
    notecard.sendRequest(req);
    
    if (!registerNotefileTemplate()) {
        usbSerial.println("Error Creating Notefile Template");
        while(1);
    }

    // Setup sensor
    if( !bmeSensor.beginI2C() ){
		usbSerial.println("Could not communicate with the BME280, check wiring.");
		while(1);
	}
    bmeSensor.setMode(MODE_SLEEP); //Sleep for now

    analogReadResolution(16);
}

void loop() {

    bmeSensor.setMode(MODE_FORCED); //Wake up sensor and take reading

    long startTime = millis();
    while(bmeSensor.isMeasuring() == false) ; //Wait for sensor to start measurment
    while(bmeSensor.isMeasuring() == true) ; //Hang out while sensor completes the reading    
    long endTime = millis();

    BMESample sample;
    sample.humidity = bmeSensor.readFloatHumidity();
    sample.pressure = bmeSensor.readFloatPressure();
    sample.temperature = bmeSensor.readTempC();

    MQ2SensorReading reading;
    reading.bme = sample;
    reading.gas = analogRead(A0);
    

    // Get the data, sleep

    int alertState; 
    // check alert state compared to previous alert state to get the alert message
    if (isAlert(reading)) {
        if (activeAlert) {
            alertState = ALERT_ONGOING;
        } else {
            alertState = ALERT_FIRST;
            activeAlert = true;
        }
    } else { 
        if (activeAlert) {
            alertState = ALERT_STANDDOWN;
            activeAlert = false;

        } else {
            alertState = ALERT_NONE;
        }                
    }

    addNote(reading, false, alertState);
    delay(200);
    usbSerial.end();
    delay(10);
    LowPower.sleep(MQ2_MONITOR_PERIOD * 1000);
    usbSerial.begin(115200);
    while (!usbSerial);
    
    delay(200);
}

static bool isAlert(MQ2SensorReading& reading) {
    return
    (ALERT_TEMPERATURE && reading.bme.temperature >= ALERT_TEMPERATURE)
    || (reading.gas && ALERT_GAS_LEVEL && reading.gas >= ALERT_GAS_LEVEL);
}

// Register the notefile template for our data
static bool registerNotefileTemplate()
{
    // Create the request
    J *req = NoteNewRequest("note.template");
    if (req == NULL) {
        usbSerial.printf("NoteNewRequest failed\r\n");
        return false;
    }

    // Create the body
    J *body = JCreateObject();
    if (body == NULL) {
        usbSerial.printf("JCreateObject failed\r\n");
        JDelete(req);
        return false;
    }


    JAddStringToObject(req, "file", APPLICATION_NOTEFILE);

    // Fill-in the body template
    JAddStringToObject(body, DATA_FIELD_APP, TSTRINGV);
    JAddNumberToObject(body, DATA_FIELD_ALERT, TUINT8);
    JAddNumberToObject(body, DATA_FIELD_TEMPERATURE, TFLOAT16);
    JAddNumberToObject(body, DATA_FIELD_HUMIDITY, TFLOAT16);
    JAddNumberToObject(body, DATA_FIELD_PRESSURE, TFLOAT32);
    JAddNumberToObject(body, DATA_FIELD_GAS, TUINT16);

    // Attach the body to the request
    JAddItemToObject(req, "body", body);

    // Setup template parameters
    JAddStringToObject(req, "format", "compact");
    JAddNumberToObject(req, "port", 14);

    // send it to the gateway
    return NoteRequest(req);
}

// Send the sensor data
static void addNote(const MQ2SensorReading& reading, bool immediate, uint8_t alert)
{

    // Create the request
    J *req = NoteNewRequest("note.add");
    if (req == NULL) {
        usbSerial.print("mq2: out of memory creating note\r\n");
        return;
    }

    // Create the body
    J *body = JCreateObject();
    if (body == NULL) {
        JDelete(req);
        usbSerial.print("mq2: out of memory creating note\r\n");
        return;
    }

    // Set the target notefile
    JAddStringToObject(req, "file", APPLICATION_NOTEFILE);

    // If immediate, sync now
    if (SYNC_ALL_NOTES || immediate) {
        JAddBoolToObject(req, "sync", true);
    }

    // Fill-in the body
    JAddStringToObject(body, DATA_FIELD_APP, APPLICATION_NAME);
    JAddNumberToObject(body, DATA_FIELD_ALERT, alert);
    JAddNumberToObject(body, DATA_FIELD_TEMPERATURE, reading.bme.temperature);
    JAddNumberToObject(body, DATA_FIELD_HUMIDITY, reading.bme.humidity);
    JAddNumberToObject(body, DATA_FIELD_PRESSURE, reading.bme.pressure);
    JAddNumberToObject(body, DATA_FIELD_GAS, reading.gas);


    // Attach the body to the request, and send it to the gateway
    JAddItemToObject(req, "body", body);
    JAddNumberToObject(req, "port", 14);
    NoteRequest(req);

    usbSerial.print("mq2: note sent. temp: ");
    usbSerial.print(reading.bme.temperature);
    usbSerial.print(" celcuis, humidity: ");
    usbSerial.print(reading.bme.humidity);
    usbSerial.print(" %RH, pressure: ");
    usbSerial.print(reading.bme.pressure);
    usbSerial.print(" Pa, gas: ");
    usbSerial.println(reading.gas);
}