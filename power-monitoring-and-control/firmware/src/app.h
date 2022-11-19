// Copyright 2022 Blues Inc.  All rights reserved.
// Use of this source code is governed by licenses granted by the
// copyright holder including that found in the LICENSE file.

#include <Arduino.h>
#include <Wire.h>
#include <Notecard.h>
#include "NoteRTOS.h"

#pragma once

// Notehub definitions
#define	NOTEHUB_PRODUCT_UID	"com.blues.mat:test"

// Define this if using USB serial, and comment it out if using the ST-Link V3
// (See USING_SWAN.txt for more info.)
//#define debug Serial

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
