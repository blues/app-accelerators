/*
 * livestock_water_tank_monitor.ino
 * Blues Application Example — Off-Grid Livestock Water Tank Monitor
 *
 * Monitors a remote stock tank's water level, submersible pump current, and
 * solar battery voltage. The Cygnet STM32L433 host on the Notecarrier CX wakes
 * every 15 minutes (configurable), reads three analog sensors, evaluates alert
 * thresholds, accumulates a rolling average for the current summary window, and
 * returns to sleep via card.attn. A template-encoded summary Note is queued
 * every 4 hours; immediate-sync alert Notes are emitted when any threshold trips.
 *
 * Hardware:
 *   Blues Notecarrier CX (Cygnet STM32L433 host MCU)
 *   Blues Notecard for Skylo (NOTE-NBGLWX) in M.2 slot — cellular-first, satellite fallback
 *   MaxBotix HRXL-MaxSonar-WRL (MB7389) — tank level, analog output on A0
 *   SCT-013-030 CT + 2×10kΩ bias divider + 10µF cap — pump current on A1
 *   BSS84 PMOS + MMBT3904 NPN + 47kΩ/10kΩ switched divider — battery voltage on A2 (enable: A3)
 *   Blues Mojo — bench energy validation only (not read at runtime)
 *
 * See README.md for full wiring, Notehub setup, and calibration instructions.
 *
 * Sensor/env-var helpers are in livestock_water_tank_monitor_helpers.h/.cpp.
 */

#include <Notecard.h>
#include "livestock_water_tank_monitor_helpers.h"

#ifndef PRODUCT_UID
#define PRODUCT_UID ""  // replace with your Notehub ProductUID
#pragma message "PRODUCT_UID is not defined. Set it to your Notehub project identifier."
#endif

// ── Payload segment ID ────────────────────────────────────────────────────────
#define SEG_GLOBAL  "GLOB"

// ── Runtime parameters (loaded from GlobalState env cache each wake) ──────────
// Declared extern in helpers.h; defined here so both translation units share
// the same storage. Declaration values are compile-time defaults and are
// immediately overwritten in setup() from g.env* (persisted last-known-good
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
void setup() {
#ifdef TANK_MONITOR_DEBUG
    // Startup delay so USB serial can enumerate before the application runs.
    // Skipped in production builds to protect the low-power duty cycle —
    // an unconditional 2.5 s delay here would burn meaningful energy on
    // every 15-minute wake.
    delay(2500);
    Serial.begin(115200);
    notecard.setDebugOutputStream(Serial);
#endif

    notecard.begin();         // open I²C channel to Notecard
    analogReadResolution(12); // Cygnet STM32 supports 12-bit ADC (0–4095)

    // Configure the battery-divider PMOS enable pin at the top of every wake.
    // The 100 kΩ gate pullup already holds the PMOS off while the MCU is
    // unpowered, but making the output explicit and LOW here ensures the NPN
    // transistor cannot be inadvertently triggered by GPIO boot-state noise
    // during the remainder of setup() before readBatteryV() is called.
    pinMode(PIN_BATT_EN, OUTPUT);
    digitalWrite(PIN_BATT_EN, LOW);

    // Determine whether this is a cold boot or a wake from card.attn sleep.
    // NotePayloadGetSegment() validates the stored segment tag and size; if the
    // payload is absent, the segment is missing, or the stored struct is corrupt,
    // `restored` will be false and the device reinitializes cleanly rather than
    // operating on stale or partially-overwritten state.
    NotePayloadDesc payload;
    bool restored = NotePayloadRetrieveAfterSleep(&payload);
    if (restored) {
        restored &= NotePayloadGetSegment(&payload, SEG_GLOBAL, &g, sizeof(g));
        NotePayloadFree(&payload);
    }
    if (!restored) {
        // Cold boot (or corrupt/missing payload): initialize state, configure
        // the Notecard, and register Note templates. Seed the env-var cache
        // with compile-time defaults so g_* have valid values on this first
        // wake even if env.get fails on the initial connection attempt.
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
    } else if (!g.templatesInstalled) {
        // Template registration failed on a previous boot — retry now.
        // notecardConfigure() is not re-issued (hub.set is one-time setup
        // that survives across wakes in Notecard flash).
        defineTemplates();
    }

    // Load runtime parameters from the persisted env-var cache. On cold boot
    // these equal the compile-time defaults set above; on subsequent wakes they
    // carry the last-known-good Notehub values. This assignment runs before
    // fetchEnvOverrides() so that a transient env.get failure leaves g_* at
    // the last-known-good values rather than reverting to compile-time defaults.
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

    // Allow the MB7389 to complete its first ranging cycle after the host 3.3V
    // rail was restored by card.attn. The sensor outputs ~one reading per 100 ms;
    // MB7389_SETTLE_MS provides margin for supply-ramp and first-conversion latency.
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
    // accumulated earlier in this very setup() call) would inflate the first
    // window beyond one configured interval. The same path is re-entered after
    // a cadence change because reapplyHubSet() resets lastSummaryEpoch to 0.
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

    doSleep(battV);

    // Should never reach here. If it does, card.attn is not connected to the
    // host power-enable rail — check Notecarrier CX EN pin and firmware wiring.
    delay(30000);
}

void loop() {
    // All application logic runs in setup(). In the sleep-wake pattern the
    // device enters setup() fresh on each card.attn wake; loop() is never
    // reached in normal operation.
}

// =============================================================================
// Configure the Notecard for this project — called once on cold boot only.
static void notecardConfigure(void) {
    // periodic mode: outbound and inbound cadences match the summary interval.
    // sendRequestWithRetry absorbs the cold-boot I²C race: the host can come
    // up before the Notecard is ready to accept transactions.
    J *req = notecard.newRequest("hub.set");
    if (req) {
        JAddStringToObject(req, "product",  PRODUCT_UID);
        JAddStringToObject(req, "mode",     "periodic");
        JAddNumberToObject(req, "outbound", (int)g_summaryIntervalMin);
        JAddNumberToObject(req, "inbound",  (int)g_summaryIntervalMin);
        // Record the applied interval only after a confirmed successful transaction.
        if (notecard.sendRequestWithRetry(req, 10)) {
            g.appliedSummaryIntervalMin = g_summaryIntervalMin;
        }
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
// Uses plain sendRequest (no retry) because this is called on a post-sleep wake
// where the Notecard is already up and the I²C cold-boot race is not a concern.
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
// NOTE-NBGLWX Starnote/NTN satellite operation; they are compatible with the
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
// stays false so setup() retries on subsequent wakes if either call fails.
static void defineTemplates(void) {
    // ── Summary Notefile template ─────────────────────────────────────────────
    J *req = notecard.newRequest("note.template");
    if (!req) return;
    JAddStringToObject(req, "file",   NOTEFILE_SUMMARY);
    JAddNumberToObject(req, "port",   50);        // required for Starnote/NTN
    JAddStringToObject(req, "format", "compact"); // required for Starnote/NTN
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
    JAddNumberToObject(req, "port",   51);        // required for Starnote/NTN
    JAddStringToObject(req, "format", "compact"); // required for Starnote/NTN
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
// Serialize runtime state and put the host to sleep via NotePayloadSaveAndSleep.
// The Notecard stores the payload in its own flash and asserts card.attn at the
// specified interval, which cuts the Notecarrier CX host 3.3V rail via the EN
// pin. Execution resumes from the top of setup() on the next wake.
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

    NotePayloadDesc payload = {0, 0, 0};
    NotePayloadAddSegment(&payload, SEG_GLOBAL, &g, sizeof(g));
    NotePayloadSaveAndSleep(&payload, sleepSec, NULL);
    // card.attn cuts host power here; the next line is never reached in
    // normal operation on a correctly-wired Notecarrier CX.
}
