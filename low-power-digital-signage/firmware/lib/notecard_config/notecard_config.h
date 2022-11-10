#include "metadata.h"
#include <Notecard.h>

extern Notecard notecard;

#define DEMO_MODE

// This is the unique Product Identifier for your device
#ifndef PRODUCT_UID
#define PRODUCT_UID "" // "com.my-company.my-name:my-project"
#pragma message "PRODUCT_UID is not defined in this example. Please ensure your Notecard has a product identifier set before running this example or define it in code here. More details at https://dev.blues.io/tools-and-sdks/samples/product-uid"
#endif

void configureNotecard() {
  J *req = notecard.newRequest("hub.set");
  if (req != NULL) {
    JAddStringToObject(req, "product", PRODUCT_UID);
  #ifdef DEMO_MODE
    JAddStringToObject(req, "mode", "continuous");
    JAddBoolToObject(req, "sync", true);
  #else
    // Tune for a long-running app that doesn't need to make
    // immediate display updates.
    AddStringToObject(req, "mode", "periodic");
    JAddBoolToObject(req, "align", true);
    JAddNumberToObject(req, "inbound", 60); // Once an hour
    // Voltage-variable outbound settings based on battery life
    JAddStringToObject(req, "voutbound", "usb:60;high:1440;normal:1440;low:10080;0");
  #endif
    notecard.sendRequest(req);
  }

  // Notify Notehub of the current firmware version
  req = notecard.newRequest("dfu.status");
  if (req != NULL) {
    JAddStringToObject(req, "version", firmwareVersion());
    notecard.sendRequest(req);
  }

  // Enable Outboard DFU
  req = notecard.newRequest("card.dfu");
  if (req != NULL) {
    JAddStringToObject(req, "name", "stm32");
    JAddBoolToObject(req, "on", true);
    notecard.sendRequest(req);
  }

  // Configure LiPo Battery Mode
  req = notecard.newRequest("card.voltage");
  if (req != NULL) {
    JAddStringToObject(req, "mode", "lipo");
    notecard.sendRequest(req);
  }

  // Turn off the accelerometer to preserve battery since we don't need
  // it for this app.
  req = notecard.newRequest("card.motion.mode");
  if (req != NULL) {
    JAddBoolToObject(req, "stop", true);
    notecard.sendRequest(req);
  }
}