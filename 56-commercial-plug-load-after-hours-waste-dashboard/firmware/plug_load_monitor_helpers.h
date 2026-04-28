// plug_load_monitor_helpers.h
//
// Shared constants, types, extern declarations, and function prototypes for
// the plug_load_monitor sketch.  Include this header in both
// plug_load_monitor.ino and plug_load_monitor_helpers.cpp.
//
// Files in this directory:
//   plug_load_monitor.ino          — Arduino sketch (setup / loop)
//   plug_load_monitor_helpers.h    — this file
//   plug_load_monitor_helpers.cpp  — helper implementations

#pragma once
#include <Notecard.h>

// ── Product UID ───────────────────────────────────────────────────────────────
// Set your Notehub ProductUID here.  Both this file and plug_load_monitor.ino
// are compiled as separate translation units, so PRODUCT_UID must be defined
// in this shared header so hubConfigure() (in helpers.cpp) picks it up.
#ifndef PRODUCT_UID
#define PRODUCT_UID "" // "com.my-company.my-name:plug-load-monitor"
#pragma message "PRODUCT_UID not set. Define it in plug_load_monitor_helpers.h or provision the device via Notehub."
#endif

// ── Debug output ──────────────────────────────────────────────────────────────
// Defined here (not in the .ino) so both translation units see the same flag.
// Uncomment to enable USB-serial tracing and Notecard wire-level output.
// Leave commented for production: no serial code is emitted and the host
// never waits for a CDC connection before sampling and sleeping.
// #define PLUG_LOAD_DEBUG
#ifdef PLUG_LOAD_DEBUG
#  define dbgSerial Serial
#endif

// ── Optional real-time after-hours alert extension ────────────────────────────
// When PLUG_LOAD_ALERTS is defined the firmware also registers a second
// Notefile (circuit_alert.qo) and emits an immediate sync:true note whenever
// a circuit exceeds CFG_AFTER_HOURS_AMPS during non-business hours.
//
// This feature is DISABLED by default.  The baseline build produces only
// circuit_summary.qo — no card.time calls, no alert notes, no extra cellular
// sessions.  Uncomment the line below to enable the alert extension:
// #define PLUG_LOAD_ALERTS

// ── ADC constants (STM32L4 12-bit, 3.3 V reference) ──────────────────────────
static const float    ADC_VREF_V  = 3.30f;
static const uint16_t ADC_COUNTS  = 4095;   // 12-bit full-scale

// ── CT clamp constants (SCT-013-030) ─────────────────────────────────────────
// The -030 variant has an internal burden resistor that produces exactly 1 V
// RMS at its rated primary current (30 A RMS).  No external burden needed.
static const float CT_VOUT_AT_FULL_SCALE = 1.0f;   // V RMS at rated primary A
static const float CT_FULL_SCALE_DEFAULT = 30.0f;  // A RMS (SCT-013-030 rated)

// Samples per RMS window.
//   CT_BIAS_SAMPLES: short pre-average to measure the DC bias mid-point.
//   CT_RMS_SAMPLES:  main window — 1480 analogRead() calls.  Actual elapsed
//                    time and mains-cycle coverage depend on the platform's ADC
//                    timing; validate on your specific hardware build if exact
//                    cycle coverage matters.
static const uint16_t CT_BIAS_SAMPLES = 256;
static const uint16_t CT_RMS_SAMPLES  = 1480;

// ── Channel mapping (Notecarrier CX dual 16-pin header) ──────────────────────
static const uint8_t MAX_CHANNELS          = 4;
static const uint8_t CT_PINS[MAX_CHANNELS] = { A0, A1, A2, A3 };

// Emitted when a channel produced zero valid samples during a window.
// Downstream consumers must treat any field equal to this value as "no data",
// not as a zero-amp or zero-minute reading.
static const float INVALID_SENTINEL = -9999.0f;

// ── Persisted configuration snapshot ─────────────────────────────────────────
// Snapshot of the active CFG_* globals, stored inside AppState so the last
// successfully fetched configuration survives a transient I²C failure at wake.
// Alert-related fields are retained in the struct regardless of PLUG_LOAD_ALERTS
// so the persisted payload layout is stable whether or not the extension is
// compiled in.
struct AppCfg {
    uint32_t sample_interval_sec;
    uint32_t report_interval_min;
    uint8_t  circuit_count;
    float    idle_threshold_amps;
    float    after_hours_amps;      // used only when PLUG_LOAD_ALERTS is defined
    int8_t   biz_hours_start;
    int8_t   biz_hours_end;
    int8_t   tz_offset_hrs;
    uint32_t alert_cooldown_sec;    // used only when PLUG_LOAD_ALERTS is defined
    float    ct_full_scale_amps;
};

// ── State preserved across sleep cycles via NotePayloadSaveAndSleep ──────────
struct AppState {
    uint32_t cycles;
    AppCfg   saved_cfg;     // last successfully fetched env-var configuration
    bool     cfg_valid;     // true once saved_cfg has been populated at least once

    // Per-channel rolling accumulators for the current summary window.
    float    sum_arms[MAX_CHANNELS];     // sum of per-sample RMS amps
    float    peak_arms[MAX_CHANNELS];   // highest single-sample RMS in window
    uint32_t n_arms[MAX_CHANNELS];      // count of valid samples per channel
    uint32_t active_secs[MAX_CHANNELS]; // total seconds at or above idle threshold

    uint32_t total_samples;         // total wakes in this summary window
    uint32_t elapsed_window_sec;    // seconds elapsed since the last summary emit
                                    // (driven by CFG_SAMPLE_INTERVAL_SEC; no
                                    // dependency on card.time)

    // Epoch of last alert per channel — used only when PLUG_LOAD_ALERTS is defined.
    // Retained in the struct so the payload layout is consistent across builds.
    // Persisted so the per-channel cooldown spans sleep boundaries correctly.
    uint32_t alert_last_unix[MAX_CHANNELS];

    // Outbound interval last successfully applied via hub.set.
    uint32_t last_applied_outbound_min;
};

// ── Extern declarations for globals defined in plug_load_monitor.ino ─────────
extern uint32_t CFG_SAMPLE_INTERVAL_SEC;
extern uint32_t CFG_REPORT_INTERVAL_MIN;
extern uint8_t  CFG_CIRCUIT_COUNT;
extern float    CFG_IDLE_THRESHOLD_AMPS;
extern float    CFG_AFTER_HOURS_AMPS;       // used only when PLUG_LOAD_ALERTS is defined
extern int8_t   CFG_BIZ_HOURS_START;
extern int8_t   CFG_BIZ_HOURS_END;
extern int8_t   CFG_TZ_OFFSET_HRS;
extern uint32_t CFG_ALERT_COOLDOWN_SEC;     // used only when PLUG_LOAD_ALERTS is defined
extern float    CFG_CT_FULL_SCALE_AMPS;

extern const char STATE_SEG_ID[];
extern AppState   state;
extern bool       g_templates_applied;
extern Notecard   notecard;

// ── Public function prototypes ────────────────────────────────────────────────
// Only functions called directly from plug_load_monitor.ino are listed here.
// Internal helpers used only within plug_load_monitor_helpers.cpp are static
// and not declared in this header.

// Config helpers (called from setup() on every wake)
void applyCfg(const AppCfg &c);
void captureCfg(AppCfg &c);

// Notecard setup (called from setup() and loop())
bool hubConfigure();
bool defineTemplates();
bool fetchEnvOverrides();

// Main sample cycle (called from setup() and loop())
void runSampleCycle();
