/***************************************************************************
  notecard_helpers.h — Notecard configuration, template registration,
  environment variable fetch, and note submission declarations for
  post_discharge_vitals_hub.  Implementation is in notecard_helpers.cpp.
***************************************************************************/
#pragma once

#include <Notecard.h>
#include "vitals_config.h"

// ─── Shared Notecard instance (defined in post_discharge_vitals_hub.ino) ──────
extern Notecard notecard;

// ─── Alert threshold globals (defined in post_discharge_vitals_hub.ino) ───────
extern float g_bp_systolic_high;
extern float g_bp_diastolic_high;
extern float g_spo2_low;
extern float g_hr_high;
extern float g_hr_low;
extern float g_weight_delta_kg;

// ─── Alert cooldown timestamps (defined in post_discharge_vitals_hub.ino) ─────
extern uint32_t g_last_weight_alert_ms;
extern uint32_t g_last_bp_alert_ms;
extern uint32_t g_last_spo2_alert_ms;
extern uint32_t g_last_hr_high_alert_ms;
extern uint32_t g_last_hr_low_alert_ms;

// ─── Function declarations ────────────────────────────────────────────────────

// Send a request and inspect the response for an error field.
// Returns true on success; always consumes the J* whether it succeeds or fails.
bool sendChecked(J *req);

// Configure Notecard hub connection with cold-boot retry and response validation.
void notecardConfigure();

// Register fixed-width binary templates for each reading Notefile.
void defineTemplates();

// Fetch alert threshold env vars from Notehub and update threshold globals.
void fetchEnvVars();

// Enqueue a reading note and, if the reading trips a threshold, also enqueue a
// vitals_alert.qo note with checked delivery and retry.
void submitWeight(float kg, float prev_kg);
void submitBp(int16_t systolic, int16_t diastolic, int16_t pulse);
void submitSpO2(int16_t spo2_pct, int16_t pulse);
void submitActivity(uint16_t hr_bpm);
