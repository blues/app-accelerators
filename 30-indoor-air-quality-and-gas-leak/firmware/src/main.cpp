#include <Arduino.h>
#include <Notecard.h>
#include "SparkFun_ENS160.h"

#include "STM32LowPower.h"

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


bool isAlert(AQISensorReading& reading);
static bool registerNotefileTemplate();
static void addNote(const AQISensorReading& reading, bool immediate, uint8_t alert);


Notecard notecard;
#define productUID "com.your-company.your-name:your_product"

#define usbSerial Serial

SparkFun_ENS160 myENS; 

int ensStatus; 
bool activeAlert;

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

// When set to `true` all notes are immediately synched to notehub. Useful for testing but not intended for production use.
#define SYNC_ALL_NOTES           (false)


#define APPLICATION_NOTEFILE        "aqi.qo"
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


void setup() {
    LowPower.begin();
    usbSerial.begin(115200);
    usbSerial.println("Starting....");
    notecard.begin();

    J *req = notecard.newRequest("hub.set");
    JAddStringToObject(req, "product", productUID);
    notecard.sendRequest(req);
    if (!registerNotefileTemplate()) {
        usbSerial.println("Error Creating Notefile Template");
        while(1);
    }

    // Setup sensor
    if( !myENS.begin() ){
		usbSerial.println("Could not communicate with the ENS160, check wiring.");
		while(1);
	}
    if( !myENS.setOperatingMode(SFE_ENS160_RESET) ){
		usbSerial.println("Error resetting ENS160.");
		while(1);        
    }
    delay(200);
    if( !myENS.setOperatingMode(SFE_ENS160_STANDARD) ){
        usbSerial.println("Error putting ENS160 into stanndard mode");
		while(1);        
    } else {
        usbSerial.println("ENS160 in stanndard mode, waiting for data");
    }
}

void loop() {
    // Loop waiting for sensor to become stable, then send a reading, before sleeping for 60 seconds
    ensStatus = myENS.getFlags();
    if (ensStatus == 0 || ensStatus == 2) {
	    //if( myENS.checkDataStatus() ) {  // This seems to return true extreamly rarely
            //usbSerial.println("Got ENS Data");

            // Get the data, sleep
            AQISensorReading sensorReading;
            sensorReading.aqi = myENS.getAQI();
            sensorReading.eco2 = myENS.getECO2();
            sensorReading.etoh = myENS.getETOH();
            sensorReading.tvoc = myENS.getTVOC();
            sensorReading.alert = isAlert(sensorReading);
            int alertState; 
            // check alert state compared to previous alert state to get the alert message
            if (sensorReading.alert) {
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
            addNote(sensorReading, false, alertState);
        //} else {
        //    usbSerial.printf("ENS Data not ready, status: %d\r\n", ensStatus);    
        //}
        if (myENS.getOperationError()) {
            usbSerial.println("ENS Operation Error not ready");
        }
        delay(200);
        usbSerial.end();
        LowPower.sleep(15000);
        usbSerial.begin(115200);
        while (!usbSerial);
    } 
    delay(200);
}

bool isAlert(AQISensorReading& reading) {
    return (ALERT_AQI_LEVEL && reading.aqi >= ALERT_AQI_LEVEL)
    || (ALERT_ECO2_LEVEL && reading.eco2 >= ALERT_ECO2_LEVEL)
    || (ALERT_TVOC_LEVEL && reading.tvoc >= ALERT_TVOC_LEVEL);
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
    JAddNumberToObject(body, DATA_FIELD_AQI, TUINT8);
    JAddNumberToObject(body, DATA_FIELD_TVOC, TUINT16);
    JAddNumberToObject(body, DATA_FIELD_ECO2, TUINT16);
    JAddNumberToObject(body, DATA_FIELD_ETOH, TUINT16);
    JAddNumberToObject(body, DATA_FIELD_TEMP, TFLOAT16);
    JAddNumberToObject(body, DATA_FIELD_HUMIDITY, TFLOAT16);

    // Attach the body to the request
    JAddItemToObject(req, "body", body);

    // Setup template parameters
    JAddStringToObject(req, "format", "compact");
    JAddNumberToObject(req, "port", 10);

    // send it to the gateway
    return NoteRequest(req);
}

// Send the sensor data
static void addNote(const AQISensorReading& reading, bool immediate, uint8_t alert)
{

    // Create the request
    J *req = NoteNewRequest("note.add");
    if (req == NULL) {
        usbSerial.print("aqi: out of memory creating note\r\n");
        return;
    }

    // Create the body
    J *body = JCreateObject();
    if (body == NULL) {
        JDelete(req);
        usbSerial.print("aqi: out of memory creating note\r\n");
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
    JAddNumberToObject(req, "port", 10);
    NoteRequest(req);
    usbSerial.printf("aqi: note sent. aqi %d, eco2 %d, tvoc: %d\r\n", reading.aqi, reading.eco2, reading.tvoc);

}