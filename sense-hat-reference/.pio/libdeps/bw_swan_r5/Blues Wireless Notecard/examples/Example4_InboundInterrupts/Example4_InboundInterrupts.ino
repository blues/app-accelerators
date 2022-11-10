//
// Copyright 2019 Blues Inc.  All rights reserved.
// Use of this source code is governed by licenses granted by the
// copyright holder including that found in the LICENSE file.
//
// This example shows the simplest possible method demonstrating how a device might poll a
// notefile used as an "inbound queue", using it to receive messages sent to the device from
// the service.  The message gets into the service by use of the Notehub's HTTP/HTTPS inbound
// request capability.
//
// In order to use this example,
// 1. Wire the Notecard's ATTN pin output to a GPIO input on your MCU, and change the definition
//    below to reflect the proper GPIO pin.  On the Notecard Development Kit with an nRF53840 Feather,
//    this would mean placing a jumper wire between the pin marked "5" and the pin marked "ATTN".
// 2. Get the device up and running the code below, successfully connecting to the servie
// 3. Use the "Devices" view on notehub.io to determine the DeviceUID of the device, which
//    is a unique string that looks like "dev:000000000000000"
// 4. Use the "Settings / Project" view on notehub.io to determine the App UID of your project,
//    a unique string that looks like "app:00000000-0000-0000-0000-000000000000"
// 5. At the command line of your PC, send an HTTP message to the service such as:
//    curl -L 'http://api.notefile.net/req?project="app:00000000-0000-0000-0000-000000000000"&device="dev:000000000000000"' -d '{"req":"note.add","file":"my-inbound.qi","body":{"my-request-type":"my-request"}}'
//

#include <Notecard.h>
#include <Wire.h>

// GPIO pin definitions
#define ATTN_INPUT_PIN  5     // Any digital GPIO pin on your board

// Parameters for this example
#define INBOUND_QUEUE_NOTEFILE    "my-inbound.qi"
#define INBOUND_QUEUE_COMMAND_FIELD "my-request-type"

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
#define myLiveDemo  true

// Set to true whenever ATTN interrupt occurs
static bool attnInterruptOccurred;

// Forwards
void attnISR(void);
void attnArm();

// One-time Arduino initialization
void setup()
{

    // Set up for debug output.
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

    // Configure the productUID, and instruct the Notecard to stay connected to the service
    J *req = notecard.newRequest("hub.set");
    if (myProductID[0]) {
        JAddStringToObject(req, "product", myProductID);
    }
#if myLiveDemo
    JAddStringToObject(req, "mode", "continuous");
    JAddBoolToObject(req, "sync", true);
#else
    JAddStringToObject(req, "mode", "periodic");
    JAddNumberToObject(req, "outbound", 60);
#endif
    notecard.sendRequest(req);

    // Disarm ATTN To clear any previous state before rearming
    req = notecard.newRequest("card.attn");
    JAddStringToObject(req, "mode", "disarm,-files");
    notecard.sendRequest(req);

    // Configure ATTN to wait for a specific list of files
    req = notecard.newRequest("card.attn");
    const char *filesToWatch[] = {INBOUND_QUEUE_NOTEFILE};
    int numFilesToWatch = sizeof(filesToWatch) / sizeof(const char *);
    J *filesArray = JCreateStringArray(filesToWatch, numFilesToWatch);
    JAddItemToObject(req, "files", filesArray);
    JAddStringToObject(req, "mode", "files");
    notecard.sendRequest(req);

    // Attach an interrupt pin
    pinMode(ATTN_INPUT_PIN, INPUT);
    attachInterrupt(digitalPinToInterrupt(ATTN_INPUT_PIN), attnISR, RISING);

    // Arm the interrupt, so that we are notified whenever ATTN rises
    attnArm();

}

// In the Arduino main loop which is called repeatedly, add outbound data every 15 seconds
void loop()
{

    // If the interrupt hasn't occurred, exit
    if (!attnInterruptOccurred) {
        return;
    }

    // Re-arm the interrupt
    attnArm();

    // Process all pending inbound requests
    while (true) {

        // Get the next available note from our inbound queue notefile, deleting it
        J *req = notecard.newRequest("note.get");
        JAddStringToObject(req, "file", INBOUND_QUEUE_NOTEFILE);
        JAddBoolToObject(req, "delete", true);
        J *rsp = notecard.requestAndResponse(req);
        if (rsp != NULL) {

            // If an error is returned, this means that no response is pending.  Note
            // that it's expected that this might return either a "note does not exist"
            // error if there are no pending inbound notes, or a "file does not exist" error
            // if the inbound queue hasn't yet been created on the service.
            if (notecard.responseError(rsp)) {
                notecard.deleteResponse(rsp);
                break;
            }

            // Get the note's body
            J *body = JGetObject(rsp, "body");
            if (body != NULL) {

                // Simulate Processing the response here
                char *myCommandType = JGetString(body, INBOUND_QUEUE_COMMAND_FIELD);
                notecard.logDebugf("INBOUND REQUEST: %s\n\n", myCommandType);
            }

        }
        notecard.deleteResponse(rsp);
    }

}

// Interrupt Service Routine for ATTN_INPUT_PIN transitions rising from LOW to HIGH
void attnISR()
{
    attnInterruptOccurred = true;
}

// Re-arm the interrupt
void attnArm()
{

    // Make sure that we pick up the next RISING edge of the interrupt
    attnInterruptOccurred = false;

    // Set the ATTN pin low, and wait for the earlier of file modification or a timeout
    J *req = notecard.newRequest("card.attn");
    JAddStringToObject(req, "mode", "reset");
    JAddNumberToObject(req, "seconds", 120);
    notecard.sendRequest(req);

}
