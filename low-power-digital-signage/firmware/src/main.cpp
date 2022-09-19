#include <Arduino.h>
#include <Wire.h>
#include <Notecard.h>
#include <Adafruit_ImageReader.h>
#include "Adafruit_ThinkInk.h"
#include "metadata.h"

#define EPD_DC      10
#define EPD_CS      9
#define EPD_BUSY    -1
#define SRAM_CS     6
#define EPD_RESET   -1

#define serialDebugOut Serial
// Uncomment to view Note requests from the Host
#define DEBUG_NOTECARD

#define PRODUCT_UID "com.blues.nf4"
#define ENV_POLL_SECS	1

Notecard notecard;

// 2.13" Tricolor EPD with IL0373 chipset
ThinkInk_213_Tricolor_RW display(EPD_DC, EPD_RESET, EPD_CS, SRAM_CS, EPD_BUSY);

// Variables for Env Var polling
static unsigned long nextPollMs = 0;
static uint32_t lastModifiedTime = 0;

struct applicationState {
  int lastUpdate;
  String text;
  String imageBytes;
};

applicationState state;

void fetchEnvironmentVariables(applicationState);
bool pollEnvVars(void);
void drawText(const char *text, uint16_t color);
void displayContent(void);

void setup() {
  Serial.begin(115200);
#ifdef DEBUG_NOTECARD
  notecard.setDebugOutputStream(serialDebugOut);
#endif
  while (!Serial) { delay(10); }
  serialDebugOut.println("Low-Power Digital Signage Demo");
  serialDebugOut.println("==============================");

  Wire.begin();
  notecard.begin();

  display.begin(THINKINK_TRICOLOR);

  J *req = notecard.newRequest("hub.set");
  if (req != NULL) {
    JAddStringToObject(req, "product", PRODUCT_UID);
    JAddStringToObject(req, "mode", "continuous");
    JAddBoolToObject(req, "sync", true);
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

  fetchEnvironmentVariables(state);
  displayContent();
}

void loop() {
  if (pollEnvVars()) {
    fetchEnvironmentVariables(state);

    serialDebugOut.println("Environment Variable Updates Received");

    displayContent();
  }
}

void displayContent() {
  if (state.text != NULL) {
    display.clearBuffer();
    display.setTextSize(4);
    drawText(state.text.c_str(), EPD_RED);
    display.display();
  } else if (state.imageBytes != NULL) {
    // Show image
    display.clearBuffer();
    display.display();
  }
}

void fetchEnvironmentVariables(applicationState vars) {
  J *req = NoteNewRequest("env.get");

  J *names = JAddArrayToObject(req, "names");
  JAddItemToArray(names, JCreateString("text"));
  JAddItemToArray(names, JCreateString("image_bytes"));

  J *rsp = NoteRequestResponse(req);
  if (rsp != NULL) {
      if (notecard.responseError(rsp)) {
          notecard.deleteResponse(rsp);
          return;
      }

      // Set the lastModifiedTime based on the return
      lastModifiedTime = JGetNumber(rsp, "time");

      // Get the note's body
      J *body = JGetObject(rsp, "body");
      if (body != NULL) {
          vars.text = JGetString(body, "text");
          vars.imageBytes = JGetString(body, "image_bytes");

          serialDebugOut.print("\nText: ");
          serialDebugOut.println(vars.text);
          serialDebugOut.print("Image Bytes: ");
          serialDebugOut.println(vars.imageBytes);

          state = vars;
      }

  }
  notecard.deleteResponse(rsp);
}

bool pollEnvVars() {
  if (millis() < nextPollMs) {
    return false;
  }

  nextPollMs = millis() + (ENV_POLL_SECS * 1000);

  J *rsp = notecard.requestAndResponse(notecard.newRequest("env.modified"));

	if (rsp == NULL) {
		return false;
	}

	uint32_t modifiedTime = JGetInt(rsp, "time");
  notecard.deleteResponse(rsp);
	if (lastModifiedTime == modifiedTime) {
		return false;
	}

	lastModifiedTime = modifiedTime;

  return true;
}

void drawText(const char *text, uint16_t color) {
  display.setCursor(10, 10);
  display.setTextColor(color);
  display.setTextWrap(true);
  display.print(text);
}
