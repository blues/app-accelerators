/*
 * notecard_helpers.h — Notecard configuration, time utilities, and
 *                       environment-variable overrides for the
 *                       Ambient Home Well-Being & Activity Hub.
 *
 * This file is specific to this project and is NOT a general-purpose library.
 */
#pragma once

#include <Notecard.h>
#include "app_state.h"

// ---------------------------------------------------------------------------
// Shared Notecard object — defined in activity_hub.ino
// ---------------------------------------------------------------------------
extern Notecard notecard;

// ---------------------------------------------------------------------------
// Runtime-configurable globals (populated from env vars each wake).
// Defined in notecard_helpers.cpp; extern'd here for sensor_alert.cpp and
// the main sketch.
// ---------------------------------------------------------------------------
extern int g_sample_interval_sec;
extern int g_summary_interval_min;
extern int g_morning_start_hour;
extern int g_morning_end_hour;
extern int g_sleep_start_hour;
extern int g_sleep_end_hour;
extern int g_night_bathroom_limit;
extern int g_bed_threshold;
extern int g_utc_offset_hours;
extern int g_quiet_minutes_for_alert;

// ---------------------------------------------------------------------------
// Notecard one-time setup helpers (each returns true on confirmed success;
// the caller persists the result in AppState and retries on false)
// ---------------------------------------------------------------------------

// Returns a djb2 hash of the compile-time PRODUCT_UID constant.
// Used to detect firmware reflashes with a changed ProductUID so hub.set
// is reissued even when hub_configured is already true.
uint32_t productUidHash();

bool hubConfigure();
bool defineTemplates();
bool motionStop();

// Reads Notehub environment variables and updates the runtime-configurable
// globals above. Takes AppState by reference so it can persist the confirmed
// outbound cadence across host power-cycles (see applied_summary_interval_min).
void fetchEnvOverrides(AppState &state);

// ---------------------------------------------------------------------------
// Time helpers
// ---------------------------------------------------------------------------
uint32_t notecardTime();
uint8_t  localHour(uint32_t epochUtc);
bool     inWindow(uint8_t hour, uint8_t start_h, uint8_t end_h);
bool     cooldownExpired(const AppState &s, int idx, uint32_t now);
