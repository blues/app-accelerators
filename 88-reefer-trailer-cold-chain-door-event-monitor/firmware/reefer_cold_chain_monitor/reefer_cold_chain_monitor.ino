/*
 * reefer_cold_chain_monitor.ino
 *
 * Refrigerated trailer cold-chain and door-event monitor.
 *
 * Reads two DS18B20 waterproof temperature probes and a magnetic door reed
 * switch; routes temperature excursions and door-state events via the
 * Notecard for Skylo (NOTE-NBGLWX) to Notehub.  The Notecard automatically
 * selects the best available radio — LTE-M / NB-IoT / GPRS cellular, or Skylo
 * NTN satellite — with no firmware involvement.
 *
 * NTN data-budget strategy (Skylo satellite best practices):
 *   Critical alerts are written to a single NTN-compatible notefile
 *   (compact format, port) and delivered over the first available transport —
 *   cellular when in range, Skylo NTN satellite as automatic fallback.
 *   Per-sample logs and hourly summaries use non-NTN-compatible notefiles
 *   (delete:true, no format/port): at sync time the Notecard discards their
 *   queued notes when NTN is the active transport, so high-volume data never
 *   consumes the 10 KB bundled satellite budget.  Documented behavior — see
 *   https://dev.blues.io/starnote/satellite-best-practices/
 *   See NOTEFILE_* defines in reefer_cold_chain_monitor_helpers.h for detail.
 *
 * Hardware:
 *   Blues Notecarrier CX (Cygnet STM32 host MCU)
 *   Notecard for Skylo  (NOTE-NBGLWX)
 *   Adafruit DS18B20 waterproof probe ×2  (#381)  on A0 (1-Wire)
 *   Adafruit magnetic reed switch        (#375)   on A1 (NO, INPUT_PULLUP)
 *   Pololu D24V22F5 5 V step-down regulator (#2858) from 12 V trailer supply
 *
 * Sleep pattern:
 *   setup() runs on every host wake from card.attn, runs one sample cycle,
 *   then returns to loop().  loop() persists state to Notecard flash via
 *   NotePayloadSaveAndSleep and cuts host power via card.attn.  On bench
 *   setups without ATTN power-gating wired, loop() falls back to delay() and
 *   calls runWakePreamble() + runSampleCycle() directly.
 *
 * Blues documentation: https://dev.blues.io
 */

// ── Library includes ──────────────────────────────────────────────────────────
#include <Notecard.h>
#include <OneWire.h>
#include <DallasTemperature.h>

// ── Debug build flag ──────────────────────────────────────────────────────────
// Define DEBUG_MODE to enable USB serial output and Notecard debug forwarding.
// In production (deployed trailer), leave it undefined: the 3-second Serial
// wait and always-on Notecard debug stream materially raise average current on
// a design whose core claim is deep sleep between 60-second samples.
//
// To enable during development, add -DDEBUG_MODE to your build flags, or
// uncomment the line below:
// #define DEBUG_MODE

// ── Shared constants, AppState, externs, and helper prototypes ────────────────
// Must be included AFTER the optional #define DEBUG_MODE line above so the
// DEBUG_PRINT / DEBUG_PRINTLN macros in the header see the correct setting.
#include "reefer_cold_chain_monitor_helpers.h"

// ── Global objects ────────────────────────────────────────────────────────────
AppState          g_state;
Notecard          notecard;
OneWire           oneWire(ONE_WIRE_PIN);
DallasTemperature probes(&oneWire);

// ── Runtime-configurable parameters (fetched from Notehub env vars each wake) ─
float    g_tempMaxC           = DEFAULT_TEMP_MAX_C;
float    g_tempMinC           = DEFAULT_TEMP_MIN_C;
uint32_t g_doorAlertSec       = DEFAULT_DOOR_ALERT_SEC;
uint32_t g_sampleIntervalSec  = DEFAULT_SAMPLE_INTERVAL_SEC;
uint32_t g_summaryIntervalMin = DEFAULT_SUMMARY_INTERVAL_MIN;
uint32_t g_alertCooldownSec   = DEFAULT_ALERT_COOLDOWN_SEC;

// ── Forward declaration ───────────────────────────────────────────────────────
static void runSampleCycle(void);

// =============================================================================
// runWakePreamble() — per-cycle preamble shared by cold-wake and bench paths
//
// Called from setup() on every true hardware wake and from the loop() bench
// fallback on every simulated wake.  Keeping these steps in one place ensures
// environment-variable updates, outbound-cadence changes, and sensor re-init
// happen consistently regardless of whether the host is power-gated by ATTN
// or is running continuously on a bench supply.
// =============================================================================
static void runWakePreamble(void) {
    fetchEnvOverrides();
    applyHubSetIfChanged(g_state);
    probes.begin();
    probes.setResolution(12);        // 12-bit: 0.0625 °C steps, ~750 ms conversion
    pinMode(DOOR_PIN, INPUT_PULLUP);
}

// =============================================================================
// setup() — runs on every host wake from card.attn sleep
// =============================================================================
void setup() {
    // ── Serial / debug output ─────────────────────────────────────────────────
    // Gated by DEBUG_MODE at compile time.  In production skip both the
    // blocking wait and the Notecard debug stream to keep the per-wake
    // current budget well under 100 ms of idle draw.
#ifdef DEBUG_MODE
    Serial.begin(115200);
    const uint32_t dbgEnd = millis() + 3000;
    while (!Serial && millis() < dbgEnd) {}
#endif

    notecard.begin();
#ifdef DEBUG_MODE
    notecard.setDebugOutputStream(Serial);
#endif

    // ── Restore state from Notecard flash ────────────────────────────────────
    memset(&g_state, 0, sizeof(g_state));
    NotePayloadDesc payload = {};
    bool wokeFromSleep = NotePayloadRetrieveAfterSleep(&payload);
    if (wokeFromSleep) {
        NotePayloadGetSegment(&payload, STATE_SEG_ID, &g_state, sizeof(g_state));
        NotePayloadFree(&payload);
    }

    // ── First-boot initialisation ─────────────────────────────────────────────
    // Accumulator bounds are initialised here, outside the config-success
    // block, so a first-boot configuration failure never leaves t1_min_c /
    // t1_max_c at their memset-zero defaults on a later successful retry.
    if (!g_state.configured) {
        g_state.t1_min_c =  999.0f;  g_state.t1_max_c = -999.0f;
        g_state.t2_min_c =  999.0f;  g_state.t2_max_c = -999.0f;

        if (hubConfigure() && defineTemplates()) {
            g_state.configured           = true;
            g_state.summary_interval_min = DEFAULT_SUMMARY_INTERVAL_MIN;
            DEBUG_PRINTLN(F("[boot] First-boot configuration complete"));
        } else {
            DEBUG_PRINTLN(F("[boot] First-boot configuration failed — will retry"));
        }
    }

    // ── Per-wake preamble (env overrides, sensor re-init) ─────────────────────
    runWakePreamble();

    // ── Configuration guard ───────────────────────────────────────────────────
    // If hub.set and the three Note templates have not been successfully
    // registered, skip the sample cycle entirely.  Without them the Notecard
    // cannot correctly route notes (missing templates break compact encoding;
    // missing hub.set leaves the device unregistered with Notehub).
    // NotePayloadSaveAndSleep in loop() will sleep for g_sampleIntervalSec
    // and wake setup() again to retry configuration.
    if (!g_state.configured) {
        DEBUG_PRINTLN(F("[boot] Skipping sample cycle — awaiting configuration"));
        return;
    }

    runSampleCycle();
}

// =============================================================================
// loop() — persists state and sleeps until the next sample interval
//
// On hardware with ATTN power-gating wired (normal deployment),
// NotePayloadSaveAndSleep cuts host power and this function never returns.
// On bench setups without the CX power path it falls back to delay() and
// calls runWakePreamble() + runSampleCycle() so monitoring continues without
// a hardware reset and env-var changes are applied on each simulated wake.
// =============================================================================
void loop() {
    NotePayloadDesc out = {};
    NotePayloadAddSegment(&out, STATE_SEG_ID, &g_state, sizeof(g_state));
    NotePayloadSaveAndSleep(&out, (int)g_sampleIntervalSec, NULL);

    // Reached only when ATTN power-gating is not wired (bench / development).
    DEBUG_PRINTLN(F("[warn] ATTN power-gate not firing — bench fallback active"));
    delay(g_sampleIntervalSec * 1000UL);
    runWakePreamble();   // apply env-var changes and re-init sensors, same as
                         // a true cold wake would do in setup()

    // ── Configuration guard (bench path) ─────────────────────────────────────
    // Mirror the setup() guard: if first-boot hub.set / template registration
    // has not yet succeeded, attempt it now before allowing any note.add
    // activity.  Without this check a failed setup() returns early and
    // NotePayloadSaveAndSleep (which does not cut power in bench mode) falls
    // through here, bypassing the Notecard configuration entirely.
    if (!g_state.configured) {
        if (hubConfigure() && defineTemplates()) {
            g_state.configured           = true;
            g_state.summary_interval_min = DEFAULT_SUMMARY_INTERVAL_MIN;
            DEBUG_PRINTLN(F("[boot] First-boot configuration complete (bench retry)"));
        } else {
            DEBUG_PRINTLN(F("[boot] Configuration retry failed — skipping sample cycle"));
            return;   // loop() is called again immediately; retries after next delay
        }
    }

    runSampleCycle();
}

// =============================================================================
// runSampleCycle() — one complete sense → evaluate → accumulate → maybe-emit
// =============================================================================
static void runSampleCycle(void) {
    float t1 = TEMP_INVALID, t2 = TEMP_INVALID;
    readTemperatures(t1, t2);
    bool doorOpen = readDoorState();

    // ── Timekeeping ───────────────────────────────────────────────────────────
    // Three-path policy:
    //
    //   Path A — card.time returns a valid epoch:
    //     Use it.  Mark rtc_synced_once permanently true.  On the FIRST real
    //     epoch after synthetic startup, translate every stored synthetic
    //     timestamp to real time by preserving the elapsed intervals:
    //       real_ts = now - (synthetic_epoch - synthetic_ts)
    //     This keeps summary-window age, door-open duration, and temperature-
    //     alert cooldown intact across the handoff.  Simply resetting them to
    //     "now" would claim a fresh window start while including pre-sync
    //     samples, corrupt door-open duration (and therefore long-open timing),
    //     and allow a temp alert to re-fire immediately if one was sent during
    //     the synthetic phase.
    //
    //   Path B — card.time returns 0 AND RTC has never been synced:
    //     Advance a monotonic synthetic counter by one sample interval per wake.
    //     This keeps cooldown timers, door durations, and summary-window elapsed
    //     time advancing correctly before the first network sync.
    //
    //   Path C — card.time returns 0 AND RTC was previously synced (transient
    //     failure):
    //     Advance last_good_epoch by one sample interval rather than falling
    //     back to tiny synthetic timestamps.  Reverting would corrupt cooldown
    //     comparisons against real past epochs (uint32_t underflow) and produce
    //     bogus elapsed-time calculations.
    uint32_t now = getEpochTime();
    if (now != 0) {
        // Path A
        g_state.rtc_synced_once = true;
        g_state.last_good_epoch = now;
        if (g_state.synthetic_epoch != 0) {
            // First real epoch after synthetic startup: translate stored
            // synthetic timestamps to real time, preserving elapsed intervals.
            uint32_t syn = g_state.synthetic_epoch;  // snapshot before clearing
            if (g_state.window_start_epoch != 0)
                g_state.window_start_epoch =
                    now - (syn - g_state.window_start_epoch);
            if (g_state.door_open && g_state.door_opened_epoch != 0)
                g_state.door_opened_epoch =
                    now - (syn - g_state.door_opened_epoch);
            if (g_state.last_temp_alert_epoch != 0)
                g_state.last_temp_alert_epoch =
                    now - (syn - g_state.last_temp_alert_epoch);
            g_state.synthetic_epoch = 0;
            DEBUG_PRINTLN(F("[time] RTC synced — synthetic timestamps translated to real time"));
        }
    } else if (!g_state.rtc_synced_once) {
        // Path B — pre-sync synthetic time
        g_state.synthetic_epoch += g_sampleIntervalSec;
        now = g_state.synthetic_epoch;
    } else {
        // Path C — transient failure after first sync
        g_state.last_good_epoch += g_sampleIntervalSec;
        now = g_state.last_good_epoch;
        DEBUG_PRINTLN(F("[time] card.time transient fail — advancing from last epoch"));
    }

    // Anchor the summary window on the very first sample.
    if (g_state.window_start_epoch == 0) {
        g_state.window_start_epoch = now;
    }

    // ── Door state machine ────────────────────────────────────────────────────
    checkDoorEvents(g_state, t1, t2, doorOpen, now);

    // ── Temperature excursion detection ───────────────────────────────────────
    checkTemperatureExcursion(g_state, t1, t2, now);

    // ── Per-sample log note (cellular/WiFi only; best effort) ─────────────────
    // One note per sample cycle.  delete:true in the template ensures these
    // never reach the satellite link.  A failed note.add silently drops the
    // sample — the minor loss is acceptable for a trend/compliance record.
    sendLog(t1, t2, doorOpen);

    // ── Accumulate data for the summary window ────────────────────────────────
    accumulateSummary(g_state, t1, t2);

    // ── Emit summary note if the window has elapsed ───────────────────────────
    // Accumulators are reset only after note.add is confirmed enqueued so a
    // transient Notecard error does not silently discard the window's data.
    if (now - g_state.window_start_epoch >= g_summaryIntervalMin * 60UL) {
        if (sendSummary(g_state, doorOpen)) {
            g_state.t1_sum_c = 0.0f;  g_state.t1_count = 0;
            g_state.t1_min_c =  999.0f; g_state.t1_max_c = -999.0f;
            g_state.t2_sum_c = 0.0f;  g_state.t2_count = 0;
            g_state.t2_min_c =  999.0f; g_state.t2_max_c = -999.0f;
            g_state.door_events        = 0;
            g_state.window_start_epoch = now;
        }
    }
}
