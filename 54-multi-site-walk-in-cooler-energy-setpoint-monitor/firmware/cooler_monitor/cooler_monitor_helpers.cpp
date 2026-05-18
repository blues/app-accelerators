// cooler_monitor_helpers.cpp — Notecard configuration, sensor reads, and note
// emission for the multi-site walk-in cooler energy & setpoint monitor.
//
// See cooler_monitor.ino for hardware wiring, env-var reference, and the
// main sample cycle.  Shared types and constants are in
// cooler_monitor_helpers.h.

#include "cooler_monitor_helpers.h"
#include <errno.h>
#include <math.h>

// ── Notecard configuration ─────────────────────────────────────────────────

// Sends hub.set to associate the Notecard with the project and set the default
// outbound/inbound cadence.  Called on cold start and on every warm wake until
// it returns true, so a transient I²C failure on first boot can never leave
// the device permanently unassociated and silently queueing Notes.
//
// Returns true when the Notecard acknowledges hub.set; false on any transient
// failure.  The caller persists the result in state.hubSetConfirmed and retries
// on the next wake while the flag is 0.  Once confirmed, setup() falls back to
// the cadence-change-only path (applyHubSetIfChanged).
bool hubConfigure() {
    J *req = notecard.newRequest("hub.set");
    if (!req) { DBG_PRINTLN("[hub] newRequest failed"); return false; }
    // Only send "product" when PRODUCT_UID is actually set.  Sending an empty
    // string can dissociate the Notecard from any project, leaving the device
    // silently queueing Notes that never reach Notehub.  The #pragma message
    // in cooler_monitor_helpers.h warns at compile time if PRODUCT_UID was not
    // set; this guards against the runtime consequence either way.
    if (PRODUCT_UID[0]) JAddStringToObject(req, "product", PRODUCT_UID);
    JAddStringToObject(req, "mode",    "periodic");
    JAddNumberToObject(req, "outbound", (int)DEFAULT_SUMMARY_INTERVAL_MIN);
    JAddNumberToObject(req, "inbound",  (int)(DEFAULT_SUMMARY_INTERVAL_MIN * 2));
    // sendRequestWithRetry handles the cold-boot race where the host MCU
    // starts before the Notecard is ready on I²C.
    if (!notecard.sendRequestWithRetry(req, 10)) {
        DBG_PRINTLN("[hub] hub.set failed — will retry on next wake");
        return false;
    }

    // Disable the onboard accelerometer to reduce idle power during bench
    // measurements and eliminate tiny current blips during Mojo validation.
    // Non-critical: a failure here does not affect hub association.
    req = notecard.newRequest("card.motion.mode");
    if (req) {
        JAddBoolToObject(req, "stop", true);
        if (!notecard.sendRequest(req)) {
            DBG_PRINTLN("[hub] card.motion.mode stop failed");
        }
    }
    return true;
}

// Re-applies hub.set only when summary_interval_min changes via env var so
// the Notecard's outbound cellular cadence tracks the local summary cadence.
// PRODUCT_UID is included on every call so the device stays associated with
// Notehub even if the first-boot hub.set was never confirmed successful.
void applyHubSetIfChanged(AppState &s) {
    if (cfgSummaryMin == s.appliedSummaryMin) return;

    J *req = notecard.newRequest("hub.set");
    if (!req) { DBG_PRINTLN("[hub] newRequest failed (cadence update)"); return; }
    // Same empty-PRODUCT_UID guard as hubConfigure(): never send "product":""
    // which would dissociate the Notecard from its project on a cadence update.
    if (PRODUCT_UID[0]) JAddStringToObject(req, "product",  PRODUCT_UID);
    JAddStringToObject(req, "mode",     "periodic");
    JAddNumberToObject(req, "outbound", (int)cfgSummaryMin);
    JAddNumberToObject(req, "inbound",  (int)(cfgSummaryMin * 2));
    if (!notecard.sendRequest(req)) {
        // Leave appliedSummaryMin unchanged so we retry on the next wake.
        DBG_PRINTLN("[hub] hub.set cadence update failed — will retry");
        return;
    }
    s.appliedSummaryMin = cfgSummaryMin;
}

// Registers fixed-width binary templates for both Notefiles.  Templates
// reduce on-wire payload size by roughly 3–5× versus free-form JSON.
// Returns true only when both templates are confirmed registered; returns
// false on any transient failure so setup() retries on the next wake.
// note.template is idempotent, so re-sending after a failure is harmless.
bool defineTemplates() {
    bool ok = true;

    // cooler_summary.qo
    // Type hints (per Blues note.template docs):
    //   14.1 = IEEE 754 4-byte float
    //   12   = 2-byte signed integer  (-32,768 … 32,767)
    //   14   = 4-byte signed integer  (-2,147,483,648 … 2,147,483,647)
    {
        J *req = notecard.newRequest("note.template");
        if (!req) {
            DBG_PRINTLN("[tmpl] newRequest failed (summary)");
            ok = false;
        } else {
            JAddStringToObject(req, "file", FILE_SUMMARY);
            J *body = JAddObjectToObject(req, "body");
            JAddNumberToObject(body, "temp_f",              14.1);
            JAddNumberToObject(body, "setpoint_f",          14.1);
            JAddNumberToObject(body, "compressor_amps",     14.1);
            JAddNumberToObject(body, "compressor_run_min",  14.1);
            JAddNumberToObject(body, "door_opens",          12);
            // 14 = 4-byte signed integer; required because multi-day windows
            // or a stuck door can accumulate values that overflow the 2-byte
            // signed range (> 32,767 s ≈ 9.1 h).  Max configurable window is
            // 1440 min × 60 s = 86,400 s, well within int32 range.
            JAddNumberToObject(body, "door_open_sec",       14);
            JAddNumberToObject(body, "kwh_window",          14.1);
            // window_sec: actual elapsed window length in seconds.  For
            // configurations where sample_interval_sec does not evenly divide
            // summary_interval_min×60, the true window length differs from the
            // configured target by up to one sample period.  Emitting the
            // measured value lets downstream analytics use the correct
            // denominator for rate calculations (e.g. average watts =
            // kwh_window / window_sec × 3,600,000).
            JAddNumberToObject(body, "window_sec",          14);
            J *rsp = notecard.requestAndResponse(req);
            if (!rsp || notecard.responseError(rsp)) {
                DBG_PRINTLN("[tmpl] summary template registration failed");
                ok = false;
            }
            if (rsp) notecard.deleteResponse(rsp);
        }
    }

    // cooler_alert.qo — string hint "14" reserves up to 14 characters
    {
        J *req = notecard.newRequest("note.template");
        if (!req) {
            DBG_PRINTLN("[tmpl] newRequest failed (alert)");
            ok = false;
        } else {
            JAddStringToObject(req, "file", FILE_ALERT);
            J *body = JAddObjectToObject(req, "body");
            JAddStringToObject(body, "alert",         "14");
            JAddNumberToObject(body, "temp_f",        14.1);
            JAddNumberToObject(body, "amps",          14.1);
            // 14 = 4-byte signed integer; doorContinuousOpenSec can grow well
            // beyond the 2-byte signed range (> 32,767 s ≈ 9.1 h) for a stuck
            // door when the 30-minute cooldown means re-alerts fire infrequently.
            JAddNumberToObject(body, "door_open_sec", 14);
            J *rsp = notecard.requestAndResponse(req);
            if (!rsp || notecard.responseError(rsp)) {
                DBG_PRINTLN("[tmpl] alert template registration failed");
                ok = false;
            }
            if (rsp) notecard.deleteResponse(rsp);
        }
    }

    return ok;
}

// ── Environment variable fetch ─────────────────────────────────────────────

// Parse helpers validate that the entire string was consumed, the string is
// non-empty, and (for integers) no overflow occurred.  A bad string returns
// false without touching the caller's config variable; the existing valid
// value is preserved and a debug warning is emitted.
static bool parseIntStr(const char *s, long &out) {
    if (!s || !*s) return false;
    char *end;
    errno = 0;
    out = strtol(s, &end, 10);
    return (end != s && *end == '\0' && errno == 0);
}

static bool parseDoubleStr(const char *s, double &out) {
    if (!s || !*s) return false;
    char *end;
    out = strtod(s, &end);
    if (end == s || *end != '\0') return false;
    // Reject "nan", "inf", "-inf", and overflowed values.  strtod() accepts
    // these strings as syntactically valid, but a NaN poisons every downstream
    // comparison silently (NaN comparisons all evaluate false, so a NaN value
    // falls through clampF's range check and ends up persisted into cfg).  An
    // infinity would similarly survive nothing useful.  Treat any non-finite
    // result as a malformed env var and let the caller retain the prior value.
    if (!isfinite(out)) return false;
    return true;
}

// Clamp helpers: return the clamped value, or fallback when out of range.
// These run after parseIntStr/parseDoubleStr have confirmed a valid numeric
// string, so fallback guards only against out-of-range values.
static uint32_t clampU32(long v, uint32_t minv, uint32_t maxv, uint32_t fallback) {
    if (v < (long)minv || v > (long)maxv) return fallback;
    return (uint32_t)v;
}

static float clampF(double v, float minv, float maxv, float fallback) {
    // Belt-and-suspenders against callers that bypass parseDoubleStr() — NaN
    // silently passes any < / > comparison, so without an isfinite() guard a
    // NaN would be cast to float and returned, poisoning every threshold that
    // consumes it.  Reject non-finite first, then range-check.
    if (!isfinite(v) || v < (double)minv || v > (double)maxv) return fallback;
    return (float)v;
}

// Returns true when env.get succeeds and the response body was processed;
// returns false on any Notecard communication failure.  Callers should only
// re-apply hub.set (applyHubSetIfChanged) when this returns true so a
// transient sync failure cannot revert the Notecard's outbound cadence to the
// compile-time default.  On success, the validated cfg globals are persisted
// into s so future wakes can restore them even if the next env.get fails.
bool fetchEnvOverrides(AppState &s) {
    J *req = notecard.newRequest("env.get");
    if (!req) { DBG_PRINTLN("[env] newRequest failed"); return false; }
    J *rsp = notecard.requestAndResponse(req);
    if (!rsp) return false;
    if (notecard.responseError(rsp)) { notecard.deleteResponse(rsp); return false; }

    J *env = JGetObject(rsp, "body");
    if (env) {
        const char *v;
        long   li;
        double df;

        v = JGetString(env, "sample_interval_sec");
        if (v && *v) {
            if (parseIntStr(v, li))
                cfgSampleSec = clampU32(li, 10u, 86400u, cfgSampleSec);
            else { DBG_PRINT("[env] bad sample_interval_sec: "); DBG_PRINTLN(v); }
        }

        v = JGetString(env, "summary_interval_min");
        if (v && *v) {
            if (parseIntStr(v, li))
                cfgSummaryMin = clampU32(li, 5u, 1440u, cfgSummaryMin);
            else { DBG_PRINT("[env] bad summary_interval_min: "); DBG_PRINTLN(v); }
        }

        // Temperature range covers commercial walk-in coolers and freezers (°F).
        v = JGetString(env, "temp_setpoint_f");
        if (v && *v) {
            if (parseDoubleStr(v, df))
                cfgTempSetpointF = clampF(df, -40.0f, 100.0f, cfgTempSetpointF);
            else { DBG_PRINT("[env] bad temp_setpoint_f: "); DBG_PRINTLN(v); }
        }

        v = JGetString(env, "temp_alert_f");
        if (v && *v) {
            if (parseDoubleStr(v, df))
                cfgTempAlertF = clampF(df, -40.0f, 100.0f, cfgTempAlertF);
            else { DBG_PRINT("[env] bad temp_alert_f: "); DBG_PRINTLN(v); }
        }

        v = JGetString(env, "door_open_alert_sec");
        if (v && *v) {
            if (parseIntStr(v, li))
                cfgDoorAlertSec = clampU32(li, 10u, 86400u, cfgDoorAlertSec);
            else { DBG_PRINT("[env] bad door_open_alert_sec: "); DBG_PRINTLN(v); }
        }

        // 0.1–100 A covers single-phase compressors from small reach-ins to
        // large walk-in units; below 0.1 A is indistinguishable from noise.
        v = JGetString(env, "compressor_on_amps");
        if (v && *v) {
            if (parseDoubleStr(v, df))
                cfgCompressorOnAmps = clampF(df, 0.1f, 100.0f, cfgCompressorOnAmps);
            else { DBG_PRINT("[env] bad compressor_on_amps: "); DBG_PRINTLN(v); }
        }

        // 90–260 V covers US 120 V, 208 V split-phase, and 230/240 V international.
        v = JGetString(env, "volts_nominal");
        if (v && *v) {
            if (parseDoubleStr(v, df))
                cfgVoltsNominal = clampF(df, 90.0f, 260.0f, cfgVoltsNominal);
            else { DBG_PRINT("[env] bad volts_nominal: "); DBG_PRINTLN(v); }
        }
    }
    notecard.deleteResponse(rsp);

    // Cross-validate: sample_interval_sec must not exceed the summary window.
    // If it does, a summary would fire on every single wake.  Clamp and warn so
    // operators know their env-var combination is out of range.
    const uint32_t summaryWindowSec = cfgSummaryMin * 60u;
    if (cfgSampleSec > summaryWindowSec) {
        DBG_PRINT("[env] sample_interval_sec (");  DBG_PRINT(cfgSampleSec);
        DBG_PRINT(") exceeds summary window (");   DBG_PRINT(summaryWindowSec);
        DBG_PRINTLN(" s) — clamping to summary window");
        cfgSampleSec = summaryWindowSec;
    }

    // Cross-validate: door_open_alert_sec must be at least sample_interval_sec.
    // A sampled door input cannot resolve alert thresholds shorter than one
    // sample period; clamp up so the configured threshold matches actual device
    // behaviour rather than silently promising precision the hardware cannot
    // deliver.
    if (cfgDoorAlertSec < cfgSampleSec) {
        DBG_PRINT("[env] door_open_alert_sec (");  DBG_PRINT(cfgDoorAlertSec);
        DBG_PRINT(") < sample_interval_sec (");    DBG_PRINT(cfgSampleSec);
        DBG_PRINTLN(" s) — clamping up to sample_interval_sec");
        cfgDoorAlertSec = cfgSampleSec;
    }

    // Persist the validated config so future wakes survive a transient env.get
    // failure without reverting operator-tuned values to compile-time defaults.
    s.persistedSampleSec        = cfgSampleSec;
    s.persistedSummaryMin       = cfgSummaryMin;
    s.persistedTempSetpointF    = cfgTempSetpointF;
    s.persistedTempAlertF       = cfgTempAlertF;
    s.persistedDoorAlertSec     = cfgDoorAlertSec;
    s.persistedCompressorOnAmps = cfgCompressorOnAmps;
    s.persistedVoltsNominal     = cfgVoltsNominal;
    s.configPersisted           = 1u;
    return true;
}

// ── Sensor reads ───────────────────────────────────────────────────────────

float readBoxTempF() {
    probe.requestTemperatures();
    // 12-bit conversion takes up to 750 ms; poll rather than using a fixed delay.
    const uint32_t deadline = millis() + 850;
    while (!probe.isConversionComplete() && millis() < deadline) {
        delay(10);
    }
    float tc = probe.getTempCByIndex(0);
    if (tc == DEVICE_DISCONNECTED_C || tc < -55.0f || tc >= 85.0f) {
        return NAN;  // sentinel: sensor missing or reading out of range
    }
    return tc * 9.0f / 5.0f + 32.0f;
}

float readCompressorAmps() {
    // Two-pass RMS: pass 1 derives the DC bias from the running mean of this
    // sample window; pass 2 computes the AC RMS around it.  Each pass runs for
    // CT_SAMPLE_MS milliseconds so the window always spans multiple complete
    // 60 Hz mains cycles regardless of actual ADC sample rate.
    const int adcMax = (1 << ADC_BITS) - 1;

    // Pass 1: DC bias (mean ADC count)
    float    sum = 0.0f;
    uint32_t n1  = 0;
    for (uint32_t t0 = millis(); millis() - t0 < CT_SAMPLE_MS; ) {
        sum += (float)analogRead(PIN_CT);
        n1++;
    }
    const float mean = (n1 > 0) ? sum / (float)n1 : 0.0f;

    // Pass 2: RMS of AC component around the bias
    float    sumSq = 0.0f;
    uint32_t n2    = 0;
    for (uint32_t t0 = millis(); millis() - t0 < CT_SAMPLE_MS; ) {
        float s = (float)analogRead(PIN_CT) - mean;
        sumSq += s * s;
        n2++;
    }
    const float rmsAdc = (n2 > 0) ? sqrtf(sumSq / (float)n2) : 0.0f;
    const float rmsV   = rmsAdc * (VREF_V / (float)adcMax);
    const float amps   = rmsV * CT_AMPS_PER_VOLT;
    return (amps < 0.15f) ? 0.0f : amps;  // floor ADC noise to zero when idle
}

bool readDoorOpen() {
    // INPUT_PULLUP: LOW when the door is closed (magnet closes the reed switch),
    // HIGH when the door is open (magnet away, switch opens).
    return (digitalRead(PIN_DOOR) == HIGH);
}

// ── Note emission ──────────────────────────────────────────────────────────

// Alert notes carry sync:true so the Notecard wakes the radio immediately
// rather than waiting for the next scheduled outbound window.
// Returns true if the Notecard confirmed the note was queued; callers must
// arm alert cooldowns only when this returns true.
bool sendAlert(const char *alert, float tempF, float amps, uint32_t doorSec) {
    J *req = notecard.newRequest("note.add");
    if (!req) {
        DBG_PRINT("[alert] newRequest failed for: "); DBG_PRINTLN(alert);
        return false;
    }
    JAddStringToObject(req, "file", FILE_ALERT);
    JAddBoolToObject(req, "sync", true);
    J *body = JAddObjectToObject(req, "body");
    JAddStringToObject(body, "alert",         alert);
    JAddNumberToObject(body, "temp_f",        (double)tempF);
    JAddNumberToObject(body, "amps",          (double)amps);
    JAddNumberToObject(body, "door_open_sec", (int32_t)doorSec);
    J *rsp = notecard.requestAndResponse(req);
    if (!rsp) {
        DBG_PRINT("[alert] no response for: "); DBG_PRINTLN(alert);
        return false;
    }
    bool ok = !notecard.responseError(rsp);
    if (!ok) { DBG_PRINT("[alert] note.add error for: "); DBG_PRINTLN(alert); }
    notecard.deleteResponse(rsp);
    return ok;
}

// Summary notes are queued without sync:true and flushed during the Notecard's
// scheduled outbound session, conserving cellular air time.
//
// windowSec is the sum of scheduled sample intervals for this window (sleep
// time only — awake time is excluded).  It may differ from cfgSummaryMin×60
// by up to one sample period when sample_interval_sec does not evenly divide
// the summary window.  Emitting it gives downstream analytics the correct
// denominator for energy-rate calculations.
//
// temp_f and compressor_amps reflect true window averages (sum / valid-sample
// count).  If no valid temperature reads occurred, temp_f carries the -9999
// sentinel so downstream can distinguish a sensor fault from a real near-zero
// reading.
//
// Returns true on success; the caller must not reset window accumulators
// unless this returns true.
bool sendSummary(AppState &s, uint32_t windowSec) {
    const float avgTempF = (s.tempFCount > 0)
                           ? (s.tempFSum / (float)s.tempFCount) : -9999.0f;
    const float avgAmps  = (s.ampsCount > 0)
                           ? (s.ampsSum / (float)s.ampsCount)   : 0.0f;
    const float runMin   = (float)s.compressorRunSec / 60.0f;

    J *req = notecard.newRequest("note.add");
    if (!req) { DBG_PRINTLN("[summary] newRequest failed"); return false; }
    JAddStringToObject(req, "file", FILE_SUMMARY);
    J *body = JAddObjectToObject(req, "body");
    JAddNumberToObject(body, "temp_f",             (double)avgTempF);
    JAddNumberToObject(body, "setpoint_f",         (double)cfgTempSetpointF);
    JAddNumberToObject(body, "compressor_amps",    (double)avgAmps);
    JAddNumberToObject(body, "compressor_run_min", (double)runMin);
    JAddNumberToObject(body, "door_opens",         (int)s.doorOpenCount);
    JAddNumberToObject(body, "door_open_sec",      (int32_t)s.doorOpenSec);
    JAddNumberToObject(body, "kwh_window",         (double)s.kwhAccum);
    JAddNumberToObject(body, "window_sec",         (int32_t)windowSec);
    J *rsp = notecard.requestAndResponse(req);
    if (!rsp) { DBG_PRINTLN("[summary] no response from Notecard"); return false; }
    bool ok = !notecard.responseError(rsp);
    if (!ok) DBG_PRINTLN("[summary] note.add failed");
    notecard.deleteResponse(rsp);
    return ok;
}
