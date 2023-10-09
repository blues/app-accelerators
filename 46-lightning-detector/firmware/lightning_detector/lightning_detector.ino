#include "SparkFun_AS3935.h"
#include <Notecard.h>
#include "NotecardEnvVarManager.h"

// Uncomment this line and replace com.my-company.my-name:my-project with your
// ProductUID.
// #define PRODUCT_UID "com.my-company.my-name:my-project"

#ifndef PRODUCT_UID
#define PRODUCT_UID ""
#pragma message "PRODUCT_UID is not defined in this example. Please ensure your Notecard has a product identifier set before running this example or define it in code here. More details at https://bit.ly/product-uid"
#endif

// The number of minutes between syncs of outbound data from the Notecard to
// Notehub (e.g. events.qo Notes).
#ifndef OUTBOUND_MINS
#define OUTBOUND_MINS 10
#endif

// The number of minutes between syncs of inbound data from Notehub to the
// Notecard (e.g. environment variable changes).
#ifndef INBOUND_MINS
#define INBOUND_MINS 5
#endif

// The maximum number of milliseconds to wait for the serial logging port to
// become available.
#ifndef MAX_SERIAL_WAIT_MS
#define MAX_SERIAL_WAIT_MS 5000
#endif

// The number of milliseconds to wait between fetches of the environment
// variables from the Notecard.
#ifndef ENV_FETCH_INTERVAL_MS
#define ENV_FETCH_INTERVAL_MS (3 * 60 * 1000) // 3 minutes
#endif

// The Notefile where detection events will be reported.
#ifndef EVENTS_FILE
#define EVENTS_FILE "events.qo"
#endif

// The GPIO pin to connect to the detector's chip select pin.
#ifndef SPI_CS_PIN
#define SPI_CS_PIN D5
#endif

// The GPIO pin to connect to the detector's interrupt pin.
#ifndef INTERRUPT_PIN
#define INTERRUPT_PIN D6
#endif

#ifndef NOISE_FLOOR_THRESHOLD_DEFAULT
#define NOISE_FLOOR_THRESHOLD_DEFAULT 2
#endif

#ifndef SPIKE_REJECTION_DEFAULT
#define SPIKE_REJECTION_DEFAULT 2
#endif

#ifndef WATCHDOG_THRESHOLD_DEFAULT
#define WATCHDOG_THRESHOLD_DEFAULT 2
#endif

#ifndef IGNORE_DISTURBERS_DEFAULT
#define IGNORE_DISTURBERS_DEFAULT false
#endif

#ifndef INDOOR_DEFAULT
#define INDOOR_DEFAULT true
#endif

Notecard notecard;
NotecardEnvVarManager *envVarManager = NULL;
SparkFun_AS3935 detector;

static const char TYPE_FIELD[] = "type";
static const char DIST_TO_STORM_FIELD[] = "distance_to_storm";
static const char ENERGY_FIELD[] = "energy";

static const int8_t DISTANCE_OUT_OF_RANGE = -1;

// These are the environment variables we'll be fetching from the Notecard.
static const char *envVars[] = {
    "ignore_disturbers",
    "indoor",
    "noise_floor_threshold",
    "spike_rejection",
    "wathdog_threshold"
};
static const size_t numEnvVars = sizeof(envVars) / sizeof(envVars[0]);

typedef struct {
    unsigned long lastEnvFetchMs;
    uint8_t noiseFloorThreshold;
    uint8_t spikeRejection;
    uint8_t watchdogThreshold;
    volatile bool gotInterrupt;
    bool ignoreDisturbers;
    bool indoor;
} AppState;

static AppState state = {
    .lastEnvFetchMs = 0,
    .noiseFloorThreshold = NOISE_FLOOR_THRESHOLD_DEFAULT,
    .spikeRejection = SPIKE_REJECTION_DEFAULT,
    .watchdogThreshold = WATCHDOG_THRESHOLD_DEFAULT,
    .gotInterrupt = false,
    .ignoreDisturbers = IGNORE_DISTURBERS_DEFAULT,
    .indoor = INDOOR_DEFAULT
};

enum EventType {
    NOISE_EVENT = 1,
    DISTURBER_EVENT = 2,
    LIGHTNING_EVENT = 3
};

// This callback is called whenever environment variables are fetched. It's
// called once per var:val pair.
static void envVarManagerCb(const char *var, const char *val, void *userCtx)
{
    if (strcmp(var, "ignore_disturbers") == 0) {
        bool ignore = atoi(val);

        if (state.ignoreDisturbers != ignore) {
            if (ignore) {
                Serial.println("Ignoring disturbers.");
            }
            else {
                Serial.println("NOT ignoring disturbers.");
            }

            detector.maskDisturber(ignore);
            state.ignoreDisturbers = ignore;
        }
    }
    else if (strcmp(var, "indoor") == 0) {
        bool indoor = atoi(val);

        if (state.indoor != indoor) {
            uint8_t indoorRegisterVal = INDOOR;

            if (indoor) {
                Serial.println("Using sensor's indoor settings.");
                indoorRegisterVal = INDOOR;
            }
            else {
                Serial.println("Using sensor's outdoor settings.");
                indoorRegisterVal = OUTDOOR;
            }

            detector.setIndoorOutdoor(indoorRegisterVal);
            state.indoor = indoor;
        }
    }
    else if (strcmp(var, "noise_floor_threshold") == 0) {
        int threshold = atoi(val);

        if (state.noiseFloorThreshold != threshold) {
            if (threshold < 0 || threshold > 7) {
                Serial.print("Noise floor value ");
                Serial.print(threshold);
                Serial.println(" is out of range. Must be in range [0, 7].");
            }
            else {
                Serial.print("Setting noise floor value to ");
                Serial.print(threshold);
                Serial.println(".");

                detector.setNoiseLevel(threshold);
                state.noiseFloorThreshold = threshold;
            }
        }
    }
    else if (strcmp(var, "spike_rejection") == 0) {
        int spikeRej = atoi(val);

        if (state.spikeRejection != spikeRej) {
            if (spikeRej < 0 || spikeRej > 15) {
                Serial.print("Spike rejection value ");
                Serial.print(spikeRej);
                Serial.println(" is out of range. Must be in range [0, 15].");
            }
            else {
                Serial.print("Setting spike rejection value to ");
                Serial.print(spikeRej);
                Serial.println(".");

                detector.spikeRejection(spikeRej);
                state.spikeRejection = spikeRej;
            }
        }
    }
    else if (strcmp(var, "watchdog_threshold") == 0) {
        int threshold = atoi(val);

        if (state.watchdogThreshold != threshold) {
            if (threshold < 0 || threshold > 15) {
                Serial.print("Watchdog threshold value ");
                Serial.print(threshold);
                Serial.println(" is out of range. Must be in range [0, 15].");
            }
            else {
                Serial.print("Setting watchdog threshold value to ");
                Serial.print(threshold);
                Serial.println(".");

                detector.watchdogThreshold(threshold);
                state.watchdogThreshold = threshold;
            }
        }
    }
}

// Register the template for lightning events.
static void registerTemplate()
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

    JAddStringToObject(req, "file", EVENTS_FILE);
    JAddNumberToObject(body, TYPE_FIELD, TUINT8);
    JAddNumberToObject(body, DIST_TO_STORM_FIELD, TINT8);
    JAddNumberToObject(body, ENERGY_FIELD, TUINT32);
    JAddItemToObject(req, "body", body);

    if (!notecard.sendRequest(req)) {
        Serial.println("Failed to send note.template request.");
    }
}

void NoteUserAgentUpdate(J *ua)
{
    JAddStringToObject(ua, "app", "nf46");
}

// Convert the binary distance value from the sensor to kilometers. The mappings
// here come from the sensor's datasheet, Figure 42: Distance Estimation.
// https://www.mouser.com/datasheet/2/588/ams_AS3935_Datasheet_EN_v5-1214568.pdf
static int8_t decodeDistanceValue(uint8_t binaryEncodedDist)
{
    int8_t distance = DISTANCE_OUT_OF_RANGE;

    switch (binaryEncodedDist) {
        case 0x7F:
            distance = DISTANCE_OUT_OF_RANGE;
            break;
        case 0x28:
            distance = 40;
            break;
        case 0x25:
            distance = 37;
            break;
        case 0x22:
            distance = 34;
            break;
        case 0x1F:
            distance = 31;
            break;
        case 0x1B:
            distance = 27;
            break;
        case 0x18:
            distance = 24;
            break;
        case 0x14:
            distance = 20;
            break;
        case 0x11:
            distance = 17;
            break;
        case 0x0E:
            distance = 14;
            break;
        case 0x0C:
            distance = 12;
            break;
        case 0x0A:
            distance = 10;
            break;
        case 0x08:
            distance = 8;
            break;
        case 0x06:
            distance = 6;
            break;
        case 0x05:
            distance = 5;
            break;
        case 0x01:
            distance = 0;
            break;
        default:
            break;
    }

    return distance;
}

static void sendEventNote(EventType type, int8_t distance, uint32_t energy)
{
    Serial.println("Sending event note...");

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

    JAddStringToObject(req, "file", EVENTS_FILE);
    JAddNumberToObject(body, TYPE_FIELD, type);
    JAddNumberToObject(body, DIST_TO_STORM_FIELD, distance);
    JAddNumberToObject(body, ENERGY_FIELD, energy);
    JAddItemToObject(req, "body", body);

    if (!notecard.sendRequest(req)) {
        Serial.println("Failed to send note.");
    }
}

void detectionISR()
{
    if (!state.gotInterrupt) {
        state.gotInterrupt = true;
    }
}

void setup()
{
    // Initialize debug output.
    Serial.begin(115200);
    unsigned long beginSerialWaitMs = millis();
    // Wait for serial port to connect.
    while (!Serial &&
           (MAX_SERIAL_WAIT_MS > (millis() - beginSerialWaitMs))) {
    }
    notecard.setDebugOutputStream(Serial);

    Serial.println("Starting SPI...");
    SPI.begin();
    Serial.println("Starting lightning detector...");
    if (!detector.beginSPI(SPI_CS_PIN)) {
        Serial.println("Failed to start lightning detector.");
    }

    // Initialize Notecard.
    notecard.begin();

    J *req = notecard.newRequest("hub.set");
    JAddStringToObject(req, "product", PRODUCT_UID);
    JAddStringToObject(req, "mode", "periodic");
    JAddIntToObject(req, "outbound", OUTBOUND_MINS);
    JAddIntToObject(req, "inbound", INBOUND_MINS);
    notecard.sendRequest(req);

    registerTemplate();

    // These defaults are the same as the factory defaults for the sensor.
    detector.maskDisturber(IGNORE_DISTURBERS_DEFAULT);
    detector.setIndoorOutdoor(INDOOR_DEFAULT);
    detector.setNoiseLevel(NOISE_FLOOR_THRESHOLD_DEFAULT);
    detector.spikeRejection(SPIKE_REJECTION_DEFAULT);
    detector.watchdogThreshold(WATCHDOG_THRESHOLD_DEFAULT);

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
        state.lastEnvFetchMs = millis();
    }

    attachInterrupt(digitalPinToInterrupt(INTERRUPT_PIN), detectionISR, RISING);
}

void loop()
{
    if (state.gotInterrupt) {
        uint8_t interruptVal = detector.readInterruptReg();

        Serial.print("Interrupt received. Source: ");

        EventType type = NOISE_EVENT;
        int8_t distance = 0;
        uint32_t energy = 0;

        switch (interruptVal) {
            case NOISE_TO_HIGH:
                type = NOISE_EVENT;
                Serial.println("noise.");
                break;
            case DISTURBER_DETECT:
                type = DISTURBER_EVENT;
                Serial.println("disturber.");
                break;
            case LIGHTNING:
                type = LIGHTNING_EVENT;
                distance = decodeDistanceValue(detector.distanceToStorm());
                energy = detector.lightningEnergy();

                Serial.println("lightning.");

                Serial.print("Distance from head of storm: ");
                if (distance != DISTANCE_OUT_OF_RANGE) {
                    Serial.print(distance);
                    Serial.println(" km.");
                }
                else {
                    Serial.println("out of range.");
                }

                Serial.print("Energy value: ");
                Serial.print(energy);
                Serial.println(".");
                break;
            default:
                state.gotInterrupt = false;

                Serial.print("Unknown interrupt value: ");
                Serial.print(interruptVal);
                Serial.println(".");
                return;
        }

        sendEventNote(type, distance, energy);
        state.gotInterrupt = false;
    }

    // Fetch environment variables, if ENV_FETCH_INTERVAL_MS milliseconds have
    // passed since the last fetch.
    if (millis() - state.lastEnvFetchMs > ENV_FETCH_INTERVAL_MS) {
        Serial.println("Fetching environment variables...");
        state.lastEnvFetchMs = millis();

        if (NotecardEnvVarManager_fetch(envVarManager, envVars, numEnvVars)
                != NEVM_SUCCESS) {
            Serial.println("NotecardEnvVarManager_fetch failed.");
        }
    }

}
