/***************************************************************************
  solar_battery_controller_notecard_helpers - Notecard & Configuration
  for Off-Grid Solar Battery Site Controller

  Implements all Notecard I/O: hub.set, note.template, env.get, and both
  outbound note.add paths (periodic summary and immediate alert).

  Both note.add paths use requestAndResponse() so the Notecard response
  err field is inspected before callers may update alert state or reset
  accumulators.

  The globals notecard, state, desired_outbound_min, and desired_inbound_min
  are defined in solar_battery_controller.ino; this file references them
  through the extern declarations below.
***************************************************************************/

#include "solar_battery_controller_notecard_helpers.h"

// Globals defined in solar_battery_controller.ino
extern Notecard     notecard;
extern AppState state;
extern uint32_t     desired_outbound_min;
extern uint32_t     desired_inbound_min;

// ---------------------------------------------------------------------------
// Validated env-var parsing helpers (file-scope only).
//
// Both functions return `cur` (the existing setting) when the key is absent,
// empty, not a valid number, or outside the supplied range — a bad env var
// can never silently zero a threshold or create an invalid interval.
// ---------------------------------------------------------------------------
static uint32_t envU32(J *body, const char *key,
                        uint32_t lo, uint32_t hi, uint32_t cur) {
    const char *s = JGetString(body, key);
    if (!s || !*s) return cur;
    char *end;
    long v = strtol(s, &end, 10);
    if (end == s || v < (long)lo || v > (long)hi) return cur;
    return (uint32_t)v;
}

static float envF32(J *body, const char *key, float lo, float hi, float cur) {
    const char *s = JGetString(body, key);
    if (!s || !*s) return cur;
    char *end;
    float v = strtof(s, &end);
    if (end == s || v < lo || v > hi) return cur;
    return v;
}

// ---------------------------------------------------------------------------
// notecardFirstBoot — one-time hardware init at power-up (from setup()):
// disables the onboard accelerometer to reduce idle draw.
//
// hub.set is NOT called here.  It is handled by applyHubSetIfChanged() on
// every wake so that cadence is authoritative for the current firmware and
// env-var settings even after a firmware update.
// ---------------------------------------------------------------------------
void notecardFirstBoot() {
    J *req = notecard.newRequest("card.motion.mode");
    if (req) JAddBoolToObject(req, "stop", true);
    if (!notecard.sendRequest(req)) {
        dbgSerial.println(F("[warn] card.motion.mode failed"));
    }
}

// ---------------------------------------------------------------------------
// defineTemplates — registers fixed-width Note templates to minimise
// on-wire payload size over the lifetime of the deployment.
//
// Returns true when the template is confirmed registered.  The caller should
// store the result in state.templates_confirmed and call this function again
// on the next wake until it returns true, so a transient failure is always
// recovered automatically.
// ---------------------------------------------------------------------------
bool defineTemplates() {
    bool registered = false;
    J *req = notecard.newRequest("note.template");
    if (req) {
        JAddStringToObject(req, "file", "solar_summary.qo");
        JAddNumberToObject(req, "port", 50);
        J *body = JAddObjectToObject(req, "body");
        JAddNumberToObject(body, "bat_v",      14.1);  // 4-byte float, 1 decimal
        JAddNumberToObject(body, "bat_a",      14.1);
        JAddNumberToObject(body, "bat_w",      14.1);
        JAddNumberToObject(body, "soc_pct",    14.1);
        JAddNumberToObject(body, "bat_temp_c", 14.1);
        JAddNumberToObject(body, "pv_v",       14.1);
        JAddNumberToObject(body, "pv_w",       14.1);
        JAddNumberToObject(body, "yield_kwh",  14.1);
        JAddNumberToObject(body, "load_w",     14.1);
        JAddNumberToObject(body, "ttg_min",    14);    // 4-byte int
        JAddNumberToObject(body, "cs",         12);    // 2-byte int
        J *rsp = notecard.requestAndResponse(req);
        if (rsp) {
            const char *err = JGetString(rsp, "err");
            registered = (!err || !*err);
            notecard.deleteResponse(rsp);
        }
    }
    if (!registered) {
        dbgSerial.println(F("[warn] note.template solar_summary.qo failed"));
    }
    return registered;
}

// ---------------------------------------------------------------------------
// applyHubSetIfChanged — issues hub.set on every wake when PRODUCT_UID is
// configured so that a firmware reflash with a different UID immediately
// corrects the Notecard's Notehub association, regardless of whether the
// sync cadence changed.  Keying hub.set replay solely on cadence would allow
// a reflashed device to remain associated with the old project indefinitely.
//
// When PRODUCT_UID is empty, hub.set is skipped if the cadence is also
// unchanged — there is nothing to correct in that edge case.
//
// Uses requestAndResponse to inspect the err field; state.last_* fields are
// only updated on confirmed success so a transient failure will be retried on
// the next wake.
// ---------------------------------------------------------------------------
void applyHubSetIfChanged(const char *product_uid) {
    bool uid_set = (product_uid && product_uid[0]);
    if (!uid_set &&
        desired_outbound_min == state.last_outbound_min &&
        desired_inbound_min  == state.last_inbound_min) {
        return;
    }

    J *req = notecard.newRequest("hub.set");
    if (req) {
        if (product_uid && product_uid[0])
            JAddStringToObject(req, "product", product_uid);
        JAddStringToObject(req, "mode",     "periodic");
        JAddNumberToObject(req, "outbound", (int)desired_outbound_min);
        JAddNumberToObject(req, "inbound",  (int)desired_inbound_min);

        J *rsp = notecard.requestAndResponse(req);
        if (!rsp) {
            dbgSerial.println(F("[warn] hub.set: no response"));
        } else {
            const char *err = JGetString(rsp, "err");
            bool ok = (!err || !*err);
            if (!ok) {
                dbgSerial.print(F("[warn] hub.set error: "));
                dbgSerial.println(err);  // log before deleteResponse
            }
            notecard.deleteResponse(rsp);
            if (ok) {
                state.last_outbound_min = desired_outbound_min;
                state.last_inbound_min  = desired_inbound_min;
                return;
            }
        }
    }
    dbgSerial.println(F("[error] hub.set failed — sync cadence may be incorrect"));
}

// ---------------------------------------------------------------------------
// fetchEnvOverrides — pulls environment variables from Notehub and updates
// cached settings.  Only modifies fields that are present and within valid
// range; a typo or out-of-range value silently keeps the existing setting.
//
// Returns true when sample_interval_sec or report_interval_min changed so
// the caller (setup()) can flush the current accumulation window before the
// new cadence takes effect.  This keeps the flush logic in the orchestrating
// sketch rather than requiring this module to call back into the .ino.
//
// When the cadence changes, state.last_outbound_min and last_inbound_min are
// zeroed here to force applyHubSetIfChanged() to re-issue hub.set even if
// the desired values happen to equal what was last applied.
// ---------------------------------------------------------------------------
bool fetchEnvOverrides() {
    // Pre-load sync cadence defaults from report_interval_min so the values are
    // valid even when env.get cannot reach the Notecard.
    // fetchEnvOverrides() is called after state is initialised so
    // state.report_interval_min is always >= DEFAULT_REPORT_INTERVAL_MIN here.
    desired_outbound_min = state.report_interval_min;
    desired_inbound_min  = state.report_interval_min * 2;

    J *req = notecard.newRequest("env.get");
    if (!req) return false;
    J *names = JCreateArray();
    JAddItemToArray(names, JCreateString("soc_alert_pct"));
    JAddItemToArray(names, JCreateString("bat_temp_max_c"));
    JAddItemToArray(names, JCreateString("load_alert_w"));
    JAddItemToArray(names, JCreateString("sample_interval_sec"));
    JAddItemToArray(names, JCreateString("report_interval_min"));
    JAddItemToArray(names, JCreateString("harvest_deficit_days"));
    JAddItemToArray(names, JCreateString("sync_outbound_min"));
    JAddItemToArray(names, JCreateString("sync_inbound_min"));
    JAddItemToObject(req, "names", names);
    J *rsp = notecard.requestAndResponse(req);
    if (rsp && notecard.responseError(rsp)) {
        notecard.deleteResponse(rsp);
        rsp = NULL;
    }
    if (!rsp) return false;

    bool cadence_changed = false;

    J *body = JGetObject(rsp, "body");
    if (body) {
        // Threshold overrides — range-clamped so a typo cannot disable an alert
        // by silently zeroing a threshold or pushing it out of sensor range.
        state.soc_alert_pct  = envF32(body, "soc_alert_pct",
                                       1.0f, 99.0f, state.soc_alert_pct);
        state.bat_temp_max_c = envF32(body, "bat_temp_max_c",
                                       0.0f, 80.0f, state.bat_temp_max_c);
        state.load_alert_w   = envF32(body, "load_alert_w",
                                       1.0f, 50000.0f, state.load_alert_w);
        state.harvest_deficit_days = envF32(body, "harvest_deficit_days",
                                             0.0f, 30.0f,
                                             state.harvest_deficit_days);

        // Cadence overrides
        uint32_t new_sample = envU32(body, "sample_interval_sec",
                                     60, 3600, state.sample_interval_sec);
        uint32_t new_report = envU32(body, "report_interval_min",
                                     15, 1440, state.report_interval_min);
        cadence_changed = (new_sample != state.sample_interval_sec ||
                           new_report != state.report_interval_min);
        state.sample_interval_sec = new_sample;
        state.report_interval_min = new_report;

        // Sync cadence defaults to report_interval_min / 2× for inbound so the
        // Notecard's outbound cadence always matches the summary window by default.
        // sync_outbound_min / sync_inbound_min are optional operator overrides for
        // decoupling transmission frequency from local summary cadence (e.g. when
        // report and sync intervals need to differ on a shared-fleet configuration).
        desired_outbound_min = state.report_interval_min;
        desired_inbound_min  = state.report_interval_min * 2;
        desired_outbound_min = envU32(body, "sync_outbound_min",
                                      15, 1440, desired_outbound_min);
        desired_inbound_min  = envU32(body, "sync_inbound_min",
                                      30, 2880, desired_inbound_min);

        if (cadence_changed) {
            // report_interval_min drives the default sync cadence; zero the
            // last-applied values to force applyHubSetIfChanged() to re-issue
            // hub.set even if the desired values happen to equal what was last
            // applied.
            state.last_outbound_min = 0;
            state.last_inbound_min  = 0;
        }
    }
    notecard.deleteResponse(rsp);
    return cadence_changed;
}

// ---------------------------------------------------------------------------
// sendAlert — emit an immediate, sync:true Note for operator notification.
//
// Uses requestAndResponse to inspect the Notecard response err field so that
// a card-side rejection (schema mismatch, out-of-memory, etc.) is detected
// rather than silently treated as success.
//
// Returns true only after the Note is confirmed queued.  Callers must not
// arm suppression state (active flags, cooldowns) on a false return.
// v1/v2/v3 carry alert-type-specific context values (see README §7).
// ---------------------------------------------------------------------------
bool sendAlert(const char *alert, float v1, float v2, float v3) {
    J *req = notecard.newRequest("note.add");
    if (req) {
        JAddStringToObject(req, "file",  "solar_alert.qo");
        JAddBoolToObject(req,   "sync",  true);
        J *body = JAddObjectToObject(req, "body");
        JAddStringToObject(body, "alert", alert);
        JAddNumberToObject(body, "v1",    v1);
        JAddNumberToObject(body, "v2",    v2);
        JAddNumberToObject(body, "v3",    v3);

        J *rsp = notecard.requestAndResponse(req);
        if (!rsp) {
            dbgSerial.println(F("[warn] note.add alert: no response"));
        } else {
            const char *err = JGetString(rsp, "err");
            bool ok = (!err || !*err);
            if (!ok) {
                dbgSerial.print(F("[warn] note.add alert error: "));
                dbgSerial.println(err);  // log before deleteResponse
            }
            notecard.deleteResponse(rsp);
            if (ok) {
                dbgSerial.print(F("[alert] ")); dbgSerial.println(alert);
                return true;
            }
        }
    }
    dbgSerial.print(F("[error] note.add failed for alert: ")); dbgSerial.println(alert);
    return false;
}

// ---------------------------------------------------------------------------
// sendSummary — compute window averages and push a solar_summary.qo Note.
//
// Uses requestAndResponse to inspect the Notecard response err field so that
// a card-side rejection is detected rather than silently treated as success.
//
// Returns true only after the Note is confirmed queued.  Callers must not
// reset accumulators on a false return so the window data is preserved for
// the next attempt rather than silently dropped.
//
// Every field in the fixed-schema template is always present in the Note body.
// Fields with no valid samples in the window are filled with sentinel constants
// (SUMMARY_SENTINEL_F = -9999 for floats; SUMMARY_SENTINEL_TTG = -9999 for
// ttg_min; SUMMARY_SENTINEL_CS = -1 for cs) rather than omitted.  This keeps
// the template schema fully populated so downstream analytics can rely on
// column presence and distinguish 'no data' from a real zero measurement.
// Aligns with the standard sentinel pattern used across Blues reference designs.
//
// Exception: if an entire window produced no valid samples from either device
// the Note is skipped entirely (returns true to open a new window, no Note
// queued) to avoid consuming satellite data quota on an all-sentinel record.
// ---------------------------------------------------------------------------
bool sendSummary() {
    // Skip if neither device provided any samples this window.
    bool any_valid = (state.bat_v_cnt > 0.0f || state.pv_w_cnt > 0.0f);
    if (!any_valid) {
        dbgSerial.println(F("[warn] sendSummary: no valid samples in window — skipping"));
        return true;  // treated as success so callers open a new window
    }

    J *req = notecard.newRequest("note.add");
    if (!req) {
        dbgSerial.println(F("[warn] note.add: failed to allocate request for solar_summary.qo"));
        return false;
    }
    JAddStringToObject(req, "file", "solar_summary.qo");
    J *body = JAddObjectToObject(req, "body");

    // Always emit every template field.  Use the sentinel when no valid samples
    // exist so the Notecard template engine never sees a missing column.
    #define SAFE_AVG(sum, cnt) \
        ((cnt) > 0.0f ? (sum) / (cnt) : SUMMARY_SENTINEL_F)

    JAddNumberToObject(body, "bat_v",      SAFE_AVG(state.bat_v_sum,  state.bat_v_cnt));
    JAddNumberToObject(body, "bat_a",      SAFE_AVG(state.bat_a_sum,  state.bat_a_cnt));
    JAddNumberToObject(body, "bat_w",      SAFE_AVG(state.bat_w_sum,  state.bat_w_cnt));
    JAddNumberToObject(body, "soc_pct",    SAFE_AVG(state.soc_sum,    state.soc_cnt));
    JAddNumberToObject(body, "bat_temp_c", SAFE_AVG(state.temp_sum,   state.temp_cnt));
    JAddNumberToObject(body, "pv_v",       SAFE_AVG(state.pv_v_sum,   state.pv_v_cnt));
    JAddNumberToObject(body, "pv_w",       SAFE_AVG(state.pv_w_sum,   state.pv_w_cnt));
    JAddNumberToObject(body, "load_w",     SAFE_AVG(state.load_w_sum, state.load_w_cnt));
    #undef SAFE_AVG

    // yield_kwh and cs carry the latest value at window-close, not an average.
    // Use the sentinel when no MPPT frame was received this window.
    JAddNumberToObject(body, "yield_kwh",
        state.pv_w_cnt > 0.0f ? state.yield_kwh : SUMMARY_SENTINEL_F);

    // ttg_min uses two sentinel levels:
    //   SUMMARY_SENTINEL_TTG (-9999) — no SmartShunt data in this window.
    //   −1                           — SmartShunt present but battery not
    //                                  discharging (TTG inapplicable or ∞).
    //   ≥ 0                          — active discharge estimate in minutes.
    JAddNumberToObject(body, "ttg_min",
        (double)(state.bat_v_cnt > 0.0f ? state.ttg_min : SUMMARY_SENTINEL_TTG));

    // cs sentinel −1 is outside the VE.Direct CS enum (all real values are
    // non-negative: 0, 2–5, 7, 245, 247, 252) so analytics can reliably
    // distinguish 'no MPPT data' from CS_OFF (0).
    JAddNumberToObject(body, "cs",
        state.pv_w_cnt > 0.0f ? (double)state.cs : (double)SUMMARY_SENTINEL_CS);

    J *rsp = notecard.requestAndResponse(req);
    if (!rsp) {
        dbgSerial.println(F("[warn] note.add solar_summary.qo: no response"));
        return false;
    }
    const char *err = JGetString(rsp, "err");
    bool ok = (!err || !*err);
    if (!ok) {
        dbgSerial.print(F("[warn] note.add solar_summary.qo error: "));
        dbgSerial.println(err);  // log before deleteResponse
    }
    notecard.deleteResponse(rsp);
    if (ok) {
        dbgSerial.println(F("[summary] sent"));
    }
    return ok;
}
