#include <Arduino.h>
#include <Wire.h>
#include <Notecard.h>
#include "Adafruit_ImageReader.h"
#include "Adafruit_ThinkInk.h"
#include "display_manager.h"
#include "notecard_config.h"
#include "blues_bmp.h"

#define EPD_DC      10
#define EPD_CS      9
#define EPD_BUSY    -1
#define SRAM_CS     6
#define EPD_RESET   -1

#define serialDebugOut Serial
// Uncomment to view Note requests from the Host
#define DEBUG_NOTECARD

#define ENV_POLL_SECS	1

Notecard notecard;

// 2.13" Tricolor EPD with IL0373 chipset
ThinkInk_213_Tricolor_RW display(EPD_DC, EPD_RESET, EPD_CS, SRAM_CS, EPD_BUSY);

// Variables for Env Var polling
static unsigned long nextPollMs = 0;
static uint32_t lastModifiedTime = 0;

applicationState state;

// Forward declarations
void fetchEnvironmentVariables(applicationState);
bool pollEnvVars(void);
void drawText(const char *text, uint16_t color);
void displayContent(void);

void setup() {
  serialDebugOut.begin(115200);
#ifdef DEBUG_NOTECARD
  notecard.setDebugOutputStream(serialDebugOut);
#endif
  while (!serialDebugOut) { delay(10); }
  serialDebugOut.println("Low-Power Digital Signage Demo");
  serialDebugOut.println("==============================");

  Wire.begin();
  notecard.begin();

  display.begin(THINKINK_TRICOLOR);

  configureNotecard();

  // Check Environment Variables
  fetchEnvironmentVariables(state);

  displayContent();
}

void loop() {
  if (pollEnvVars()) {
    fetchEnvironmentVariables(state);

    if (state.variablesUpdated)
    {
      serialDebugOut.println("Environment Variable Updates Received");

      displayContent();

      J *req = notecard.newRequest("note.add");
      if (req != NULL)
      {
        JAddStringToObject(req, "file", "notify.qo");
        JAddBoolToObject(req, "sync", true);
        J *body = JCreateObject();
        if (body != NULL)
        {
          JAddBoolToObject(body, "updated", true);
          JAddStringToObject(body, "app", "nf4");

          JAddItemToObject(req, "body", body);
          notecard.sendRequest(req);
        }
      }
      state.variablesUpdated = false;
    }
  }
}

void displayContent() {
  if (state.imageBytes != NULL) {
    // Show image
    display.clearBuffer();
    display.drawBitmap(0, 0, blues_logo, 250, 122, EPD_RED, EPD_WHITE);
    display.display();
  } else if (state.text != NULL) {
    display.clearBuffer();
    display.setTextSize(4);
    drawText(state.text.c_str(), EPD_RED);
    display.display();
  }
}

void fetchEnvironmentVariables(applicationState vars) {
  J *req = notecard.newRequest("env.get");

  J *names = JAddArrayToObject(req, "names");
  JAddItemToArray(names, JCreateString("text"));
  JAddItemToArray(names, JCreateString("image_bytes"));

  J *rsp = notecard.requestAndResponse(req);
  if (rsp != NULL) {
      if (notecard.responseError(rsp)) {
          notecard.deleteResponse(rsp);
          return;
      }

      // Get the note's body
      J *body = JGetObject(rsp, "body");
      if (body != NULL) {
          char *text = JGetString(body, "text");
          if (strcmp(vars.text.c_str(), text) != 0)
          {
            vars.text = String(text);
            vars.variablesUpdated = true;
          }

          char *imageBytes = JGetString(body, "image_bytes");
          if (strcmp(vars.imageBytes.c_str(), imageBytes) != 0)
          {
            vars.imageBytes = String(imageBytes);
            vars.variablesUpdated = true;
          }

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
