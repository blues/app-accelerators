/*
 * creek_flood_gauge_helpers.h
 *
 * Shared types, constants, extern declarations, and function prototypes for
 * the Creek / Culvert Flood-Warning Stream Gauge firmware.
 * Included by creek_flood_gauge.ino and creek_flood_gauge_helpers.cpp.
 */
#pragma once

#include <Arduino.h>
#include <Notecard.h>

// ---------------------------------------------------------------------------
// Debug output — uncomment the #define below to enable Serial logging in
// both the main sketch and all helper functions.  Leave it commented for
// production builds: eliminating Serial output reduces awake time and keeps
// the active-current trace clean when validating power consumption with Mojo.
// ---------------------------------------------------------------------------
// #define DEBUG_SERIAL

#ifdef DEBUG_SERIAL
#  define DBG_BEGIN(baud)  do { Serial.begin(baud); } while (0)
#  define DBG_NOTECARD()   do { notecard.setDebugOutputStream(Serial); } while (0)
#  define DBG_PRINT(...)   Serial.print(__VA_ARGS__)
#  define DBG_PRINTLN(...) Serial.println(__VA_ARGS__)
#else
#  define DBG_BEGIN(baud)  do {} while (0)
#  define DBG_NOTECARD()   do {} while (0)
#  define DBG_PRINT(...)   do {} while (0)
#  define DBG_PRINTLN(...) do {} while (0)
#endif

// ---------------------------------------------------------------------------
// Pin assignments (Notecarrier CX dual-16-pin header)
// ---------------------------------------------------------------------------
#define PIN_RAIN_GAUGE    5   // D5 — rain gauge reed switch (pull-up, active LOW)
// MB7389 VCC is wired to +3V3_OUT. On Notecarrier CX the EN pin gates the
// board's host 3.3V rail (including +3V3_OUT), so the sensor powers off
// during card.attn sleep and powers back on with the Cygnet at each wake.
// The I2C transactions in setup()/loop() before the first read exceed the
// MB7389 startup time, so no explicit warm-up delay is needed.

// ---------------------------------------------------------------------------
// Notefile names
// ---------------------------------------------------------------------------
#define NOTEFILE_SUMMARY  "gauge_reading.qo"
#define NOTEFILE_ALERT    "gauge_alert.qo"
// One-time commissioning diagnostics (e.g., gnss_timeout) are routed to a
// separate Notefile so downstream consumers never have to special-case a
// commissioning fault mixed in with threshold-based flood alerts.
// gauge_fault.qo is not subject to the alert cooldown; configure a dedicated
// Notehub route for it pointing to an installer notification endpoint.
#define NOTEFILE_FAULT    "gauge_fault.qo"

// ---------------------------------------------------------------------------
// Sensor and protocol constants
// ---------------------------------------------------------------------------
#define MB7389_BAUD       9600   // UART baud rate for MB7389 (also used in setup())
#define HISTORY_LEN          8   // rolling level readings kept for rate calc
#define RAIN_POLL_MS      3000   // observation window (ms) counted per wake
#define STATE_SEG_ID      "CGST" // segment ID for NotePayload sleep state

// GNSS commissioning bounds.
// Phase 1 (continuous mode) runs until a fix is confirmed.  If no fix is
// obtained within GNSS_COMMISSION_TIMEOUT_SEC the firmware:
//   • falls back from continuous to a lower-duty periodic retry cadence
//     (GNSS_RETRY_PERIOD_SEC) to reduce the elevated GNSS-active power draw
//     on the solar budget;
//   • emits a "gnss_timeout" alert to Notehub so the installer can diagnose
//     the sky-view problem without a site visit.
// The device continues sampling and evaluating water-level thresholds in the
// degraded (no-NTN) state; only Skylo satellite fallback is unavailable until
// a fix is finally confirmed and Phase 2 (daily periodic re-check) begins.
#define GNSS_COMMISSION_TIMEOUT_SEC  1800  // 30-minute continuous-GNSS window
#define GNSS_RETRY_PERIOD_SEC         300  // 5-min periodic retry after timeout

// ---------------------------------------------------------------------------
// Hub / radio strategy
//
// "periodic": the Notecard schedules outbound syncs on the outbound interval
// (g_summaryIntervalMin, default 60 min).  Summary notes batch into the next
// scheduled session — no radio wake until the timer fires.  Alert notes carry
// sync:true, bypassing the timer for an immediate sync so flood warnings get
// through without waiting for the next scheduled window.
//
// Inbound is intentionally decoupled from outbound: g_inboundIntervalMin
// defaults to 1440 min (24 h) rather than matching the outbound cadence.
// Each inbound session is a radio transaction that consumes NTN budget when
// cellular is unavailable; polling for env-var updates at 60-minute intervals
// over Skylo satellite would burn the 10 KB budget on control traffic, not
// alert delivery.  24-hour inbound is appropriate for a static field device
// where threshold changes are rare; lower it via inbound_interval_min only
// when rapid remote reconfiguration is needed.
//
// Change to "continuous" during development for immediate log visibility.
// ---------------------------------------------------------------------------
#define HUB_MODE          "periodic"

// ---------------------------------------------------------------------------
// Persistent application state (survives card.attn sleep via NotePayload)
// ---------------------------------------------------------------------------
struct AppState {
    // Rolling water-level history: circular buffer.  historyIdx is the next
    // write slot; historyCount is the number of valid entries (0..HISTORY_LEN).
    // Each slot stores the sensor distance reading and the Notecard epoch at
    // insertion time so the rate calculator uses actual elapsed time rather
    // than assuming a constant sample cadence.
    float    levelHistoryMm[HISTORY_LEN];
    uint32_t levelHistoryTimeSec[HISTORY_LEN]; // Notecard epoch at insert; 0 = unavailable
    uint8_t  historyIdx;                        // next write position
    uint8_t  historyCount;                      // valid entries (0..HISTORY_LEN)
    uint32_t historyIntervalSec;                // g_sampleIntervalSec when history was last
                                                // built; history resets when this changes

    uint32_t totalTips;           // cumulative rain tips since boot

    // Epoch-based scheduler state (valid when Notecard has time-sync)
    uint32_t lastAlertSec;        // epoch of last successfully queued alert note
    uint32_t lastSummarySec;      // epoch of last successfully queued summary note

    // Wake-count fallback scheduler.
    // Used when Notecard epoch is 0 (no time sync yet, or transient error).
    // Prevents two failure modes of relying on epoch==0:
    //   (a) nowSec==0 with lastAlertSec==0 would make cooldownOk true every
    //       single wake, emitting sync:true alerts on every cycle during outages.
    //   (b) After an epoch-based alert, lastAlertSec stays 0 on the next wake
    //       if time is still unavailable, silently re-opening the cooldown.
    uint32_t wakeCount;           // incremented on every loop() entry
    uint32_t lastAlertWake;       // wakeCount at the last successfully queued alert
    uint32_t lastSummaryWake;     // wakeCount at the last successfully queued summary

    // Tracks the outbound and inbound intervals last committed to the Notecard
    // via hub.set.  Compared against g_summaryIntervalMin / g_inboundIntervalMin
    // after each env.get; hub.set is re-issued when either value changes.
    uint32_t lastAppliedOutboundMin;
    uint32_t lastAppliedInboundMin;

    // Set after hubConfigure(), defineTemplates(), and configureSkyloTransport()
    // all succeed on first boot.  If any step fails (e.g., Notecard I2C
    // enumeration race on cold boot), this stays false; loop() skips sensing
    // and note emission, persists state, and sleeps so setup is retried on the
    // next wake rather than continuing unconfigured.
    bool     setupDone;

    // Set true once card.location has confirmed a valid GNSS fix is stored in
    // Notecard flash.  A valid fix is a hard prerequisite for Skylo NTN
    // satellite sessions: the modem cannot register with the network without
    // knowing its location.  The Notecard retains the fix across power cycles,
    // so this passes immediately on every subsequent boot after the first
    // successful outdoor acquisition.  Re-checked on each wake while false so
    // a power-cycle before the first fix does not permanently silence the
    // diagnostic.
    bool     locationOk;

    // GNSS commissioning bounds — support bounded Phase 1 / Phase 2 strategy.
    //
    // gnssCommissionStartSec: Notecard epoch when continuous commissioning
    //   began; 0 until the first wake that has a valid Notecard time.
    //   Primary timeout mechanism: accurate wall-clock bound when available.
    //
    // gnssCommissionStartWake: wakeCount+1 at the first no-fix wake, latched
    //   regardless of whether the Notecard has time-sync.  0 = not yet set.
    //   Secondary timeout mechanism: guarantees the 30-minute cap fires even
    //   before the Notecard has contacted Notehub for its first timestamp.
    //   The window expires when either mechanism reaches GNSS_COMMISSION_TIMEOUT_SEC.
    //
    // gnssCommissionTimedOut: set true once the continuous window has expired
    //   and the fallback periodic retry mode has been applied (and the
    //   gnss_timeout fault note has been emitted to gauge_fault.qo).
    //   Not reset until locationOk becomes true.
    uint32_t gnssCommissionStartSec;
    uint32_t gnssCommissionStartWake;
    bool     gnssCommissionTimedOut;

    // Persisted effective configuration — written to AppState whenever
    // env.get succeeds, then pre-loaded on the next wake before
    // fetchEnvOverrides() runs.  A transient env.get failure therefore
    // leaves the last successfully synced thresholds in effect rather
    // than silently reverting to compile-time defaults.
    bool     cfgValid;
    uint32_t cfgSampleIntervalSec;
    uint32_t cfgSummaryIntervalMin;
    uint32_t cfgInboundIntervalMin;
    float    cfgLevelWarningMm;
    float    cfgLevelCriticalMm;
    float    cfgRateWarningMmPerMin;
    uint32_t cfgRainIntenseTips;
    float    cfgSensorHeightMm;
    uint32_t cfgAlertCooldownSec;
};

// ---------------------------------------------------------------------------
// Shared globals — defined in creek_flood_gauge.ino, referenced by helpers
// ---------------------------------------------------------------------------
extern AppState  g_state;
extern Notecard  notecard;
extern uint32_t  g_sampleIntervalSec;
extern uint32_t  g_summaryIntervalMin;
extern uint32_t  g_inboundIntervalMin;
extern float     g_levelWarningMm;
extern float     g_levelCriticalMm;
extern float     g_rateWarningMmPerMin;
extern uint32_t  g_rainIntenseTips;
extern float     g_sensorHeightMm;
extern uint32_t  g_alertCooldownSec;

// ---------------------------------------------------------------------------
// Helper function prototypes (implemented in creek_flood_gauge_helpers.cpp)
//
// hubConfigure() takes an optional productUid: pass PRODUCT_UID on first
// boot; pass NULL when re-issuing hub.set to update intervals only (the
// Notecard retains the previously configured product UID in flash).
// ---------------------------------------------------------------------------
bool        hubConfigure(const char *productUid);
bool        configureSkyloTransport();
bool        checkLocationAcquired();
bool        defineTemplates();
void        fetchEnvOverrides();
float       readWaterLevelMm();
uint32_t    countRainTips(uint32_t windowMs);
void        updateHistory(float levelMm, uint32_t timeSec);
float       calcRisingRateMmPerMin();
uint32_t    notecardTimeSec();
const char *trendLabel(float rateMmPerMin);
bool        sendAlert(const char *kind, float levelMm, float rateMmPerMin,
                      uint32_t tips, float waterDepthMm);
bool        sendSummary(float levelMm, float rateMmPerMin,
                        uint32_t windowTips, float waterDepthMm);
void        sleepUntilNextSample();
uint32_t    clampU32(float v, uint32_t lo, uint32_t hi, uint32_t fallback);
float       clampF(float v, float lo, float hi, float fallback);
