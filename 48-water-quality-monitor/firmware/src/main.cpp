#include <Notecard.h>
#include "NotecardEnvVarManager.h"
// See https://github.com/DFRobot/DFRobot_PH.
#include <DFRobot_PH.h>
// See https://github.com/DFRobot/GravityTDS.
#include "GravityTDS.h"
#include <OneWire.h>

// Uncomment this line and replace com.your-company:your-product-name with your
// ProductUID.
// #define PRODUCT_UID "com.my-company.my-name:my-project"

#ifndef PRODUCT_UID
#define PRODUCT_UID ""
#pragma message "PRODUCT_UID is not defined in this example. Please ensure your Notecard has a product identifier set before running this example or define it in code here. More details at https://bit.ly/product-uid"
#endif

#ifndef ORP_PIN
#define ORP_PIN A0
#endif

#ifndef PH_PIN
#define PH_PIN A1
#endif

#ifndef TDS_PIN
#define TDS_PIN A2
#endif

#ifndef TEMP_PIN
#define TEMP_PIN 6
#endif

#ifndef ENV_FETCH_INTERVAL_MS
#define ENV_FETCH_INTERVAL_MS (5 * 60 * 1000) // 5 minutes
#endif

#ifndef ORP_OFFSET
#define ORP_OFFSET 0
#endif

#define ORP_SAMPLE_COUNT 25
#define ORP_SAMPLE_DELAY_MS (1000 / ORP_SAMPLE_COUNT)

#define PH_SAMPLE_COUNT 25
#define PH_SAMPLE_DELAY_MS (1000 / PH_SAMPLE_COUNT)

#define MAX_TEMP_SAMPLE_ATTEMPTS 3
#define TEMP_SAMPLING_DELAY_MS 702
#define DS18B20_SCRATCHPAD_SIZE 9

#define MAX_SERIAL_WAIT_MS 5000

Notecard notecard;
NotecardEnvVarManager *envVarManager = NULL;

DFRobot_PH ph;
GravityTDS tds;
OneWire ds18b20(TEMP_PIN);

// Application state.
static unsigned long lastSampleMs = 0;
static unsigned long lastEnvFetchMs = 0;
static unsigned long samplePeriodMs = 10 * 60 * 1000; // 10 minutes

// These are the environment variables we'll be fetching from the Notecard.
const char *envVars[] = {
    "sample_period"
};
static const size_t numEnvVars = sizeof(envVars) / sizeof(envVars[0]);

// Based on sampleAverageAnalogSignal from here:
// https://github.com/zfields/StreamResearch/blob/main/StreamResearch.ino
float getPhVoltage()
{
    int samples[PH_SAMPLE_COUNT];
    int maxSample = INT_MIN;
    int minSample = INT_MAX;
    int maxSampleIndex = 0;
    int minSampleIndex = 0;
    int sampleAggregate = 0;

    // Sample the sensor multiple times over one second.
    for (int i = 0 ; i < PH_SAMPLE_COUNT ; ++i) {
        samples[i] = analogRead(PH_PIN);
        delay(PH_SAMPLE_DELAY_MS);

        if (samples[i] > maxSample) {
            maxSample = samples[i];
            maxSampleIndex = i;
        }
        if (samples[i] < minSample) {
            minSample = samples[i];
            minSampleIndex = i;
        }
    }

    // Average the remaining readings.
    int numSamples = 0;
    for (int i = 0 ; i < PH_SAMPLE_COUNT ; ++i) {
        // Remove the highest and lowest samples.
        if (i == maxSampleIndex || i == minSampleIndex) {
            continue;
        }

        ++numSamples;
        sampleAggregate += samples[i];
    }

    return ((sampleAggregate / numSamples) / 1024.0) * 5000;
}

// Based on sampleDs18b20 from the StreamResearch.ino code linked above.
float sampleDs18b20(int pin)
{
    int encodedTemp = 0;

    // Sample the sensor (with retries).
    for (int i = 0 ; i < MAX_TEMP_SAMPLE_ATTEMPTS ; ++i) {
        byte scratchpad[DS18B20_SCRATCHPAD_SIZE];

        // Begin a temperature conversion (Convert T [0x44]).
        ds18b20.reset();
        ds18b20.skip();
        ds18b20.write(0x44);
        delay(TEMP_SAMPLING_DELAY_MS);

        // Read the contents of the scratchpad (Read Scratchpad [0xBE]).
        ds18b20.reset();
        ds18b20.skip();
        ds18b20.write(0xBE); // Read scratchpad.
        for (int i = 0; i < DS18B20_SCRATCHPAD_SIZE; ++i) {
          scratchpad[i] = ds18b20.read();
        }

        // Validate CRC.
        if (OneWire::crc8(scratchpad, (DS18B20_SCRATCHPAD_SIZE-1)) !=
            scratchpad[(DS18B20_SCRATCHPAD_SIZE-1)]) {
            continue;
        }

        // Reassemble temperature value.
        encodedTemp = ((int)scratchpad[1] << 8) | scratchpad[0];
        break;
    }

    // The encoded temperature carries fractional information and is shifted 4
    // bits to the left. Divide by 16 into float value to preserve fractional
    // information.
    return (float)(encodedTemp)/16.0;
}

// Adapted from the sample code provided by DFRobot here:
// https://wiki.dfrobot.com/Analog_ORP_Meter_SKU_SEN0165_
float averageArray(int* arr, int len){
    int i;
    int min;
    int max;
    float avg;
    long amount = 0;

    if (len <= 0) {
        return 0;
    }

    // Calculate average directly.
    if (len < 5) {
        for (i = 0; i < len; ++i) {
            amount += arr[i];
        }

        avg = amount/len;
        return avg;
    }

    if (arr[0]<arr[1]) {
        min = arr[0];
        max = arr[1];
    }
    else {
        min = arr[1];
        max = arr[0];
    }
    for (i = 2; i < len; ++i) {
        if (arr[i] < min) {
            amount += min;
            min = arr[i];
        }
        else {
            if(arr[i] > max) {
                amount += max;
                max = arr[i];
            }
            else {
                amount += arr[i];
            }
        }
    }

    avg = (float)amount / (len - 2);

    return avg;
}

float getOrp()
{
    int samples[ORP_SAMPLE_COUNT];

    // Sample the sensor multiple times over one second.
    for (int i = 0 ; i < ORP_SAMPLE_COUNT ; ++i) {
        samples[i] = analogRead(ORP_PIN);
        delay(ORP_SAMPLE_DELAY_MS);
    }

    // Taken from ORP sample code here:
    // https://wiki.dfrobot.com/Analog_ORP_Meter_SKU_SEN0165_
    return ((30 * 5.0 * 1000) - (75 * averageArray(samples, ORP_SAMPLE_COUNT) *
            5.0 * 1000 / 1024)) / 75 - ORP_OFFSET;
}

// This callback is called whenever environment variables are fetched. It's
// called once per var:val pair.
static void envVarManagerCb(const char *var, const char *val, void *userCtx)
{
    if (strcmp(var, "sample_period") == 0) {
        int period = atoi(val);

        if (period <= 0) {
            Serial.println("sample_period must be a positive integer.");
        }
        else {
            samplePeriodMs = period * 60 * 1000;
            Serial.print("Sample period set to ");
            Serial.print(period);
            Serial.println(" minutes.");
        }
    }
}

void setup()
{
    // Initialize debug output.
    Serial.begin(115200);
    unsigned long beginSerialWaitMs = millis();
    while (!Serial &&
           (MAX_SERIAL_WAIT_MS > (millis() - beginSerialWaitMs))) {
        // Wait for serial port to connect.
    }
    notecard.setDebugOutputStream(Serial);

    // Initialize Notecard.
    notecard.begin();

    // See sample code here:
    // https://wiki.dfrobot.com/Gravity__Analog_TDS_Sensor___Meter_For_Arduino_SKU__SEN0244
    tds.setPin(TDS_PIN);
    tds.setAref(5.0); // Reference voltage.
    tds.setAdcRange(1024);  // 1024 for 10-bit ADC.
    tds.begin();

    // See sample code here:
    // https://wiki.dfrobot.com/Gravity__Analog_pH_Sensor_Meter_Kit_V2_SKU_SEN0161-V2
    ph.begin();

    J *req = notecard.newRequest("hub.set");
    JAddStringToObject(req, "product", PRODUCT_UID);
    JAddStringToObject(req, "mode", "periodic");
    notecard.sendRequest(req);

    // Establish a template to optimize queue size and data usage.
    req = notecard.newRequest("note.template");
    JAddStringToObject(req, "file", "sensor.qo");
    J *body = JAddObjectToObject(req, "body");
    JAddNumberToObject(body, "orp", 12.1);
    JAddNumberToObject(body, "ph", 12.1);
    JAddNumberToObject(body, "tds", 12.1);
    JAddNumberToObject(body, "temp", 12.1);
    notecard.sendRequest(req);

    // Set up the environment variable manager.
    envVarManager = NotecardEnvVarManager_alloc();
    if (envVarManager == NULL) {
        Serial.println("Failed to allocate env var manager.");
    }
    else if (NotecardEnvVarManager_setEnvVarCb(envVarManager, envVarManagerCb,
            NULL) != NEVM_SUCCESS) {
        Serial.println("Failed to set env var manager callback.");
    }
    // Fetch environment variables.
    else if (NotecardEnvVarManager_fetch(envVarManager, envVars, numEnvVars)
            != NEVM_SUCCESS) {
        Serial.println("NotecardEnvVarManager_fetch failed.");
    }
    else {
        lastEnvFetchMs = millis();
    }
}

void loop()
{
    // Fetch environment variables, if ENV_FETCH_INTERVAL_MS milliseconds have
    // passed since the last fetch.
    if (millis() - lastEnvFetchMs > ENV_FETCH_INTERVAL_MS) {
        Serial.println("Fetching environment variables...");
        lastEnvFetchMs = millis();

        if (NotecardEnvVarManager_fetch(envVarManager, envVars, numEnvVars)
                != NEVM_SUCCESS) {
            Serial.println("NotecardEnvVarManager_fetch failed.");
        }
    }

    if (millis() - lastSampleMs > samplePeriodMs) {
        Serial.println("Taking measurements...");
        lastSampleMs = millis();

        // Sample sensors and queue results.
        J *req = notecard.newRequest("note.add");
        JAddStringToObject(req, "file", "sensor.qo");

        J *body = JAddObjectToObject(req, "body");
        float orpValue = getOrp();
        JAddNumberToObject(body, "orp", orpValue);

        float temp = sampleDs18b20(TEMP_PIN);
        JAddNumberToObject(body, "temp", temp);

        float phValue = ph.readPH(getPhVoltage(), temp);
        JAddNumberToObject(body, "ph", phValue);

        // Set the temperature and perform temperature compensation.
        tds.setTemperature(temp);
        // Sample the sensor and calculate the TDS.
        tds.update();
        float tdsValue = tds.getTdsValue();
        JAddNumberToObject(body, "tds", tdsValue);

        Serial.print("Temperature: ");
        Serial.println(temp);
        Serial.print("ORP: ");
        Serial.println(orpValue);
        Serial.print("pH: ");
        Serial.println(phValue);
        Serial.print("TDS: ");
        Serial.println(tdsValue);

        Serial.println("Sending measurements to Notehub.");
        notecard.sendRequest(req);
    }
}
