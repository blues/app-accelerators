/***************************************************************************
  lift_station_monitor_helpers.h — Compile-time constants, shared types,
  extern declarations, and helper-function prototypes for
  lift_station_monitor.ino.

  All #defines, the AppState struct layout, and extern references to the
  runtime globals defined in the main sketch are placed here so both
  compilation units share a single authoritative source.
***************************************************************************/
#pragma once

#include <Notecard.h>

// ---------------------------------------------------------------------------
// Configuration — edit PRODUCT_UID before flashing
// ---------------------------------------------------------------------------
#ifndef PRODUCT_UID
#define PRODUCT_UID ""  // e.g. "com.your-company.your-name:lift_station"
#endif
// Enforce at build time: an empty PRODUCT_UID causes hub.set to register the
// device under no project, making it unreachable from Notehub.
static_assert(sizeof(PRODUCT_UID) > 1,
              "PRODUCT_UID is empty — set it to your Notehub ProductUID before building.");

// Notecard sync timing
#define OUTBOUND_INTERVAL_MIN   60   // cellular sync for queued notes
#define INBOUND_INTERVAL_MIN   120   // env-var pull cadence (default; tunable via inbound_interval_min env var)

// Host sleep / sample interval
#define SAMPLE_INTERVAL_SEC     60   // wake every 60 s to sample sensors

// ADC pins (Notecarrier CX / Cygnet STM32L433)
#define PIN_LEVEL_SENSOR   A0        // 4-20 mA level transducer → 150 Ω shunt
#define PIN_PUMP1_CT       A1        // SCT-013-030 CT, pump 1
#define PIN_PUMP2_CT       A2        // SCT-013-030 CT, pump 2
#define PIN_FLOAT_SWITCH   D2        // SPST float switch, active-low, INPUT_PULLUP
                                     // D2 = Notecarrier CX header pin D2 (Arduino digital pin 2)

// Level sensor ADC calibration (150 Ω shunt, 3.3 V VREF, 12-bit ADC)
//   4 mA × 150 Ω = 0.60 V → (0.60 / 3.3) × 4095 ≈ 745
//  20 mA × 150 Ω = 3.00 V → (3.00 / 3.3) × 4095 ≈ 3723
#define LEVEL_ADC_MIN      745       // ADC count at 4 mA (0 % full)
#define LEVEL_ADC_MAX     3723       // ADC count at 20 mA (100 % full)
#define LEVEL_SAMPLES       64       // averaged ADC samples per read

// ADC bounds for sensor-fault detection (checked before any clamping).
// A 100-count grace zone (≈ 0.08 mA, ~0.3 % of full range) accommodates
// wiring-resistance and ADC offset without masking genuine faults:
//   < LEVEL_ADC_FAULT_LO (≈ 645) → open loop; sensor unplugged or broken wire
//   > LEVEL_ADC_FAULT_HI (≈ 3823) → short-circuit or severe overpressure
// An open loop produces ≈ 0 ADC counts, well outside the grace zone.
#define LEVEL_ADC_FAULT_LO (LEVEL_ADC_MIN - 100)
#define LEVEL_ADC_FAULT_HI (LEVEL_ADC_MAX + 100)

// Sentinel for invalid level readings. Chosen well outside any plausible
// physical range [0..100 %] so downstream analytics can distinguish
// "sensor faulted" from a true near-zero fill level — the same convention
// used in Blues reference accelerators 51 and 52.
#define LEVEL_INVALID_SENTINEL (-9999.0f)

// CT / current sensing
#define CT_BIAS_SAMPLES    256       // samples to estimate the DC mid-rail bias
#define CT_RMS_SAMPLES    1024       // samples for RMS integration (~100 ms)
#define CT_AMPS_PER_VOLT  30.0f     // SCT-013-030 spec: 30 A per 1 V RMS

// CT fault detection thresholds (12-bit ADC, 3.3 V VREF, 10 kΩ/10 kΩ divider)
//
// CT_BIAS_MIN / CT_BIAS_MAX — valid window for the DC mid-rail bias point.
//   Ideal centre = 2048 counts (VREF/2). Accepting ±50 % of full-scale
//   [1024..3072] catches a broken/missing bias resistor or a CT terminal
//   shorted to VCC/GND while tolerating worst-case 1 % resistor mismatch.
//
// CT_RAIL_MARGIN — an ADC sample within this many counts of 0 or 4095 during
//   the RMS window indicates rail saturation (clipping, shorted secondary, or
//   a severely over-ranged signal).
//
// Note: an open CT secondary with an intact bias network reads ≈ VREF/2 with
//   near-zero variance — indistinguishable from a legitimately idle pump in
//   software alone. The bias-window and rail checks catch the most common
//   hardware failures; an open-winding fault is best diagnosed by an operator
//   noticing sustained ct*_faults while a pump is known to be running.
#define CT_BIAS_MIN       1024
#define CT_BIAS_MAX       3072
#define CT_RAIL_MARGIN    10

// Sentinel for an invalid CT reading. Chosen outside any plausible physical
// range [0..30 A] so downstream analytics can distinguish a failed current
// sensor from a legitimately idle pump, using the same convention as
// LEVEL_INVALID_SENTINEL.
#define CT_INVALID_SENTINEL (-9999.0f)

// ---------------------------------------------------------------------------
// Bench-test overrides — define at compile time to inject synthetic sensor
// values for bench fault-simulation without requiring live sensors. Never
// define these macros in a production build; the #warning below ensures they
// appear in the build log if accidentally left in.
//
//   BENCH_FORCE_LEVEL_PCT  (float, 0.0–100.0)
//       Bypasses readLevelPct() and injects a synthetic wet-well level on
//       every cycle; sets level_valid = true automatically. Pair with
//       high_level_pct = 1.0 in Notehub to guarantee pump_fail_to_start fires.
//
//   BENCH_CLOG_DELTA  (float, % per cycle)
//       Replaces the measured sample-to-sample level delta inside
//       runDetectionCycle() Rule 3. Set to 2.0 (or any value ≥ rising_rate_pct)
//       and ensure a CT reads above pump_on_amps for two consecutive cycles to
//       guarantee pump_clog fires.
// ---------------------------------------------------------------------------
#if defined(BENCH_FORCE_LEVEL_PCT) || defined(BENCH_CLOG_DELTA)
#  warning "Bench-test override active — BENCH_FORCE_LEVEL_PCT/BENCH_CLOG_DELTA defined. Do not flash to a deployed station."
#endif

// Alert deduplication: 30 cycles × 60 s/cycle = 30-minute cooldown per type
#define ALERT_COOLDOWN_CYCLES   30

// Notefile names
#define NOTEFILE_ALERT    "lift_alert.qo"
#define NOTEFILE_SUMMARY  "lift_summary.qo"

// Template type hints (note-c convention: 14.1 = 4-byte float, 12 = 2-byte int)
#define TTYPE_FLOAT   14.1
#define TTYPE_INT16   12
#define TTYPE_BOOL    true

// ---------------------------------------------------------------------------
// Persistent application state — serialized into Notecard between sleeps
// ---------------------------------------------------------------------------
struct AppState {
    float    prev_level_pct;          // last valid sample's level (trend detection)
    bool     have_prev_level;         // false until a valid prior sample exists;
                                      // guards the clog rule against a false
                                      // first-cycle trigger on cold boot
    bool     prev_any_on;             // true when at least one pump was confirmed
                                      // running on the previous sample; used by the
                                      // clog rule to require two consecutive running
                                      // samples before evaluating rising level, so
                                      // a pump-off → pump-on transition into a
                                      // still-rising wet well cannot trip the rule
                                      // on the very first cycle after start-up

    // Alert cooldown counters; alert fires when counter reaches 0
    uint16_t cooldown_fail_to_start;
    uint16_t cooldown_clog;
    uint16_t cooldown_high_water;

    // Hourly summary accumulators
    float    sum_level_pct;           // sum of valid level readings only
    float    sum_pump1_amps;          // sum of valid CT1 readings only
    float    sum_pump2_amps;          // sum of valid CT2 readings only
    uint32_t sample_count;            // total samples this summary window
    uint32_t pump1_run_samples;       // samples where pump 1 was confirmed running
    uint32_t pump2_run_samples;       // samples where pump 2 was confirmed running
    uint32_t alert_count;             // alerts successfully queued this window
    uint32_t level_fault_count;       // samples with an out-of-range level ADC
    uint32_t ct1_fault_count;         // samples where CT1 returned CT_INVALID_SENTINEL
    uint32_t ct2_fault_count;         // samples where CT2 returned CT_INVALID_SENTINEL

    // Last outbound/inbound intervals (min) applied to the Notecard via hub.set.
    // Initialized to 0 on cold boot — an impossible value for valid intervals
    // (≥ 1 min) — so applyHubSetIfChanged() always issues hub.set at least once
    // and retries on every wake until it confirms success.
    uint32_t applied_outbound_min;
    uint32_t applied_inbound_min;

    // Template registration guard. False on cold boot; set to true only after
    // both note.template requests succeed. defineTemplates() is retried on
    // every wake while this remains false.
    bool     templates_registered;

    // Last-known-good env-var overrides. Initialized from compile-time defaults
    // on cold boot and updated only when a freshly-parsed value passes both
    // string-validity and range checks, ensuring a failed env.get retains
    // the last operator-configured value rather than reverting to defaults.
    float    cfg_pump_on_amps;
    float    cfg_high_level_pct;
    float    cfg_rising_rate_pct;
    uint32_t cfg_summary_interval_min;
    uint32_t cfg_inbound_interval_min;
};

// ---------------------------------------------------------------------------
// Extern declarations — variables defined in lift_station_monitor.ino
// ---------------------------------------------------------------------------
extern Notecard  notecard;
extern AppState  g_state;
extern float     g_pump_on_amps;
extern float     g_high_level_pct;
extern float     g_rising_rate_pct;
extern uint32_t  g_summary_interval_min;
extern uint32_t  g_inbound_interval_min;

// ---------------------------------------------------------------------------
// Helper function declarations (implemented in lift_station_monitor_helpers.cpp)
// ---------------------------------------------------------------------------
bool     parseFloat(const char *str, float *out);
bool     parseLong(const char *str, long *out);
uint32_t clampU32(long v, uint32_t minv, uint32_t maxv, uint32_t fallback);
float    clampF(double v, float minv, float maxv, float fallback);
void     notecardConfigure(void);
bool     defineTemplates(void);
void     fetchEnvOverrides(void);
void     applyHubSetIfChanged(void);
float    readLevelPct(bool *valid_out);
float    readPumpAmps(uint8_t pin, bool *valid_out);
bool     readFloatSwitch(void);
