// Copyright 2023 Zachary J. Fields. All rights reserved.
//
// Use of this source code is governed by licenses granted by the
// copyright holder including that found in the LICENSE file.
//

// Include the Standard libraries
#include <stdint.h>
#include <stdlib.h>

// Include the Arduino libraries
#include <Adafruit_SSD1306.h>
#include <Notecard.h>
#include <NotecardEnvVarManager.h>

HardwareSerial stlinkSerial(PIN_VCP_RX, PIN_VCP_TX);
#define txRxPinsSerial Serial1

// #define PRODUCT_UID "com.my-company.my-name:my-project"

// This is the unique Product Identifier for your device
#ifndef PRODUCT_UID
#define PRODUCT_UID "" // "com.my-company.my-name:my-project"
#pragma message "PRODUCT_UID is not defined in this example. Please ensure your Notecard has a product identifier set before running this example or define it in code here. More details at https://dev.blues.io/tools-and-sdks/samples/product-uid"
#endif
#define myProductID PRODUCT_UID

static const char * ENV_VAR_LIST[] = {"alarm_threshold", "wifi_ssid", "wifi_password"};
static const uint32_t THREE_MINUTES_MS(3 * 60 * 1000);
static const uint32_t FIFTEEN_MINUTES_MS(15 * 60 * 1000);
static const int SSD1306_CHAR_WIDTH = 6;
static const int SSD1306_TEXT_SIZE = 2;
static const int SSD1306_VERT_OFFSET = 8;
static const uint8_t CO2_DENSITY_REQ[9] = {0xFF, 0x01, 0x86, 0x00, 0x00, 0x00, 0x00, 0x00, 0x79};
static const uint8_t SSD1306_128_32_I2C_ADDRESS = 0x3C;

Adafruit_SSD1306 display = Adafruit_SSD1306(128, 32, &Wire);
Notecard notecard;
NotecardEnvVarManager *env_var_mgr;

// CO2 Threasold Limit Values
// https://www.fsis.usda.gov/sites/default/files/media_file/2020-08/Carbon-Dioxide.pdf
static const uint16_t CO2_SAFE         =     0;
static const uint16_t CO2_OSHA_LIMIT   =  5000;
static const uint16_t CO2_DROWSINESS   = 10000;
static const uint16_t CO2_RESPIRATORY  = 15000;
static const uint16_t CO2_ACGIH_MAX    = 30000;
static const uint16_t CO2_IDLH_EVAC    = 40000;
static const uint16_t CO2_LETHAL_LEVEL = 50000;

static const uint32_t PIN_BUTTON = D13;
static const uint32_t PIN_BUZZER = D12;

static volatile bool co2_ready = false;
static volatile bool send_immediately = false;

static uint16_t co2_threashold = CO2_OSHA_LIMIT;
static uint32_t last_report_ms = 0;
static char *wifi_ssid = nullptr;
static char *wifi_password = nullptr;

struct SensorReadings {
    double temperature;
    double voltage;
    uint16_t co2_ppm;
};

void ISR_buttonPress (void) {
    co2_ready = true;
    send_immediately = true;
    ::digitalWrite(LED_BUILTIN, HIGH);
}

void alarm (void) {
    ::tone(PIN_BUZZER, 1480, 500);
    ::delay(500);
    ::noTone(PIN_BUZZER);
    ::tone(PIN_BUZZER, 330, 250);
    ::delay(250);
    ::noTone(PIN_BUZZER);
    ::tone(PIN_BUZZER, 1480, 500);
    ::delay(500);
    ::noTone(PIN_BUZZER);
    ::tone(PIN_BUZZER, 330, 250);
    ::delay(250);
    ::noTone(PIN_BUZZER);
    ::tone(PIN_BUZZER, 1480, 500);
    ::delay(500);
    ::noTone(PIN_BUZZER);
    ::tone(PIN_BUZZER, 330, 250);
    ::delay(250);
    ::noTone(PIN_BUZZER);
}

const char * co2Message (uint16_t co2_ppm_) {
    const char * message;

    if (co2_ppm_ > CO2_LETHAL_LEVEL) {
        message = "LETHAL LEVELS OF CO2 DETECTED!!!";
    } else if (co2_ppm_ > CO2_IDLH_EVAC) {
        message = "CO2 LEVEL IMMEDIATELY DANGEROUS TO LIFE AND HEALTH!";
    } else if (co2_ppm_ > CO2_ACGIH_MAX) {
        message = "CO2 level exceeds ACGIH short-term exposure limit.";
    } else if (co2_ppm_ > CO2_RESPIRATORY) {
        message = "CO2 level likely to cause repiratory distress.";
    } else if (co2_ppm_ > CO2_DROWSINESS) {
        message = "CO2 level may cause drowsiness.";
    } else if (co2_ppm_ > CO2_OSHA_LIMIT) {
        message = "CO2 level exceeds OSHA/ACGIH 8-hour exposure limit.";
    } else {
        message = "CO2 level is within safe limits.";
    }

    return message;
}

int configureNotecard (void) {
    if (J *req = notecard.newRequest("hub.set")) {
        if (myProductID[0])
        {
            JAddStringToObject(req, "product", myProductID);
        }
        JAddIntToObject(req, "inbound", 5);
        JAddStringToObject(req, "mode", "continuous");
        JAddStringToObject(req, "sn", "Smart CO2 Sensor");
        notecard.sendRequestWithRetry(req, 5); // 5 seconds
    }

    return 0;
}

void envVarManagerCb(const char *var, const char *val, void *user_ctx) {
    uint16_t *co2_threashold = (uint16_t *)user_ctx;
    bool update_wifi = false;

    if (strcmp(var, "alarm_threshold") == 0) {
        char *end_ptr;
        uint16_t alarm_threshold = ::strtol(val, &end_ptr, 10);
        if (end_ptr != val) {
            notecard.logDebugf("[APP] Setting alarm threshold to %uppm.\n", alarm_threshold);
            *co2_threashold = alarm_threshold;
        }
    } else if (strcmp(var, "wifi_ssid") == 0) {
        if (!wifi_ssid || strncmp(wifi_ssid, val, 255) != 0) {
            delete(wifi_ssid);
            if (256 < strnlen(val, 255)) {
                notecard.logDebugf("[ERROR] Wi-Fi SSID is too long! (max: 255)\n");
            } else {
                const size_t ssid_len = (strnlen(val, 255) + 1);
                wifi_ssid = new char[256]; // Allocate as reusable chunk to preserve heap
                strlcpy(wifi_ssid, val, ssid_len);
                notecard.logDebugf("[APP] Updating Wi-Fi SSID to %s.\n", val);
                update_wifi = true;
            }
        }
    } else if (strcmp(var, "wifi_password") == 0) {
        if (!wifi_password || strncmp(wifi_password, val, 255) != 0) {
            delete(wifi_password);
            if (256 < strnlen(val, 255)) {
                notecard.logDebugf("[ERROR] Wi-Fi password is too long! (max: 255)\n");
            } else {
                const size_t password_len = (strnlen(val, 255) + 1);
                wifi_password = new char[256]; // Allocate as reusable chunk to preserve heap
                strlcpy(wifi_password, val, password_len);
                notecard.logDebug("[APP] Updating Wi-Fi password to ");
                for (size_t i = 0; i < password_len; ++i) {
                    notecard.logDebug("*");
                }
                notecard.logDebug(".\n");
                update_wifi = true;
            }
        }
    } else {
        notecard.logDebugf("[APP] Ignoring unknown environment variable: %s\n", var);
    }

    // Update Wi-Fi credentials (if necessary)
    if (update_wifi) {
        if (J *req = notecard.newRequest("card.wifi")) {
            JAddItemReferenceToObject(req, "ssid", JCreateString(wifi_ssid));
            JAddItemReferenceToObject(req, "password", JCreateString(wifi_password));
            notecard.sendRequest(req);
        }
    }
}

void interruptibleDelay (uint32_t delay_ms_) {
    const uint32_t start_ms = ::millis();
    while ((::millis() - start_ms) < delay_ms_) {
        if (send_immediately) {
            break;
        }
        ::delay(10);
    }
}

void printCO2Concentration (uint16_t co2_ppm_) {
    char co2_ppm_string[10];
    sprintf(co2_ppm_string, "%6uppm", co2_ppm_);
    display.clearDisplay();
    display.setCursor(0, 0);
    display.setTextSize(1);
    display.println("CO2 Concentration:\n");
    display.setTextSize(SSD1306_TEXT_SIZE);
    display.println(co2_ppm_string);
    display.display();
}

uint16_t sampleCo2InPpm (void) {
    uint16_t result = 0;

    if (!co2_ready) {
        notecard.logDebug("[APP] CO2 sensor not ready (preheating).\n");
    } else {
        // Send the request to the sensor
        txRxPinsSerial.write(CO2_DENSITY_REQ, sizeof(CO2_DENSITY_REQ));

        // Allow the sensor to perform the measurement
        ::delay(500);

        // Read the response from the sensor (9 bytes)
        const uint32_t read_timeout_ms = 100;
        const uint32_t start_ms = ::millis();
        uint8_t checksum = 0;
        notecard.logDebug("[APP] CO2 sensor response: { ");
        for (int byte_count = 0; (byte_count < 9) && ((::millis() - start_ms) < read_timeout_ms);)
        {
            if (txRxPinsSerial.available())
            {
                int byte = txRxPinsSerial.read();
                notecard.logDebugf("0x%02X ", byte);

                // Synchronize
                if (byte_count == 0 && byte != 0xFF) {
                    notecard.logDebugf("\n[ERROR] Unexpected byte from CO2 sensor!\n");
                    notecard.logDebugf("[WARN] Attempting to reestablish synchronization...\n");
                    notecard.logDebug("[APP] CO2 sensor response: { ");
                    continue;
                }

                // Capture PPM hi-byte
                if (byte_count == 2) {
                    result = ((byte & 0xFF) << 8);
                }

                // Capture PPM lo-byte
                if (byte_count == 3) {
                    result |= (byte & 0xFF);
                }

                // Calculate/validate the checksum
                if (byte_count++ < 8) {
                    checksum += byte;
                } else {
                    notecard.logDebug("}\n");
                    // checksum = (~checksum + 1);  // Implementation does not match the datasheet
                    checksum = ~checksum;
                    if (checksum != byte) {
                        notecard.logDebugf("[APP] checksum mismatch! (calculated: %d != provided: %d)\n", checksum, byte);
                        result = 0;
                    } else {
                        notecard.logDebugf("[APP] CO2 sampled at %dppm.\n", result);
                    }
                }
            }
        }
    }

    return result;
}

int sampleSensors (SensorReadings &readings) {
    // Sample the temperature of the Notecard
    if (J *rsp = notecard.requestAndResponse(notecard.newRequest("card.temp")))
    {
        readings.temperature = JGetNumber(rsp, "value");
        notecard.deleteResponse(rsp);
    }

    // Sample the voltage that is detected on the `V+` pin.
    // Validate the integrity of the power supply.
    if (J *rsp = notecard.requestAndResponse(notecard.newRequest("card.voltage")))
    {
        readings.voltage = JGetNumber(rsp, "value");
        notecard.deleteResponse(rsp);
    }

    // Sample the Infrared CO2 Sensor
    readings.co2_ppm = sampleCo2InPpm();

    // Log results
    notecard.logDebug("\n");
    notecard.logDebug("[APP] Sensor Readings:\n");
    notecard.logDebugf("[APP] %10s: %.2lfC\n", "Temp", readings.temperature);
    notecard.logDebugf("[APP] %10s: %.2lfV\n", "Voltage", readings.voltage);
    notecard.logDebugf("[APP] %10s: %uppm\n", "CO2", readings.co2_ppm);
    notecard.logDebug("\n");

    return 0;
}

void scrollMessage (const char *message_) {
    int  first_char_pixel, left_most_pixel;

    display.clearDisplay();
    display.setTextSize(SSD1306_TEXT_SIZE);

    // Begin scrolling in from off-screen (right)
    first_char_pixel = display.width();

    // Calculate the left-most pixel (starting position) of the cursor
    left_most_pixel = -1 * SSD1306_CHAR_WIDTH * SSD1306_TEXT_SIZE * strlen(message_);

    for (;;) {
        // Tell the OLED display where to begin printing the message
        display.setCursor(first_char_pixel, SSD1306_VERT_OFFSET);

        // Print the message at the cursor position
        display.println(message_);

        // Render the display contents
        display.display();

        // Advance the cursor into the negative space,
        // until message is no longer visible.
        if (--first_char_pixel < left_most_pixel) {
            break;
        }
    }
}

int sendReadingsToNotehub (SensorReadings &readings_) {
    J *req = notecard.newRequest("note.add");
    if (req != NULL)
    {
        JAddBoolToObject(req, "sync", true);
        J *body = JAddObjectToObject(req, "body");
        if (body != NULL)
        {
            JAddNumberToObject(body, "temp", readings_.temperature);
            JAddNumberToObject(body, "voltage", readings_.voltage);
            JAddNumberToObject(body, "co2", readings_.co2_ppm);
        }
        if (notecard.sendRequest(req)) {
            const uint32_t now_ms = ::millis();
            last_report_ms = now_ms;
        }
    }

    return 0;
}

// One-time Arduino initialization
void setup()
{
    // Initialize the built-in LED pin as an output
    ::pinMode(LED_BUILTIN, OUTPUT);

    // Initialize debug output
#ifndef RELEASE
    stlinkSerial.begin(115200);
    const size_t usb_timeout_ms = 3000;
    for (const size_t start_ms = millis(); !stlinkSerial && (millis() - start_ms) < usb_timeout_ms;)
        ;
    notecard.setDebugOutputStream(stlinkSerial);
#endif

    // Initialize the physical I/O channel to the Notecard
    notecard.begin();

    // Configure the Notecard
    configureNotecard();

    // Configure the Notecard Environment Variable Manager
    env_var_mgr = NotecardEnvVarManager_alloc();
    NotecardEnvVarManager_setEnvVarCb(env_var_mgr, envVarManagerCb, &co2_threashold);

    // Attach the ISR to the button press
    ::pinMode(PIN_BUTTON, INPUT_PULLUP);
    ::attachInterrupt(digitalPinToInterrupt(PIN_BUTTON), ISR_buttonPress, FALLING);

    // Initialize the UART for the CO2 Sensor
    txRxPinsSerial.begin(9600);

    // Initialize OLED display
    display.begin(SSD1306_SWITCHCAPVCC, SSD1306_128_32_I2C_ADDRESS);
    display.clearDisplay();
    display.setTextSize(SSD1306_TEXT_SIZE);
    display.setTextWrap(false);

    // By default, the background text color is transparent.
    // Setting the text background to BLACK allows the text to
    // scroll, without requiring the entire screen to be cleared.
    display.setTextColor(SSD1306_WHITE, SSD1306_BLACK);
}

// In the Arduino main loop which is called repeatedly
void loop()
{
    // Check CO2 Sensor preheating interval
    if (::millis() > THREE_MINUTES_MS) {
        // Audibly indicate that the CO2 Sensor is ready
        if (!co2_ready) {
            // Do not play tone on roll-over
            ::tone(PIN_BUZZER, 1047, 250);
            ::delay(250);
            ::noTone(PIN_BUZZER);
        }
        co2_ready = true;
    }

    if (co2_ready) {
        // Sample the sensors
        SensorReadings readings{0.0f,0.0f,0};
        sampleSensors(readings);

        // Sound alarm
        if (readings.co2_ppm > co2_threashold || readings.co2_ppm >= CO2_ACGIH_MAX) {
            alarm();
        }

        // Periodically send readings to Notehub
        if (send_immediately || ((::millis() - last_report_ms) > FIFTEEN_MINUTES_MS) || !last_report_ms)
        {
            send_immediately = false;
            sendReadingsToNotehub(readings);
            NotecardEnvVarManager_fetch(env_var_mgr, ENV_VAR_LIST, (sizeof(ENV_VAR_LIST) / sizeof(ENV_VAR_LIST[0])));
            scrollMessage(co2Message(readings.co2_ppm));
            ::digitalWrite(LED_BUILTIN, LOW);
        }

        // Display the CO2 reading
        printCO2Concentration(readings.co2_ppm);
    } else {
        scrollMessage("Preheating the CO2 sensor...");
    }

    interruptibleDelay(1000);
}
