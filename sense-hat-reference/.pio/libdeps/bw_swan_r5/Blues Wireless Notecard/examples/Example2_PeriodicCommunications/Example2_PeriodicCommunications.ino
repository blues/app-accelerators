//
// Copyright 2020 Blues Inc.  All rights reserved.
// Use of this source code is governed by licenses granted by the
// copyright holder including that found in the LICENSE file.
//
// This example does the same function as the "using library" example, but rather than
// keeping the modem turned on constantly this example demonstrates how a developer would
// gather sensor measurements "offline", then perform uploads on a periodic basis.
//

#define ledPin              LED_BUILTIN

// Define the pin number of the pushbutton pin
#define buttonPin           B0
#define buttonPressedState  LOW

#if defined(ARDUINO_FEATHER_F405)
  #define NON_AF_COMPAT_FEATHER
#elif defined(ARDUINO_ARCH_APOLLO3)
//  #undef buttonPin
//  #define buttonPin 10
#elif defined(ARDUINO_NUCLEO_L432KC)
  #define BREADBOARD_REQUIRED
#elif defined(ARDUINO_ARCH_STM32)
  #undef buttonPin
  #define buttonPin USER_BTN
#elif defined(ARDUINO_NRF52840_FEATHER)
  #undef buttonPin
  #define buttonPin 7
#elif defined(ARDUINO_RASPBERRY_PI_PICO)
  #define BREADBOARD_REQUIRED
#elif defined(ARDUINO_ARCH_AVR) || defined(ARDUINO_ARCH_MBED)
  #define BREADBOARD_REQUIRED
#endif

#ifdef NON_AF_COMPAT_FEATHER
  #pragma message("This feather does not support the Notecarrier-AF button, additional hardware is required.")
  #undef buttonPin
  #define buttonPin A5
#endif
#ifdef BREADBOARD_REQUIRED
  #pragma message("The board selected requires additional hardware.")
  #undef buttonPin
  #define buttonPin 3
#endif

// Include the Arduino library for the Notecard
#include <Notecard.h>
#include <Notecarrier.h>
#include <Wire.h>

// Note that both of these definitions are optional; just prefix either line with // to remove it.
//  Remove txRxPinsSerial if you wired your Notecard using I2C SDA/SCL pins instead of serial RX/TX
//  Remove usbSerial if you don't want the Notecard library to output debug information
// #define txRxPinsSerial Serial1
#define usbSerial Serial

// This is the unique Product Identifier for your device
#ifndef PRODUCT_UID
#define PRODUCT_UID ""		// "com.my-company.my-name:my-project"
#pragma message "PRODUCT_UID is not defined in this example. Please ensure your Notecard has a product identifier set before running this example or define it in code here. More details at https://dev.blues.io/tools-and-sdks/samples/product-uid"
#endif

#define myProductID PRODUCT_UID
Notecard notecard;

// Button handling
#define BUTTON_IDLE         0
#define BUTTON_PRESS        1
#define BUTTON_DOUBLEPRESS  2
int buttonPress(void);

// One-time Arduino initialization
void setup()
{

    // Initialize Arduino GPIO pins
    pinMode(ledPin, OUTPUT);
    pinMode(buttonPin, buttonPressedState == LOW ? INPUT_PULLUP : INPUT);

    // During development, set up for debug output from the Notecard.  Note that the initial delay is
    // required by some Arduino cards before debug UART output can be successfully displayed in the
    // Arduino IDE, including the Adafruit Feather nRF52840 Express.
#ifdef usbSerial
    delay(2500);
    usbSerial.begin(115200);
    notecard.setDebugOutputStream(usbSerial);
#endif

    // Initialize the physical I/O channel to the Notecard
#ifdef txRxPinsSerial
    notecard.begin(txRxPinsSerial, 9600);
#else
    Wire.begin();

    notecard.begin();
#endif

    // Service configuration request
    J *req = notecard.newRequest("hub.set");

    // This command (required) causes the data to be delivered to the Project on notehub.io that has claimed
    // this Product ID.  (see above)
    if (myProductID[0]) {
        JAddStringToObject(req, "product", myProductID);
    }
    // This sets the notecard's connectivity mode to periodic, rather than being continuously connected
    JAddStringToObject(req, "mode", "periodic");

    // This parameter establishes how often, in minutes, the Notecard will check for data that is waiting to be
    // uploaded to the service.  Generally this might be something like 60 minutes, or perhaps even
    // 12 hours * 60 min = 720 min.  For the purpose of this demonstration, however, we'll set the
    // period such that it checks for outgoing data at most every 2 minutes.
    JAddNumberToObject(req, "outbound", 2);

    // This parameter establishes how often, in minutes, the Notecard will check for data that is waiting on the
    // service to be downloaded to the Notecard.  Generally this might be 12, 24, or even 48 hours,
    // however for the purpose of this demonstration we will connect to the service to check for
    // incoming data at least once every hour.
    JAddNumberToObject(req, "inbound", 60);

    // Issue the request
    notecard.sendRequest(req);

}

// In the Arduino main loop which is called repeatedly, add outbound data every 15 seconds
void loop()
{
    static unsigned long lastStatusMs = 0;

    // Activity indicator
    digitalWrite(ledPin, HIGH);

    // Wait for a button press, or perform idle activities
    int buttonState = buttonPress();
    switch (buttonState) {

    case BUTTON_IDLE:
        if (notecard.debugSyncStatus(2500, 0)) {
            lastStatusMs = millis();
        }
        if (millis() > lastStatusMs + 10000) {
            lastStatusMs = millis();
            notecard.logDebug("press button to simulate a sensor measurement\n");
        }
        delay(25);
        digitalWrite(ledPin, LOW);
        delay(100);
        return;

    case BUTTON_DOUBLEPRESS:
        notecard.requestAndResponse(notecard.newRequest("hub.sync"));
        digitalWrite(ledPin, LOW);
        return;

    }

    // The button was pressed, so we should begin a transaction
    notecard.logDebug("performing sensor measurement\n");
    lastStatusMs = millis();

    // Count the simulated measurements that we send to the cloud, and stop the demo before long.
    static unsigned eventCounter = 0;
    if (eventCounter++ > 25) {
        return;
    }

    // Read the notecard's current temperature and voltage, as simulated sensor measurements
    double temperature = 0;
    J *rsp = notecard.requestAndResponse(notecard.newRequest("card.temp"));
    if (rsp != NULL) {
        temperature = JGetNumber(rsp, "value");
        notecard.deleteResponse(rsp);
    }
    double voltage = 0;
    rsp = notecard.requestAndResponse(notecard.newRequest("card.voltage"));
    if (rsp != NULL) {
        voltage = JGetNumber(rsp, "value");
        notecard.deleteResponse(rsp);
    }

    // Enqueue the measurement to the Notecard for transmission to the Notehub.  These measurements
    // will be staged in the Notecard's flash memory until it's time to transmit them to the service.
    J *req = notecard.newRequest("note.add");
    if (req != NULL) {
        J *body = JCreateObject();
        if (body != NULL) {
            JAddNumberToObject(body, "temp", temperature);
            JAddNumberToObject(body, "voltage", voltage);
            JAddNumberToObject(body, "count", eventCounter);
            JAddItemToObject(req, "body", body);
        }
        notecard.sendRequest(req);
    }

    // Done with transaction
    digitalWrite(ledPin, LOW);

}

// Button handling
int buttonPress()
{

    // Detect the "press" transition
    static bool buttonBeingDebounced = false;
    int buttonState = digitalRead(buttonPin);
    if (buttonState != buttonPressedState) {
        if (buttonBeingDebounced) {
            buttonBeingDebounced = false;
        }
        return BUTTON_IDLE;
    }
    if (buttonBeingDebounced) {
        return BUTTON_IDLE;
    }

    // Wait to see if this is a double-press
    bool buttonDoublePress = false;
    bool buttonReleased = false;
    unsigned long buttonPressedMs = millis();
    unsigned long ignoreBounceMs = 100;
    unsigned long doublePressMs = 750;
    while (millis() < buttonPressedMs+doublePressMs || digitalRead(buttonPin) == buttonPressedState) {
        if (millis() < buttonPressedMs+ignoreBounceMs) {
            continue;
        }
        if (digitalRead(buttonPin) != buttonPressedState) {
            if (!buttonReleased) {
                buttonReleased = true;
            }
            continue;
        }
        if (buttonReleased) {
            buttonDoublePress = true;
            if (digitalRead(buttonPin) != buttonPressedState) {
                break;
            }
        }
    }

    return (buttonDoublePress ? BUTTON_DOUBLEPRESS : BUTTON_PRESS);

}