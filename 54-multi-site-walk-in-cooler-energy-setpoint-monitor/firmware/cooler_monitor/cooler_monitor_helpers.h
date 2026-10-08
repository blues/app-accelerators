#pragma once
// cooler_monitor_helpers.h — shared constants, types, and declarations for the
// multi-site walk-in cooler energy & setpoint monitor.
//
// See cooler_monitor.ino for the full hardware wiring diagram and environment-
// variable reference.  Helper function implementations live in
// cooler_monitor_helpers.cpp.

#include <Arduino.h>
#include <Notecard.h>
#include <OneWire.h>
#include <DallasTemperature.h>

// ── Compile-time configuration ─────────────────────────────────────────────

#ifndef PRODUCT_UID
#define PRODUCT_UID ""  // "com.your-company.your-name:your_project"
#pragma message "PRODUCT_UID not set — paste your Notehub ProductUID here before flashing."
#endif

// Compile-time debug output — uncomment to enable tracing during bench work.
// Output goes to debugSerial, the LPUART on the Notecarrier CX debug jack
// (ST-LINK virtual COM port); USB CDC is disabled (usb=none) so the host can
// stay in STOP2.  Leave commented out in production: serializing every
// Notecard transaction over UART adds awake time on every wake.
// #define DEBUG_SERIAL

#ifdef DEBUG_SERIAL
extern Uart debugSerial;   // defined in cooler_monitor.ino
#  define DBG_BEGIN(baud)  debugSerial.begin(baud)
#  define DBG_SET_STREAM() notecard.setDebugOutputStream(debugSerial)
#  define DBG_PRINT(...)   debugSerial.print(__VA_ARGS__)
#  define DBG_PRINTLN(...) debugSerial.println(__VA_ARGS__)
#else
#  define DBG_BEGIN(baud)  do {} while (0)
#  define DBG_SET_STREAM() do {} while (0)
#  define DBG_PRINT(...)   do {} while (0)
#  define DBG_PRINTLN(...) do {} while (0)
#endif

// GPIO assignments (Notecarrier CX dual 16-pin header)
#define PIN_DS18B20  5    // D5 — OneWire data line; needs 4.7 kΩ pull-up to 3V3
#define PIN_CT       A0   // A0 — CT bias-circuit output (SCT-013-030 + 2×10 kΩ + 10 µF)
#define PIN_DOOR     6    // D6 — Reed switch, INPUT_PULLUP; LOW = closed, HIGH = open
// D9 — Notecard ATTN wake input (jumpered from the ATTN header pin); D5 is
// taken by the DS18B20, so CX_ATTN_PIN is overridden in cooler_monitor.ino.

// Current-transformer scaling (SCT-013-030 with built-in burden resistor)
// 150 ms ≈ 9 complete 60 Hz mains cycles (one cycle ≈ 16.7 ms).
#define CT_SAMPLE_MS       150
#define CT_AMPS_PER_VOLT   30.0f  // 30 A / 1 V RMS (SCT-013-030 spec)
#define ADC_BITS           12     // must match analogReadResolution(12) in setup()
#define VREF_V             3.3f

// Firmware defaults — all overridable via Notehub environment variables
#define DEFAULT_SAMPLE_INTERVAL_SEC   60u
#define DEFAULT_SUMMARY_INTERVAL_MIN  60u
#define DEFAULT_TEMP_SETPOINT_F       35.0f
#define DEFAULT_TEMP_ALERT_F          40.0f
#define DEFAULT_DOOR_ALERT_SEC        300u
#define DEFAULT_COMPRESSOR_ON_AMPS    2.0f
#define DEFAULT_VOLTS_NOMINAL         120.0f

// One alert per type per 30-minute cooldown window
#define ALERT_COOLDOWN_SEC  1800u

// Notefiles
#define FILE_SUMMARY  "cooler_summary.qo"
#define FILE_ALERT    "cooler_alert.qo"

// ── Application state ──────────────────────────────────────────────────────
// Lives in RAM.  STOP2 retains SRAM, so this survives every sleep/wake cycle
// and is reset only by a power cycle or reset (which re-runs setup()).

struct AppState {
    // Summary cadence: sum of scheduled sample intervals (prevSampleSec ticks)
    // elapsed since the last summary was sent.  Awake time is excluded, so this
    // tracks scheduled sleep time rather than true wall-clock elapsed time.
    // Reset to zero after each successful summary.  When sample_interval_sec
    // does not evenly divide summary_interval_min×60, the window overshoots by
    // at most one sample period; window_sec in the emitted Note carries this
    // measured sum so downstream rate calculations use the correct denominator.
    uint32_t elapsedSecSinceSummary;

    // Per-window energy and runtime accumulators
    float    kwhAccum;               // kWh accumulated this window
    uint32_t compressorRunSec;       // compressor-on seconds this window
    uint32_t doorOpenSec;            // total door-open seconds this window
    uint16_t doorOpenCount;          // door-open events (low→high transitions)

    // Alert cooldowns in wall-clock seconds.  Storing seconds (not sample counts)
    // keeps timing stable across sample_interval_sec changes.
    uint32_t doorAlertCooldownSec;
    uint32_t tempAlertCooldownSec;

    // Edge-detection and continuous-open alert tracking
    uint8_t  prevDoorOpen;           // door state at previous wake (0 or 1)
    uint32_t doorContinuousOpenSec;  // unbroken open span; resets to 0 on close

    // Tracks the last hub.set outbound cadence so re-send is skipped when
    // summary_interval_min has not changed.
    uint32_t appliedSummaryMin;

    // Actual sleep duration of the completed interval.  Set to cfgSampleSec at
    // the end of each runSampleCycle() so the next wake correctly accounts for
    // elapsed time independent of any in-flight env-var change.  Zero on first boot.
    uint32_t prevSampleSec;

    // Window-average accumulators: running sums + per-metric valid-sample counts.
    // A failed sensor read (NAN) increments neither sum nor count, so it cannot
    // bias the average or produce a spurious sentinel in a window where other
    // reads succeeded.
    float    tempFSum;
    uint16_t tempFCount;
    float    ampsSum;
    uint16_t ampsCount;

    // Set to 1 after both note.template calls succeed; retried on every wake
    // until confirmed so a transient I²C failure never leaves the device
    // running with untemplated Notes indefinitely.
    uint8_t  templatesRegistered;

    // Last-known-good configuration, mirrored from the cfg globals whenever
    // env.get returns a valid response.  The cfg globals themselves stay in
    // RAM across sleeps, so a transient inbound-sync failure cannot silently
    // revert operator-tuned values to compile-time defaults.
    uint32_t persistedSampleSec;
    uint32_t persistedSummaryMin;
    float    persistedTempSetpointF;
    float    persistedTempAlertF;
    uint32_t persistedDoorAlertSec;
    float    persistedCompressorOnAmps;
    float    persistedVoltsNominal;
    uint8_t  configPersisted;    // non-zero once a successful env.get has run

    // Set to 1 the first time hubConfigure() returns true (hub.set
    // acknowledged by the Notecard).  While this is 0, every wake
    // retries hubConfigure() unconditionally in loop() — independent of
    // env.get success — so a transient I²C failure can never leave the
    // device permanently unassociated and silently queueing Notes.
    // Once set, the device falls back to the cadence-only re-application path
    // (applyHubSetIfChanged).
    uint8_t  hubSetConfirmed;    // non-zero once hub.set has been acknowledged
};

// ── Globals defined in cooler_monitor.ino ─────────────────────────────────

extern Notecard          notecard;
extern DallasTemperature probe;

extern uint32_t cfgSampleSec;
extern uint32_t cfgSummaryMin;
extern float    cfgTempSetpointF;
extern float    cfgTempAlertF;
extern uint32_t cfgDoorAlertSec;
extern float    cfgCompressorOnAmps;
extern float    cfgVoltsNominal;

// ── Helper function declarations ───────────────────────────────────────────
// Implementations in cooler_monitor_helpers.cpp

bool  hubConfigure();
bool  defineTemplates();
bool  fetchEnvOverrides(AppState &s);
void  applyHubSetIfChanged(AppState &s);
float readBoxTempF();
float readCompressorAmps();
bool  readDoorOpen();
bool  sendAlert(const char *alert, float tempF, float amps, uint32_t doorSec);
bool  sendSummary(AppState &s, uint32_t windowSec);
