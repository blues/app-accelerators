// Copyright 2022 Blues Inc.  All rights reserved.
// Use of this source code is governed by licenses granted by the
// copyright holder including that found in the LICENSE file.

#include <Arduino.h>
#include <Wire.h>
#include <Notecard.h>
#include "NoteRTOS.h"


#pragma once

#ifndef PRODUCT_UID
#define PRODUCT_UID ""		// "com.my-company.my-name:my-project"
#pragma message "PRODUCT_UID is not defined in this example. Please ensure your Notecard has a product identifier set before running this example or define it in code here. More details at https://bit.ly/product-uid"
#endif

// When set to true, all power monitoring notes are synced immediately to Notehub.
// When set to false, only alerts are synched immediately.
#ifndef SYNC_POWER_MONITORING_NOTES
#define SYNC_POWER_MONITORING_NOTES        (false)
#endif

// Define this when using USB serial, and comment it out when using the ST-Link V3
// (See USING_SWAN.txt for more info.)
// #define debug Serial

// Define the debug output stream device, as well as a method enabling us
// to determine whether or not the Serial device is available for app usage.
#ifdef debug
#define	serialIsAvailable() false
#else
#define	serialIsAvailable() true
#ifdef APP_MAIN
HardwareSerial debug(PG8, PG7);
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

// app.cpp
uint32_t appTasks(uint32_t **taskSchedMs, uint8_t **contextBase, uint32_t *contextSize);
bool appSetup(void);
uint32_t appLoop(void);
bool taskSetup(void *mcp);
uint32_t taskLoop(void *mcp);

#include "app-name.h"