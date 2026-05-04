/***************************************************************************
  notecard_helpers.cpp — Notecard configuration, template registration,
  environment variable fetch, and note submission for post_discharge_vitals_hub.
***************************************************************************/

#include "notecard_helpers.h"

// ─────────────────────────────────────────────────────────────────────────────
// LOW-LEVEL REQUEST HELPERS
// ─────────────────────────────────────────────────────────────────────────────

// Send a request and check the response for an error field.  Used for
// configuration calls where a silent failure would leave the device
// misconfigured.  Returns true on success; always consumes the J*.
bool sendChecked(J *req) {
    J *rsp = notecard.requestAndResponse(req);
    if (!rsp) {
        DBG_PRINTLN("[NOTE] No response from Notecard");
        return false;
    }
    const char *err = JGetString(rsp, "err");
    bool ok = !(err && *err);
    if (!ok) { DBG_PRINT("[NOTE] Notecard error: %s\n", err); }
    notecard.deleteResponse(rsp);
    return ok;
}

// Enqueue a vitals_alert.qo note with checked delivery and retry.
// Takes ownership of `body` (always freed on return).  Retries up to
// ALERT_ENQUEUE_RETRIES times on I2C failure; each attempt re-wraps body in
// a fresh note.add request.  Logs clearly on final failure so the fault is
// visible in the serial monitor rather than being silently swallowed.
static bool sendAlertNote(J *body) {
    bool ok = false;
    for (uint8_t attempt = 1; attempt <= ALERT_ENQUEUE_RETRIES; ++attempt) {
        J *req = notecard.newRequest("note.add");
        if (!req) { delay(50); continue; }
        JAddStringToObject(req, "file", "vitals_alert.qo");
        JAddBoolToObject  (req, "sync", true);
        // Duplicate body for each retry; the request object takes ownership of
        // whatever is added to it and frees it inside requestAndResponse.
        J *b = JDuplicate(body, /*recurse=*/true);
        if (!b) { JDelete(req); delay(50); continue; }
        JAddItemToObject(req, "body", b);
        J *rsp = notecard.requestAndResponse(req);
        if (rsp) {
            const char *err = JGetString(rsp, "err");
            ok = !(err && *err);
            if (!ok) {
                DBG_PRINT("[NOTE] vitals_alert enqueue error (attempt %d/%d): %s\n",
                          attempt, (int)ALERT_ENQUEUE_RETRIES, err);
            }
            notecard.deleteResponse(rsp);
            if (ok) break;
        } else {
            DBG_PRINT("[NOTE] vitals_alert: no Notecard response (attempt %d/%d)\n",
                      attempt, (int)ALERT_ENQUEUE_RETRIES);
        }
        if (attempt < ALERT_ENQUEUE_RETRIES) delay(100);
    }
    JDelete(body);
    if (!ok) DBG_PRINTLN("[NOTE] vitals_alert.qo enqueue failed after all retries — alert may be lost");
    return ok;
}

// Enqueue a vital measurement note with checked delivery and retry.
// Takes ownership of `body` (built by caller; always freed on return).
// `addSync` controls whether sync:true is added.  Threshold-tripping notes
// always pass true so the Notecard starts an immediate outbound session;
// the companion vitals_alert.qo note (also sync:true) is enqueued next and
// travels in the same or immediately following cellular session.
static bool sendVitalNoteChecked(const char *file, J *body, bool addSync) {
    bool ok = false;
    for (uint8_t attempt = 1; attempt <= ALERT_ENQUEUE_RETRIES; ++attempt) {
        J *req = notecard.newRequest("note.add");
        if (!req) { delay(50); continue; }
        JAddStringToObject(req, "file", file);
        if (addSync) JAddBoolToObject(req, "sync", true);
        // Duplicate body for each retry; requestAndResponse frees the request
        // (and any children added to it) on return.
        J *b = JDuplicate(body, /*recurse=*/true);
        if (!b) { JDelete(req); delay(50); continue; }
        JAddItemToObject(req, "body", b);
        J *rsp = notecard.requestAndResponse(req);
        if (rsp) {
            const char *err = JGetString(rsp, "err");
            ok = !(err && *err);
            if (!ok) {
                DBG_PRINT("[NOTE] %s enqueue error (attempt %d/%d): %s\n",
                          file, attempt, (int)ALERT_ENQUEUE_RETRIES, err);
            }
            notecard.deleteResponse(rsp);
            if (ok) break;
        } else {
            DBG_PRINT("[NOTE] %s: no Notecard response (attempt %d/%d)\n",
                      file, attempt, (int)ALERT_ENQUEUE_RETRIES);
        }
        if (attempt < ALERT_ENQUEUE_RETRIES) delay(100);
    }
    JDelete(body);
    if (!ok) DBG_PRINT("[NOTE] %s enqueue failed after all retries\n", file);
    return ok;
}

// ─────────────────────────────────────────────────────────────────────────────
// NOTECARD CONFIGURATION
// ─────────────────────────────────────────────────────────────────────────────

// Configure Notecard hub connection.  An empty PRODUCT_UID is caught at
// compile time by the guard in vitals_config.h; the runtime check below is a
// belt-and-suspenders safety net for the ALLOW_EMPTY_PRODUCT_UID development
// path.  Retries up to 5 s to paper over the cold-boot I2C race where the
// nRF52840 comes up before the Notecard firmware has finished initializing,
// and inspects the response err field so a rejected configuration is logged
// rather than silently swallowed.
void notecardConfigure() {
    if (PRODUCT_UID[0] == '\0') {
        DBG_PRINTLN("[CFG] PRODUCT_UID is empty — hub will not associate with a Notehub project");
        return;
    }
    bool ok = false;
    const uint32_t kDeadlineMs = 5000UL;
    const uint32_t t0 = millis();
    do {
        J *req = notecard.newRequest("hub.set");
        if (!req) break;
        JAddStringToObject(req, "product",  PRODUCT_UID);
        JAddStringToObject(req, "mode",     "periodic");
        JAddNumberToObject(req, "outbound", OUTBOUND_INTERVAL_MIN);
        JAddNumberToObject(req, "inbound",  INBOUND_INTERVAL_MIN);
        ok = sendChecked(req);
        if (!ok && (millis() - t0) < kDeadlineMs) delay(500);
    } while (!ok && (millis() - t0) < kDeadlineMs);

    if (!ok) {
        DBG_PRINTLN("[CFG] hub.set failed — check Notecard connection and PRODUCT_UID value");
    }
}

// Register fixed-width templates for each Notefile.  Templates store notes as
// compact binary records on the Notecard, reducing per-reading wire cost by
// 3–5× versus free-form JSON.
void defineTemplates() {
    // Weight: two 4-byte floats (current + previous, for delta calculation)
    {
        J *req  = notecard.newRequest("note.template");
        JAddStringToObject(req, "file", "weight.qo");
        J *body = JAddObjectToObject(req, "body");
        JAddNumberToObject(body, "weight_kg", TFLOAT32);
        JAddNumberToObject(body, "prev_kg",   TFLOAT32);
        sendChecked(req);
    }
    // Blood pressure: three 2-byte signed integers
    {
        J *req  = notecard.newRequest("note.template");
        JAddStringToObject(req, "file", "bp.qo");
        J *body = JAddObjectToObject(req, "body");
        JAddNumberToObject(body, "systolic_mmhg",  TINT16);
        JAddNumberToObject(body, "diastolic_mmhg", TINT16);
        JAddNumberToObject(body, "pulse_bpm",      TINT16);
        sendChecked(req);
    }
    // Pulse oximeter: two 2-byte signed integers
    {
        J *req  = notecard.newRequest("note.template");
        JAddStringToObject(req, "file", "spo2.qo");
        J *body = JAddObjectToObject(req, "body");
        JAddNumberToObject(body, "spo2_pct",  TINT16);
        JAddNumberToObject(body, "pulse_bpm", TINT16);
        sendChecked(req);
    }
    // Activity band: 2-byte heart rate
    {
        J *req  = notecard.newRequest("note.template");
        JAddStringToObject(req, "file", "activity.qo");
        J *body = JAddObjectToObject(req, "body");
        JAddNumberToObject(body, "heart_rate_bpm", TINT16);
        sendChecked(req);
    }
    // Alert notes are not templated — their body shape varies by alert type
    // and they are low-frequency, so JSON overhead is negligible.
}

// ─────────────────────────────────────────────────────────────────────────────
// ENVIRONMENT VARIABLE FETCH
// ─────────────────────────────────────────────────────────────────────────────

// Validate and clamp a parsed env-var float.  Returns the value unchanged when
// it falls within [lo, hi]; returns the existing fallback and logs a warning
// otherwise.  Prevents out-of-range or negative values from inverting alert
// comparisons (e.g. a negative HR threshold compared against uint16_t bpm).
static float validateEnvFloat(const char *name, float val,
                               float lo, float hi, float fallback) {
    if (val >= lo && val <= hi) return val;
    DBG_PRINT("[ENV] %s=%.2f out of range [%.2f, %.2f] — keeping %.2f\n",
              name, val, lo, hi, fallback);
    return fallback;
}

// Pull all six threshold variables from Notehub and update globals.
// Called once at boot and then every ENV_POLL_MS thereafter.
// Each value is range-checked; related pairs (diastolic/systolic, hr_low/
// hr_high) are cross-validated before being stored.
void fetchEnvVars() {
    J *req   = notecard.newRequest("env.get");
    J *names = JAddArrayToObject(req, "names");
    JAddItemToArray(names, JCreateString("bp_systolic_high"));
    JAddItemToArray(names, JCreateString("bp_diastolic_high"));
    JAddItemToArray(names, JCreateString("spo2_low"));
    JAddItemToArray(names, JCreateString("hr_high"));
    JAddItemToArray(names, JCreateString("hr_low"));
    JAddItemToArray(names, JCreateString("weight_delta_kg"));

    J *rsp = notecard.requestAndResponse(req);
    if (!rsp) return;
    const char *rsp_err = JGetString(rsp, "err");
    if (rsp_err && *rsp_err) {
        DBG_PRINT("[ENV] env.get error: %s\n", rsp_err);
        notecard.deleteResponse(rsp);
        return;
    }

    J *body = JGetObject(rsp, "body");
    if (body) {
        const char *v;

        // BP thresholds — range-check each, then enforce diastolic < systolic
        float new_sys = g_bp_systolic_high, new_dia = g_bp_diastolic_high;
        if ((v = JGetString(body, "bp_systolic_high"))  && *v)
            new_sys = validateEnvFloat("bp_systolic_high",  atof(v),
                                       60.0f, 260.0f, g_bp_systolic_high);
        if ((v = JGetString(body, "bp_diastolic_high")) && *v)
            new_dia = validateEnvFloat("bp_diastolic_high", atof(v),
                                       30.0f, 160.0f, g_bp_diastolic_high);
        if (new_dia < new_sys) {
            g_bp_systolic_high  = new_sys;
            g_bp_diastolic_high = new_dia;
        } else {
            DBG_PRINTLN("[ENV] bp_diastolic_high >= bp_systolic_high — rejecting both BP thresholds");
        }

        // SpO2 threshold
        if ((v = JGetString(body, "spo2_low")) && *v)
            g_spo2_low = validateEnvFloat("spo2_low", atof(v),
                                          70.0f, 99.0f, g_spo2_low);

        // HR thresholds — range-check each, then enforce hr_low < hr_high
        float new_hr_high = g_hr_high, new_hr_low = g_hr_low;
        if ((v = JGetString(body, "hr_high")) && *v)
            new_hr_high = validateEnvFloat("hr_high", atof(v),
                                           60.0f, 250.0f, g_hr_high);
        if ((v = JGetString(body, "hr_low"))  && *v)
            new_hr_low  = validateEnvFloat("hr_low",  atof(v),
                                           20.0f, 100.0f, g_hr_low);
        if (new_hr_low < new_hr_high) {
            g_hr_high = new_hr_high;
            g_hr_low  = new_hr_low;
        } else {
            DBG_PRINTLN("[ENV] hr_low >= hr_high — rejecting both HR thresholds");
        }

        // Weight delta threshold
        if ((v = JGetString(body, "weight_delta_kg")) && *v)
            g_weight_delta_kg = validateEnvFloat("weight_delta_kg", atof(v),
                                                  0.5f, 20.0f, g_weight_delta_kg);
    }
    notecard.deleteResponse(rsp);
    DBG_PRINTLN("[ENV] Thresholds refreshed");
}

// ─────────────────────────────────────────────────────────────────────────────
// NOTE SUBMISSION HELPERS
// ─────────────────────────────────────────────────────────────────────────────
// Each function enqueues one reading note into the appropriate Notefile.
//
// All readings — routine and threshold-tripping alike — use checked+retried
// delivery via sendVitalNoteChecked() so a transient host→Notecard I²C failure
// cannot silently drop a patient measurement.  Routine readings pass
// addSync=false (the normal periodic outbound cadence applies); threshold-
// tripping readings pass addSync=true so the Notecard starts an immediate
// outbound session for that measurement regardless of alert-cooldown state.
//
// The companion vitals_alert.qo note (also sync:true) is enqueued immediately
// after a threshold trip (when out of cooldown) so both notes typically travel
// in the same cellular session.  The alert cooldown timestamp advances only
// after sendAlertNote() confirms successful enqueue, ensuring a transient I²C
// failure never silently suppresses future alerts for the same vital type.

void submitWeight(float kg, float prev_kg) {
    float delta             = (prev_kg > 0.0f) ? (kg - prev_kg) : 0.0f;
    bool  threshold_tripped = (prev_kg > 0.0f) && (delta >= g_weight_delta_kg);
    bool  alert             = threshold_tripped &&
                              (millis() - g_last_weight_alert_ms >= ALERT_COOLDOWN_MS);

    if (threshold_tripped) {
        // sync:true on the measurement note guarantees an immediate outbound
        // session for this reading regardless of the alert-cooldown state.
        J *mbody = JCreateObject();
        JAddNumberToObject(mbody, "weight_kg", kg);
        JAddNumberToObject(mbody, "prev_kg",   prev_kg);
        bool note_ok = sendVitalNoteChecked("weight.qo", mbody, /*addSync=*/true);

        if (note_ok && alert) {
            J *ab = JCreateObject();
            JAddStringToObject(ab, "alert",     "weight_gain");
            JAddNumberToObject(ab, "weight_kg", (double)kg);
            JAddNumberToObject(ab, "delta_kg",  (double)delta);
            if (sendAlertNote(ab)) { g_last_weight_alert_ms = millis(); }
            DBG_PRINT("[ALERT] Weight gain %.2f kg vs. threshold %.2f kg\n",
                      delta, g_weight_delta_kg);
        }
    } else {
        J *mbody = JCreateObject();
        JAddNumberToObject(mbody, "weight_kg", kg);
        JAddNumberToObject(mbody, "prev_kg",   prev_kg);
        sendVitalNoteChecked("weight.qo", mbody, /*addSync=*/false);
    }
}

void submitBp(int16_t systolic, int16_t diastolic, int16_t pulse) {
    bool  threshold_tripped = ((float)systolic  >= g_bp_systolic_high) ||
                              ((float)diastolic >= g_bp_diastolic_high);
    bool  alert             = threshold_tripped &&
                              (millis() - g_last_bp_alert_ms >= ALERT_COOLDOWN_MS);

    if (threshold_tripped) {
        J *mbody = JCreateObject();
        JAddNumberToObject(mbody, "systolic_mmhg",  systolic);
        JAddNumberToObject(mbody, "diastolic_mmhg", diastolic);
        JAddNumberToObject(mbody, "pulse_bpm",      pulse);
        bool note_ok = sendVitalNoteChecked("bp.qo", mbody, /*addSync=*/true);

        if (note_ok && alert) {
            J *ab = JCreateObject();
            JAddStringToObject(ab, "alert",          "bp_high");
            JAddNumberToObject(ab, "systolic_mmhg",  systolic);
            JAddNumberToObject(ab, "diastolic_mmhg", diastolic);
            if (sendAlertNote(ab)) { g_last_bp_alert_ms = millis(); }
            DBG_PRINT("[ALERT] BP %d/%d mmHg exceeds threshold\n", systolic, diastolic);
        }
    } else {
        J *mbody = JCreateObject();
        JAddNumberToObject(mbody, "systolic_mmhg",  systolic);
        JAddNumberToObject(mbody, "diastolic_mmhg", diastolic);
        JAddNumberToObject(mbody, "pulse_bpm",      pulse);
        sendVitalNoteChecked("bp.qo", mbody, /*addSync=*/false);
    }
}

void submitSpO2(int16_t spo2_pct, int16_t pulse) {
    bool  threshold_tripped = (spo2_pct > 0) && ((float)spo2_pct < g_spo2_low);
    bool  alert             = threshold_tripped &&
                              (millis() - g_last_spo2_alert_ms >= ALERT_COOLDOWN_MS);

    if (threshold_tripped) {
        J *mbody = JCreateObject();
        JAddNumberToObject(mbody, "spo2_pct",  spo2_pct);
        JAddNumberToObject(mbody, "pulse_bpm", pulse);
        bool note_ok = sendVitalNoteChecked("spo2.qo", mbody, /*addSync=*/true);

        if (note_ok && alert) {
            J *ab = JCreateObject();
            JAddStringToObject(ab, "alert",    "spo2_low");
            JAddNumberToObject(ab, "spo2_pct", spo2_pct);
            if (sendAlertNote(ab)) { g_last_spo2_alert_ms = millis(); }
            DBG_PRINT("[ALERT] SpO2 %d%% below threshold %.0f%%\n", spo2_pct, g_spo2_low);
        }
    } else {
        J *mbody = JCreateObject();
        JAddNumberToObject(mbody, "spo2_pct",  spo2_pct);
        JAddNumberToObject(mbody, "pulse_bpm", pulse);
        sendVitalNoteChecked("spo2.qo", mbody, /*addSync=*/false);
    }
}

// hr_high and hr_low use independent cooldown timestamps so a tachycardia
// alert cannot suppress a clinically distinct bradycardia alert, or vice versa.
void submitActivity(uint16_t hr_bpm) {
    bool hr_high_tripped   = (hr_bpm > 0) && ((float)hr_bpm > g_hr_high);
    bool hr_low_tripped    = (hr_bpm > 0) && ((float)hr_bpm < g_hr_low);
    bool threshold_tripped = hr_high_tripped || hr_low_tripped;

    uint32_t now = millis();
    bool hr_high_alert = hr_high_tripped && (now - g_last_hr_high_alert_ms >= ALERT_COOLDOWN_MS);
    bool hr_low_alert  = hr_low_tripped  && (now - g_last_hr_low_alert_ms  >= ALERT_COOLDOWN_MS);

    if (threshold_tripped) {
        J *mbody = JCreateObject();
        JAddNumberToObject(mbody, "heart_rate_bpm", hr_bpm);
        bool note_ok = sendVitalNoteChecked("activity.qo", mbody, /*addSync=*/true);

        if (note_ok) {
            if (hr_high_alert) {
                J *ab = JCreateObject();
                JAddStringToObject(ab, "alert",          "hr_high");
                JAddNumberToObject(ab, "heart_rate_bpm", hr_bpm);
                if (sendAlertNote(ab)) { g_last_hr_high_alert_ms = now; }
                DBG_PRINT("[ALERT] Heart rate %d bpm — hr_high\n", hr_bpm);
            }
            if (hr_low_alert) {
                J *ab = JCreateObject();
                JAddStringToObject(ab, "alert",          "hr_low");
                JAddNumberToObject(ab, "heart_rate_bpm", hr_bpm);
                if (sendAlertNote(ab)) { g_last_hr_low_alert_ms = now; }
                DBG_PRINT("[ALERT] Heart rate %d bpm — hr_low\n", hr_bpm);
            }
            // No hub.sync needed: sync:true on the measurement note above
            // already triggers an outbound session for care-team visibility.
        }
    } else {
        J *mbody = JCreateObject();
        JAddNumberToObject(mbody, "heart_rate_bpm", hr_bpm);
        sendVitalNoteChecked("activity.qo", mbody, /*addSync=*/false);
    }
}
