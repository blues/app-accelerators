/*
 * grease_interceptor_monitor_helpers.h
 *
 * Shared constants, State type, and utility-function declarations for the
 * Hydromechanical (HGI) and Batch-Collection Grease Interceptor Level Monitor sketch.
 *
 * Isolating these definitions keeps the main sketch file under 500 lines
 * while making the sensor-protocol and Notecard helper logic easy to locate
 * and review independently.
 *
 * The Notecard global (`notecard`) is defined in the main sketch;
 * grease_interceptor_monitor_helpers.cpp accesses it via `extern`.
 */
#pragma once

#include <Notecard.h>

// ---------------------------------------------------------------------------
// Debug output (comment out this line to silence Serial in production)
// ---------------------------------------------------------------------------
#define usbSerial Serial

// ---------------------------------------------------------------------------
// Operator-tunable config — persisted inside State across sleep cycles.
// Seeded from compile-time defaults on cold boot; initialized from State on
// every subsequent wake so a transient env.get failure retains the last
// operator-applied values instead of silently reverting to defaults.
// All fields can be overridden via Notehub environment variables.
// ---------------------------------------------------------------------------
struct Config {
    float    interceptor_depth_mm;
    float    alert_threshold_pct;
    uint32_t sample_interval_sec;
    uint32_t report_interval_min;
};

// ---------------------------------------------------------------------------
// DFRobot A02YYUW UART protocol constants
// Packets arrive at 9600 baud: [0xFF][high][low][checksum]
// Distance in mm = (high << 8) | low
// ---------------------------------------------------------------------------
#define SENSOR_BAUD        9600
#define SENSOR_PACKET_LEN     4
#define SENSOR_START_BYTE  0xFF
#define SENSOR_WAIT_MS     2000  // max wait for a well-formed packet
#define NUM_READINGS          5  // readings per sample; median is used

// ---------------------------------------------------------------------------
// State — persisted across sleep cycles via NotePayloadSaveAndSleep.
//
// Epoch sentinel values:
//   0  — event never fired (initial cold-boot value)
//   1  — event fired once before card.time was available (clock not yet synced)
//   >1 — real Unix epoch of last emission; normal cooldown/interval math applies
//
// The main sketch normalises sentinel 1 → now on the first wake where
// card.time returns a valid (>0) timestamp, preventing a spurious re-fire.
// ---------------------------------------------------------------------------
struct State {
    float    fill_pct_sum;         // accumulator for window average
    uint32_t valid_samples;        // valid readings in the current window
    float    fill_pct_peak;        // highest fill % seen in this window
    float    fill_pct_last_valid;  // most recent valid instantaneous fill reading;
                                   // -1.0 until the first valid sample is taken
    uint32_t last_alert_epoch;     // epoch of last alert note sent
    uint32_t last_report_epoch;    // epoch of last summary note sent
    uint32_t applied_outbound_min; // last hub.set outbound value applied;
                                   // re-issued when report_interval_min changes
    bool     notecard_configured;  // true once hub.set has been confirmed on first boot;
                                   // retried every wake until success
    bool     templates_defined;    // true once note.template has been confirmed on first
                                   // boot; retried every wake until success
    Config   cfg;                  // active operator config; seeded from compile-time
                                   // defaults on cold boot, updated by fetchEnvOverrides()
                                   // on each wake, and persisted here so a transient
                                   // env.get failure retains the last applied values
};

// ---------------------------------------------------------------------------
// Utility-function declarations
// Implemented in grease_interceptor_monitor_helpers.cpp.
// ---------------------------------------------------------------------------
float    readDistanceMm(void);
float    medianOf(float *arr, int n);
float    distanceToFillPct(float distance_mm, float depth_mm);
uint32_t getEpochTime(void);
bool     notecardResponseOk(J *rsp);
bool     sendSummary(const State &state);
bool     sendAlert(float fill_pct, float threshold_pct);
