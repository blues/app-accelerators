/*!
 *
 * Written by the Blues Inc. team.
 *
 *
 * Copyright (c) 2019 Blues Inc. MIT License. Use of this source code is
 * governed by licenses granted by the copyright holder including that found in
 * the
 * <a href="https://github.com/blues/app-accelerators/blob/main/LICENSE">LICENSE</a>
 * file.
 *
 */

#include <Arduino.h>
#include <Wire.h>
#include <Notecard.h>
#include "Adafruit_ThinkInk.h"
#include <SdFat.h>
#include <Adafruit_ImageReader_EPD.h>
#include "display_manager.h"
#include "notecard_config.h"

#define EPD_DC      10
#define EPD_CS      9
#define EPD_BUSY    -1
#define SRAM_CS     6
#define EPD_RESET   -1
#define SD_CS       5

#define serialDebugOut Serial
// Uncomment to view Note requests from the Host
// #define DEBUG_NOTECARD

#define ENV_POLL_SECS	1

Notecard notecard;

// 2.13" Tricolor EPD with IL0373 chipset
ThinkInk_213_Tricolor_RW display(EPD_DC, EPD_RESET, EPD_CS, SRAM_CS, EPD_BUSY);

bool usingSD = false;
SdFat SD;
Adafruit_ImageReader_EPD reader(SD);

// Variables for Env Var polling
static unsigned long nextPollMs = 0;
static uint32_t lastModifiedTime = 0;
uint16_t displayUpdateInterval = 300;
static unsigned long nextDisplayRotation = 0;

// Struct for holding the current state of the application, while running
applicationState state;

// Forward declarations
void fetchEnvironmentVariables(applicationState);
bool pollEnvVars(void);
void rotateContent(void);
void enumerateSDFiles();
void displayImage(String);
void displayText(String);
void sendNotifyNote(J *);

void setup() {
  serialDebugOut.begin(115200);
  while (!serialDebugOut) { delay(10); }

  serialDebugOut.println("Low-Power Digital Signage Demo");
  serialDebugOut.println("==============================");

  Wire.begin();
#ifdef DEBUG_NOTECARD
  notecard.setDebugOutputStream(serialDebugOut);
#endif
  notecard.begin();

  display.begin(THINKINK_TRICOLOR);

  if(!SD.begin(SD_CS, SD_SCK_MHZ(10))) {
    serialDebugOut.println("SD begin() failed");
  } else {
    usingSD = true;
  }

  // Notecard configuration for low-power operation
  configureNotecard();

  if (usingSD) {
    // Query SD Card for files and send Note to Notecard
    enumerateSDFiles();
  }
}

void loop() {
  // Check for environment variable updates and display new content or images.
  // If state.displayValues is not NULL, rotate to the next image or content
  // in the collection.
  if (pollEnvVars()) {
    fetchEnvironmentVariables(state);

    if (state.variablesUpdated)
    {
      serialDebugOut.println("Environment Variable Updates Received");

      J *body = JCreateObject();
      if (body != NULL)
      {
        JAddBoolToObject(body, "updated", true);
        JAddStringToObject(body, "app", "nf4");

        sendNotifyNote(body);
      }

      rotateContent();

      state.variablesUpdated = false;
    }
  }

  if (state.displayValues) {
    rotateContent();
  }
}

void rotateContent() {
  //Rotate items on the display based on interval
  if (millis() < nextDisplayRotation) {
    return;
  }

  nextDisplayRotation = millis() + (displayUpdateInterval * 1000);

  J *itemsToDisplay = state.displayObject->child;

  if (itemsToDisplay != NULL) {
    int size = JGetArraySize(itemsToDisplay);

    // If there's only one item, don't rotate
    if (size == 1 && state.displayUpdated) {
      return;
    }

    if (state.currentDisplayObjectIndex == size) {
      state.currentDisplayObjectIndex = 0;
    }

    J *item = JGetArrayItem(itemsToDisplay, state.currentDisplayObjectIndex);

    // If the string contains a ".bmp" it's an image to display.
    // Otherwise, assume it's text.
    if (strstr(item->valuestring, ".bmp") != NULL) {
      serialDebugOut.print("Displaying image ");
      serialDebugOut.println(item->valuestring);
      displayImage(item->valuestring);
    } else {
      serialDebugOut.print("Displaying text ");
      serialDebugOut.println(item->valuestring);
      displayText(item->valuestring);
    }

    state.displayUpdated = true;
    state.currentDisplayObjectIndex++;
  }
}

void fetchEnvironmentVariables(applicationState vars) {
  J *req = notecard.newRequest("env.get");

  J *names = JAddArrayToObject(req, "names");
  JAddItemToArray(names, JCreateString("display_interval_sec"));
  JAddItemToArray(names, JCreateString("display_values"));

  J *rsp = notecard.requestAndResponse(req);
  if (rsp != NULL) {
      if (notecard.responseError(rsp)) {
          notecard.deleteResponse(rsp);
          return;
      }

      // Get the note's body
      J *body = JGetObject(rsp, "body");
      if (body != NULL) {
          int displayInterval = atoi(JGetString(body, "display_interval_sec"));
          if (displayInterval != vars.displayIntervalSec)
          {
            vars.displayIntervalSec = displayInterval;
            vars.variablesUpdated = true;

            displayUpdateInterval = vars.displayIntervalSec;
          }

          char *displayValues = JGetString(body, "display_values");
          if (strcmp(vars.displayValues.c_str(), displayValues) != 0) {
            vars.displayValues = String(displayValues);
            vars.variablesUpdated = true;
            vars.displayUpdated = false;

            J *valueList = JCreateObject();
            if (valueList != NULL) {
              J* listItems = JAddArrayToObject(valueList, "list_items");

              if (listItems != NULL) {
                char* value = strtok(displayValues, ";");

                if (value == NULL) {
                  JAddItemToArray(listItems, JCreateString(displayValues));
                } else {
                  while (value != NULL) {
                    JAddItemToArray(listItems, JCreateString(value));

                    value = strtok(NULL, ";");
                  }
                }
              }
              vars.displayObject = valueList;
            }
          }

          /*
          serialDebugOut.print("\nDisplay Interval (sec): ");
          serialDebugOut.println(vars.displayIntervalSec);
          serialDebugOut.print("Display Values: ");
          serialDebugOut.println(vars.displayValues);
          */

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

void sendNotifyNote(J *body) {
  J *req = notecard.newRequest("note.add");
  if (req != NULL)
  {
    JAddStringToObject(req, "file", "notify.qo");
    JAddBoolToObject(req, "sync", true);
    JAddItemToObject(req, "body", body);
    notecard.sendRequest(req);
  }
}

void displayText(String val) {
  display.clearBuffer();

  uint8_t textSize = 4;
  uint8_t cursorY = 30;

  char* text = const_cast<char*>(val.c_str());
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

  // Split the string by word and use the number of words to align and format
  // the text so it looks centered on the display.
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

void displayImage(String fileName) {
  Adafruit_Image_EPD img;
  int32_t width  = 0, height = 0;
  ImageReturnCode ret;
  const char *file = fileName.c_str();

  if (!usingSD) {
    return;
  }

  serialDebugOut.print("Querying image size...");
  ret = reader.bmpDimensions(file, &width, &height);
  reader.printStatus(ret);
  if(ret == IMAGE_SUCCESS) {
    serialDebugOut.print(F("Image dimensions: "));
    serialDebugOut.print(width);
    serialDebugOut.write('x');
    serialDebugOut.println(height);

    if (width != 250 && height != 122)
    {
      serialDebugOut.println("Image dimensions are incorrect.");

      J *body = JCreateObject();
      if (body != NULL)
      {
        JAddStringToObject(body, "message", "image dimensions are incorrect.");
        JAddStringToObject(body, "file", file);
        JAddStringToObject(body, "app", "nf4");

        sendNotifyNote(body);
      }
    }

    serialDebugOut.print(F("Loading image to canvas..."));
    ret = reader.drawBMP((char *)file, display, 0, 0);
    reader.printStatus(ret);
    display.display();
  } else {
    serialDebugOut.println("Image not found.");
    J *body = JCreateObject();
      if (body != NULL)
      {
        JAddStringToObject(body, "message", "image not found.");
        JAddStringToObject(body, "file", file);
        JAddStringToObject(body, "app", "nf4");

        sendNotifyNote(body);
      }
  }
}

void enumerateSDFiles() {
  File dir;
  File file;

  if (!dir.open("/")){
    serialDebugOut.println("dir.open failed");
  }

  J* req = notecard.newRequest("note.add");
  if (req != NULL) {
    JAddBoolToObject(req, "sync", true);
    JAddStringToObject(req, "file", "image_files.qo");
    J *body = JCreateObject();
    if (body != NULL)
    {
      JAddStringToObject(body, "message","images detected on this display. Use the image environment variable to display an image to the screen.");
      J* files = JAddArrayToObject(body, "images");
      if (files != NULL) {
        while (file.openNext(&dir, O_RDONLY)) {
          char fileName[255];
          file.getName(fileName, 255);

          if (fileName[0] != '.' && !file.isDir()) {
            JAddItemToArray(files, JCreateString(fileName));

            // Uncomment to see file names during enumeration
            //file.printName(&Serial);
            //serialDebugOut.println();
          }
          file.close();
        }

        JAddItemToObject(req, "body", body);
        notecard.sendRequest(req);
      }
    }
  }

  if (dir.getError()) {
    serialDebugOut.println("file enumeration error.");
  }
}
