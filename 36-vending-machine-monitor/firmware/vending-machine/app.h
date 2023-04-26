// Copyright 2023 Blues Inc.  All rights reserved.
// Use of this source code is governed by licenses granted by the
// copyright holder including that found in the LICENSE file.

#pragma once

#include <Arduino.h>
#include <Wire.h>
#include <Notecard.h>
#include "monitor.h"
#include "debug.h"

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
#define APP_NAME    "nf36"
#endif

// Notecard definition
#ifdef APP_MAIN
Notecard notecard;
#else
extern Notecard notecard;
#endif

class AppReporter : public ReportEventsConfig {

    virtual const char* monitorNotefile() {
        return "soda.qo";
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
