/***************************************************************************
  ble_central.h — BLE Central declarations for post_discharge_vitals_hub.

  Exposes the four buffered reading structs, the shared connection-state
  globals (extern'd to post_discharge_vitals_hub.ino), and the initBLE()
  entry point.  Implementation is in ble_central.cpp.
***************************************************************************/
#pragma once

#include <bluefruit.h>
#include "vitals_config.h"

// ─── Buffered reading structs ─────────────────────────────────────────────────
// BLE indication/notification callbacks (SoftDevice task context) write to
// these structs and set the volatile `valid` flag.  The main loop copies and
// clears each struct inside a noInterrupts()/interrupts() critical section.

struct WeightReading {
    float    kg;
    float    prev_kg;   // most recent previously accepted reading (0 if none)
    volatile bool valid;
};

struct BpReading {
    int16_t  systolic;
    int16_t  diastolic;
    int16_t  pulse_bpm;
    volatile bool valid;
};

struct SpO2Reading {
    int16_t  spo2_pct;
    int16_t  pulse_bpm;
    volatile bool valid;
};

struct ActivityReading {
    uint16_t heart_rate_bpm;
    volatile bool valid;
};

// ─── Extern globals (defined in post_discharge_vitals_hub.ino) ───────────────
extern WeightReading   g_weight;
extern BpReading       g_bp;
extern SpO2Reading     g_spo2;
extern ActivityReading g_activity;

extern float    g_last_weight_kg;
extern uint16_t g_hrConnHandle;
extern uint16_t g_oneshot_conn_handle;
extern uint16_t g_active_conn_handle;
extern uint32_t g_conn_start_ms;
extern uint32_t g_last_hr_sample_ms;

// ─── Entry point ─────────────────────────────────────────────────────────────
void initBLE();
