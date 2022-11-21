#include <Arduino.h>
#include <Wire.h>
#include <Notecard.h>
#include <base64_decode.h>
#include "Adafruit_ThinkInk.h"
#include "Adafruit_ImageReader.h"
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
// #define DEBUG_NOTECARD

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
void displayContent(void);
int decodeImage(unsigned char *decoded);

void setup() {
  serialDebugOut.begin(115200);

  //while (!serialDebugOut) { delay(10); }
  delay(2500);
  serialDebugOut.println("Low-Power Digital Signage Demo");
  serialDebugOut.println("==============================");

  Wire.begin();
#ifdef DEBUG_NOTECARD
  notecard.setDebugOutputStream(serialDebugOut);
#endif
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
  if (state.imageString != NULL) {
    // Show image
    unsigned char * imageBytes PROGMEM;
    int imageBytesSize = decodeImage(imageBytes);

    if (imageBytesSize > 1) {
      serialDebugOut.print("First element:\t");
      serialDebugOut.println(imageBytes[885]);

      for (size_t i = 0; i < imageBytesSize; i++)
      {
        if (imageBytes[i] != blues_logo[i]) {
          serialDebugOut.println(imageBytes[i], HEX);
          serialDebugOut.println(blues_logo[i], HEX);
          serialDebugOut.println();
        }
      }

      if (strcmp(reinterpret_cast<const char*>(imageBytes), reinterpret_cast<const char*>(blues_logo)) != 0) {
        serialDebugOut.println("Strings are not the same!");
      } else {
        serialDebugOut.println("Strings are the same!");
      }

      display.clearBuffer();
      display.drawBitmap(0, 0, imageBytes, 250, 122, EPD_RED, EPD_WHITE);
      display.display();
    }
  } else if (state.text != NULL) {
    display.clearBuffer();

    uint8_t textSize = 4;
    uint8_t cursorY = 30;

    char* text = const_cast<char*>(state.text.c_str());
    size_t textLength = strlen(text);

    if (textLength > 16 && textLength <= 32) {
      textSize = 3;
      cursorY = 20;
    } else if (textLength > 32 && textLength <= 64) {
      textSize = 2;
      cursorY = 10;
    } else if (textLength > 64) {
      textSize = 1;
      cursorY = 5;
    }

    display.setCursor(0, cursorY);
    display.setTextSize(textSize);
    display.setTextColor(EPD_RED);

    char* segment = strtok(text, " ");
    while (segment != NULL) {
      size_t segmentLen = strlen(segment);
      uint8_t padSpaces = floor((44 / textSize - segmentLen) / 2.00);

      for (size_t i = 0; i < padSpaces; i++)
      {
        display.print(" ");
      }
      display.println(segment);

      segment = strtok(NULL, " ");
    }

    display.display();
  }
}

void fetchEnvironmentVariables(applicationState vars) {
  J *req = notecard.newRequest("env.get");

  J *names = JAddArrayToObject(req, "names");
  JAddItemToArray(names, JCreateString("text"));
  JAddItemToArray(names, JCreateString("image_string"));

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

          char *imageString = JGetString(body, "image_string");
          if (strcmp(vars.imageString.c_str(), imageString) != 0)
          {
            vars.imageString = String(imageString);
            vars.variablesUpdated = true;
          }

          serialDebugOut.print("\nText: ");
          serialDebugOut.println(vars.text);
          // serialDebugOut.print("Image String: ");
          // serialDebugOut.println(vars.imageString);

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

int decodeImage(unsigned char *decoded) {
  char *imageString = const_cast<char*>(state.imageString.c_str());
  int inputStringLength = strlen(imageString);
  int decodedLength = b64_decoded_size(imageString)+1;

  serialDebugOut.print("Input string length:\t");
  serialDebugOut.println(inputStringLength);

  b64_decode(imageString, decoded, decodedLength);

  serialDebugOut.print("Decoded string length:\t");
  serialDebugOut.println(decodedLength);

  return decodedLength;
}
