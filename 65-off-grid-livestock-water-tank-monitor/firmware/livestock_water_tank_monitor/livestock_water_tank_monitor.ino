/*
 * livestock_water_tank_monitor.ino
 * Blues Application Example — Off-Grid Livestock Water Tank Monitor
 *
 * Monitors a remote stock tank's water level, submersible pump current, and
 * solar battery voltage. The Notecarrier CX's STM32L433 host wakes every
 * 15 minutes (configurable), reads three analog sensors, evaluates alert
 * thresholds, accumulates a rolling average for the current summary window, and
 * returns to sleep. A template-encoded summary Note is queued every 4 hours;
 * immediate-sync alert Notes are emitted when any threshold trips.
 *
 * Sleep: between samples the host sits in STM32 STOP2 (~1-2 µA, RAM retained)
 * and is woken by the Notecard's ATTN pin — card.attn "sleep" holds ATTN low
 * for the sample interval and raises it when the next sample is due. Jumper
 * the Notecarrier CX ATTN pin to D5 (same 16-pin header); leave EN
 * unconnected. See cx_sleep.h for the rationale and wiring.
 *
 * Build: Tools > USB support (if available) > None (usb=none). With the USB
 * CDC stack enabled and no USB host attached the host cannot stay in STOP2.
 * Debug output goes to the LPUART on the CX debug jack (ST-LINK VCP).
 *
 * Hardware:
 *   Blues Notecarrier CX (STM32L433 host MCU)
 *   Blues Notecard for Skylo (NOTE-NBGLWX) in M.2 slot — cellular-first, satellite fallback
 *   MaxBotix HRXL-MaxSonar-WRL (MB7389) — tank level, analog output on A0
 *   SCT-013-030 CT + 2×10kΩ bias divider + 10µF cap — pump current on A1
 *   BSS84 PMOS + MMBT3904 NPN + 47kΩ/10kΩ switched divider — battery voltage on A2 (enable: A3)
 *   ATTN → D5 jumper — Notecard wakes the host from STOP2
 *   Blues Mojo — bench energy validation only (not read at runtime)
 *
 * See README.md for full wiring, Notehub setup, and calibration instructions.
 *
 * Sensor/env-var helpers are in livestock_water_tank_monitor_helpers.h/.cpp.
 */

#include <Notecard.h>
#include "livestock_water_tank_monitor_helpers.h"
#include "cx_sleep.h"

#ifndef PRODUCT_UID
#define PRODUCT_UID ""  // replace with your Notehub ProductUID
#pragma message "PRODUCT_UID is not defined. Set it to your Notehub project identifier."
#endif

// ── Debug output ──────────────────────────────────────────────────────────────
// USB CDC is disabled in this build, so Serial is not USB. Logging (when
// TANK_MONITOR_DEBUG is defined) goes to LPUART1 on the Notecarrier CX debug
// jack, which an ST-LINK V3 exposes as a virtual COM port.
#ifdef TANK_MONITOR_DEBUG
Uart dbgSerial(PIN_VCP_RX, PIN_VCP_TX);
#endif

// ── Runtime parameters (loaded from GlobalState env cache each wake) ──────────
// Declared extern in helpers.h; defined here so both translation units share
// the same storage. Declaration values are compile-time defaults and are
// overwritten at the top of every loop() pass from g.env* (last-known-good
// values) before fetchEnvOverrides() is called, so a transient env.get
// failure never reverts thresholds to these defaults for that wake cycle.
Notecard  notecard;
GlobalState g;

uint32_t g_tankDepthMm        = DEFAULT_TANK_DEPTH_MM;
uint32_t g_sensorMinMm        = DEFAULT_SENSOR_MIN_MM;
uint8_t  g_levelAlertPct      = DEFAULT_LEVEL_ALERT_PCT;
uint8_t  g_levelCriticalPct   = DEFAULT_LEVEL_CRITICAL_PCT;
float    g_pumpOnAmps         = DEFAULT_PUMP_ON_AMPS;
float    g_batteryAlertV      = DEFAULT_BATTERY_ALERT_V;
uint32_t g_sampleIntervalSec  = DEFAULT_SAMPLE_INTERVAL_SEC;
uint32_t g_summaryIntervalMin = DEFAULT_SUMMARY_INTERVAL_MIN;
uint32_t g_alertCooldownSec   = DEFAULT_ALERT_COOLDOWN_SEC;

// ── Forward declarations for sketch-local functions ───────────────────────────
static void notecardConfigure(void);
static bool reapplyHubSet(void);
static void defineTemplates(void);
static void doSleep(float battV);

// =============================================================================
// setup() runs once at power-up: it configures the Notecard, registers the
// Note templates, and arms the ATTN wake. The host resumes in place after each
// STOP2 sleep, so every per-wake step lives in loop().
void setup() {
#ifdef TANK_MONITOR_DEBUG
    dbgSerial.begin(115200);
    notecard.setDebugOutputStream(dbgSerial);
#endif

    notecard.begin();         // open I²C channel to Notecard
    analogReadResolution(12); // the STM32L433 host supports 12-bit ADC (0–4095)

    // Configure the battery-divider PMOS enable pin. The 100 kΩ gate pullup
    // already holds the PMOS off while the pin is undriven, but making the
    // output explicit and LOW here ensures the NPN transistor cannot be
    // inadvertently triggered by GPIO boot-state noise before readBatteryV()
    // is called. STOP2 retains GPIO state, so the pin stays LOW while asleep.
    pinMode(PIN_BATT_EN, OUTPUT);
    digitalWrite(PIN_BATT_EN, LOW);

    // Initialize state, configure the Notecard, and register Note templates.
    // Seed the env-var cache with compile-time defaults so g_* have valid
    // values on the first wake even if env.get fails on the initial
    // connection attempt.
    memset(&g, 0, sizeof(g));
    g.envTankDepthMm        = DEFAULT_TANK_DEPTH_MM;
    g.envSensorMinMm        = DEFAULT_SENSOR_MIN_MM;
    g.envLevelAlertPct      = DEFAULT_LEVEL_ALERT_PCT;
    g.envLevelCriticalPct   = DEFAULT_LEVEL_CRITICAL_PCT;
    g.envPumpOnAmps         = DEFAULT_PUMP_ON_AMPS;
    g.envBatteryAlertV      = DEFAULT_BATTERY_ALERT_V;
    g.envSampleIntervalSec  = DEFAULT_SAMPLE_INTERVAL_SEC;
    g.envSummaryIntervalMin = DEFAULT_SUMMARY_INTERVAL_MIN;
    g.envAlertCooldownSec   = DEFAULT_ALERT_COOLDOWN_SEC;
    notecardConfigure();
    defineTemplates();

    // Wake from STOP2 on the ATTN rising edge (ATTN jumpered to D5).
    cxSleepBegin();
}

// =============================================================================
// loop() runs one sample/wake cycle and then sleeps the host until the
// Notecard raises ATTN.
void loop() {
    // Template registration failed on a previous wake — retry now.
    // notecardConfigure() is not re-issued (hub.set is one-time setup that
    // persists in Notecard flash).
    if (!g.templatesInstalled) {
        defineTemplates();
    }

    // Load runtime parameters from the env-var cache. On the first wake these
    // equal the compile-time defaults seeded in setup(); on subsequent wakes
    // they carry the last-known-good Notehub values. This assignment runs
    // before fetchEnvOverrides() so that a transient env.get failure leaves
    // g_* at the last-known-good values rather than reverting to defaults.
    g_tankDepthMm        = g.envTankDepthMm;
    g_sensorMinMm        = g.envSensorMinMm;
    g_levelAlertPct      = g.envLevelAlertPct;
    g_levelCriticalPct   = g.envLevelCriticalPct;
    g_pumpOnAmps         = g.envPumpOnAmps;
    g_batteryAlertV      = g.envBatteryAlertV;
    g_sampleIntervalSec  = g.envSampleIntervalSec;
    g_summaryIntervalMin = g.envSummaryIntervalMin;
    g_alertCooldownSec   = g.envAlertCooldownSec;

    // Attempt a fresh env-var update from Notehub. Returns true only when
    // env.get responded with a valid body; on false, g_* keep the cache
    // values loaded above and g.env* is left unchanged.
    bool envOk = fetchEnvOverrides();

    // Re-apply hub.set only when a fresh env read confirms a changed cadence.
    // Guarding on envOk prevents a transient env.get failure from treating the
    // compile-time default (or stale cache default) as a new operator-intended
    // value, which would issue a spurious hub.set and flush accumulated samples.
    //
    // The accumulator/epoch reset is gated on a confirmed successful hub.set.
    // If the Notecard rejects the request (e.g. transient I²C fault, low memory),
    // g.appliedSummaryIntervalMin is left unchanged so reapplyHubSet() retries on
    // the next wake, and the already-collected samples are preserved so they can
    // still be emitted under the old cadence rather than silently discarded.
    // Once hub.set succeeds, lastSummaryEpoch is set to 0: the time-seed block
    // below re-anchors the window start the first time valid time is available,
    // guaranteeing the next summary covers exactly one full new-cadence window
    // and keeping the calibration workflow (set sample_interval_sec = 60 and
    // summary_interval_min = 5, wait one complete 5-minute window) reliable.
    if (envOk && g_summaryIntervalMin != g.appliedSummaryIntervalMin) {
        if (reapplyHubSet()) {
            g.lastSummaryEpoch   = 0;
            g.levelPctAccum      = 0.0f;
            g.distMmAccum        = 0.0f;
            g.pumpAmpsAccum      = 0.0f;
            g.battVAccum         = 0.0f;
            g.validLevelSamples  = 0;
            g.validDistSamples   = 0;
            g.validPumpSamples   = 0;
            g.validBattSamples   = 0;
        }
    }

    // Allow the MB7389 to complete a fresh ranging cycle before sampling. The
    // sensor outputs ~one reading per 100 ms; MB7389_SETTLE_MS provides margin
    // for first-conversion latency after power-up and for a current reading
    // on every subsequent wake.
    delay(MB7389_SETTLE_MS);

    // ── Read sensors ──────────────────────────────────────────────────────────
    float distMm   = readDistanceMm();
    float levelPct = readLevelPct(distMm);
    float pumpAmps = readPumpAmps();
    float battV    = readBatteryV();

    // Accumulate valid readings into the rolling summary window.
    if (levelPct >= 0.0f) { g.levelPctAccum += levelPct; g.validLevelSamples++; }
    if (distMm   >= 0.0f) { g.distMmAccum   += distMm;   g.validDistSamples++;  }
    if (pumpAmps >= 0.0f) { g.pumpAmpsAccum += pumpAmps; g.validPumpSamples++;  }
    if (battV    >  0.0f) { g.battVAccum    += battV;    g.validBattSamples++;  }

    // Get current Unix time for alert gating and summary scheduling.
    // Returns 0 if the Notecard has not yet synced and obtained network time.
    uint32_t now = 0;
    J *timeReq = notecard.newRequest("card.time");
    if (timeReq) {
        J *rsp = notecard.requestAndResponse(timeReq);
        if (rsp) {
            if (!notecard.responseError(rsp)) {
                now = (uint32_t)JGetNumber(rsp, "time");
            }
            notecard.deleteResponse(rsp);
        }
    }

    // Seed the summary window start epoch on the first successful time sync.
    // Also flush all accumulators at the same moment so the first emitted
    // summary represents exactly one clean post-sync interval. Without this
    // flush, readings collected before time was known (including those
    // accumulated earlier in this very wake) would inflate the first window
    // beyond one configured interval. The same path is re-entered after a
    // cadence change because reapplyHubSet() resets lastSummaryEpoch to 0.
    if (now > 0 && g.lastSummaryEpoch == 0) {
        g.lastSummaryEpoch   = now;
        g.levelPctAccum      = 0.0f;
        g.distMmAccum        = 0.0f;
        g.pumpAmpsAccum      = 0.0f;
        g.battVAccum         = 0.0f;
        g.validLevelSamples  = 0;
        g.validDistSamples   = 0;
        g.validPumpSamples   = 0;
        g.validBattSamples   = 0;
    }

    // Evaluate alert thresholds; emit sync:true Notes if any trip.
    // State is updated only after a confirmed successful queue operation.
    evaluateAlerts(levelPct, pumpAmps, battV, now);

    // Check if the summary window has elapsed; emit and reset if so.
    // Accumulator reset only occurs after the Note is confirmed queued so that
    // a transient Notecard error does not silently discard accumulated data.
    if (now > 0 &&
        (now - g.lastSummaryEpoch) >= (g_summaryIntervalMin * 60UL)) {

        float avgLevel = (g.validLevelSamples > 0)
            ? g.levelPctAccum / g.validLevelSamples : -1.0f;
        float avgDist  = (g.validDistSamples  > 0)
            ? g.distMmAccum   / g.validDistSamples  : -1.0f;
        float avgPump  = (g.validPumpSamples  > 0)
            ? g.pumpAmpsAccum / g.validPumpSamples  : -1.0f;
        float avgBatt  = (g.validBattSamples  > 0)
            ? g.battVAccum    / g.validBattSamples  : -1.0f;

        if (sendSummary(avgLevel, avgDist, avgPump, avgBatt)) {
            // Reset accumulators for the next window only after confirmed send.
            g.lastSummaryEpoch       = now;
            g.levelPctAccum          = 0.0f;
            g.distMmAccum            = 0.0f;
            g.pumpAmpsAccum          = 0.0f;
            g.battVAccum             = 0.0f;
            g.validLevelSamples      = 0;
            g.validDistSamples       = 0;
            g.validPumpSamples       = 0;
            g.validBattSamples       = 0;
            g.alertsSinceLastSummary = 0;
        }
    }

    // Sleep until the Notecard raises ATTN; execution resumes at the top of
    // loop() for the next sample cycle.
    doSleep(battV);
}

// =============================================================================
// Configure the Notecard for this project — called once from setup().
static void notecardConfigure(void) {
    // periodic mode: outbound and inbound cadences match the summary interval.
    J *req = notecard.newRequest("hub.set");
    if (req) {
        JAddStringToObject(req, "product",  PRODUCT_UID);
        JAddStringToObject(req, "mode",     "periodic");
        JAddNumberToObject(req, "outbound", (int)g_summaryIntervalMin);
        JAddNumberToObject(req, "inbound",  (int)g_summaryIntervalMin);
        // Record the applied interval only after a confirmed successful transaction.
        if (notecard.sendRequest(req)) {
            g.appliedSummaryIntervalMin = g_summaryIntervalMin;
        }
    }

    // Enable the Skylo satellite (NTN) transport. NTN is OFF by default on the
    // Notecard for Skylo (NOTE-NBGLWX) — the factory transport never engages the
    // satellite radio. Setting "wifi-cell-ntn" enables automatic WiFi → cellular
    // → Skylo-satellite fallback with no firmware branching: the Notecard prefers
    // WiFi, falls back to cellular, and finally to Skylo NTN at sites beyond
    // terrestrial coverage. Skylo requires at least one non-NTN (cellular/WiFi)
    // sync first to associate with Notehub and register templates before NTN can
    // be used; in periodic mode the cold-boot hub.set above triggers that initial
    // sync. This setting persists in the Notecard's own flash, so issuing it once
    // at cold boot is sufficient.
    req = notecard.newRequest("card.transport");
    if (req) {
        JAddStringToObject(req, "method", "wifi-cell-ntn");
        notecard.sendRequest(req);
    }

    // Disable the onboard accelerometer to eliminate interrupt noise on bench
    // power-trace measurements. Non-critical if this fails; idempotent.
    req = notecard.newRequest("card.motion.mode");
    if (req) {
        JAddBoolToObject(req, "stop", true);
        notecard.sendRequest(req);
    }
}

// =============================================================================
// Re-issue hub.set with the currently active summary interval — called when a
// Notehub env-var change updates g_summaryIntervalMin so that the Notecard's
// outbound cellular cadence stays aligned with the local summary schedule.
// Returns true on a confirmed successful transaction so the caller can safely
// discard accumulated samples; returns false on allocation or send failure so
// the caller keeps existing samples and retries on the next wake.
static bool reapplyHubSet(void) {
    J *req = notecard.newRequest("hub.set");
    if (!req) return false;
    JAddStringToObject(req, "product",  PRODUCT_UID);
    JAddStringToObject(req, "mode",     "periodic");
    JAddNumberToObject(req, "outbound", (int)g_summaryIntervalMin);
    JAddNumberToObject(req, "inbound",  (int)g_summaryIntervalMin);
    if (notecard.sendRequest(req)) {
        g.appliedSummaryIntervalMin = g_summaryIntervalMin;
        return true;
    }
    return false;
}

// =============================================================================
// Register compact binary templates for both Notefiles.
// "compact" format and an explicit port number are both required for
// NOTE-NBGLWX Skylo NTN satellite operation; they are compatible with the
// cellular path too, so one template definition per Notefile covers both
// transports. Type hints use literal numeric values (14.1 → float32,
// 12 → int16 (2-byte signed, −32 768..+32 767), true → boolean) matching
// the pattern documented in the note-arduino / note-c library. _time is
// included explicitly so the device-side Unix timestamp is preserved in
// compact mode — the Notecard auto-populates it from its own clock when each
// Note is created, requiring no change to note.add calls in sendSummary or
// sendAlert.
// alert_code in tank_alert.qo uses type 12 (int16, 2-byte signed); values
// 0=level_low / 1=level_critical / 2=battery_low fit well within the range.
// alerts in tank_status.qo also uses type 12; the worst-case window count
// (4 320 alerts) fits within the +32 767 ceiling. Both mappings are defined
// by ALERT_CODE_* in the header. Fixed-length binary encoding means
// pump_amps = 0.0 (pump off) is always transmitted faithfully — no omitempty
// drop for zero-valued fields.
//
// Sets g.templatesInstalled only after BOTH templates are confirmed registered;
// stays false so loop() retries on subsequent wakes if either call fails.
static void defineTemplates(void) {
    // ── Summary Notefile template ─────────────────────────────────────────────
    J *req = notecard.newRequest("note.template");
    if (!req) return;
    JAddStringToObject(req, "file",   NOTEFILE_SUMMARY);
    JAddNumberToObject(req, "port",   50);        // required for Skylo NTN
    JAddStringToObject(req, "format", "compact"); // required for Skylo NTN
    J *body = JAddObjectToObject(req, "body");
    if (!body) { JDelete(req); return; }
    JAddNumberToObject(body, "_time",       14);   // Unix timestamp (Notecard auto-fills)
    JAddNumberToObject(body, "level_pct",   14.1); // float32
    JAddNumberToObject(body, "distance_mm", 14.1); // float32
    JAddNumberToObject(body, "pump_amps",   14.1); // float32
    JAddBoolToObject(body,   "pump_on",     true); // boolean
    JAddNumberToObject(body, "battery_v",   14.1); // float32
    JAddNumberToObject(body, "alerts",      12);   // int16 (2-byte signed)
    if (!notecard.sendRequest(req)) return; // retry on next wake if this fails

    // ── Alert Notefile template ───────────────────────────────────────────────
    req = notecard.newRequest("note.template");
    if (!req) return;
    JAddStringToObject(req, "file",   NOTEFILE_ALERT);
    JAddNumberToObject(req, "port",   51);        // required for Skylo NTN
    JAddStringToObject(req, "format", "compact"); // required for Skylo NTN
    body = JAddObjectToObject(req, "body");
    if (!body) { JDelete(req); return; }
    JAddNumberToObject(body, "_time",      14);   // Unix timestamp (Notecard auto-fills)
    JAddNumberToObject(body, "alert_code", 12);   // int16 (2-byte signed): 0=level_low, 1=level_critical, 2=battery_low
    JAddNumberToObject(body, "level_pct",  14.1); // float32
    JAddNumberToObject(body, "pump_amps",  14.1); // float32
    JAddNumberToObject(body, "battery_v",  14.1); // float32
    if (notecard.sendRequest(req)) {
        g.templatesInstalled = true;  // set only after both templates confirmed
    }
}

// =============================================================================
// Put the host to sleep until the Notecard raises ATTN. cxSleepUntilAttn()
// asks the Notecard to hold ATTN low for sleepSec (card.attn "sleep"), waits
// for the pin to go low, and enters STOP2. The rising edge on D5 wakes the
// host and execution resumes in place — state simply stays in RAM.
//
// Sleep duration adapts to the solar battery state: longer sleep conserves
// energy during extended overcast periods without requiring any configuration
// change from Notehub.
//
// The battery multipliers can push sleepSec past the env-var ceiling when
// sample_interval_sec is near its maximum. A hard clamp at ENV_SAMPLE_SEC_MAX
// (86 400 s / 24 h) ensures the device never goes silent longer than operators
// expect regardless of the configured base interval.
static void doSleep(float battV) {
    uint32_t sleepSec = g_sampleIntervalSec;

    // Moderately discharged (< 12.0V): extend sleep to reduce radio wakes.
    if (battV > 0.0f && battV < 12.0f) {
        sleepSec = g_sampleIntervalSec * 2;
    }
    // Critically low (below battery_alert_v): extend further to survive.
    if (battV > 0.0f && battV < g_batteryAlertV) {
        sleepSec = g_sampleIntervalSec * 4;
    }
    // Hard ceiling: clamp after multipliers so the device never sleeps longer
    // than the documented 24-hour maximum (e.g. 86400 × 4 would be 4 days).
    if (sleepSec > ENV_SAMPLE_SEC_MAX) {
        sleepSec = ENV_SAMPLE_SEC_MAX;
    }

#ifdef TANK_MONITOR_DEBUG
    Stream *log = &dbgSerial;
#else
    Stream *log = NULL;
#endif
    if (!cxSleepUntilAttn(notecard, sleepSec, NULL, log)) {
        // The Notecard didn't take the sleep request, or ATTN never went low
        // (check the ATTN -> D5 jumper). Keep the sample cadence awake and
        // try again on the next cycle.
#ifdef TANK_MONITOR_DEBUG
        dbgSerial.println("[sleep] ATTN sleep failed — waiting out the interval awake");
#endif
        delay(sleepSec * 1000UL);
    }
}
