/*
 * sensor_alert.h — Sensor reads and outbound Note helpers for the
 *                   Ambient Home Well-Being & Activity Hub.
 *
 * This file is specific to this project and is NOT a general-purpose library.
 */
#pragma once

#include <Adafruit_SHT31.h>
#include "notecard_helpers.h"   // transitively includes app_state.h + Notecard.h

// SHT31 object — defined in activity_hub.ino
extern Adafruit_SHT31 sht31;

// ---------------------------------------------------------------------------
// Sensor reads
// ---------------------------------------------------------------------------
bool readHumidity(float &outTempC, float &outHumidPct);
bool readBedMotion();

// ---------------------------------------------------------------------------
// Outbound Note helpers
//
// Both functions return true only when note.add is accepted by the Notecard.
// Callers MUST check the return value:
//   sendAlert  — update alert latches only on true; leave armed on false so
//                the rule retries on the next wake cycle.
//   sendSummary — reset window counters only on true; leave them intact on
//                 false so no collected data is silently discarded.
// ---------------------------------------------------------------------------
bool sendSummary(AppState &state);
bool sendAlert(const char *alertType, const char *detail);
