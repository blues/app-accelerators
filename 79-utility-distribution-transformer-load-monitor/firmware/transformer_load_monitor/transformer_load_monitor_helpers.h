/***************************************************************************
  transformer_load_monitor_helpers.h — shared types, constants, extern
  declarations, and function prototypes for the Utility Distribution
  Transformer Load Monitor sketch.

  Included by both transformer_load_monitor.ino and
  transformer_load_monitor_helpers.cpp so all translation units share a
  single, consistent definition of every struct and constant.
***************************************************************************/
#pragma once

#include <Notecard.h>
#include <Wire.h>
#include <Adafruit_MCP9808.h>

// ---------------------------------------------------------------------------
// Notecarrier CX analog pins for the three CT channels
// ---------------------------------------------------------------------------
#define PIN_CT_A  A0
#define PIN_CT_B  A1
#define PIN_CT_C  A2

// ---------------------------------------------------------------------------
// CT electrical constants (YHDC SCT-013-000, 100 A / 50 mA output)
// ---------------------------------------------------------------------------
#define CT_BURDEN_OHMS   22.0f  // external burden resistor value (Ω)
#define CT_TURNS_RATIO   2000   // primary/secondary turns ratio (100 A / 0.05 A)
#define CT_NOISE_FLOOR_A  0.5f  // readings below this are treated as zero (A)

// Two-pass RMS sampling — 256 samples to establish the DC bias offset;
// 1480 samples to compute the AC RMS component.  Samples are paced at
// CT_SAMPLE_PERIOD_US so the 1480-sample window spans a deterministic ~333 ms
// (≈20 mains cycles at 60 Hz / ≈16.7 cycles at 50 Hz) regardless of the host
// ADC's native throughput.  Without explicit pacing, analogRead() on the
// Cygnet (STM32L4) runs in single-digit µs and the whole burst would complete
// in well under one mains cycle, producing nonsense RMS values that drift
// with whatever phase of the wave the loop happens to start on.
#define CT_DC_SAMPLES         256
#define CT_RMS_SAMPLES        1480
#define CT_SAMPLE_PERIOD_US   225U  // 4.44 kHz; 1480 × 225 µs ≈ 333 ms

// ADC full-scale count — must match the resolution set by analogReadResolution()
// in setup().  CT_SCALE is derived from this; a mismatch shifts every reading.
#define ADC_COUNTS  4096

// Scale factor: (Vref / ADC_counts) × turns_ratio / burden_ohms
// Multiply by RMS count value to convert to primary amps.
#define CT_SCALE  ((3.3f / (float)ADC_COUNTS) * (float)CT_TURNS_RATIO / CT_BURDEN_OHMS)

// ---------------------------------------------------------------------------
// Default thresholds — all overridable via Notehub environment variables
// ---------------------------------------------------------------------------
#define DEFAULT_SAMPLE_INTERVAL_SEC    300      // 5 minutes
#define DEFAULT_SUMMARY_INTERVAL_MIN    60      // 1 hour
#define DEFAULT_RATED_AMPS            100.0f    // matches SCT-013-000 100 A range
#define DEFAULT_OVERLOAD_PCT           95.0f
#define DEFAULT_IMBALANCE_PCT_THRESH   20.0f
#define DEFAULT_TEMP_ALERT_C           70.0f
#define DEFAULT_ALERT_COOLDOWN_SEC    1800      // 30-minute rearm window
// Default to 2 (split-phase L1/L2) — the most common residential distribution
// transformer installation.  Three-phase deployments must set phase_count=3
// via a Notehub environment variable before first power-on; leaving this at 2
// on a three-phase install would leave the A2/Phase-C channel unread and
// produce an inflated false imbalance between A0/A1 and the silent C phase.
#define DEFAULT_PHASE_COUNT             2

// ---------------------------------------------------------------------------
// Notefile names and sleep-payload segment identifier
// ---------------------------------------------------------------------------
#define NOTEFILE_SUMMARY  "xfmr_summary.qo"
#define NOTEFILE_ALERT    "xfmr_alert.qo"
#define SEG_ID            "XFMR"

// ---------------------------------------------------------------------------
// Per-type pending-alert slots — one slot per distinct alert type so an
// overload and a phase-imbalance that both trip on the same wake are each
// preserved independently if note.add fails and must be retried next wake.
// ---------------------------------------------------------------------------
#define ALERT_SLOT_OVERLOAD   0
#define ALERT_SLOT_IMBALANCE  1
#define ALERT_SLOT_TEMP       2
#define ALERT_SLOT_COUNT      3

// ---------------------------------------------------------------------------
// Runtime config fetched from Notehub env vars
// ---------------------------------------------------------------------------
struct EnvConfig {
    uint32_t sample_interval_sec;
    uint32_t summary_interval_min;
    float    rated_amps;
    float    overload_pct;
    float    imbalance_pct_thresh;
    float    temp_alert_c;
    uint32_t alert_cooldown_sec;
    int      phase_count;
};

// ---------------------------------------------------------------------------
// Pending-alert record — one slot per alert type; persisted in the sleep
// payload so a note.add failure on one wake is retried on the next.
// ---------------------------------------------------------------------------
struct PendingAlert {
    bool  active;
    char  type[20];
    float i_a, i_b, i_c, temp_c, extra;
};

// ---------------------------------------------------------------------------
// Persistent state (survives host power-down via Notecard sleep payload)
// ---------------------------------------------------------------------------
struct PersistState {
    // Hourly summary accumulators
    float    sum_i_a;
    float    sum_i_b;
    float    sum_i_c;
    float    sum_temp_c;
    uint16_t valid_samples;       // CT samples above noise floor
    uint16_t valid_temp_samples;  // temperature samples within plausible range
    uint16_t total_cycles;        // all wakes since last summary flush
    uint16_t overload_count;      // sample intervals that exceeded the overload
                                  // threshold in the current summary window

    // Alert cooldown counters (sample cycles remaining; decremented each wake).
    // uint16_t holds up to 65535 cycles; with alert_cooldown_sec ≤ 86400 and
    // sample_interval_sec ≥ 30, the maximum value is 86400/30 = 2880.
    uint16_t cd_overload;
    uint16_t cd_imbalance;
    uint16_t cd_temp;

    // Elapsed-time tracking for summary window
    uint32_t elapsed_sec;
    // Notecard epoch at the last successful summary flush; 0 = no confirmed
    // time reference (window sizing falls back to elapsed_sec)
    uint32_t last_summary_epoch;

    // Last outbound cadence successfully sent to hub.set — persisted here so
    // fetchEnvOverrides() can skip re-sending across card.attn wakes (a
    // function-static would be zeroed on every wake when the host power-cycles).
    uint32_t lastAppliedSummaryMin;

    // One-time configuration flags — retried every wake until confirmed.
    bool hub_configured;
    bool template_configured;

    // Env-var change detection: skip env.get round-trips when env.modified
    // matches the last successful fetch.  last_env_modified is only committed
    // after a complete, successful batch env.get so a transient read failure
    // never permanently masks a pending config update.
    uint32_t  last_env_modified;
    EnvConfig cached_cfg;

    // Per-type pending-alert queue: ALERT_SLOT_COUNT independent slots so
    // simultaneous multi-type alerts are each preserved across wakes.
    PendingAlert pending_alerts[ALERT_SLOT_COUNT];
};

// ---------------------------------------------------------------------------
// Extern references to globals defined in transformer_load_monitor.ino
// ---------------------------------------------------------------------------
extern Notecard         notecard;
extern Adafruit_MCP9808 mcp9808;
extern PersistState     state;
extern EnvConfig        cfg;
extern bool             coldBoot;

// ---------------------------------------------------------------------------
// Helper function declarations
// ---------------------------------------------------------------------------
// product_uid: the Notehub ProductUID string defined in the main sketch.
// Passed as a parameter (rather than accessed as a macro) so the helpers
// translation unit does not need to see the PRODUCT_UID preprocessor define,
// which is defined only in transformer_load_monitor.ino.
bool  hubConfigure(const char *product_uid);
bool  defineTemplates();
void  fetchEnvOverrides(EnvConfig &c);
float readCtRms(uint8_t pin);
float readTemperatureC();
void  checkAlerts(float i_a, float i_b, float i_c, float temp_c);
void  sendAlert(uint8_t slot, const char *type, float i_a, float i_b,
                float i_c, float temp_c, float extra);
bool  sendSummary();
