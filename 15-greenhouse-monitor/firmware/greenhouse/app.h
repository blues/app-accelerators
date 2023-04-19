// Copyright 2023 Blues Inc.  All rights reserved.
// Use of this source code is governed by licenses granted by the
// copyright holder including that found in the LICENSE file.

#pragma once

#include <Arduino.h>
#include <Wire.h>
#include <Notecard.h>
#include "monitor.h"

#ifndef PRODUCT_UID
#define PRODUCT_UID ""		// "com.my-company.my-name:my-project"
#pragma message "PRODUCT_UID is not defined in this example. Please ensure your Notecard has a product identifier set before running this example or define it in code here. More details at https://bit.ly/product-uid"
#endif

// When set to true, all monitoring notes are synced immediately to Notehub.
// When set to false, only alerts are synched immediately.
#ifndef SYNC_MONITORING_NOTES
#define SYNC_MONITORING_NOTES        (false)
#endif

#ifndef DEFAULT_REPORT_INTERVAL
#define DEFAULT_REPORT_INTERVAL (1000*60*5)
#endif

#ifndef DEFAULT_POLL_ENVIRONMENT_INTERVAL
#define DEFAULT_POLL_ENVIRONMENT_INTERVAL (1000*60*5)
#endif

#ifndef DEFAULT_POLL_SENSORS_INTERVAL
#define DEFAULT_POLL_SENSORS_INTERVAL (1000*15)
#endif

#ifndef APP_NAME
#define APP_NAME    "nf15"
#endif

// `debug` is the Stream interface to send debugging/logging separate from the app's serial output.
// Comment-in this when using USB serial for debugging, and comment it out when using the ST-Link V3
#define debug Serial

// Define the debug output stream device, as well as a method enabling us
// to determine whether or not the Serial device is available for app usage.
#ifndef debug
#ifdef APP_MAIN
    #if defined(ARDUINO_SWAN_R5)
    HardwareSerial debug(PG8, PG7);
    #else
    #error MCU support for ST-Link VCP serial channel for debug is not available for this board.
    #endif
#else
extern HardwareSerial debug;
#endif
#endif

// Notecard definition
#ifdef APP_MAIN
Notecard notecard;
#else
extern Notecard notecard;
#endif

class AppReporter : public ReportEventsConfig {

    virtual const char* monitorNotefile() {
        return "greenhouse.qo";
    }

    virtual void updateNoteTemplate(J* noteTemplate) {
        JAddStringToObject(noteTemplate, "app", TSTRINGV);
    };

    virtual void updateNote(J* note) {
        JAddStringToObject(note, "app", APP_NAME);
    };

    virtual bool syncMonitoringNotes() {
        return SYNC_MONITORING_NOTES;
    }
};

void NoteUserAgentUpdate(J *ua) {
   JAddStringToObject(ua, "app", APP_NAME);
}
