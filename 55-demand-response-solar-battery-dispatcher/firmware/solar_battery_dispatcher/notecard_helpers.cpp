// notecard_helpers.cpp
//
// Notecard configuration, environment-variable fetch and validation,
// outbound sync-cadence management, and inbound dispatch-note polling
// for the solar_battery_dispatcher sketch.

#include "dispatcher.h"

// -------- Module-private cadence tracking -----------------------------------
// Sentinel (0) forces hub.set on first successful call to notecardConfigure().
// applyHubSetIfChanged() retries from the main loop until the Notecard accepts
// the request — a failure leaves this at its current value so the retry fires
// on the next report boundary automatically.
static uint32_t s_last_report_minutes = 0;

// -------- Environment-variable and clamp utilities --------------------------
// envLong / envFloat parse env.get body fields, rejecting malformed input.
// Only a fully-consumed, finite value replaces the previous safe default —
// a typo such as "2x" or "nan" is detected and discarded rather than silently
// overwriting thresholds.

static long envLong(J *rsp, const char *name, long fallback) {
    J *body = JGetObject(rsp, "body");
    if (!body) return fallback;
    const char *s = JGetString(body, name);
    if (!s || !*s) return fallback;
    char *end = nullptr;
    long v = strtol(s, &end, 10);
    // Reject if no characters were consumed or trailing non-numeric chars remain.
    if (!end || end == s || *end != '\0') return fallback;
    return v;
}

static double envFloat(J *rsp, const char *name, double fallback) {
    J *body = JGetObject(rsp, "body");
    if (!body) return fallback;
    const char *s = JGetString(body, name);
    if (!s || !*s) return fallback;
    char *end = nullptr;
    double v = strtod(s, &end);
    // Reject if no characters were consumed, trailing chars remain, or the
    // value is non-finite ("nan", "inf", overflow, etc.).
    if (!end || end == s || *end != '\0' || isnan(v) || isinf(v)) return fallback;
    return v;
}

// Clamp helpers guard against pathological env-var values: a bad value must
// not create a tight loop (sample_minutes=0), overflow a uint8_t, or set an
// invalid Modbus slave address (0 or >247).
static uint32_t clampU32(long v, uint32_t lo, uint32_t hi, uint32_t fb) {
    if (v < (long)lo || v > (long)hi) return fb;
    return (uint32_t)v;
}

static float clampF(double v, float lo, float hi, float fb) {
    // Explicit NaN/Inf guard: both comparisons below are false for NaN, which
    // would otherwise allow NaN to pass through and silently corrupt thresholds.
    if (isnan(v) || isinf(v) || v < (double)lo || v > (double)hi) return fb;
    return (float)v;
}

// -------- Notecard configuration --------------------------------------------
void notecardConfigure(const char *product_uid) {
    // Periodic mode batches outbound telemetry every report_minutes and checks
    // for inbound dispatch commands every 5 minutes — well within the typical
    // 10-minute utility DR response window.
    J *req = notecard.newRequest("hub.set");
    if (!req) return;
    JAddStringToObject(req, "product",  product_uid);
    JAddStringToObject(req, "mode",     "periodic");
    JAddNumberToObject(req, "outbound", (int)g_report_minutes);
    JAddNumberToObject(req, "inbound",  5);
    // Record the applied cadence only on success. A failure leaves
    // s_last_report_minutes at its sentinel (0) so applyHubSetIfChanged()
    // retries hub.set from the main loop until it succeeds.
    if (notecard.sendRequestWithRetry(req, 5)) {
        s_last_report_minutes = g_report_minutes;
    } else {
        usbSerial.println("[notecard] hub.set initial config failed; will retry from main loop");
    }
}

void defineTemplates() {
    J *req = notecard.newRequest("note.template");
    if (!req) return;
    JAddStringToObject(req, "file", "solar_telemetry.qo");
    JAddNumberToObject(req, "port", 50);
    J *body = JAddObjectToObject(req, "body");
    JAddNumberToObject(body, "pv_w",         14.1);  // 4-byte float
    JAddNumberToObject(body, "ac_out_w",     14.1);
    JAddNumberToObject(body, "grid_w",       14.1);  // negative = importing
    JAddNumberToObject(body, "batt_soc_pct", 14.1);
    JAddNumberToObject(body, "batt_v",       14.1);
    JAddNumberToObject(body, "batt_a",       14.1);  // negative = discharging
    JAddStringToObject(body, "mode",         "overnight_charge");  // 16-char max exemplar
    J *rsp = notecard.requestAndResponse(req);
    if (rsp) {
        if (notecard.responseError(rsp)) {
            usbSerial.print("[notecard] note.template solar_telemetry.qo err: ");
            usbSerial.println(JGetString(rsp, "err"));
        }
        notecard.deleteResponse(rsp);
    } else {
        usbSerial.println("[notecard] note.template solar_telemetry.qo: no response");
    }
}

void fetchEnvOverrides() {
    J *req = notecard.newRequest("env.get");
    if (!req) return;
    J *names = JAddArrayToObject(req, "names");
    JAddItemToArray(names, JCreateString("sample_minutes"));
    JAddItemToArray(names, JCreateString("report_minutes"));
    JAddItemToArray(names, JCreateString("soc_min_pct"));
    JAddItemToArray(names, JCreateString("soc_max_pct"));
    JAddItemToArray(names, JCreateString("soc_hyst_pct"));
    JAddItemToArray(names, JCreateString("soc_max_hyst_pct"));
    JAddItemToArray(names, JCreateString("peak_start_utc"));
    JAddItemToArray(names, JCreateString("peak_end_utc"));
    JAddItemToArray(names, JCreateString("charge_start_utc"));
    JAddItemToArray(names, JCreateString("charge_end_utc"));
    JAddItemToArray(names, JCreateString("modbus_slave_inv"));
    JAddItemToArray(names, JCreateString("modbus_slave_bms"));
    JAddItemToArray(names, JCreateString("modbus_baud"));
    JAddItemToArray(names, JCreateString("modbus_parity"));
    JAddItemToArray(names, JCreateString("modbus_stop_bits"));
    JAddItemToArray(names, JCreateString("reg_inv_base"));
    JAddItemToArray(names, JCreateString("reg_bms_base"));

    J *rsp = notecard.requestAndResponse(req);
    if (!rsp) return;
    if (notecard.responseError(rsp)) { notecard.deleteResponse(rsp); return; }

    // DR dispatch latency = inbound cadence (5 min) + sample_minutes.
    // Clamping sample_minutes to 5 keeps worst-case end-to-end latency at
    // ≤10 minutes — the response envelope most utility DR programs require.
    g_sample_minutes = clampU32(envLong(rsp, "sample_minutes", g_sample_minutes), 1, 5,    g_sample_minutes);
    g_report_minutes = clampU32(envLong(rsp, "report_minutes", g_report_minutes), 1, 1440, g_report_minutes);

    // SOC quad cross-validation — parse all four candidates first, then commit
    // only if the combination is coherent. An invalid set (e.g. min + hyst > max,
    // or max_hyst >= max) would create an impossible recovery threshold or sticky
    // charge/discharge behaviour; the previous safe values are retained and the
    // fault is logged to the debug serial port.
    {
        const float cand_min      = clampF(envFloat(rsp, "soc_min_pct",      g_soc_min_pct),      0.0f,  50.0f,  g_soc_min_pct);
        const float cand_max      = clampF(envFloat(rsp, "soc_max_pct",      g_soc_max_pct),      50.0f, 100.0f, g_soc_max_pct);
        const float cand_hyst     = clampF(envFloat(rsp, "soc_hyst_pct",     g_soc_hyst_pct),     0.0f,  20.0f,  g_soc_hyst_pct);
        const float cand_max_hyst = clampF(envFloat(rsp, "soc_max_hyst_pct", g_soc_max_hyst_pct), 0.0f,  20.0f,  g_soc_max_hyst_pct);
        if (cand_min + cand_hyst > cand_max || cand_max_hyst >= cand_max) {
            usbSerial.print("[config] SOC set rejected (min ");
            usbSerial.print(cand_min);
            usbSerial.print(" + hyst ");
            usbSerial.print(cand_hyst);
            usbSerial.print(" > max ");
            usbSerial.print(cand_max);
            usbSerial.print(", or max_hyst ");
            usbSerial.print(cand_max_hyst);
            usbSerial.println(" >= max); retaining previous safe values");
        } else {
            g_soc_min_pct      = cand_min;
            g_soc_max_pct      = cand_max;
            g_soc_hyst_pct     = cand_hyst;
            g_soc_max_hyst_pct = cand_max_hyst;
        }
    }

    g_peak_start_utc   = (uint8_t)clampU32(envLong(rsp, "peak_start_utc",   g_peak_start_utc),   0, 23, g_peak_start_utc);
    g_peak_end_utc     = (uint8_t)clampU32(envLong(rsp, "peak_end_utc",     g_peak_end_utc),     0, 23, g_peak_end_utc);
    g_charge_start_utc = (uint8_t)clampU32(envLong(rsp, "charge_start_utc", g_charge_start_utc), 0, 23, g_charge_start_utc);
    g_charge_end_utc   = (uint8_t)clampU32(envLong(rsp, "charge_end_utc",   g_charge_end_utc),   0, 23, g_charge_end_utc);
    g_modbus_slave_inv = (uint8_t)clampU32(envLong(rsp, "modbus_slave_inv", g_modbus_slave_inv), 1, 247, g_modbus_slave_inv);
    g_modbus_slave_bms = (uint8_t)clampU32(envLong(rsp, "modbus_slave_bms", g_modbus_slave_bms), 1, 247, g_modbus_slave_bms);
    g_modbus_baud      =          clampU32(envLong(rsp, "modbus_baud",      g_modbus_baud),   1200, 230400, g_modbus_baud);
    g_modbus_parity    = (uint8_t)clampU32(envLong(rsp, "modbus_parity",    g_modbus_parity),    0, 2, g_modbus_parity);
    g_modbus_stop_bits = (uint8_t)clampU32(envLong(rsp, "modbus_stop_bits", g_modbus_stop_bits), 1, 2, g_modbus_stop_bits);
    g_reg_inv_base     = (uint16_t)clampU32(envLong(rsp, "reg_inv_base",    g_reg_inv_base),  0, 65530, g_reg_inv_base);
    g_reg_bms_base     = (uint16_t)clampU32(envLong(rsp, "reg_bms_base",    g_reg_bms_base),  0, 65530, g_reg_bms_base);

    notecard.deleteResponse(rsp);
}

void applyHubSetIfChanged(const char *product_uid) {
    if (g_report_minutes == s_last_report_minutes) return;
    J *req = notecard.newRequest("hub.set");
    if (!req) return;
    JAddStringToObject(req, "product",  product_uid);
    JAddStringToObject(req, "mode",     "periodic");
    JAddNumberToObject(req, "outbound", (int)g_report_minutes);
    JAddNumberToObject(req, "inbound",  5);   // fixed dispatch-check cadence
    // Use requestAndResponse so a Notecard-side error is surfaced before we
    // record the applied cadence. sendRequest treats transport success as full
    // success and would update s_last_report_minutes even if the Notecard
    // rejected the request (e.g., bad product UID or transient internal error).
    J *rsp = notecard.requestAndResponse(req);
    if (rsp) {
        if (!notecard.responseError(rsp)) {
            s_last_report_minutes = g_report_minutes;
            usbSerial.print("[notecard] hub.set outbound updated -> ");
            usbSerial.print(s_last_report_minutes);
            usbSerial.println(" min");
        } else {
            usbSerial.print("[notecard] hub.set update failed: ");
            usbSerial.println(JGetString(rsp, "err"));
            // s_last_report_minutes is left unchanged so the next report
            // boundary retries applyHubSetIfChanged() automatically.
        }
        notecard.deleteResponse(rsp);
    }
}

// -------- Inbound dispatch --------------------------------------------------
// Returns true when a note.get error is the normal empty-queue response.
// Matches the documented leading phrase "note not found" precisely rather than
// the broader "not found" to avoid suppressing unrelated Notecard faults such
// as "notefile not found" (a distinct misconfiguration error).
bool isQueueEmpty(const char *err) {
    return err != nullptr && strstr(err, "note not found") != nullptr;
}

void checkDispatch() {
    static uint8_t s_err_count = 0;

    J *req = notecard.newRequest("note.get");
    if (!req) return;
    JAddStringToObject(req, "file",   "dispatch.qi");
    JAddBoolToObject  (req, "delete", true);   // pop — consumed once

    J *rsp = notecard.requestAndResponse(req);
    if (!rsp) return;
    if (notecard.responseError(rsp)) {
        const char *err = JGetString(rsp, "err");
        // isQueueEmpty() is the normal no-pending-notes outcome; any other
        // error is a genuine Notecard or I²C fault — log it to the debug port.
        if (!isQueueEmpty(err)) {
            ++s_err_count;
            usbSerial.print("[dispatch] note.get error (");
            usbSerial.print(s_err_count);
            usbSerial.print("): ");
            usbSerial.println(err ? err : "(unknown)");
        }
        notecard.deleteResponse(rsp);
        return;
    }

    s_err_count = 0;   // successful dequeue — reset consecutive error counter

    J *body = JGetObject(rsp, "body");
    if (body) {
        const char *mode_str = JGetString(body, "mode");
        double      expires  = JGetNumber(body, "expires_epoch");

        // Resolve the mode string before committing. An unrecognised mode must
        // not alter g_dr_expires_epoch and thereby corrupt the lifetime of the
        // currently active valid command.
        DispatchMode new_cmd    = g_commanded_mode;   // keep current if invalid
        bool         valid_mode = true;
        if      (mode_str && strcasecmp(mode_str, "peak_discharge")   == 0) new_cmd = MODE_PEAK_DISCHARGE;
        else if (mode_str && strcasecmp(mode_str, "overnight_charge") == 0) new_cmd = MODE_OVERNIGHT_CHARGE;
        else if (mode_str && strcasecmp(mode_str, "dr_curtail")       == 0) new_cmd = MODE_DR_CURTAIL;
        // "normal" maps to MODE_FORCED_NORMAL — a distinct commanded state that
        // is != MODE_NORMAL and therefore suppresses TOU schedule evaluation for
        // the duration of any expires_epoch window rather than silently releasing
        // back to the schedule while a TOU window is still active.
        else if (mode_str && strcasecmp(mode_str, "normal")           == 0) new_cmd = MODE_FORCED_NORMAL;
        else {
            valid_mode = false;
            usbSerial.print("[dispatch] unknown mode, ignoring: ");
            usbSerial.println(mode_str ? mode_str : "(null)");
        }

        if (valid_mode) {
            g_commanded_mode   = new_cmd;
            g_dr_expires_epoch = (expires > 0) ? (uint32_t)expires : 0;
            usbSerial.print("[dispatch] commanded -> ");
            usbSerial.println(modeName(g_commanded_mode));
        }
    }
    notecard.deleteResponse(rsp);
}
