/***************************************************************************
  solar_string_monitor_helpers.cpp — Sensor, Modbus, and Notecard helpers

  Data-path implementations: template registration, env-var fetch,
  sensor reads, performance model, accumulation, alert logic, and note emission.

  THIS FILE SHOULD BE EDITED AFTER GENERATION.
  IT IS PROVIDED AS A STARTING POINT FOR THE USER TO EDIT AND EXTEND.
***************************************************************************/

#include "solar_string_monitor_helpers.h"
#include <Wire.h>

#define SUMMARY_NOTEFILE "solar_summary.qo"
#define ALERT_NOTEFILE   "solar_alert.qo"

// Maximum attempts and inter-attempt delay for note.template registration.
// note.template is idempotent — repeating a call with the same schema is safe;
// the Notecard simply reconfirms the existing template.  Three attempts with a
// 500 ms pause adds at most ~2.5 s of awake time on a complete failure budget,
// which is acceptable on the rare first-boot or retry-wake path.
#define TEMPLATE_RETRIES  3
#define TEMPLATE_RETRY_MS 500

// ---------------------------------------------------------------------------
// defineTemplates — registers fixed-length schemas for both Notefiles.
// Returns true only when BOTH templates are confirmed accepted by the Notecard.
//
// Template-backed notes travel as packed binary records rather than free-form
// JSON, reducing per-note wire size by 3–5× — meaningful over a multi-year
// deployment. Type hints: 14.1 = 4-byte IEEE 754 float, 21 = uint8, 22 = uint16.
//
// Each note.template request is retried up to TEMPLATE_RETRIES times with a
// short delay between attempts.  A nullptr response is logged explicitly; the
// loop continues to exhaust the retry budget before returning false.  A Notecard
// error string causes an immediate false return — application-level schema
// errors (wrong field type, etc.) will not self-resolve on retry.
//
// The caller (setup()) treats this as a must-succeed step: it persists
// templates_ok in AppState so any failure is retried on every subsequent wake
// until both templates are confirmed.
// ---------------------------------------------------------------------------
bool defineTemplates() {
    const char *sfx[] = {"1","2","3","4"};
    char key[8];

    // --- Summary template: one note per report window -----------------------
    bool summary_ok = false;
    for (int attempt = 0; attempt < TEMPLATE_RETRIES && !summary_ok; attempt++) {
        if (attempt > 0) delay(TEMPLATE_RETRY_MS);

        J *req = notecard.newRequest("note.template");
        JAddStringToObject(req, "file", SUMMARY_NOTEFILE);
        JAddNumberToObject(req, "port", 50);
        J *b = JAddObjectToObject(req, "body");
        JAddNumberToObject(b, "irradiance_wm2", 14.1);
        JAddNumberToObject(b, "mod_temp_c",     14.1);
        // Per-string fields: v=voltage mean, a=current mean, w=power mean,
        // ew=expected power mean, pr=performance ratio
        for (uint8_t i = 0; i < MAX_STRINGS; i++) {
            snprintf(key, sizeof(key), "s%s_v",  sfx[i]); JAddNumberToObject(b, key, 14.1);
            snprintf(key, sizeof(key), "s%s_a",  sfx[i]); JAddNumberToObject(b, key, 14.1);
            snprintf(key, sizeof(key), "s%s_w",  sfx[i]); JAddNumberToObject(b, key, 14.1);
            snprintf(key, sizeof(key), "s%s_ew", sfx[i]); JAddNumberToObject(b, key, 14.1);
            snprintf(key, sizeof(key), "s%s_pr", sfx[i]); JAddNumberToObject(b, key, 14.1);
        }
        JAddNumberToObject(b, "alert_flags", 21); // uint8 bitmask; bit N = string N+1
        JAddNumberToObject(b, "n_samples",   22); // uint16

        J *rsp = notecard.requestAndResponse(req);
        if (!rsp) {
            Serial.print(F("[app] note.template (summary) — no Notecard response (attempt "));
            Serial.print(attempt + 1); Serial.println(F(")"));
            continue; // retry
        }
        const char *err = JGetString(rsp, "err");
        if (err && *err) {
            Serial.print(F("[app] note.template (summary) err: ")); Serial.println(err);
            notecard.deleteResponse(rsp);
            return false; // schema error — retrying will not help
        }
        notecard.deleteResponse(rsp);
        summary_ok = true;
    }
    if (!summary_ok) return false;

    // --- Alert template: immediate single-string underperformance event -----
    bool alert_ok = false;
    for (int attempt = 0; attempt < TEMPLATE_RETRIES && !alert_ok; attempt++) {
        if (attempt > 0) delay(TEMPLATE_RETRY_MS);

        J *req = notecard.newRequest("note.template");
        JAddStringToObject(req, "file", ALERT_NOTEFILE);
        JAddNumberToObject(req, "port", 51);
        J *b = JAddObjectToObject(req, "body");
        JAddNumberToObject(b, "string_id",      21);   // uint8 (1-based); 0 = bus fault
        JAddStringToObject(b,  "reason", "xxxxxxxxxxxxxxxx"); // 16-char exemplar sizes the field
        JAddNumberToObject(b, "perf_ratio",     14.1);
        JAddNumberToObject(b, "actual_w",       14.1);
        JAddNumberToObject(b, "expected_w",     14.1);
        JAddNumberToObject(b, "string_v",       14.1);
        JAddNumberToObject(b, "string_a",       14.1);
        JAddNumberToObject(b, "irradiance_wm2", 14.1);
        JAddNumberToObject(b, "mod_temp_c",     14.1);

        J *rsp = notecard.requestAndResponse(req);
        if (!rsp) {
            Serial.print(F("[app] note.template (alert) — no Notecard response (attempt "));
            Serial.print(attempt + 1); Serial.println(F(")"));
            continue; // retry
        }
        const char *err = JGetString(rsp, "err");
        if (err && *err) {
            Serial.print(F("[app] note.template (alert) err: ")); Serial.println(err);
            notecard.deleteResponse(rsp);
            return false; // schema error — retrying will not help
        }
        notecard.deleteResponse(rsp);
        alert_ok = true;
    }
    return alert_ok;
}

// ---------------------------------------------------------------------------
// serialConfigFromEnv — translates (g_modbus_parity, g_modbus_stop_bits) into
// the HardwareSerial SERIAL_8xx config constant.
//
// Valid parity strings: "none" (default), "even", "odd".
// Valid stop-bit values: 1 (default), 2.
// Unrecognised parity strings fall back to "none".
// ---------------------------------------------------------------------------
uint32_t serialConfigFromEnv() {
    bool two_stop = (g_modbus_stop_bits == 2);
    if (strncmp(g_modbus_parity, "even", 8) == 0)
        return two_stop ? SERIAL_8E2 : SERIAL_8E1;
    if (strncmp(g_modbus_parity, "odd", 8) == 0)
        return two_stop ? SERIAL_8O2 : SERIAL_8O1;
    return two_stop ? SERIAL_8N2 : SERIAL_8N1;
}

// ---------------------------------------------------------------------------
// fetchEnvVars — pulls Notehub-distributed environment variables from the
// Notecard's local cache. All variables are optional; compile-time defaults
// apply if not set. Operators retune thresholds and Modbus addressing without
// re-flashing. Integer env vars that encode floats use a fixed multiplier
// (e.g. string_v_scale_x100=10 → 0.10 V/count) to keep values as integers.
// ---------------------------------------------------------------------------
void fetchEnvVars() {
    J *req = notecard.newRequest("env.get");
    J *names = JAddArrayToObject(req, "names");
    const char *keys[] = {
        "sample_interval_sec", "report_interval_min",
        "modbus_slave_id",     "modbus_baud",
        "modbus_parity",       "modbus_stop_bits",
        "n_strings",           "reg_base",
        "string_v_scale_x100", "string_a_scale_x1000",
        "string_stc_w",        "perf_thresh_pct",
        "irradiance_min_wm2",  "temp_coeff_per10000",
        "alert_cooldown_sec",  "pyranometer_mv_per_wm2_x1000"
    };
    for (uint8_t i = 0; i < 16; i++)
        JAddItemToArray(names, JCreateString(keys[i]));

    J *rsp = notecard.requestAndResponse(req);
    if (!rsp) {
        Serial.println(F("[app] WARN: env.get — no Notecard response; using existing config"));
        return;
    }
    const char *env_err = JGetString(rsp, "err");
    if (env_err && *env_err) {
        Serial.print(F("[app] env.get err: ")); Serial.println(env_err);
        notecard.deleteResponse(rsp);
        return; // leave existing config values in place; do not apply stale/partial overrides
    }
    J *body = JGetObject(rsp, "body");
    if (body) {
        const char *v;
#define EU(k,d,lo,hi)  if((v=JGetString(body,k))&&*v) d=clampU32((uint32_t)atoi(v),lo,hi)
#define EF(k,d,lo,hi,sc) if((v=JGetString(body,k))&&*v) d=clampF((float)atoi(v)/(sc),lo,hi)
        EU("sample_interval_sec",  g_sample_interval_sec,   60,  3600);
        EU("report_interval_min",  g_report_interval_min,   10,  1440);
        EU("modbus_slave_id",      g_modbus_slave_id,        1,   247);
        EU("modbus_baud",          g_modbus_baud,         1200, 115200);
        // modbus_parity is a string — handled below the macro block
        // modbus_stop_bits: clamp to 1–2; safe narrowing to uint8_t
        if ((v = JGetString(body, "modbus_stop_bits")) && *v) {
            uint32_t sb = clampU32((uint32_t)atoi(v), 1, 2);
            g_modbus_stop_bits = (uint8_t)sb;
        }
        EU("n_strings",            g_n_strings,              1, MAX_STRINGS);
        // reg_base: clamp against the highest legal Modbus start address for the
        // current string count so that all 2×g_n_strings registers stay in range.
        // n_strings is parsed first above, so g_n_strings already has its new value.
        if ((v = JGetString(body, "reg_base")) && *v) {
            uint16_t rb_hi = (uint16_t)(65535u - 2u * (uint32_t)g_n_strings + 1u);
            g_reg_base = (uint16_t)clampU32((uint32_t)atoi(v), 0, rb_hi);
        }
        EF("string_v_scale_x100",  g_string_v_scale,  0.001f,  10.f,  100.f);
        EF("string_a_scale_x1000", g_string_a_scale,  0.001f,  10.f, 1000.f);
        EF("string_stc_w",         g_string_stc_w,       10.f, 1e6f,    1.f);
        EF("perf_thresh_pct",      g_perf_threshold,    0.50f,  1.f,  100.f);
        EF("irradiance_min_wm2",   g_irradiance_min,    10.0f, 500.f,   1.f);
        EF("temp_coeff_per10000",  g_temp_coeff,       -0.006f,-0.001f,10000.f);
        EU("alert_cooldown_sec",   g_alert_cooldown_sec,    60, 86400);
        EF("pyranometer_mv_per_wm2_x1000", g_pyranometer_sensitivity, 0.100f, 0.500f, 1000.f);
        // String env var: modbus_parity ("none" | "even" | "odd").
        // Copy, normalise to lowercase, and reject unrecognised values.
        if ((v = JGetString(body, "modbus_parity")) && *v) {
            strncpy(g_modbus_parity, v, sizeof(g_modbus_parity) - 1);
            g_modbus_parity[sizeof(g_modbus_parity) - 1] = '\0';
            for (char *c = g_modbus_parity; *c; c++)
                if (*c >= 'A' && *c <= 'Z') *c |= 0x20; // to lowercase
            if (strncmp(g_modbus_parity, "even", 8) != 0 &&
                strncmp(g_modbus_parity, "odd",  8) != 0)
                strncpy(g_modbus_parity, "none", sizeof(g_modbus_parity));
        }
#undef EU
#undef EF
    }
    notecard.deleteResponse(rsp);
}

// ---------------------------------------------------------------------------
// readIrradiance — 8-sample average of the Apogee SP-110-SS on A0.
// Sensitivity: 0.2 mV per W/m². At STC (1000 W/m²) output ≈ 200 mV.
// Resolution at 12-bit / 3.3V ref: ~0.8 mV/count → ~4 W/m² per count.
// Adequate for threshold comparisons; see README for amplification option.
// ---------------------------------------------------------------------------
float readIrradiance() {
    long sum = 0;
    for (int i = 0; i < 8; i++) { sum += analogRead(A0); delay(5); }
    float mv = ((float)sum / 8.0f) * (3300.0f / 4095.0f); // floating-point average preserves oversampled resolution
    return max(0.0f, mv / g_pyranometer_sensitivity); // mV / (mV per W/m²)
}

// ---------------------------------------------------------------------------
// readModuleTemp — DS18B20 1-Wire probe on PIN_ONE_WIRE.
// Returns -9999.0f on any sensor fault (see validity checks below) and emits
// a rate-limited temp_probe_fault alert note (sync:true, once per report
// window) so the fault is visible in Notehub.  The caller must treat a
// -9999.0f return as invalid data: PR evaluation and per-string window
// accumulation must be suppressed for that sample cycle.
//
// Validity checks:
//   DEVICE_DISCONNECTED_C  — probe physically absent; no retry will help.
//   < −40 °C or > 110 °C  — outside the DS18B20's rated range and outside
//                            any physically plausible backsheet temperature.
//
// Note on the 85 °C "power-on sentinel": the DS18B20 returns 85.0 only when
// the master reads the scratchpad BEFORE conversion completes.  Because
// DallasTemperature::requestTemperatures() blocks for the full conversion
// time (375 ms at 11-bit), the sentinel does not appear on this code path
// under normal operation.  Special-casing 85.0 as a fault would falsely
// reject genuine backsheet readings near 85 °C — a realistic value on a
// hot-rooftop array in summer — so it is not in the validity checks above.
// ---------------------------------------------------------------------------
float readModuleTemp(float irr_wm2) {
    tempSensor.requestTemperatures();
    float t = tempSensor.getTempCByIndex(0);
    if (t == DEVICE_DISCONNECTED_C || t < -40.0f || t > 110.0f) {
        Serial.println(F("[app] WARN: DS18B20 invalid reading — sensor fault"));
        // Rate-limit the Notehub-visible alert to once per report window so a
        // persistently disconnected probe does not flood the alert Notefile.
        // last_temp_fault_sample lives in g_state (Notecard flash) so it
        // survives sleep-cycle power cuts — a static local would reset on wake.
        uint32_t rpt_sec = g_report_interval_min * 60UL;
        uint32_t wlen    = (rpt_sec + g_sample_interval_sec - 1UL) / g_sample_interval_sec;
        if (wlen < 1) wlen = 1;
        uint32_t cur_win  = g_state.sample_count / wlen;
        uint32_t last_win = g_state.last_temp_fault_sample / wlen;
        if (cur_win != last_win) {
            J *req = notecard.newRequest("note.add");
            JAddStringToObject(req, "file", ALERT_NOTEFILE);
            JAddBoolToObject(req,   "sync", true);
            J *b = JAddObjectToObject(req, "body");
            JAddNumberToObject(b, "string_id",      0);           // system-level fault
            JAddStringToObject(b, "reason",         "temp_probe_fault");
            JAddNumberToObject(b, "perf_ratio",     0.0f);
            JAddNumberToObject(b, "actual_w",       0.0f);
            JAddNumberToObject(b, "expected_w",     0.0f);
            JAddNumberToObject(b, "string_v",       0.0f);
            JAddNumberToObject(b, "string_a",       0.0f);
            JAddNumberToObject(b, "irradiance_wm2", irr_wm2);
            JAddNumberToObject(b, "mod_temp_c",     -9999.0f);    // sentinel — probe unavailable
            J *rsp = notecard.requestAndResponse(req);
            if (rsp) {
                const char *err = JGetString(rsp, "err");
                if (err && *err) {
                    Serial.print(F("[app] Alert (temp_probe_fault) err: ")); Serial.println(err);
                } else {
                    // Only advance the rate-limit cursor after the note is accepted.
                    g_state.last_temp_fault_sample = g_state.sample_count;
                }
                notecard.deleteResponse(rsp);
            }
        }
        return -9999.0f;  // sentinel: caller must not use this for PR or temperature derating
    }
    return t;
}

// ---------------------------------------------------------------------------
// readStrings — Modbus RTU read of 2×count holding registers.
//
// Register layout: [Vraw0, Iraw0, Vraw1, Iraw1, …] starting at g_reg_base.
// This assumes the monitored device exposes an independent voltage and current
// register for each string — as provided by multi-MPPT inverters implementing
// SunSpec Model 160 (Multiple MPPT) or equivalent per-string V+I Modbus sources.
// Combiner boxes that report only per-string current against a shared bus
// voltage do not supply per-string voltage and therefore do not match this
// layout; see README §9 for root-cause classification implications.
//
// Retries 3× on error. On full failure emits a bus-level alert note and returns false.
// ---------------------------------------------------------------------------
bool readStrings(float v_out[], float a_out[], uint8_t count, float irr, float mod_temp) {
    uint8_t result = 0xFF;
    for (int t = 0; t < 3 && result != modbus.ku8MBSuccess; t++)
        result = modbus.readHoldingRegisters(g_reg_base, (uint16_t)(count * 2));

    if (result != modbus.ku8MBSuccess) {
        Serial.print(F("[app] Modbus fail 0x")); Serial.println(result, HEX);
        // Rate-limit error note to once per report window.
        // last_err_sample lives in g_state (Notecard flash) so it survives
        // sleep-cycle power cuts — a static local would reset on every wake.
        uint32_t rpt_sec = g_report_interval_min * 60UL;
        uint32_t wlen = (rpt_sec + g_sample_interval_sec - 1UL) / g_sample_interval_sec;
        if (wlen < 1) wlen = 1;
        uint32_t cur_win  = g_state.sample_count / wlen;
        uint32_t last_win = g_state.last_err_sample / wlen; // 0xFFFFFFFF/wlen on cold boot
        if (cur_win != last_win) {
            J *req = notecard.newRequest("note.add");
            JAddStringToObject(req, "file", ALERT_NOTEFILE);
            JAddBoolToObject(req,   "sync", true);
            J *b = JAddObjectToObject(req, "body");
            JAddNumberToObject(b, "string_id",      0); // bus-level
            JAddStringToObject(b, "reason",         "modbus_fail");
            JAddNumberToObject(b, "perf_ratio",     0.0f);
            JAddNumberToObject(b, "actual_w",       0.0f);
            JAddNumberToObject(b, "expected_w",     0.0f);
            JAddNumberToObject(b, "string_v",       0.0f);
            JAddNumberToObject(b, "string_a",       0.0f);
            // Use the irr/mod_temp already sampled this wake cycle — avoids
            // a redundant DS18B20 conversion and ensures the alert context
            // matches the sample that triggered the fault path.
            JAddNumberToObject(b, "irradiance_wm2", irr);
            JAddNumberToObject(b, "mod_temp_c",     mod_temp);
            J *rsp = notecard.requestAndResponse(req);
            if (rsp) {
                const char *err = JGetString(rsp, "err");
                if (err && *err) {
                    Serial.print(F("[app] Alert (modbus_fail) err: ")); Serial.println(err);
                } else {
                    // Only advance the rate-limit cursor after the note is accepted.
                    g_state.last_err_sample = g_state.sample_count;
                }
                notecard.deleteResponse(rsp);
            }
        }
        return false;
    }
    for (uint8_t i = 0; i < count; i++) {
        v_out[i] = (float)modbus.getResponseBuffer(i * 2)     * g_string_v_scale;
        a_out[i] = (float)modbus.getResponseBuffer(i * 2 + 1) * g_string_a_scale;
    }
    return true;
}

// ---------------------------------------------------------------------------
// computeExpected — temperature-derated expected power per string.
// Pexp = Pstc × (G / 1000) × [1 + α_P × (Tmod − 25)]
// α_P is the module power temperature coefficient (negative for c-Si; typical
// mono-PERC value is −0.0035 /°C). At Tmod = 65 °C: derating ≈ −14%.
// ---------------------------------------------------------------------------
float computeExpected(float irr_wm2, float mod_temp_c) {
    if (irr_wm2 < 1.0f) return 0.0f;
    float temp_factor = 1.0f + g_temp_coeff * (mod_temp_c - 25.0f);
    if (temp_factor < 0.40f) temp_factor = 0.40f;
    return g_string_stc_w * (irr_wm2 / 1000.0f) * temp_factor;
}

// ---------------------------------------------------------------------------
// accumulateWindow — adds this sample to per-string running sums.
// ---------------------------------------------------------------------------
void accumulateWindow(float v[], float a[], float irr, float mod_temp) {
    float exp_w = computeExpected(irr, mod_temp);
    for (uint8_t i = 0; i < g_n_strings && i < MAX_STRINGS; i++) {
        g_state.accum[i].v_sum     += v[i];
        g_state.accum[i].a_sum     += a[i];
        g_state.accum[i].w_sum     += v[i] * a[i];
        g_state.accum[i].exp_w_sum += exp_w;
        g_state.accum[i].n++;
    }
}

// ---------------------------------------------------------------------------
// evaluateAndAlert — computes per-string Performance Ratio (PR = actual W /
// expected W) and fires an alert Note when PR falls below g_perf_threshold.
//
// Per-string root-cause hypothesis from operating V/I signature
// -------------------------------------------------------------
// These are operating-point measurements under load (not open-circuit values)
// read from each string's [Vop, Iop] register pair.  The full three-way
// classification (shading / soiling / string_fault) depends on having an
// independent operating voltage register per string — the signal model
// provided by multi-MPPT inverters via SunSpec Model 160 (Multiple MPPT) or
// equivalent per-string V+I sources.  Sources that expose only per-string
// current with a shared bus voltage cannot supply the per-string Vop required
// for the shading hypothesis; on such hardware the "shading" reason will never
// fire and fault resolution is limited to current-magnitude anomalies
// (soiling / string_fault / degraded).
//
//   low Vop, normal Iop  → "shading"      (shadow reduces operating voltage;
//                                           bypass diodes limit current loss)
//   normal Vop, low Iop  → "soiling"      (uniform soiling cuts operating current
//                                           across all cells in the string)
//   low Vop and low Iop  → "string_fault" (open cell, bad bypass diode,
//                                           high series resistance)
//
// Comparative hypothesis requires n_strings >= 2.  When only one string is
// configured, v_mean == v[0] and a_mean == a[0], so vr == ar == 1.0 and none
// of the comparative conditions can trigger; reason stays "degraded".
//
// Cooldown: re-alert is suppressed for g_alert_cooldown_sec wall-clock seconds
// (default 30 min) after each fire.  Cooldown state persists in g_state
// (Notecard flash) and survives sleep-cycle power cuts.
//
// alert_active[] is a last-known-state indicator.  Above the irradiance
// threshold it is updated only when Modbus polling succeeds (evaluateAndAlert
// is not called on Modbus failure or when mod_temp is invalid), so daytime
// telemetry dropouts let the flags carry forward from the most recent successful
// poll.  Clearing on low-irradiance windows is handled unconditionally at the
// loop() level — independent of Modbus/probe success — so overnight and
// low-light summaries always report 0.  The early-return clear below is
// belt-and-suspenders and is reached only on the (ok && mod_temp_valid) path.
// ---------------------------------------------------------------------------
void evaluateAndAlert(float v[], float a[], float irr, float mod_temp) {
    if (irr < g_irradiance_min) {
        // Below evaluation threshold — clear all alert state so overnight and
        // low-light summaries do not report stale daytime fault flags.
        for (uint8_t i = 0; i < MAX_STRINGS; i++) g_state.alert_active[i] = false;
        return;
    }
    float exp_w = computeExpected(irr, mod_temp);
    if (exp_w < 1.0f) return;

    uint8_t n = min(g_n_strings, (uint8_t)MAX_STRINGS);

    // Fleet-wide means for relative anomaly detection (shading/soiling/string_fault
    // hypothesis compares each string's V and I against the fleet mean).
    float v_mean = 0.0f, a_mean = 0.0f;
    for (uint8_t i = 0; i < n; i++) { v_mean += v[i]; a_mean += a[i]; }
    v_mean /= (float)n;
    a_mean /= (float)n;

    // Pre-compute per-string PR so both the threshold check and the hypothesis
    // block operate on the same calculated values.
    float pr[MAX_STRINGS];
    memset(pr, 0, sizeof(pr));
    for (uint8_t i = 0; i < n; i++)
        pr[i] = (exp_w > 0.1f) ? (v[i] * a[i]) / exp_w : 0.0f;

    for (uint8_t i = 0; i < n; i++) {
        if (pr[i] >= g_perf_threshold) { g_state.alert_active[i] = false; continue; }

        // Default hypothesis: "degraded" (PR below threshold, cause unclear).
        // Comparative shading/soiling/string_fault classification requires at
        // least two peer strings; with n == 1 both ratios equal 1.0 and the
        // block below is skipped, keeping the reason as "degraded".
        const char *reason = "degraded";
        if (n >= 2) {
            float vr = (v_mean > 0.1f) ? v[i] / v_mean : 1.0f;
            float ar = (a_mean > 0.1f) ? a[i] / a_mean : 1.0f;
            if      (vr < 0.85f && ar >= 0.90f) reason = "shading";
            else if (ar < 0.85f && vr >= 0.90f) reason = "soiling";
            else if (vr < 0.85f && ar <  0.85f) reason = "string_fault";
        }

        // Derive cooldown in samples from the configured wall-clock duration so
        // the 30-minute window holds regardless of sample_interval_sec changes.
        uint32_t cooldown_samples = (g_alert_cooldown_sec + g_sample_interval_sec - 1u)
                                    / g_sample_interval_sec;
        if (cooldown_samples < 1u) cooldown_samples = 1u;
        uint32_t since = g_state.sample_count - g_state.last_alert_sample[i];
        if (!g_state.alert_active[i] || since >= cooldown_samples) {
            if (sendAlert((uint8_t)(i + 1), reason, pr[i], v[i], a[i], irr, mod_temp)) {
                // Only advance cooldown state after the note is successfully queued;
                // a Notecard I²C failure must not suppress the next alert attempt.
                g_state.alert_active[i]      = true;
                g_state.last_alert_sample[i] = g_state.sample_count;
            }
        }
    }
}

// ---------------------------------------------------------------------------
// sendSummary — one template-backed Note per report window, queued to the
// Notecard and transmitted on the next periodic outbound sync.
//
// irradiance_wm2 is the window mean over all n_env sample cycles.
// mod_temp_c is the window mean over n_temp_valid valid-probe cycles only;
// it emits -9999 when the DS18B20 returned no valid reading this window.
// n_samples reports n_env — the number of irradiance cycles in the window.
//
// Per-string fields use -9999 as a sentinel for "no data" (string not
// configured or Modbus failed every sample this window) so downstream
// analytics can distinguish a sensor failure from a real near-zero reading.
// ---------------------------------------------------------------------------
bool sendSummary(void) {
    float irr_m      = (g_state.n_env > 0)
                       ? g_state.irr_sum / (float)g_state.n_env : 0.0f;
    // Use n_temp_valid (not n_env) so the mean is over valid-probe cycles only.
    // Emit -9999 when no valid reading was obtained this window so downstream
    // consumers can distinguish 'sensor failed' from 'measured 25 °C'.
    float mod_temp_m = (g_state.n_temp_valid > 0)
                       ? g_state.mod_temp_sum / (float)g_state.n_temp_valid : -9999.0f;
    uint16_t n_win   = g_state.n_env;

    J *req = notecard.newRequest("note.add");
    JAddStringToObject(req, "file", SUMMARY_NOTEFILE);
    J *b = JAddObjectToObject(req, "body");
    JAddNumberToObject(b, "irradiance_wm2", irr_m);
    JAddNumberToObject(b, "mod_temp_c",     mod_temp_m);

    const char *sfx[] = {"1","2","3","4"};
    char kv[8], ka[8], kw[8], kew[8], kpr[8];
    uint8_t alert_flags = 0;

    for (uint8_t i = 0; i < MAX_STRINGS; i++) {
        snprintf(kv,  sizeof(kv),  "s%s_v",  sfx[i]);
        snprintf(ka,  sizeof(ka),  "s%s_a",  sfx[i]);
        snprintf(kw,  sizeof(kw),  "s%s_w",  sfx[i]);
        snprintf(kew, sizeof(kew), "s%s_ew", sfx[i]);
        snprintf(kpr, sizeof(kpr), "s%s_pr", sfx[i]);

        uint16_t n = g_state.accum[i].n;
        if (n > 0 && i < g_n_strings) {
            float v_m  = g_state.accum[i].v_sum     / n;
            float a_m  = g_state.accum[i].a_sum     / n;
            float w_m  = g_state.accum[i].w_sum     / n;
            float ew_m = g_state.accum[i].exp_w_sum / n;
            float pr   = (ew_m > 0.1f) ? w_m / ew_m : 0.0f;
            JAddNumberToObject(b, kv,  v_m);  JAddNumberToObject(b, ka,  a_m);
            JAddNumberToObject(b, kw,  w_m);  JAddNumberToObject(b, kew, ew_m);
            JAddNumberToObject(b, kpr, pr);
            if (g_state.alert_active[i]) alert_flags |= (uint8_t)(1u << i);
        } else {
            JAddNumberToObject(b, kv, -9999.0f); JAddNumberToObject(b, ka, -9999.0f);
            JAddNumberToObject(b, kw, -9999.0f); JAddNumberToObject(b, kew,-9999.0f);
            JAddNumberToObject(b, kpr,-9999.0f);
        }
    }
    JAddNumberToObject(b, "alert_flags", alert_flags);
    JAddNumberToObject(b, "n_samples",   n_win);
    J *rsp = notecard.requestAndResponse(req);
    if (!rsp) {
        Serial.println(F("[app] Summary: no response from Notecard — will retry next window"));
        return false;
    }
    const char *err = JGetString(rsp, "err");
    if (err && *err) {
        Serial.print(F("[app] Summary err: ")); Serial.println(err);
        notecard.deleteResponse(rsp);
        return false;
    }
    notecard.deleteResponse(rsp);
    Serial.println(F("[app] Summary queued"));
    return true;
}

// ---------------------------------------------------------------------------
// sendAlert — immediate alert Note with sync:true.
// sync:true bypasses the periodic outbound window — the Notecard wakes the
// radio immediately; typical alert latency is 15–60 seconds to Notehub.
// ---------------------------------------------------------------------------
bool sendAlert(uint8_t str_id, const char *reason,
               float pr, float v, float a, float irr, float mod_temp) {
    J *req = notecard.newRequest("note.add");
    JAddStringToObject(req, "file", ALERT_NOTEFILE);
    JAddBoolToObject(req,   "sync", true);
    J *b = JAddObjectToObject(req, "body");
    JAddNumberToObject(b, "string_id",      str_id);
    JAddStringToObject(b, "reason",         reason);
    JAddNumberToObject(b, "perf_ratio",     pr);
    JAddNumberToObject(b, "actual_w",       v * a);
    JAddNumberToObject(b, "expected_w",     computeExpected(irr, mod_temp));
    JAddNumberToObject(b, "string_v",       v);
    JAddNumberToObject(b, "string_a",       a);
    JAddNumberToObject(b, "irradiance_wm2", irr);
    JAddNumberToObject(b, "mod_temp_c",     mod_temp);
    J *rsp = notecard.requestAndResponse(req);
    if (!rsp) {
        Serial.println(F("[app] Alert: no response from Notecard"));
        return false;
    }
    const char *err = JGetString(rsp, "err");
    if (err && *err) {
        Serial.print(F("[app] Alert err: ")); Serial.println(err);
        notecard.deleteResponse(rsp);
        return false;
    }
    notecard.deleteResponse(rsp);
    Serial.print(F("[app] Alert str")); Serial.print(str_id);
    Serial.print(F(" ")); Serial.print(reason);
    Serial.print(F(" PR=")); Serial.println(pr, 3);
    return true;
}

// ---------------------------------------------------------------------------
// Utility clamping helpers
// ---------------------------------------------------------------------------
uint32_t clampU32(uint32_t v, uint32_t lo, uint32_t hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}
float clampF(float v, float lo, float hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}
