#include <Notecard.h>
#include "NotecardEnvVarManager.h"

// Uncomment this line and replace com.my-company.my-name:my-project with your
// ProductUID.
// #define PRODUCT_UID "com.my-company.my-name:my-project"

#ifndef PRODUCT_UID
#define PRODUCT_UID ""
#pragma message "PRODUCT_UID is not defined in this example. Please ensure your Notecard has a product identifier set before running this example or define it in code here. More details at https://bit.ly/product-uid"
#endif

#ifndef OUTBOUND_MINS
#define OUTBOUND_MINS 10
#endif

#ifndef INBOUND_MINS
#define INBOUND_MINS 5
#endif

#ifndef MAX_SERIAL_WAIT_MS
#define MAX_SERIAL_WAIT_MS 5000
#endif

#ifndef ENV_FETCH_INTERVAL_MS
#define ENV_FETCH_INTERVAL_MS (3 * 60 * 1000) // 3 minutes
#endif

#ifndef ANALOG_PIN
#define ANALOG_PIN A0
#endif

#ifndef ADC_BITS
#define ADC_BITS 10
#endif

#define ADC_RANGE (1 << ADC_BITS)

#ifndef SAMPLES_PER_CALC
#define SAMPLES_PER_CALC 1024
#endif

#ifndef DEFAULT_SECONDS_BETWEEN_MEASUREMENTS
#define DEFAULT_SECONDS_BETWEEN_MEASUREMENTS 15
#endif

#ifndef DEFAULT_PERCENT_CHANGE_THRESHOLD
#define DEFAULT_PERCENT_CHANGE_THRESHOLD 50
#endif

#ifndef AMPS_PER_VOLT
#define AMPS_PER_VOLT 30
#endif

#ifndef ADC_REF_VOLTAGE
#define ADC_REF_VOLTAGE 3.3
#endif

#ifndef MEASUREMENTS_PER_NOTE
#define MEASUREMENTS_PER_NOTE 10
#endif

#ifndef MEASUREMENTS_FILE
#define MEASUREMENTS_FILE "measurements.qo"
#endif

Notecard notecard;
NotecardEnvVarManager *envVarManager = NULL;

// These are the environment variables we'll be fetching from the Notecard.
static const char *envVars[] = {
    "seconds_between_measurements",
    "percentage_change_threshold"
};
static const size_t numEnvVars = sizeof(envVars) / sizeof(envVars[0]);
static int msBetweenMeasurements = DEFAULT_SECONDS_BETWEEN_MEASUREMENTS * 1000;
static float percentChangeThreshold = DEFAULT_PERCENT_CHANGE_THRESHOLD;

static float measurements[MEASUREMENTS_PER_NOTE];
static size_t measurementsIdx = 0;

static unsigned long lastMeasurementMs = 0;
static unsigned long lastEnvFetchMs = 0;
static float lastAvgCurrent = -1;

void sendMeasurementsNote(const float *data, size_t len,
    uint32_t msBetweenMeasurements, float percentChangeThreshold,
    float *lastAvgCurrent)
{
    if (len == 0) {
        // Nothing to send.
        return;
    }

    Serial.print("Adding measurements note...");

    J *req = notecard.newRequest("note.add");
    if (req == NULL) {
        Serial.println("Failed to create note.add request.");
        return;
    }

    J *body = JCreateObject();
    if (body == NULL) {
        JDelete(req);
        Serial.println("Failed to create note.add request body.");
        return;
    }

    JAddStringToObject(req, "file", MEASUREMENTS_FILE);
    J *dataArray = JAddArrayToObject(body, "measurements");
    float sum = 0;
    for (int i = 0; i < len; ++i) {
        float current = data[i];
        sum += current;
        JAddItemToArray(dataArray, JCreateNumber(current));
    }
    JAddNumberToObject(body, "length", len);
    JAddNumberToObject(body, "seconds_between_measurements",
        msBetweenMeasurements / 1000);
    JAddItemToObject(req, "body", body);

    float avg = sum / len;
    // When the first measurements note is sent, lastAvgCurrent will be -1,
    // which indicates that there's no lastAvgCurrent value, yet. So, we only
    // run through the logic below after we've already published a note.
    if (*lastAvgCurrent > 0) {
        float percentChange = 100 * fabs(avg - *lastAvgCurrent) /
            *lastAvgCurrent;
        if (percentChange > percentChangeThreshold) {
            // Sync the note immediately if the percentage change threshold is
            // exceeded.
            JAddBoolToObject(req, "sync", true);
        }
    }
    *lastAvgCurrent = avg;

    if (!notecard.sendRequest(req)) {
        Serial.println("Failed to send note.");
        return;
    }

    Serial.println("Done.");
}

// This callback is called whenever environment variables are fetched. It's
// called once per var:val pair.
static void envVarManagerCb(const char *var, const char *val, void *userCtx)
{
    if (strcmp(var, "seconds_between_measurements") == 0) {
        int seconds = atoi(val);

        if (seconds <= 0) {
            Serial.println("seconds_between_measurements must be a positive "
                "integer.");
        }
        else {
            // Flush out the measurements before changing any environment
            // variables.
            sendMeasurementsNote(measurements, measurementsIdx,
                msBetweenMeasurements, percentChangeThreshold,
                &lastAvgCurrent);

            msBetweenMeasurements = seconds * 1000; // Convert seconds to ms.
            Serial.print("Seconds between measurements set to ");
            Serial.print(seconds);
            Serial.println(".");
        }
    }
    else if (strcmp(var, "percentage_change_threshold") == 0) {
        float threshold = atof(val);

        if (threshold <= 0) {
            Serial.println("percentage_change_threshold must be a positive "
                "number.");
        }
        else {
            // Flush out the measurements before changing any environment
            // variables.
            sendMeasurementsNote(measurements, measurementsIdx,
                msBetweenMeasurements, percentChangeThreshold,
                &lastAvgCurrent);

            percentChangeThreshold = threshold;
            Serial.print("Percentage change threshold set to ");
            Serial.print(percentChangeThreshold);
            Serial.println(".");
        }
    }
}

float calcVrms()
{
    static float samples[SAMPLES_PER_CALC];

    int sample = 0;
    float sum = 0;

    // Read the samples into an array and sum them up so we can compute the DC
    // offset via an average.
    for (size_t i = 0; i < SAMPLES_PER_CALC; ++i) {
        samples[i] = analogRead(ANALOG_PIN);
        sum += samples[i];
    }

    // Average the samples to get the DC offset.
    float offset = sum / SAMPLES_PER_CALC;
    float sampleWithOffsetRemoved = 0;
    sum = 0;

    for (size_t i = 0; i < SAMPLES_PER_CALC; ++i) {
        // Subtract the offset from each sample.
        sampleWithOffsetRemoved = samples[i] - offset;
        // Sum the squares of the samples with offset removed.
        sum += sampleWithOffsetRemoved * sampleWithOffsetRemoved;
    }

    // Use the sum of squares to compute the RMS voltage.
    float rms = (sqrt(sum / SAMPLES_PER_CALC) / ADC_RANGE) * ADC_REF_VOLTAGE;

    return rms;
}

// Register the template for current consumption data.
void registerTemplate()
{
    J *req = notecard.newRequest("note.template");
    if (req == NULL) {
        Serial.println("Failed to create note.template request.");
        return;
    }

    J *body = JCreateObject();
    if (body == NULL) {
        JDelete(req);
        Serial.println("Failed to create note.template request body.");
        return;
    }

    JAddStringToObject(req, "file", MEASUREMENTS_FILE);
    JAddNumberToObject(body, "seconds_between_measurements", TUINT32);
    JNUMBER measurementsTemplate[MEASUREMENTS_PER_NOTE];
    for (size_t i = 0; i < MEASUREMENTS_PER_NOTE; ++i) {
        measurementsTemplate[i] = TFLOAT32;
    }
    J *data = JCreateNumberArray(measurementsTemplate, MEASUREMENTS_PER_NOTE);
    JAddItemToObject(body, "measurements", data);
    // Add the length field, which indicates how many valid measurements are in
    // the measurements array.
    JAddNumberToObject(body, "length", TUINT8);
    JAddItemToObject(req, "body", body);

    if (!notecard.sendRequest(req)) {
        Serial.println("Failed to send note.template request.");
    }
}

void NoteUserAgentUpdate(J *ua)
{
    JAddStringToObject(ua, "app", "nf49");
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

    J *req = notecard.newRequest("hub.set");
    JAddStringToObject(req, "product", PRODUCT_UID);
    JAddStringToObject(req, "mode", "periodic");
    JAddIntToObject(req, "outbound", OUTBOUND_MINS);
    JAddIntToObject(req, "inbound", INBOUND_MINS);
    notecard.sendRequest(req);

    registerTemplate();

    envVarManager = NotecardEnvVarManager_alloc();
    if (envVarManager == NULL) {
        Serial.println("Failed to allocate env var manager.");
    }
    else if (NotecardEnvVarManager_setEnvVarCb(envVarManager, envVarManagerCb,
            NULL) != NEVM_SUCCESS) {
        Serial.println("Failed to set env var manager callback.");
    }
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

    if (millis() - lastMeasurementMs > msBetweenMeasurements) {
        Serial.println("Taking measurement...");
        lastMeasurementMs = millis();

        float vrms = calcVrms();
        float current = vrms * AMPS_PER_VOLT;
        measurements[measurementsIdx++] = current;

        Serial.print("\tCT voltage: ");
        Serial.print(vrms);
        Serial.println("V");
        Serial.print("\tImplied current: ");
        Serial.print(current);
        Serial.println("A");
    }

    if (measurementsIdx == MEASUREMENTS_PER_NOTE) {
        sendMeasurementsNote(measurements, measurementsIdx,
                msBetweenMeasurements, percentChangeThreshold,
                &lastAvgCurrent);
        measurementsIdx = 0;
    }
}
