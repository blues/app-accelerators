/*
 * grease_interceptor_monitor.ino
 *
 * Hydromechanical (HGI) and Batch-Collection Grease Interceptor Level Monitor
 *
 * Blues Notecarrier CX (onboard Cygnet STM32L433 host MCU) +
 * Notecard Cell+WiFi (MBGLW) +
 * DFRobot A02YYUW IP67 Waterproof Ultrasonic Sensor (SEN0311)
 *
 * Measures the fill level of a hydromechanical grease interceptor (HGI) by
 * reading the distance from a waterproof ultrasonic probe (mounted at the
 * top of the access hatch, pointing down) to the top of the liquid/FOG
 * surface. On HGI and batch-collection units without a fixed outlet weir,
 * the liquid surface rises with accumulated FOG and wastewater, so the
 * derived fill percentage is a direct proxy for pump-out urgency. Converts
 * distance to fill percentage, accumulates a daily summary, and sends an
 * immediate alert when fill level crosses a configurable threshold. All
 * tuning parameters are adjustable via Notehub environment variables without
 * re-flashing firmware.
 *
 * NOTE: This design is scoped to HGI / batch-collection geometries. On
 * conventional constant-level interceptors (fixed outlet weir), the liquid
 * surface height is nearly constant regardless of FOG accumulation; the fill
 * percentage will not track FOG buildup in that geometry. See the project
 * README §1 and §9 before deploying on a different interceptor type.
 *
 * NOTE: This file is provided as a starting point and SHOULD BE EDITED
 * before deployment (set PRODUCT_UID, verify serial port assignment,
 * measure and configure interceptor_depth_mm for each installation site).
 *
 * Utility functions (sensor read, median filter, distance-to-fill, Notecard
 * helpers) live in grease_interceptor_monitor_helpers.h/.cpp to keep this
 * file under 500 lines.
 */

#include <Notecard.h>
#include "grease_interceptor_monitor_helpers.h"

// ---------------------------------------------------------------------------
// Product UID — paste yours from Notehub
// ---------------------------------------------------------------------------
#ifndef PRODUCT_UID
#define PRODUCT_UID ""  // "com.your-company.your-name:your_project"
#pragma message "PRODUCT_UID not defined. Set this before flashing."
#endif

// ---------------------------------------------------------------------------
// Notecard hub sync cadence
// Alert notes use sync:true and bypass these intervals.
// ---------------------------------------------------------------------------
#define HUB_OUTBOUND_MIN   1440  // 24 h outbound; non-alert notes batch here
#define HUB_INBOUND_MIN     120  // 2 h inbound; pulls env-var updates

// ---------------------------------------------------------------------------
// Default sensor / application config
// All can be overridden via Notehub environment variables.
// ---------------------------------------------------------------------------
#define DEFAULT_INTERCEPTOR_DEPTH_MM  600.0f  // distance (mm) from sensor face to the
                                               // liquid surface immediately after pump-out
                                               // (0% grease fill); varies by interceptor
                                               // model and probe mounting height — measure
                                               // once per installation during commissioning
#define DEFAULT_ALERT_THRESHOLD_PCT    75.0f  // fill % that triggers alert
#define DEFAULT_SAMPLE_INTERVAL_SEC    900    // 15 min between samples
#define DEFAULT_REPORT_INTERVAL_MIN   1440    // 24 h between summary notes
#define ALERT_COOLDOWN_SEC            3600    // 1 h minimum between alerts

// ---------------------------------------------------------------------------
// State segment ID stored inside Notecard during host sleep.
// Four characters, unique to this project.
// ---------------------------------------------------------------------------
static const char STATE_SEG_ID[] = "GRIM";  // Grease Interceptor Monitor

// ---------------------------------------------------------------------------
// Globals
// ---------------------------------------------------------------------------
Notecard notecard;

// ---------------------------------------------------------------------------
// Forward declarations (Notecard-configuration and env-var helpers)
// ---------------------------------------------------------------------------
static bool notecardConfigure(void);
static bool defineTemplates(void);
static void fetchEnvOverrides(Config &cfg, State &state);

// ===========================================================================
// setup() — re-runs on every wake from NotePayloadSaveAndSleep
// ===========================================================================
void setup() {
#ifdef usbSerial
    usbSerial.begin(115200);
    const uint32_t t0 = millis();
    while (!usbSerial && millis() - t0 < 3000) {}
#endif

    // Sensor UART: A02YYUW TX wire → Notecarrier CX RX header pin (Serial1).
    // Verify that Serial1 maps to the RX/TX header on your board variant.
    Serial1.begin(SENSOR_BAUD);

    // I2C link to the Notecard (Notecarrier CX has pull-ups on-board)
    notecard.begin();
#ifdef usbSerial
    notecard.setDebugOutputStream(usbSerial);
#endif

    // -----------------------------------------------------------------------
    // Recover state from the Notecard's wake-up payload, or cold-boot init
    // -----------------------------------------------------------------------
    NotePayloadDesc payload;
    bool recovered = NotePayloadRetrieveAfterSleep(&payload);

    State state = {};

    if (recovered) {
        recovered &= NotePayloadGetSegment(&payload, STATE_SEG_ID,
                                           &state, sizeof(state));
        NotePayloadFree(&payload);
    }

    if (!recovered) {
        // Cold boot: zero all state, set sentinels, and seed cfg with
        // compile-time defaults.  On every subsequent wake, cfg is initialized
        // from the persisted state.cfg copy instead (see below).
        memset(&state, 0, sizeof(state));
        state.fill_pct_last_valid = -1.0f;  // sentinel: no valid reading yet
        state.cfg = Config{
            DEFAULT_INTERCEPTOR_DEPTH_MM,
            DEFAULT_ALERT_THRESHOLD_PCT,
            DEFAULT_SAMPLE_INTERVAL_SEC,
            DEFAULT_REPORT_INTERVAL_MIN
        };
    }

    // Initialize cfg from the persisted copy so a transient env.get failure
    // retains the last operator-applied values rather than reverting to
    // compile-time defaults.  fetchEnvOverrides() then overwrites individual
    // fields only when env.get succeeds and the value passes range validation.
    Config cfg = state.cfg;

    // Retry Notecard configuration on every wake until both calls succeed.
    // On a transient first-boot I2C failure the flags stay false so the next
    // wake re-enters the block and tries again rather than silently skipping.
    if (!recovered || !state.notecard_configured) {
        if (notecardConfigure()) {
            state.notecard_configured  = true;
            // Only record the applied cadence after hub.set is confirmed so
            // fetchEnvOverrides does not treat a failed first-boot as "already
            // applied" and miss the retry.
            state.applied_outbound_min = HUB_OUTBOUND_MIN;
        }
    }
    if (!recovered || !state.templates_defined) {
        if (defineTemplates()) {
            state.templates_defined = true;
        }
    }

    // Fetch current env-var values; only fields that parse cleanly and pass
    // range validation are updated in cfg (others retain state.cfg values).
    fetchEnvOverrides(cfg, state);
    // Persist the (possibly updated) config back into state so it survives
    // the upcoming sleep and initializes cfg correctly on the next wake.
    state.cfg = cfg;

    // -----------------------------------------------------------------------
    // Sample the sensor: take NUM_READINGS and keep the median
    // -----------------------------------------------------------------------
    float readings[NUM_READINGS];
    int   valid_count = 0;

    for (int i = 0; i < NUM_READINGS; i++) {
        float d = readDistanceMm();
        // Accept only readings within a reasonable window above the configured
        // depth — allow 10 % over to catch near-overflow conditions cleanly.
        if (d > 0.0f && d <= cfg.interceptor_depth_mm * 1.1f) {
            readings[valid_count++] = d;
        }
        delay(110);  // sensor response time ≥ 100 ms; wait between pulses
    }

    float fill_pct = -1.0f;
    if (valid_count >= 2) {
        float median_dist = medianOf(readings, valid_count);
#ifdef usbSerial
        // Commissioning aid: print raw distance so a tech can compare against
        // a tape measure without waiting for a full summary note to appear in
        // Notehub. Connect via USB-C with the CX DIP switch set to HST and
        // open a serial monitor at 115200 baud.
        usbSerial.print("[DBG] median distance mm: ");
        usbSerial.println(median_dist);
#endif
        fill_pct = distanceToFillPct(median_dist, cfg.interceptor_depth_mm);
        state.fill_pct_sum += fill_pct;
        state.valid_samples++;
        state.fill_pct_last_valid = fill_pct;  // track most recent valid reading
        if (fill_pct > state.fill_pct_peak) {
            state.fill_pct_peak = fill_pct;
        }
    }

    uint32_t now = getEpochTime();

    // Normalize pre-sync sentinels on first valid clock tick.
    //
    // When a cold-boot alert or summary fires before card.time is available,
    // the corresponding epoch field is stored as 1 (0 = never fired,
    // 1 = fired once before sync). As soon as card.time returns a real
    // timestamp (now > 0), the raw expression (now - 1) evaluates to roughly
    // the full Unix epoch — far above any configured cooldown or report
    // interval — which would immediately re-fire the alert or summary,
    // defeating the intended cadence. Snapping the sentinel to 'now' instead
    // restarts the interval from this wake cycle, so the next fire is at least
    // one full ALERT_COOLDOWN_SEC / report_interval_min away.
    if (now > 0) {
        if (state.last_alert_epoch  == 1) state.last_alert_epoch  = now;
        if (state.last_report_epoch == 1) state.last_report_epoch = now;
    }

    // -----------------------------------------------------------------------
    // Threshold alert — level-triggered: fires whenever fill is at or above
    // the limit and the cooldown has elapsed. A persistently full interceptor
    // generates a new alert every ALERT_COOLDOWN_SEC until it is serviced.
    //
    // When the Notecard has not yet synced time (now == 0), the cooldown
    // comparison is meaningless. Allow at most one alert before time is
    // available (last_alert_epoch == 0) and suppress further alerts until the
    // clock is synced. Storing the sentinel value 1 rather than 0 ensures the
    // "first boot" branch does not re-fire on every subsequent wake while the
    // Notecard is still acquiring time.
    // -----------------------------------------------------------------------
    bool alert_allowed = (state.last_alert_epoch == 0) ||
                         (now > 0 &&
                          (now - state.last_alert_epoch) >= ALERT_COOLDOWN_SEC);

    if (fill_pct >= 0.0f && fill_pct >= cfg.alert_threshold_pct && alert_allowed) {
        if (sendAlert(fill_pct, cfg.alert_threshold_pct)) {
            // Advance the cooldown epoch only after confirmed delivery so a
            // failed send retries on the next wake rather than silently dropping.
            state.last_alert_epoch = (now > 0) ? now : 1;  // 1 = fired; time not yet known
        }
    }

    // -----------------------------------------------------------------------
    // Daily summary — emits once per report_interval_min window.
    //
    // Same sentinel strategy as alerts: allow the first cold-boot summary
    // (last_report_epoch == 0) then block repeated reports while the Notecard
    // clock is unsynced by storing 1 instead of 0 after that first fire.
    // Normal interval-based cadence resumes once now > 0.
    // -----------------------------------------------------------------------
    uint32_t report_interval_sec = cfg.report_interval_min * 60UL;
    bool report_due = (state.last_report_epoch == 0) ||
                      (now > 0 &&
                       (now - state.last_report_epoch) >= report_interval_sec);

    if (report_due && state.valid_samples > 0) {
        if (sendSummary(state)) {
            // Reset window accumulators only after confirmed delivery so a
            // failed send retries on the next wake with the full window intact.
            state.fill_pct_sum      = 0.0f;
            state.valid_samples     = 0;
            state.fill_pct_peak     = 0.0f;
            state.last_report_epoch = (now > 0) ? now : 1;  // 1 = fired; time not yet known
        }
    }

    // -----------------------------------------------------------------------
    // Save state and sleep; the Notecard will power the host back on after
    // sample_interval_sec seconds via its ATTN pin.
    // -----------------------------------------------------------------------
    NotePayloadDesc new_payload = {0, 0, 0};
    NotePayloadAddSegment(&new_payload, STATE_SEG_ID, &state, sizeof(state));
    if (!NotePayloadSaveAndSleep(&new_payload, cfg.sample_interval_sec, NULL)) {
#ifdef usbSerial
        usbSerial.println("[ERR] NotePayloadSaveAndSleep failed — ATTN pin may not be wired "
                          "or Notecard ATTN mode not enabled. Restarting in 15 s.");
#endif
        // Actually restart so the device doesn't strand itself awake forever.
        // NVIC_SystemReset() is the standard ARM Cortex-M mechanism; it is
        // available on the Cygnet (STM32L433) via CMSIS without extra headers.
        delay(15000);
        NVIC_SystemReset();
    }
}

// ===========================================================================
// loop() — not used in this sleep pattern
// ===========================================================================
void loop() {
    // All logic lives in setup(). The sleep pattern means the MCU is
    // powered off by the Notecard; setup() re-runs on every wake.
}

// ===========================================================================
// Notecard configuration — retried every wake until hub.set is confirmed.
// Returns true only when hub.set succeeds; the caller sets
// state.notecard_configured and state.applied_outbound_min on a true return.
// card.motion.mode is attempted regardless; failure is logged but is
// non-critical (accelerometer stays active; device still functions normally).
// ===========================================================================
static bool notecardConfigure(void) {
    // sendRequestWithRetry on the first transaction to handle the cold-boot
    // race condition where the host comes up before the Notecard is ready.
    J *req = notecard.newRequest("hub.set");
    JAddStringToObject(req, "product",  PRODUCT_UID);
    JAddStringToObject(req, "mode",     "periodic");
    JAddNumberToObject(req, "outbound", HUB_OUTBOUND_MIN);
    JAddNumberToObject(req, "inbound",  HUB_INBOUND_MIN);
    bool hub_ok = notecard.sendRequestWithRetry(req, 5);  // retry for up to 5 s
#ifdef usbSerial
    if (!hub_ok) {
        usbSerial.println("[WRN] hub.set failed; will retry on next wake");
    }
#endif

    // Disable the onboard accelerometer to reduce idle current noise;
    // this also makes Mojo bench measurements cleaner during development.
    J *req2 = notecard.newRequest("card.motion.mode");
    JAddBoolToObject(req2, "stop", true);
    J *rsp2 = notecard.requestAndResponse(req2);
    if (!notecardResponseOk(rsp2)) {
#ifdef usbSerial
        usbSerial.println("[WRN] card.motion.mode failed; accelerometer remains active");
#endif
    }
    notecard.deleteResponse(rsp2);

    return hub_ok;
}

// ===========================================================================
// Note templates — retried every wake until note.template is confirmed.
// Templates store each summary note as a fixed-length record (~30 bytes
// on the wire) rather than free-form JSON, significantly reducing cellular
// data usage over a multi-year deployment.
// Returns true only when note.template succeeds; the caller sets
// state.templates_defined on a true return.
// ===========================================================================
static bool defineTemplates(void) {
    J *req = notecard.newRequest("note.template");
    JAddStringToObject(req, "file", "grease_summary.qo");
    JAddNumberToObject(req, "port", 50);
    J *body = JAddObjectToObject(req, "body");
    JAddNumberToObject(body, "fill_pct_avg",  TFLOAT32);  // 4-byte float
    JAddNumberToObject(body, "fill_pct_peak", TFLOAT32);
    JAddNumberToObject(body, "fill_pct_now",  TFLOAT32);
    JAddNumberToObject(body, "valid_samples", TUINT32);   // 4-byte unsigned integer
    J *rsp = notecard.requestAndResponse(req);
    bool ok = notecardResponseOk(rsp);
    notecard.deleteResponse(rsp);
#ifdef usbSerial
    if (!ok) {
        usbSerial.println("[WRN] note.template failed; will retry on next wake — "
                          "summary notes will be untemplated until this succeeds");
    }
#endif
    return ok;
}

// ===========================================================================
// Environment variable fetch — runs on every wake.
// cfg arrives pre-seeded from state.cfg (the persisted copy), so only the
// fields that env.get returns, parse successfully as numbers, and pass range
// validation are updated; all others retain the last successfully applied
// operator values.  A complete env.get transport/API failure leaves cfg
// unchanged and returns early.
//
// Notehub environment variables are string-backed.  The env.get response body
// delivers each value as a JSON string, not a number, so JGetNumber returns 0
// for every key (no numeric field present) and the range checks silently
// suppress every update.  The correct pattern is JGetString → strtof /
// strtoul with an end-pointer check → range validation → apply.
//
// Also re-applies hub.set whenever report_interval_min differs from the last
// applied value so the Notecard's outbound sync cadence stays aligned with
// the local summary period.  Without this, lowering report_interval_min
// (e.g. 1440 → 360) would queue summaries more frequently locally but still
// ship them only once a day until the Notecard's outbound schedule was also
// updated.
// ===========================================================================
static void fetchEnvOverrides(Config &cfg, State &state) {
    J *req = notecard.newRequest("env.get");
    J *names = JAddArrayToObject(req, "names");
    JAddItemToArray(names, JCreateString("interceptor_depth_mm"));
    JAddItemToArray(names, JCreateString("alert_threshold_pct"));
    JAddItemToArray(names, JCreateString("sample_interval_sec"));
    JAddItemToArray(names, JCreateString("report_interval_min"));

    J *rsp = notecard.requestAndResponse(req);
    if (!notecardResponseOk(rsp)) {
        // I2C failure or Notecard API error — keep current cfg defaults and
        // retry on the next wake rather than silently treating errors as
        // 'no overrides configured'.
        notecard.deleteResponse(rsp);
        return;
    }

    J *body = JGetObject(rsp, "body");
    if (body != NULL) {
        // Notehub env-var values arrive as strings in the env.get body.
        // Read each field with JGetString, parse explicitly with strtof /
        // strtoul, and verify the entire token was consumed (end != s &&
        // *end == '\0') before applying.  Each value is also clamped to a
        // safe operating range [low, high] — values outside that range are
        // silently left at their persisted default, preventing bad Notehub
        // inputs from causing overflow, impossible sleep durations, or
        // divide-by-zero.
        const char *s;
        char *end;
        float fv;
        unsigned long uv;

        s = JGetString(body, "interceptor_depth_mm");
        if (s && *s) {
            fv = strtof(s, &end);
            if (end != s && *end == '\0' && fv >= 50.0f && fv <= 3000.0f) {
                cfg.interceptor_depth_mm = fv;
            }
        }

        s = JGetString(body, "alert_threshold_pct");
        if (s && *s) {
            fv = strtof(s, &end);
            if (end != s && *end == '\0' && fv >= 1.0f && fv <= 100.0f) {
                cfg.alert_threshold_pct = fv;
            }
        }

        // Upper bound of 86400 s (24 h) prevents absurd sleep durations.
        s = JGetString(body, "sample_interval_sec");
        if (s && *s) {
            uv = strtoul(s, &end, 10);
            if (end != s && *end == '\0' && uv >= 60UL && uv <= 86400UL) {
                cfg.sample_interval_sec = (uint32_t)uv;
            }
        }

        // Upper bound of 10080 min (7 days) keeps report_interval_min * 60UL
        // within uint32_t range (max 604800 s) and bounded to sane operation.
        s = JGetString(body, "report_interval_min");
        if (s && *s) {
            uv = strtoul(s, &end, 10);
            if (end != s && *end == '\0' && uv >= 60UL && uv <= 10080UL) {
                cfg.report_interval_min = (uint32_t)uv;
            }
        }
    }
    notecard.deleteResponse(rsp);

    // Keep the Notecard's outbound cadence in sync with report_interval_min.
    // cfg.report_interval_min reflects the operator's current env-var setting
    // when env.get succeeded, or the previously persisted value when it failed.
    // In both cases the comparison is safe: a transient env.get failure leaves
    // both sides equal, suppressing a spurious hub.set; a genuine operator
    // change triggers hub.set exactly once.
    if (cfg.report_interval_min != state.applied_outbound_min) {
        J *req2 = notecard.newRequest("hub.set");
        JAddStringToObject(req2, "mode",     "periodic");
        JAddNumberToObject(req2, "outbound", cfg.report_interval_min);
        JAddNumberToObject(req2, "inbound",  HUB_INBOUND_MIN);
        J *rsp2 = notecard.requestAndResponse(req2);
        if (notecardResponseOk(rsp2)) {
            // Only advance the applied value after confirmed success; on failure
            // the stale value remains so the firmware retries on the next wake.
            state.applied_outbound_min = cfg.report_interval_min;
        }
        notecard.deleteResponse(rsp2);
    }
}
