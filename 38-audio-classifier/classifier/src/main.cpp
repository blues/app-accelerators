#include <SPI.h>
#include <Notecard.h>
#include "NotecardEnvVarManager.h"
#include <MEMSAudioCapture.h>
// Include your specific Edge Impulse header file here:
// #include <running_faucet_detector_inferencing.h>

// Uncomment this line and replace com.your-company:your-product-name with your
// ProductUID.
// #define PRODUCT_UID "com.your-company:your-product-name"

#ifndef PRODUCT_UID
#define PRODUCT_UID "" // "com.my-company.my-name:my-project"
#pragma message "PRODUCT_UID is not defined in this example. Please ensure your Notecard has a product identifier set before running this example or define it in code here. More details at https://bit.ly/product-uid"
#endif

// If the hub.set request during setup fails, retry it for up to 5 seconds.
#ifndef HUB_SET_TIMEOUT_SECS
#define HUB_SET_TIMEOUT_SECS 5
#endif

#ifndef MIN
#define MIN(a,b) (((a)<(b))?(a):(b))
#endif

#ifndef LED_ON_PERIOD_MS
#define LED_ON_PERIOD_MS 1000
#endif

// Define DEBUG_CLASSIFIER to print debug messages related to the classifier.
#define DEBUG_CLASSIFIER

// Fetch environment variables every 5 minutes.
#ifndef ENV_FETCH_INTERVAL_MS
#define ENV_FETCH_INTERVAL_MS (15 * 1000)
// #define ENV_FETCH_INTERVAL_MS (5 * 60 * 1000)
#endif

#ifndef NUM_PCM_BUFS
#define NUM_PCM_BUFS 3
#endif

#define PCM_BUF_SIZE EI_CLASSIFIER_DSP_INPUT_FRAME_SIZE

MEMSAudioCapture mic;
Notecard notecard;
NotecardEnvVarManager *envVarManager = NULL;

// These are the environment variables we'll be fetching from the Notecard.
const char *envVars[] = {
    "label",
    "detection_threshold",
    "publish_rate"
};
static const size_t numEnvVars = sizeof(envVars) / sizeof(envVars[0]);

// pcmBuf is a simple FIFO of NUM_PCM_BUFS audio buffers. Audio captured by the
// mic is pushed in and the main loop pulls it out for classification.
static float pcmBuf[NUM_PCM_BUFS][PCM_BUF_SIZE];
static size_t pcmBufLen = 0;
static size_t numUnclassifiedBufs = 0;
static uint8_t writeIndex = 0;
static uint8_t readIndex = 0;

static unsigned long ledTurnedOnMs = 0;
static unsigned long envFetchedMs = 0;
static unsigned long lastPublishMs = 0;
static bool ledOn = false;

// By default, a detection will be published if the probability value for the
// label of interest is > 0.6. This can be changed by setting the
// "detection_threshold" environment variable.
static float detectionThreshold = 0.6;
// This is the label of interest. By default, it's set to nothing, so the user
// must set the environment variable "label" for detections to be published.
static char label[32] = {0};
// A detection note will be published a maximum of once every publishRateMs
// milliseconds. By default, this is set to 1 minute. The user can change this
// by setting the environment variable "publish_rate".
static uint32_t publishRateMs = 60 * 1000;

static void envVarManagerCb(const char *var, const char *val, void *userCtx)
{
    if (strcmp(var, "label") == 0) {
        if (strlen(val) >= sizeof(label)) {
            Serial.print("Label \"");
            Serial.print(val);
            Serial.println("\" too long.");
        }
        else {
            strlcpy(label, val, sizeof(label));
            Serial.print("Label of interest set to ");
            Serial.print(label);
            Serial.println(".");
        }
    }
    else if (strcmp(var, "detection_threshold") == 0) {
        char *endPtr;
        float value = strtof(val, &endPtr);

        if (value == 0 && val == endPtr) {
            Serial.print("Failed to convert ");
            Serial.print(val);
            Serial.println(" to float for detection_threshold.");
        }
        else if (value <= 0.0 || value > 1.0) {
            Serial.print("Value ");
            Serial.print(val);
            Serial.println(" is out of range for detection_threshold.");
        }
        else {
            detectionThreshold = value;
            Serial.print("Detection threshold set to ");
            Serial.print(detectionThreshold);
            Serial.println(".");
        }
    }
    else if (strcmp(var, "publish_rate") == 0) {
        int rate = atoi(val);

        if (rate <= 0) {
            Serial.println("publish_rate must be a positive integer.");
        }
        else {
            publishRateMs = rate * 1000;
            Serial.print("Publish rate set to ");
            Serial.print(rate);
            Serial.println(" seconds.");
        }
    }
}

void accumulateSamples(MemsAudio* audio, pcm_sample_t* samples, size_t len)
{
    if (numUnclassifiedBufs > 0 && writeIndex == readIndex) {
        Serial.println("Overflow! Increase NUM_PCM_BUFS.");
    }

    if (pcmBufLen < PCM_BUF_SIZE) {
        size_t lenToCopy = MIN(PCM_BUF_SIZE - pcmBufLen, len);
        for (size_t i = 0; i < lenToCopy; ++i) {
            (pcmBuf[writeIndex] + pcmBufLen)[i] = (float)samples[i];
        }

        pcmBufLen += lenToCopy;
    }

    if (pcmBufLen == PCM_BUF_SIZE) {
        ++numUnclassifiedBufs;
        pcmBufLen = 0;

        if (writeIndex == NUM_PCM_BUFS - 1) {
            writeIndex = 0;
        }
        else {
            ++writeIndex;
        }
    }
}

void setup()
{
    delay(2500);

    Serial.begin(115200);
    notecard.setDebugOutputStream(Serial);

    if (!mic.begin(accumulateSamples)) {
        Serial.println("Unable to start audio capture.");
    }

    // The Swan's LED will light up when the classifier detects the label of
    // interest (e.g. faucet running).
    pinMode(LED_BUILTIN, OUTPUT);

    notecard.begin();

    // Configure the productUID and set the Notecard to periodic mode.
    J *req = notecard.newRequest("hub.set");
    if (PRODUCT_UID[0]) {
       JAddStringToObject(req, "product", PRODUCT_UID);
    }
    JAddStringToObject(req, "mode", "periodic");
    if (!notecard.sendRequestWithRetry(req, HUB_SET_TIMEOUT_SECS)) {
        Serial.println("Failed to send hub.set request to Notecard.");
    }

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
        envFetchedMs = millis();
    }
}

bool publishDetection(float probability)
{
    bool success = true;

    Serial.println("Publishing detection...");

    J *req = notecard.newRequest("note.add");
    if (req != NULL) {
        JAddStringToObject(req, "file", "detections.qo");
        JAddBoolToObject(req, "sync", true);

        J *body = JCreateObject();
        if (body != NULL) {
            JAddStringToObject(body, "label", label);
            JAddNumberToObject(body, "probability", probability);
            JAddItemToObject(req, "body", body);

            if (!notecard.sendRequest(req)) {
                Serial.println("note.add for detection note failed.");
                success = false;
            }
        }
        else {
            JDelete(req);
            Serial.println("Failed to create body for detection note.");
            success = false;
        }
    }
    else {
        Serial.println("Failed to create note.add request for detection note.");
        success = false;
    }

    return success;
}

static void updateReadIndex()
{
    --numUnclassifiedBufs;
    if (readIndex == NUM_PCM_BUFS - 1) {
        readIndex = 0;
    }
    else {
        ++readIndex;
    }
}

void loop()
{
    // If the LED was turned on to signal an anomaly, turn it off after
    // LED_ON_PERIOD_MS milliseconds have passed.
    unsigned long currentMs = millis();
    if (ledOn && (currentMs - ledTurnedOnMs > LED_ON_PERIOD_MS)) {
        digitalWrite(LED_BUILTIN, LOW);
        ledOn = false;
    }

    // Fetch environment variables, if ENV_FETCH_INTERVAL_MS milliseconds have
    // passed since the last fetch.
    if (currentMs - envFetchedMs >= ENV_FETCH_INTERVAL_MS) {
        Serial.println("Fetching environment variables...");
        if (NotecardEnvVarManager_fetch(envVarManager, envVars, numEnvVars)
                != NEVM_SUCCESS) {
            Serial.println("NotecardEnvVarManager_fetch failed.");
        }
        envFetchedMs = currentMs;
    }

    // Run the classifier once we have a buffer to process.
    if (numUnclassifiedBufs > 0) {
        // Turn the raw buffer into a signal which we can the classify.
        signal_t signal;
        int err = numpy::signal_from_buffer(pcmBuf[readIndex], PCM_BUF_SIZE,
                                            &signal);
        if (err != 0) {
            ei_printf("Failed to create signal from buffer (%d)\n", err);
            updateReadIndex();
            return;
        }

        // Run the classifier.
        ei_impulse_result_t result = {0};
        err = run_classifier(&signal, &result, false);
        if (err != EI_IMPULSE_OK) {
            ei_printf("ERR: Failed to run classifier (%d)\n", err);
            updateReadIndex();
            return;
        }

        updateReadIndex();

    #ifdef DEBUG_CLASSIFIER
        ei_printf("(DSP: %d ms., Classification: %d ms.)\n",
            result.timing.dsp, result.timing.classification);
    #endif

        float probability = 0;
        for (size_t i = 0; i < EI_CLASSIFIER_LABEL_COUNT; i++) {
            if (strcmp(result.classification[i].label, label) == 0) {
                probability = result.classification[i].value;
            }

        #ifdef DEBUG_CLASSIFIER
            Serial.print("    ");
            Serial.print(result.classification[i].label);
            Serial.print(": ");
            Serial.println(result.classification[i].value);
        #endif
        }

        if (probability > detectionThreshold) {
            // Turn on the LED.
            digitalWrite(LED_BUILTIN, HIGH);
            ledTurnedOnMs = millis();
            ledOn = true;

            // Only publish the detection if it's been more than publishRateMs
            // milliseconds since the last publish.
            currentMs = millis();
            if (lastPublishMs == 0 ||
                (currentMs - lastPublishMs > publishRateMs)) {
                lastPublishMs = currentMs;

                // Publish a detection to Notehub.
                if (!publishDetection(probability)) {
                    Serial.println("Failed to publish detection note.");
                }
            }
        }
    }
}
