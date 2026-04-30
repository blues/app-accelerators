/*
 * livestock_water_tank_monitor_helpers.h
 * Blues Application Example — Off-Grid Livestock Water Tank Monitor
 *
 * Shared compile-time constants, the GlobalState struct, extern declarations
 * for all globals accessed by both the sketch and the helpers translation unit,
 * and prototypes for every helper function defined in
 * livestock_water_tank_monitor_helpers.cpp.
 */
#pragma once
#include <Notecard.h>

// ── I/O pins ──────────────────────────────────────────────────────────────────
#define PIN_LEVEL_SENSOR    A0   // MB7389 analog voltage output (AN pin)
#define PIN_PUMP_CT         A1   // SCT-013 bias-circuit midpoint (~Vcc/2 DC)
#define PIN_BATTERY         A2   // 47kΩ/10kΩ divider tap from 12V solar bus (switched)
// BSS84 PMOS high-side switch enable (via MMBT3904 NPN level shifter).
// Drive HIGH to enable the divider for a sample; drive LOW when done.
// A 100 kΩ gate pullup to battery+ holds the PMOS off whenever the host MCU
// is unpowered, so A2 sits at GND through the low-side 10 kΩ during sleep
// and no leakage path reaches the A2 input-protection diode.
#define PIN_BATT_EN         A3   // active HIGH → PMOS on → divider active

// ── Notefile names ────────────────────────────────────────────────────────────
#define NOTEFILE_SUMMARY    "tank_status.qo"  // periodic, compact template-encoded, port 50
#define NOTEFILE_ALERT      "tank_alert.qo"   // immediate, compact template-encoded, port 51

// ── Alert index constants (indices into GlobalState.lastAlertEpoch[]) ─────────
#define ALERT_LEVEL_LOW     0
#define ALERT_LEVEL_CRIT    1
#define ALERT_BATTERY_LOW   2
#define NUM_ALERTS          3

// ── Alert type codes (tank_alert.qo body field "alert_code") ─────────────────
// Integer codes instead of strings keep the compact-template payload
// fixed-length binary — the same requirement as the summary template for the
// NOTE-NBGLWX Starnote/NTN satellite path.  The "alert_code" template field
// uses type hint 12, which encodes as a 2-byte signed integer (int16_t,
// range −32 768 to +32 767); values 0–2 fit trivially within that range.
// All fields are always present in compact binary encoding, so pump_amps = 0.0
// (pump off at alert time) is transmitted faithfully with no omitempty-style
// drop for zero-valued fields.
// Downstream Notehub routes and cloud functions decode the integer:
//   0 → level_low  |  1 → level_critical  |  2 → battery_low
#define ALERT_CODE_LEVEL_LOW    0
#define ALERT_CODE_LEVEL_CRIT   1
#define ALERT_CODE_BATTERY_LOW  2

// ── MB7389 ultrasonic level sensor constants ───────────────────────────────────
// Analog output: Vout = Vcc/5120 × range_mm.
// At 3.3V supply, Vout/Vcc cancels in the ADC conversion, leaving:
//   distance_mm = adc_count × (5120 / 4095)
#define MB7389_MM_PER_COUNT     (5120.0f / 4095.0f)
#define MB7389_MIN_MM            300.0f   // blanking zone (datasheet minimum)
#define MB7389_MAX_MM           5000.0f   // maximum rated range
#define MB7389_SETTLE_MS         250      // power-up / first-ranging stabilization
#define LEVEL_ADC_SAMPLES         16      // averages to suppress ADC noise

// ── SCT-013-030 current transformer constants ──────────────────────────────────
// Voltage-output variant: 30A primary → 1V RMS secondary (built-in burden R).
// The AC signal rides on a ~1.65V DC bias created by the 2×10kΩ divider.
#define CT_AMPS_PER_VOLT        30.0f
#define CT_BIAS_SAMPLES          256      // samples to estimate DC midpoint
#define CT_RMS_SAMPLES          1480      // ~20 mains cycles at ADC throughput
#define CT_NOISE_FLOOR_A         0.15f    // clamp below this to 0.0 (pump off)

// ── Battery voltage divider: R1=47kΩ, R2=10kΩ ────────────────────────────────
// Scale: 10/(47+10) = 0.1754 → maps 0–14.5V to 0–2.54V (fits 3.3V ADC rail).
#define BATT_DIVIDER_RATIO      (10.0f / 57.0f)
#define BATT_ADC_SAMPLES           4

// ── Battery voltage plausibility bounds ───────────────────────────────────────
// Any reading outside this window indicates an open or shorted divider, a
// floating A2 pin, or an ADC near the rail. These faults must not be mistaken
// for valid battery data — a falsely low voltage would suppress the adaptive-
// sleep multipliers that are the primary power-conservation mechanism in this
// design, and a falsely high voltage would prevent a warranted battery_low alert.
// Lower bound is set just above the D24V10F5 step-down regulator's minimum
// viable input (~5.4 V dropout): if this code is executing, the host is
// powered, which means the battery is above the regulator floor — so any
// reading above 6.0 V is a plausible real measurement. A floating or open-
// divider A2 pin produces near 0 V and is correctly rejected. This bound
// must remain well below any operator-configured battery_alert_v so that the
// battery_low alert and the adaptive extended-sleep multipliers remain active
// through the full range of voltage the device can observe while still running.
//   6.0 V → just above regulator dropout floor; rejects open/shorted-divider
//           noise near GND while accepting the full depleted-battery range
//  15.5 V → above the equalization-charge maximum for both SLA and LiFePO4
#define BATT_PLAUSIBLE_V_MIN     6.0f
#define BATT_PLAUSIBLE_V_MAX    15.5f

// ── CT bias plausibility bounds (raw ADC counts, 12-bit / 0–4095) ─────────────
// The 2×10kΩ bias resistors should centre the AC signal near 2048 counts
// (Vcc/2 = 1.65 V). Readings outside ±25% of full scale indicate an open or
// shorted divider; RMS deviation calculated from a corrupted midpoint yields
// meaningless amps and must be discarded with a -1.0 sentinel.
#define CT_BIAS_MIN_COUNTS      1024    // < 25% of ADC full scale → bias fault
#define CT_BIAS_MAX_COUNTS      3072    // > 75% of ADC full scale → bias fault

// ── Battery-alert hysteresis ──────────────────────────────────────────────────
// The battery_low alert is edge-triggered: it fires once when voltage first
// crosses below battery_alert_v, then suppresses further alerts until voltage
// climbs back above battery_alert_v + BATTERY_LOW_HYSTERESIS_V. Without this
// margin, a battery sitting at exactly the threshold can chatter alerts on every
// wake, each of which forces a sync:true radio session while the device is
// already in its power-conserving 60-minute emergency-sleep cadence.
#define BATTERY_LOW_HYSTERESIS_V    0.5f

// ── Compile-time defaults — all overridable via Notehub environment variables ──
#define DEFAULT_TANK_DEPTH_MM       1200   // sensor-to-bottom distance when empty
#define DEFAULT_SENSOR_MIN_MM        300   // sensor-to-water distance when full
#define DEFAULT_LEVEL_ALERT_PCT       20   // level_low fires below this (%)
#define DEFAULT_LEVEL_CRITICAL_PCT    10   // level_critical fires below this (%)
#define DEFAULT_PUMP_ON_AMPS         1.0f  // RMS amps → pump considered running
#define DEFAULT_BATTERY_ALERT_V     11.5f  // battery_low fires below this (V)
#define DEFAULT_SAMPLE_INTERVAL_SEC  900   // 15 minutes between sensor reads
#define DEFAULT_SUMMARY_INTERVAL_MIN 240   // 4 hours between summary Notes
#define DEFAULT_ALERT_COOLDOWN_SEC  3600   // 1 hour between repeated alerts

// ── Env-var clamp bounds ──────────────────────────────────────────────────────
// Invalid or extreme values must not create tight-loop sleeps, impossible
// calibrations, or sample-counter overflows.  Every parsed env-var is validated
// against these limits; the compile-time default is kept when a value falls
// outside the allowed range.
#define ENV_TANK_DEPTH_MM_MIN      400u
#define ENV_TANK_DEPTH_MM_MAX    10000u
#define ENV_SENSOR_MIN_MM_MIN    ((uint32_t)MB7389_MIN_MM)   // 300
#define ENV_SENSOR_MIN_MM_MAX     9000u
#define ENV_LEVEL_PCT_MIN            1u
#define ENV_LEVEL_PCT_MAX          100u
#define ENV_PUMP_AMPS_MIN          0.05f
#define ENV_PUMP_AMPS_MAX        100.0f
#define ENV_BATT_V_MIN             5.0f
#define ENV_BATT_V_MAX            20.0f
#define ENV_SAMPLE_SEC_MIN          60u   // 1-min floor prevents tight-loop sleep
#define ENV_SAMPLE_SEC_MAX       86400u   // 24-h ceiling (also hard cap for doSleep)
#define ENV_SUMMARY_MIN_MIN          5u   // 5-min floor
#define ENV_SUMMARY_MIN_MAX       1440u   // 24-h ceiling
#define ENV_COOLDOWN_SEC_MIN        60u
#define ENV_COOLDOWN_SEC_MAX     86400u

// ── Persisted application state (saved to Notecard flash across sleeps) ───────
struct GlobalState {
    uint32_t lastSummaryEpoch;              // Unix time of last summary Note (0 = window not yet started)
    uint32_t lastAlertEpoch[NUM_ALERTS];    // Unix time of last alert per type
    // Rolling-window accumulators — reset to zero after each summary is sent.
    float    levelPctAccum;                 // running sum → window average
    float    distMmAccum;                   // running sum → window average
    float    pumpAmpsAccum;
    float    battVAccum;
    // Per-metric sample counters for the current summary window.
    // uint16_t: worst case = ENV_SUMMARY_MIN_MAX * 60 / ENV_SAMPLE_SEC_MIN = 1440 samples.
    uint16_t validLevelSamples;
    uint16_t validDistSamples;
    uint16_t validPumpSamples;
    uint16_t validBattSamples;
    // alertsSinceLastSummary: promoted to uint16_t, saturated on increment.
    // With alert_cooldown_sec=60 and summary_interval_min=1440 (env-var maxima),
    // up to 4320 alerts can fire per window — far beyond uint8_t's range of 255.
    // The "alerts" template field uses type 12 (int16_t, −32 768..+32 767);
    // the 4320 worst-case count is well below that ceiling, so no overflow
    // occurs when the value is cast to int in sendSummary().
    uint16_t alertsSinceLastSummary;
    // batteryLowActive: edge-triggered state for the battery_low alert.
    // Set true on first crossing below battery_alert_v; cleared only after
    // voltage recovers above battery_alert_v + BATTERY_LOW_HYSTERESIS_V.
    // Prevents every 60-min emergency-sleep wake from firing a sync:true alert
    // while the battery is persistently low — the opposite of low-power behavior.
    bool     batteryLowActive;
    // templatesInstalled: set true after note.template registration succeeds.
    // Checked on each restored wake so a cold-boot failure is retried.
    bool     templatesInstalled;
    uint32_t appliedSummaryIntervalMin;     // outbound value last issued to hub.set
    // ── Cached last-known-good Notehub env-var values ─────────────────────────
    // Persisted across card.attn sleeps so that a transient env.get failure
    // never reverts thresholds or cadence to compile-time defaults for that
    // wake cycle.  Initialised to compile-time defaults on cold boot; updated
    // on every wake where fetchEnvOverrides() returns true.
    uint32_t envTankDepthMm;
    uint32_t envSensorMinMm;
    uint8_t  envLevelAlertPct;
    uint8_t  envLevelCriticalPct;
    float    envPumpOnAmps;
    float    envBatteryAlertV;
    uint32_t envSampleIntervalSec;
    uint32_t envSummaryIntervalMin;
    uint32_t envAlertCooldownSec;
};

// ── Shared globals (defined in the .ino, referenced by helpers) ───────────────
extern Notecard  notecard;
extern GlobalState g;
extern uint32_t  g_tankDepthMm;
extern uint32_t  g_sensorMinMm;
extern uint8_t   g_levelAlertPct;
extern uint8_t   g_levelCriticalPct;
extern float     g_pumpOnAmps;
extern float     g_batteryAlertV;
extern uint32_t  g_sampleIntervalSec;
extern uint32_t  g_summaryIntervalMin;
extern uint32_t  g_alertCooldownSec;

// ── Helper function prototypes ────────────────────────────────────────────────
uint32_t clampU32(long v, uint32_t minv, uint32_t maxv, uint32_t fallback);
float    clampF(double v, float minv, float maxv, float fallback);
// Returns true when env.get succeeded and the response body was valid;
// false on any I²C, allocation, or parse failure.  Callers must not treat
// a false return as a valid env read — g_* remain at last-known-good values.
bool     fetchEnvOverrides(void);
float    readDistanceMm(void);
float    readLevelPct(float distanceMm);
float    readPumpAmps(void);
float    readBatteryV(void);
void     evaluateAlerts(float levelPct, float pumpAmps, float battV, uint32_t now);
bool     sendAlert(int alertCode, float levelPct, float pumpAmps, float battV);
bool     sendSummary(float levelPct, float distMm, float pumpAmps, float battV);
