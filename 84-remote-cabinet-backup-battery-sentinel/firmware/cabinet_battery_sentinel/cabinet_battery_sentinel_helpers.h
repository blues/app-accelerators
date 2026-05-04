// ============================================================================
// cabinet_battery_sentinel_helpers.h
//
// Shared configuration, types, extern globals, and function prototypes for
// Blues Application Accelerator #84 — Remote Cabinet Backup Battery Sentinel.
//
// ► Set PRODUCT_UID below before flashing.
// ============================================================================
#pragma once

#include <Notecard.h>
#include <Adafruit_INA228.h>
#include <Wire.h>
#include <math.h>
#include <string.h>   // memset()

// ─── Product UID ─────────────────────────────────────────────────────────────
#ifndef PRODUCT_UID
#define PRODUCT_UID ""  // ← Paste your Notehub ProductUID here, then flash.
#endif
// Empty PRODUCT_UID is caught at runtime in setup() before any telemetry runs.

// ─── Hardware pins ───────────────────────────────────────────────────────────
// NTC thermistor voltage divider: +3V3 → 10 kΩ → A0 → NTC → GND
#define NTC_PIN  A0

// ─── Timing defaults ─────────────────────────────────────────────────────────
// Float batteries change slowly; 2-minute samples are more than sufficient.
// Overridable at runtime via Notehub environment variables.
#define DEFAULT_SAMPLE_INTERVAL_SEC   120U   // 2 minutes between sensor wakes
#define DEFAULT_SUMMARY_INTERVAL_MIN   60U   // hourly summary note

// ─── Alert cooldown (time-based) ─────────────────────────────────────────────
// Stored in the state struct as remaining seconds; decremented by g_sampleSec
// on each wake so the 30-minute suppression window is cadence-independent.
#define ALERT_COOLDOWN_SEC        1800U  // 30 min between repeated alerts

// Post-discharge settling: float_current_high suppressed for this long after
// the last discharge sample so normal bulk-recharge current is not
// misclassified as the elevated float current that signals VRLA degradation.
#define POST_DISCHARGE_SETTLE_SEC 1800U  // 30 min after discharge ends

// ─── State-of-charge estimation (coulomb counting) ───────────────────────────
// Operators commission usable_capacity_ah at installation (nameplate capacity
// or measured from a timed full-discharge cycle).  soc_pct_init is set once
// after a known-full charge; the firmware tracks SoC from that baseline by
// integrating current.  soc_low_pct fires the soc_low alert when SoC falls
// below the threshold.  A changed soc_pct_init value (detected by comparing
// to lastSocInit in the state struct) forces a one-shot reinitialisation so
// the operator can recalibrate after a known-full charge without reflashing.
#define DEFAULT_USABLE_CAPACITY_AH  100.0f   // Ah — common cabinet VRLA size
#define DEFAULT_SOC_LOW_PCT          20.0f   // % — alert threshold
#define DEFAULT_SOC_PCT_INIT         -1.0f   // negative = not yet commissioned

// ─── Default alert thresholds (all env-var overridable) ──────────────────────
#define DEFAULT_VOLT_MIN_V        13.2f   // V  — float-voltage low  (12 V VRLA)
#define DEFAULT_VOLT_MAX_V        14.8f   // V  — float-voltage high (12 V VRLA)
#define DEFAULT_FLOAT_CURR_HI_MA  500.0f  // mA — elevated float current
#define DEFAULT_TEMP_ALERT_C       40.0f  // °C — pack surface temperature
#define DEFAULT_DISCHARGE_MA      -200.0f // mA — below this = power outage

// ─── INA228 calibration ───────────────────────────────────────────────────────
// Adafruit #5832 ships with an onboard 15 mΩ shunt; conservative max ~8 A.
// For higher-current installations use the external-shunt footprint and update
// both constants to match.
#define INA228_I2C_ADDR      0x40    // default address; A0/A1 both GND
#define INA228_SHUNT_OHMS    0.015f  // 15 mΩ onboard shunt
#define INA228_MAX_CURRENT_A 8.0f    // A — full-scale calibration

// ─── NTC thermistor — β-equation, pull-up topology ───────────────────────────
#define NTC_R_PULLUP  10000.0f  // Ω — series resistor to +3V3
#define NTC_R0        10000.0f  // Ω — nominal resistance at T0
#define NTC_BETA       3950.0f  // K — characteristic temperature coefficient
#define NTC_T0_K        298.15f // K — reference temperature (25 °C)
#define NTC_VCC           3.3f  // V — supply rail
#define ADC_FULL          4095  // 12-bit ADC on Cygnet (set via analogReadResolution)

// ─── Notefiles ───────────────────────────────────────────────────────────────
#define NOTEFILE_SUMMARY  "battery_summary.qo"
#define NOTEFILE_ALERT    "battery_alert.qo"
#define STATE_SEG_ID      "BSNT"

// ─── Sentinel and window-extreme initializers ─────────────────────────────────
// SUMMARY_INVALID_SENTINEL replaces NAN in Note bodies (NAN is not valid JSON).
// Chosen well outside any physical range so analytics can distinguish
// "sensor failed" from a true near-zero reading.
#define SUMMARY_INVALID_SENTINEL  -9999.0f
#define VOLT_MIN_INIT   99.0f
#define CURR_MIN_INIT    0.0f   // 0 so the first real discharge pulls it negative
#define TEMP_MAX_INIT  -99.0f

// ─── Persistent state (serialised into Notecard flash across sleep cycles) ───
struct SentinelState {
    // Per-metric sums, valid-sample counts, and window extremes.
    // Tracked independently so a failed sensor never suppresses accumulation
    // or alert evaluation for the other metrics.
    float    voltSum;   uint32_t voltCount;  float voltMin;
    float    currSum;   uint32_t currCount;  float currMin;
                                             // currMin: most-negative current seen
                                             //   in the window (deepest discharge);
                                             //   stays 0.0 on float-only windows
    float    tempSum;   uint32_t tempCount;  float tempMax;
    float    chargeAh;  // net coulombs this summary window (Ah); negative = net discharge.
                     // This is NOT state-of-charge: it is a per-window delta only.
                     // Use socPct below for the running SoC estimate.

    // State-of-charge estimate (%).  Initialised from the soc_pct_init env var
    // when the operator sets it after a known-full charge; subsequently tracked
    // by integrating current each sample.  −1.0 = not yet commissioned (the
    // operator has not set soc_pct_init; soc_pct in summary notes will carry
    // SUMMARY_INVALID_SENTINEL until commissioned).
    float    socPct;
    // Last soc_pct_init value applied.  Compared against g_socPctInit on every
    // wake: a changed value re-initialises socPct so the operator can recalibrate
    // after a known-full charge by updating the env var without reflashing.
    float    lastSocInit;
    uint32_t coolSocLowSec;  // per-alert cooldown for soc_low

    // Elapsed seconds in the current summary window.  Closed by wall-clock time
    // so the interval is stable even when samples are skipped or cadence changes.
    uint32_t windowElapsedSec;

    // Per-alert cooldown remaining (seconds).  Decremented by g_sampleSec each
    // wake; alert fires when it reaches 0.  Storing seconds keeps the real-world
    // suppression window at 30 minutes regardless of sampling cadence.
    uint32_t coolVoltLowSec;
    uint32_t coolVoltHighSec;
    uint32_t coolCurrHighSec;
    uint32_t coolTempHighSec;
    uint32_t coolOutageSec;
    uint32_t coolInaFaultSec;   // rate-limits ina228_unreachable remote alert

    // Post-discharge settling window remaining (seconds).  Reloaded to
    // POST_DISCHARGE_SETTLE_SEC on every discharge sample so the countdown
    // starts from the *last* discharge sample, not the first.
    uint32_t postDischargeSec;

    // Last hub.set outbound cadence and sample interval written to the Notecard.
    uint32_t lastSummaryMin;
    uint32_t lastSampleSec;

    // Configuration-success flags.  Cleared by first-boot memset; set only
    // after confirmed delivery.
    bool hubConfigured;
    bool templateDefined;
};

// ─── Extern globals (defined in cabinet_battery_sentinel.ino) ────────────────
extern Notecard        notecard;
extern Adafruit_INA228 ina228;

extern uint32_t g_sampleSec;
extern uint32_t g_summaryMin;
extern float    g_voltMinV;
extern float    g_voltMaxV;
extern float    g_floatCurrHiMa;
extern float    g_tempAlertC;
extern float    g_dischargeMa;
extern float    g_usableCapacityAh;  // Ah — battery bank nameplate (for SoC)
extern float    g_socLowPct;         // % — soc_low alert threshold
extern float    g_socPctInit;        // % — operator-set SoC starting point; <0 = unset

extern SentinelState state;

// ─── Helper function prototypes ───────────────────────────────────────────────
void  doFirstBoot(void);
void  notecardConfigure(void);
void  defineTemplate(void);
void  fetchEnvOverrides(void);
bool  initINA228(void);
float readBatteryVoltage(void);
float readBatteryCurrent(void);   // positive = float current; negative = discharge
float readPackTempC(void);
bool  sendSummary(void);
// Returns true when the note.add request was accepted by the Notecard.
// Callers must only arm the per-alert cooldown on a true return so that a
// transient I2C or Notecard fault at alert time does not silently suppress
// the alert for the full cooldown window.
bool  sendAlert(const char *alertType, float volt, float curr, float temp);
void  sleepHost(void);
