/*
 * reefer_cold_chain_monitor.ino
 *
 * Refrigerated trailer cold-chain and door-event monitor.
 *
 * Reads two DS18B20 waterproof temperature probes and a magnetic door reed
 * switch; routes temperature excursions and door-state events via the
 * Notecard for Skylo (NOTE-NBGLWX) to Notehub.  Once the firmware sets
 * `card.transport` to `wifi-cell-ntn` at first boot, the Notecard handles
 * radio selection autonomously — preferring WiFi, then LTE-M / NB-IoT / GPRS
 * cellular, then Skylo NTN satellite — with no further firmware involvement.
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
 *   Blues Notecarrier CX (onboard STM32L433 host MCU)
 *   Notecard for Skylo  (NOTE-NBGLWX)
 *   Adafruit DS18B20 waterproof probe ×2  (#381)  on A0 (1-Wire)
 *   Adafruit magnetic reed switch        (#375)   on A1 (NO, INPUT_PULLUP)
 *   Pololu D24V22F5 5 V step-down regulator (#2858) from 12 V trailer supply
 *
 * Sleep pattern:
 *   setup() runs once at power-up and arms the ATTN wake.  loop() runs one
 *   sample cycle, then sleeps the host in STM32 STOP2 (~1-2 µA, RAM retained)
 *   until the Notecard raises ATTN g_sampleIntervalSec later (card.attn
 *   "sleep"; see cx_sleep.h).  Execution resumes in place, so AppState simply
 *   lives in RAM — nothing is persisted to the Notecard.
 *
 * Wiring for sleep: jumper the Notecarrier CX ATTN pin to D5 (both on the
 * same 16-pin header).  Leave EN unconnected: on the CX it enables the shared
 * 3.3 V VIO rail, so driving it from ATTN browns out the whole board instead
 * of sleeping the host.
 *
 * Build: Tools > USB support (if available) > None (usb=none).  With the USB
 * CDC stack enabled and no USB host attached the host cannot stay in STOP2.
 * Debug output (DEBUG_MODE) goes to the ST-LINK virtual COM port on the CX
 * debug jack.
 *
 * Blues documentation: https://dev.blues.io
 */

// ── Library includes ──────────────────────────────────────────────────────────
#include <Notecard.h>
#include <OneWire.h>
#include <DallasTemperature.h>

// ── Debug build flag ──────────────────────────────────────────────────────────
// Define DEBUG_MODE to enable debug serial output and Notecard debug forwarding.
// In production (deployed trailer), leave it undefined: the always-on Notecard
// debug stream materially raises average current on a design whose core claim
// is deep sleep between 60-second samples.
//
// To enable during development, add -DDEBUG_MODE to your build flags, or
// uncomment the line below:
// #define DEBUG_MODE

// ── Shared constants, AppState, externs, and helper prototypes ────────────────
// Must be included AFTER the optional #define DEBUG_MODE line above so the
// DEBUG_PRINT / DEBUG_PRINTLN macros in the header see the correct setting.
#include "reefer_cold_chain_monitor_helpers.h"
#include "cx_sleep.h"

// ── Global objects ────────────────────────────────────────────────────────────
AppState          g_state;
Notecard          notecard;
OneWire           oneWire(ONE_WIRE_PIN);
DallasTemperature probes(&oneWire);
#ifdef DEBUG_MODE
// LPUART1 on the CX debug jack, which an ST-LINK exposes as a virtual COM
// port.  (USB CDC must be disabled for STOP2 to work; see cx_sleep.h.)
Uart              debugSerial(PIN_VCP_RX, PIN_VCP_TX);
#endif

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
// configureOnce() — first-boot Notecard configuration, retried until it sticks
//
// hub.set, card.transport, card.location.mode and the three Note templates.
// Accumulator bounds are initialised in setup(), outside this block, so a
// configuration failure never leaves t1_min_c / t1_max_c at their memset-zero
// defaults on a later successful retry.
// =============================================================================
static void configureOnce(void) {
    if (hubConfigure() && defineTemplates()) {
        g_state.configured           = true;
        g_state.summary_interval_min = DEFAULT_SUMMARY_INTERVAL_MIN;
        DEBUG_PRINTLN(F("[boot] First-boot configuration complete"));
    } else {
        DEBUG_PRINTLN(F("[boot] First-boot configuration failed — will retry next wake"));
    }
}

// =============================================================================
// setup() — runs once at power-up
//
// Serial, Notecard bring-up, state init, first configuration attempt, sensor
// init, and the one-time ATTN wake arming.  Everything that must run on every
// wake (env overrides, cadence changes, the sample cycle) lives in loop().
// =============================================================================
void setup() {
    // ── Serial / debug output ─────────────────────────────────────────────────
    // Gated by DEBUG_MODE at compile time.  In production skip the Notecard
    // debug stream to keep the per-wake current budget tight.
#ifdef DEBUG_MODE
    debugSerial.begin(115200);
#endif

    notecard.begin();
#ifdef DEBUG_MODE
    notecard.setDebugOutputStream(debugSerial);
#endif

    // ── Initialise application state ──────────────────────────────────────────
    memset(&g_state, 0, sizeof(g_state));
    g_state.t1_min_c =  999.0f;  g_state.t1_max_c = -999.0f;
    g_state.t2_min_c =  999.0f;  g_state.t2_max_c = -999.0f;

    // ── First-boot configuration (retried from loop() if it fails) ────────────
    configureOnce();

    // ── Sensor init ───────────────────────────────────────────────────────────
    // Pin modes and the probes' resolution setting survive STOP2 (the sensors
    // stay powered from the 3.3 V rail), so this runs once.
    probes.begin();
    probes.setResolution(12);        // 12-bit: 0.0625 °C steps, ~750 ms conversion
    pinMode(DOOR_PIN, INPUT_PULLUP);

    // ── Arm the ATTN wake: the Notecard raises ATTN (D5) to end each sleep ────
    cxSleepBegin();
}

// =============================================================================
// loop() — one sample cycle, then STOP2 until the Notecard raises ATTN
// =============================================================================
void loop() {
    // ── Configuration retry ───────────────────────────────────────────────────
    // If hub.set and the three Note templates have not been successfully
    // registered, retry now.  Without them the Notecard cannot correctly route
    // notes (missing templates break compact encoding; missing hub.set leaves
    // the device unregistered with Notehub).
    if (!g_state.configured) {
        configureOnce();
    }

    // ── Per-wake preamble (env overrides, outbound-cadence changes) ───────────
    fetchEnvOverrides();
    applyHubSetIfChanged(g_state);

    // ── Configuration guard ───────────────────────────────────────────────────
    // Skip the sample cycle until configuration has succeeded; the sleep below
    // still runs so the retry happens g_sampleIntervalSec later.
    if (g_state.configured) {
        runSampleCycle();
    } else {
        DEBUG_PRINTLN(F("[boot] Skipping sample cycle — awaiting configuration"));
    }

    // ── Sleep until the next sample interval ──────────────────────────────────
#ifdef DEBUG_MODE
    Stream *log = &debugSerial;
#else
    Stream *log = NULL;
#endif
    if (!cxSleepUntilAttn(notecard, g_sampleIntervalSec, NULL, log)) {
        // The Notecard didn't take the sleep request, or ATTN never went low
        // (check the ATTN -> D5 jumper). Keep the sample cadence and try again.
        DEBUG_PRINTLN(F("[warn] ATTN sleep failed — waiting out the interval awake"));
        delay(g_sampleIntervalSec * 1000UL);
    }
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
