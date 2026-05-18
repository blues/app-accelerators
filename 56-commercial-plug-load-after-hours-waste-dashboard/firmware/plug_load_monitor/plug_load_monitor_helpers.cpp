// plug_load_monitor_helpers.cpp
//
// Implementations of all helper functions for the plug_load_monitor sketch.
// Globals (CFG_*, state, notecard, g_*_template_applied) are defined in
// plug_load_monitor.ino and accessed here via extern declarations in
// plug_load_monitor_helpers.h.
//
// Functions used only internally are declared static (file-local linkage).
// The six functions called directly from the .ino are non-static and match
// their prototypes in plug_load_monitor_helpers.h.
//
// The real-time after-hours alert extension (circuit_alert.qo) is compiled
// only when PLUG_LOAD_ALERTS is defined in plug_load_monitor_helpers.h.
// It is disabled by default.  Every alert-specific block is enclosed in
// #ifdef PLUG_LOAD_ALERTS / #endif guards so the baseline build contains
// no alert templates, no card.time calls, and no extra cellular sessions.

#include "plug_load_monitor_helpers.h"

// ── Env-var clamping helpers (internal) ──────────────────────────────────────
// A bad Notehub value must not collapse the sleep cadence (sample_interval=0),
// invert after-hours logic (out-of-range biz hours), or make current-scaling
// invalid (ct_full_scale<=0). Every remotely-supplied variable is clamped
// before being applied; out-of-range values are silently discarded and the
// prior valid config is retained.
static uint32_t clampU32(long v, uint32_t lo, uint32_t hi, uint32_t fallback) {
    if (v < (long)lo || v > (long)hi) return fallback;
    return (uint32_t)v;
}
static float clampF(double v, float lo, float hi, float fallback) {
    if ((float)v < lo || (float)v > hi) return fallback;
    return (float)v;
}

// ── Config helpers ────────────────────────────────────────────────────────────

// Apply a persisted AppCfg snapshot to the live CFG_* globals.
void applyCfg(const AppCfg &c) {
    CFG_SAMPLE_INTERVAL_SEC = c.sample_interval_sec;
    CFG_REPORT_INTERVAL_MIN = c.report_interval_min;
    CFG_CIRCUIT_COUNT       = c.circuit_count;
    CFG_IDLE_THRESHOLD_AMPS = c.idle_threshold_amps;
    CFG_AFTER_HOURS_AMPS    = c.after_hours_amps;
    CFG_BIZ_HOURS_START     = c.biz_hours_start;
    CFG_BIZ_HOURS_END       = c.biz_hours_end;
    CFG_TZ_OFFSET_HRS       = c.tz_offset_hrs;
    CFG_ALERT_COOLDOWN_SEC  = c.alert_cooldown_sec;
    CFG_CT_FULL_SCALE_AMPS  = c.ct_full_scale_amps;
}

// Capture current CFG_* globals into an AppCfg snapshot for persistence.
void captureCfg(AppCfg &c) {
    c.sample_interval_sec = CFG_SAMPLE_INTERVAL_SEC;
    c.report_interval_min = CFG_REPORT_INTERVAL_MIN;
    c.circuit_count       = CFG_CIRCUIT_COUNT;
    c.idle_threshold_amps = CFG_IDLE_THRESHOLD_AMPS;
    c.after_hours_amps    = CFG_AFTER_HOURS_AMPS;
    c.biz_hours_start     = CFG_BIZ_HOURS_START;
    c.biz_hours_end       = CFG_BIZ_HOURS_END;
    c.tz_offset_hrs       = CFG_TZ_OFFSET_HRS;
    c.alert_cooldown_sec  = CFG_ALERT_COOLDOWN_SEC;
    c.ct_full_scale_amps  = CFG_CT_FULL_SCALE_AMPS;
}

// ── Notecard configuration ────────────────────────────────────────────────────

bool hubConfigure() {
    J *req = notecard.newRequest("hub.set");
    if (PRODUCT_UID[0]) JAddStringToObject(req, "product", PRODUCT_UID);
    JAddStringToObject(req, "mode",     "periodic");
    JAddNumberToObject(req, "outbound", (int)CFG_REPORT_INTERVAL_MIN);
    JAddNumberToObject(req, "inbound",  360);  // check for env var updates every 6 h
    bool ok = notecard.sendRequestWithRetry(req, 10); // retry absorbs any transient I2C hiccup
    if (ok) state.last_applied_outbound_min = CFG_REPORT_INTERVAL_MIN;
    return ok;
}

// Registers each Notefile template independently and updates the per-Notefile
// confirmation flags from each send result.  Returns true only when every
// template attempted in this build succeeded; the per-Notefile flags carry
// the granular state used to gate emission of each Notefile.
//
// Called unconditionally at every boot — note.template is idempotent, so
// re-issuing it on an intact Notecard is a no-op, and re-issuing after a
// factory reset or card replacement restores the fixed-schema encoding before
// any note.add calls reach the Notecard.
bool defineTemplates() {
    // circuit_summary.qo – hourly, template-backed.
    // Template type hint legend: 14.1 = 4-byte float, 12 = 2-byte signed int.
    // Fixed-length encoding reduces on-wire size ~3–5× vs. free-form JSON;
    // material for a multi-year deployment with 24 notes/day/device.
    J *req = notecard.newRequest("note.template");
    JAddStringToObject(req, "file", "circuit_summary.qo");
    JAddNumberToObject(req, "port", 50);
    J *body = JAddObjectToObject(req, "body");
    JAddNumberToObject(body, "ch1_mean",    14.1);
    JAddNumberToObject(body, "ch1_peak",    14.1);
    JAddNumberToObject(body, "ch1_act_min", 14.1);
    JAddNumberToObject(body, "ch2_mean",    14.1);
    JAddNumberToObject(body, "ch2_peak",    14.1);
    JAddNumberToObject(body, "ch2_act_min", 14.1);
    JAddNumberToObject(body, "ch3_mean",    14.1);
    JAddNumberToObject(body, "ch3_peak",    14.1);
    JAddNumberToObject(body, "ch3_act_min", 14.1);
    JAddNumberToObject(body, "ch4_mean",    14.1);
    JAddNumberToObject(body, "ch4_peak",    14.1);
    JAddNumberToObject(body, "ch4_act_min", 14.1);
    JAddNumberToObject(body, "samples",     12);
    g_summary_template_applied = notecard.sendRequest(req);
#ifdef PLUG_LOAD_DEBUG
    if (!g_summary_template_applied) {
        dbgSerial.println("[init] circuit_summary.qo template definition failed; will retry next boot");
    }
#endif

#ifdef PLUG_LOAD_ALERTS
    // circuit_alert.qo – immediate sync on after-hours anomaly.
    // Registered only when PLUG_LOAD_ALERTS is defined in
    // plug_load_monitor_helpers.h.  Attempted independently of the summary
    // template above so a failure here does not suppress the summary stream.
    //
    // The exemplar string for alert_type must be at least as long as the
    // longest value the firmware will ever send ("after_hours_load" = 16 chars).
    req  = notecard.newRequest("note.template");
    JAddStringToObject(req, "file", "circuit_alert.qo");
    JAddNumberToObject(req, "port", 51);
    body = JAddObjectToObject(req, "body");
    JAddNumberToObject(body, "circuit",    11);               // 1-byte int: circuit number 1–4
    JAddNumberToObject(body, "arms",       14.1);             // 4-byte float: RMS amps at trigger
    JAddStringToObject(body, "alert_type", "after_hours_load"); // exemplar sets max string length
    JAddNumberToObject(body, "hour_local", 11);               // 1-byte int: local hour 0–23
    g_alert_template_applied = notecard.sendRequest(req);
#ifdef PLUG_LOAD_DEBUG
    if (!g_alert_template_applied) {
        dbgSerial.println("[init] circuit_alert.qo template definition failed; will retry next boot");
    }
#endif

    return g_summary_template_applied && g_alert_template_applied;
#else
    return g_summary_template_applied;
#endif // PLUG_LOAD_ALERTS
}

// ── Env-var fetch ─────────────────────────────────────────────────────────────

// Builds an env.get request listing all configurable variables.
// Alert-specific variables are included only when PLUG_LOAD_ALERTS is defined.
static J* buildEnvGetReq() {
    J *req   = notecard.newRequest("env.get");
    J *names = JAddArrayToObject(req, "names");
    JAddItemToArray(names, JCreateString("sample_interval_sec"));
    JAddItemToArray(names, JCreateString("report_interval_min"));
    JAddItemToArray(names, JCreateString("circuit_count"));
    JAddItemToArray(names, JCreateString("idle_threshold_amps"));
#ifdef PLUG_LOAD_ALERTS
    JAddItemToArray(names, JCreateString("after_hours_threshold_amps"));
    JAddItemToArray(names, JCreateString("biz_hours_start"));
    JAddItemToArray(names, JCreateString("biz_hours_end"));
    JAddItemToArray(names, JCreateString("tz_offset_hours"));
    JAddItemToArray(names, JCreateString("alert_cooldown_min"));
#endif
    JAddItemToArray(names, JCreateString("ct_full_scale_amps"));
    return req;
}

// Fetches environment variables from the Notecard and updates CFG_* globals.
// Returns true when env.get completed successfully and the body was parsed;
// returns false on I²C failure or a Notecard error response, leaving all
// CFG_* variables at their current values (either compile-time defaults on
// first boot, or the last known-good snapshot restored from AppState.saved_cfg
// on subsequent wakes — see setup()).
// One retry with a 250 ms gap absorbs transient Notecard I²C hiccups at wake
// without holding the host up for a full sendRequestWithRetry timeout.
bool fetchEnvOverrides() {
    J *rsp = notecard.requestAndResponse(buildEnvGetReq());
    if (!rsp || notecard.responseError(rsp)) {
        if (rsp) { notecard.deleteResponse(rsp); rsp = NULL; }
#ifdef PLUG_LOAD_DEBUG
        dbgSerial.println("[env] env.get failed; retrying once");
#endif
        delay(250);
        rsp = notecard.requestAndResponse(buildEnvGetReq());
    }

    if (!rsp) {
#ifdef PLUG_LOAD_DEBUG
        dbgSerial.println("[env] env.get failed after retry; retaining prior config");
#endif
        return false;
    }
    if (notecard.responseError(rsp)) {
#ifdef PLUG_LOAD_DEBUG
        dbgSerial.println("[env] env.get error after retry; retaining prior config");
#endif
        notecard.deleteResponse(rsp);
        return false;
    }

    J *body = JGetObjectItem(rsp, "body");
    if (body) {
        const char *v;

        // Clamp to a safe engineering range; values outside the range are
        // discarded and the current (known-good) value is retained.
        v = JGetString(body, "sample_interval_sec");
        if (v && v[0]) CFG_SAMPLE_INTERVAL_SEC =
            clampU32(atol(v), 10, 3600, CFG_SAMPLE_INTERVAL_SEC);

        v = JGetString(body, "report_interval_min");
        if (v && v[0]) CFG_REPORT_INTERVAL_MIN =
            clampU32(atol(v), 1, 1440, CFG_REPORT_INTERVAL_MIN);

        v = JGetString(body, "circuit_count");
        if (v && v[0]) {
            uint8_t c = (uint8_t)atoi(v);
            if (c >= 1 && c <= MAX_CHANNELS) CFG_CIRCUIT_COUNT = c;
        }

        v = JGetString(body, "idle_threshold_amps");
        if (v && v[0]) CFG_IDLE_THRESHOLD_AMPS =
            clampF(atof(v), 0.0f, 1000.0f, CFG_IDLE_THRESHOLD_AMPS);

#ifdef PLUG_LOAD_ALERTS
        v = JGetString(body, "after_hours_threshold_amps");
        if (v && v[0]) CFG_AFTER_HOURS_AMPS =
            clampF(atof(v), 0.0f, 1000.0f, CFG_AFTER_HOURS_AMPS);

        // Business hours: parse both endpoints into temporaries, then validate
        // the pair together.  The firmware uses a simple same-day daytime model
        // (start < end, e.g. 08:00–18:00); it does not support overnight
        // schedules (e.g. 22:00–06:00).  A pair where start >= end would invert
        // the after-hours logic and turn the device into a continuous alert
        // generator, so both values are discarded together when start >= end.
        {
            int8_t newStart = CFG_BIZ_HOURS_START;
            int8_t newEnd   = CFG_BIZ_HOURS_END;
            v = JGetString(body, "biz_hours_start");
            if (v && v[0]) {
                int h = atoi(v);
                if (h >= 0 && h <= 23) newStart = (int8_t)h;
            }
            v = JGetString(body, "biz_hours_end");
            if (v && v[0]) {
                int h = atoi(v);
                if (h >= 0 && h <= 23) newEnd = (int8_t)h;
            }
            if (newStart < newEnd) {
                CFG_BIZ_HOURS_START = newStart;
                CFG_BIZ_HOURS_END   = newEnd;
            } else if (newStart != CFG_BIZ_HOURS_START || newEnd != CFG_BIZ_HOURS_END) {
#ifdef PLUG_LOAD_DEBUG
                dbgSerial.println("[env] biz_hours_start >= biz_hours_end; pair ignored");
#endif
            }
        }

        // UTC offset: valid range is −12 to +14 hours.
        v = JGetString(body, "tz_offset_hours");
        if (v && v[0]) {
            int tz = atoi(v);
            if (tz >= -12 && tz <= 14) CFG_TZ_OFFSET_HRS = (int8_t)tz;
        }

        // Cooldown floor is 1 minute to prevent continuous alert storms.
        v = JGetString(body, "alert_cooldown_min");
        if (v && v[0]) {
            uint32_t cdmin = clampU32(atol(v), 1, 10080,
                                      CFG_ALERT_COOLDOWN_SEC / 60);
            CFG_ALERT_COOLDOWN_SEC = cdmin * 60UL;
        }
#endif // PLUG_LOAD_ALERTS

        v = JGetString(body, "ct_full_scale_amps");
        if (v && v[0]) CFG_CT_FULL_SCALE_AMPS =
            clampF(atof(v), 0.1f, 1000.0f, CFG_CT_FULL_SCALE_AMPS);
    }
    notecard.deleteResponse(rsp);
    return true;
}

// ── CT channel reading (internal) ────────────────────────────────────────────
//
// Measures RMS amps on one CT channel using a two-pass approach:
//   Pass 1 (CT_BIAS_SAMPLES): compute the running mean → DC offset (the
//           bias mid-point, nominally VREF/2 = 1.65 V from the bias divider).
//   Pass 2 (CT_RMS_SAMPLES): subtract DC offset, accumulate sum-of-squares,
//           divide by sample count, take square root → RMS counts → volts → amps.
//
// Deriving the DC offset from each read tolerates resistor-tolerance drift
// in the bias network rather than assuming a perfect 1.65 V mid-point.
static float readChannelAmpsRMS(uint8_t pin) {
    uint32_t acc = 0;
    for (uint16_t i = 0; i < CT_BIAS_SAMPLES; i++) acc += analogRead(pin);
    int32_t dc_offset = (int32_t)(acc / CT_BIAS_SAMPLES);

    uint64_t sum_sq = 0;
    for (uint16_t i = 0; i < CT_RMS_SAMPLES; i++) {
        int32_t s = (int32_t)analogRead(pin) - dc_offset;
        sum_sq += (uint64_t)((int64_t)s * s);
    }

    float rms_counts = sqrtf((float)sum_sq / (float)CT_RMS_SAMPLES);
    float rms_v      = rms_counts * ADC_VREF_V / (float)ADC_COUNTS;
    float arms       = rms_v * (CFG_CT_FULL_SCALE_AMPS / CT_VOUT_AT_FULL_SCALE);
    return (arms < 0.0f) ? 0.0f : arms;
}

#ifdef PLUG_LOAD_ALERTS
// ── Utility: get current epoch from Notecard (internal) ──────────────────────
// Only compiled when PLUG_LOAD_ALERTS is defined; the baseline build does not
// issue card.time at all, saving one I²C round-trip per 60-second wake.

static uint32_t notecardEpoch() {
    uint32_t epoch = 0;
    J *rsp = notecard.requestAndResponse(notecard.newRequest("card.time"));
    if (rsp) {
        if (!notecard.responseError(rsp)) {
            epoch = (uint32_t)JGetNumber(rsp, "time");
        }
#ifdef PLUG_LOAD_DEBUG
        else {
            dbgSerial.println("[time] card.time error; time-dependent actions skipped");
        }
#endif
        notecard.deleteResponse(rsp);
    }
    return epoch;
}

// ── After-hours detection (internal) ─────────────────────────────────────────

// Returns true when the local hour falls outside business hours.
// Assumes CFG_BIZ_HOURS_START < CFG_BIZ_HOURS_END (same-day daytime model,
// e.g. 08:00–18:00).  fetchEnvOverrides() rejects any pair that violates this
// invariant, so the check here is always correct for same-day windows.
static bool isAfterHours(uint32_t unix_epoch) {
    if (unix_epoch == 0) return false; // Notecard doesn't have valid time yet
    uint32_t local_epoch = unix_epoch + (int32_t)CFG_TZ_OFFSET_HRS * 3600L;
    int hour = (int)((local_epoch / 3600UL) % 24UL);
    return (hour < (int)CFG_BIZ_HOURS_START || hour >= (int)CFG_BIZ_HOURS_END);
}

// ── Immediate after-hours alert note (internal) ───────────────────────────────
// Fires a circuit_alert.qo note with sync:true so the Notecard opens a session
// immediately rather than waiting for the next scheduled outbound window.
// Returns true on successful queue; the caller must only advance
// alert_last_unix[] on true so a failed send does not start the cooldown and
// suppress the retry on the next wake.
static bool sendAlert(uint8_t circuit_1based, float arms,
                      const char *alert_type, int hour_local) {
    // Gate only on the alert Notefile's own template flag — a failure on the
    // summary template must not suppress alerts (and vice versa in sendSummary).
    if (!g_alert_template_applied) {
#ifdef PLUG_LOAD_DEBUG
        dbgSerial.println("[alert] circuit_alert.qo template not yet confirmed; suppressing note");
#endif
        return false;
    }
    J *req  = notecard.newRequest("note.add");
    JAddStringToObject(req, "file", "circuit_alert.qo");
    JAddBoolToObject(req, "sync", true);  // bypass outbound timer — alert now
    J *body = JAddObjectToObject(req, "body");
    JAddNumberToObject(body, "circuit",    (int)circuit_1based);
    JAddNumberToObject(body, "arms",       arms);
    JAddStringToObject(body, "alert_type", alert_type);
    JAddNumberToObject(body, "hour_local", hour_local);
    return notecard.sendRequest(req);
}
#endif // PLUG_LOAD_ALERTS

// ── Note emission helpers (internal) ─────────────────────────────────────────

static float safeAvg(float sum, uint32_t n) {
    return (n > 0) ? (sum / (float)n) : INVALID_SENTINEL;
}

// Returns true if the summary was successfully queued (or there was nothing
// to send). Accumulators are only reset on confirmed success so the window
// data survives to the next wake if queueing fails.
static bool sendSummary() {
    // Gate only on the summary Notefile's own template flag.  A failure on
    // circuit_alert.qo (PLUG_LOAD_ALERTS builds only) must not suppress
    // circuit_summary.qo emission, since they are independent Notefiles.
    // Returning false here preserves the accumulated window data so it is not
    // silently discarded.
    if (!g_summary_template_applied) {
#ifdef PLUG_LOAD_DEBUG
        dbgSerial.println("[summary] circuit_summary.qo template not yet confirmed; suppressing note");
#endif
        return false;
    }
    if (state.total_samples == 0) return true; // nothing to send; not a failure

    J *req  = notecard.newRequest("note.add");
    JAddStringToObject(req, "file", "circuit_summary.qo");
    J *body = JAddObjectToObject(req, "body");

    // Fixed field names matching the note.template definition above.
    static const char *mean_keys[] = { "ch1_mean", "ch2_mean", "ch3_mean", "ch4_mean" };
    static const char *peak_keys[] = { "ch1_peak", "ch2_peak", "ch3_peak", "ch4_peak" };
    static const char *actm_keys[] = { "ch1_act_min", "ch2_act_min", "ch3_act_min", "ch4_act_min" };

    for (uint8_t ch = 0; ch < MAX_CHANNELS; ch++) {
        float mean    = safeAvg(state.sum_arms[ch], state.n_arms[ch]);
        float peak    = (state.n_arms[ch] > 0) ? state.peak_arms[ch] : INVALID_SENTINEL;
        // Active minutes: accumulated active seconds divided by 60. Seconds are
        // accumulated as each sample is taken (not reconstructed from the current
        // interval at emit time) so the value is correct even when
        // sample_interval_sec changes mid-window via env vars.
        // Emit INVALID_SENTINEL (not 0.0) for channels with zero valid samples
        // so downstream classifiers can reliably distinguish an absent/uninstalled
        // CT from a circuit that was legitimately idle for the entire window.
        float act_min = (state.n_arms[ch] > 0)
            ? (state.active_secs[ch] / 60.0f)
            : INVALID_SENTINEL;
        JAddNumberToObject(body, mean_keys[ch], mean);
        JAddNumberToObject(body, peak_keys[ch], peak);
        JAddNumberToObject(body, actm_keys[ch], act_min);
    }
    JAddNumberToObject(body, "samples", (int)state.total_samples);

    if (!notecard.sendRequest(req)) {
        // Queue failure: leave accumulators intact so the window data survives
        // to the next wake rather than being silently discarded.
        return false;
    }

    // Reset accumulators for the next window only after confirmed successful queue.
    // alert_last_unix[] is intentionally preserved so cooldowns persist.
    for (uint8_t ch = 0; ch < MAX_CHANNELS; ch++) {
        state.sum_arms[ch]    = 0;
        state.peak_arms[ch]   = 0;
        state.n_arms[ch]      = 0;
        state.active_secs[ch] = 0;
    }
    state.total_samples      = 0;
    state.elapsed_window_sec = 0;  // restart the sample-count clock
    return true;
}

// ── One sample cycle (runs on every wake) ────────────────────────────────────

void runSampleCycle() {
#ifdef PLUG_LOAD_ALERTS
    // card.time is used only for after-hours detection and the hour_local field
    // in alert notes.  A zero return (time not yet acquired) means after-hours
    // alerts are suppressed until the Notecard has a valid UTC fix.
    // In the baseline build (PLUG_LOAD_ALERTS not defined) this call is omitted
    // entirely, saving one I²C round-trip per 60-second wake.
    uint32_t now = notecardEpoch();
    bool after_hours = isAfterHours(now);
    int  hour_local  = (now > 0)
        ? (int)(((now + (int32_t)CFG_TZ_OFFSET_HRS * 3600L) / 3600UL) % 24UL)
        : -1;
#endif

    for (uint8_t ch = 0; ch < CFG_CIRCUIT_COUNT; ch++) {
        float arms = readChannelAmpsRMS(CT_PINS[ch]);

#ifdef PLUG_LOAD_DEBUG
        dbgSerial.print("[sample] ch");
        dbgSerial.print(ch + 1);
        dbgSerial.print(" = ");
        dbgSerial.print(arms, 3);
        dbgSerial.println(" A RMS");
#endif

        // Accumulate into the current summary window.
        state.sum_arms[ch] += arms;
        state.n_arms[ch]++;
        if (arms > state.peak_arms[ch]) state.peak_arms[ch] = arms;
        // Accumulate active seconds (not a sample count) so act_min stays
        // correct even when sample_interval_sec changes mid-window via env vars.
        if (arms >= CFG_IDLE_THRESHOLD_AMPS) state.active_secs[ch] += CFG_SAMPLE_INTERVAL_SEC;

#ifdef PLUG_LOAD_ALERTS
        // Fire an immediate alert once per cooldown window per channel when
        // load exceeds the threshold during non-business hours and the Notecard
        // has a valid UTC time base.  Only start the cooldown on a successful
        // queue; a failed send must not suppress retries on the next wake.
        if (after_hours && hour_local >= 0 &&
            arms >= CFG_AFTER_HOURS_AMPS &&
            (now - state.alert_last_unix[ch]) >= CFG_ALERT_COOLDOWN_SEC) {
            if (sendAlert(ch + 1, arms, "after_hours_load", hour_local)) {
                state.alert_last_unix[ch] = now;
            }
        }
#endif // PLUG_LOAD_ALERTS
    }
    state.total_samples++;

    // Advance the elapsed-seconds counter AFTER taking all samples for this
    // wake, not before.  This ensures the window boundary is reached only once
    // a full sample interval has actually elapsed since the last sample was
    // recorded — the summary fires after report_interval_min minutes of
    // accumulated intervals rather than one interval early.  Summary windows
    // are driven entirely by this sample-based clock so the first summary is
    // never delayed waiting for a valid card.time epoch.  The counter is reset
    // to 0 by sendSummary() on a confirmed successful queue.
    state.elapsed_window_sec += CFG_SAMPLE_INTERVAL_SEC;

    // Emit a summary note when the elapsed-seconds counter reaches the reporting
    // interval.  No card.time dependency: the window opens on the first sample
    // and the first summary arrives reliably after report_interval_min minutes
    // of uptime regardless of cellular/GPS acquisition timing.
    if (state.elapsed_window_sec >= CFG_REPORT_INTERVAL_MIN * 60UL) {
        sendSummary();
    }
}
