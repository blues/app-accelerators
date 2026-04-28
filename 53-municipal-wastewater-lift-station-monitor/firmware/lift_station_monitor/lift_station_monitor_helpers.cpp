/***************************************************************************
  lift_station_monitor_helpers.cpp — Parsing, clamping, sensor reads, and
  Notecard helpers for lift_station_monitor.ino.

  All functions here operate on globals defined (and extern-declared via
  lift_station_monitor_helpers.h) in the main sketch file.
***************************************************************************/

#include <Arduino.h>
#include <math.h>
#include "lift_station_monitor_helpers.h"

// ---------------------------------------------------------------------------
// String-to-number helpers — use strtof/strtol with end-pointer validation so
// malformed env-var strings (e.g. "abc", "12abc", "") are rejected rather than
// silently converting to 0 and then passing a range check whose lower bound
// includes 0 (e.g. high_level_pct, rising_rate_pct).
//
// Returns true only when at least one digit was consumed, no trailing
// non-whitespace garbage remains, and the parsed float is finite. The
// isfinite() guard rejects "nan", "inf", and "-inf", which strtof() accepts
// as valid but which would poison threshold comparisons (NaN) or pin
// thresholds at unreachable extremes (Inf) if persisted into g_state.cfg_*.
// The caller is responsible for range-checking *out before use.
// ---------------------------------------------------------------------------
bool parseFloat(const char *str, float *out) {
    if (!str || !*str) return false;
    char *end;
    *out = strtof(str, &end);
    if (end == str) return false;                          // no digits consumed
    while (*end) {
        if (!isspace((unsigned char)*end)) return false;  // trailing garbage
        end++;
    }
    if (!isfinite(*out)) return false;                    // reject nan / inf
    return true;
}

bool parseLong(const char *str, long *out) {
    if (!str || !*str) return false;
    char *end;
    *out = strtol(str, &end, 10);
    if (end == str) return false;                          // no digits consumed
    while (*end) {
        if (!isspace((unsigned char)*end)) return false;  // trailing garbage
        end++;
    }
    return true;
}

// ---------------------------------------------------------------------------
// Clamping helpers — mirror the pattern from Blues reference accelerator 52.
// Each returns `fallback` when the parsed value lies outside [minv, maxv],
// ensuring a bad Notehub env var cannot destabilise runtime behavior.
//
// clampF() checks isfinite(v) first as belt-and-suspenders against callers
// that bypass parseFloat(). NaN silently passes a finite range check (all
// NaN comparisons return false), so without the isfinite() guard a NaN input
// would be cast and returned, poisoning any threshold that consumes it.
// ---------------------------------------------------------------------------
uint32_t clampU32(long v, uint32_t minv, uint32_t maxv, uint32_t fallback) {
    if (v < (long)minv || v > (long)maxv) return fallback;
    return (uint32_t)v;
}

float clampF(double v, float minv, float maxv, float fallback) {
    if (!isfinite(v) || v < (double)minv || v > (double)maxv) return fallback;
    return (float)v;
}

// ---------------------------------------------------------------------------
// notecardConfigure — cold-boot hub.set (periodic mode) and accelerometer
// disable. PRODUCT_UID is included here and in applyHubSetIfChanged() so that
// any successful hub.set, regardless of call site, binds the device to the
// intended Notehub project.
// ---------------------------------------------------------------------------
void notecardConfigure(void) {
    J *req = notecard.newRequest("hub.set");
    JAddStringToObject(req, "product",  PRODUCT_UID);
    JAddStringToObject(req, "mode",     "periodic");
    JAddNumberToObject(req, "outbound", OUTBOUND_INTERVAL_MIN);
    JAddNumberToObject(req, "inbound",  INBOUND_INTERVAL_MIN);
    // sendRequestWithRetry handles the cold-boot race where the host comes up
    // before the Notecard's I²C listener is fully initialized.
    if (!notecard.sendRequestWithRetry(req, 10)) {
        Serial.println("[CONFIG] hub.set (cold boot) failed; applyHubSetIfChanged() will retry.");
    }

    // Disable the onboard accelerometer to keep scope traces clean during
    // bench validation — removes occasional interrupt-driven current blips.
    req = notecard.newRequest("card.motion.mode");
    JAddBoolToObject(req, "stop", true);
    notecard.sendRequest(req);
}

// ---------------------------------------------------------------------------
// defineTemplates — fixed-width Notefile schemas to minimize cellular data.
// Returns true only when both note.template requests succeed. Called on every
// wake while g_state.templates_registered is false so a transient I²C failure
// on cold boot is retried rather than silently accepted.
// ---------------------------------------------------------------------------
bool defineTemplates(void) {
    bool ok = true;

    // lift_alert.qo: immediate-sync alert notes (low volume, real-time).
    // format:"compact" produces a fixed-width binary encoding that keeps
    // each Note well within Starnote for Skylo's 256-byte payload ceiling.
    // level_pct may carry LEVEL_INVALID_SENTINEL (-9999) when the level sensor
    // is faulted; the 4-byte float field accommodates any float value.
    J *req = notecard.newRequest("note.template");
    JAddStringToObject(req, "file",   NOTEFILE_ALERT);
    JAddNumberToObject(req, "port",   50);
    JAddStringToObject(req, "format", "compact");
    J *body = JAddObjectToObject(req, "body");
    JAddStringToObject(body, "alert",      "20");       // max 20-char type string
    JAddNumberToObject(body, "level_pct",  TTYPE_FLOAT);
    JAddNumberToObject(body, "pump1_amps", TTYPE_FLOAT);
    JAddNumberToObject(body, "pump2_amps", TTYPE_FLOAT);
    JAddBoolToObject(body,   "float_sw",   TTYPE_BOOL);
    if (!notecard.sendRequestWithRetry(req, 10)) {
        Serial.println("[CONFIG] note.template (alert) failed");
        ok = false;
    }

    // lift_summary.qo: hourly aggregates (batched outbound sync).
    // format:"compact" keeps the note within the 256-byte satellite ceiling.
    // level_pct and level_avg_pct may carry LEVEL_INVALID_SENTINEL; the 4-byte
    // float field accommodates any float value. level_faults counts samples in
    // the window where the level sensor returned an out-of-range ADC value
    // (open loop or short); non-zero indicates that level data for that window
    // may be partially or fully unreliable.
    // pump*_amps_avg carries CT_INVALID_SENTINEL when no valid CT sample existed
    // in the entire window; ct*_faults is non-zero whenever readPumpAmps()
    // detected a bias-network fault or rail saturation on a given channel.
    req = notecard.newRequest("note.template");
    JAddStringToObject(req, "file",   NOTEFILE_SUMMARY);
    JAddNumberToObject(req, "port",   51);
    JAddStringToObject(req, "format", "compact");
    body = JAddObjectToObject(req, "body");
    JAddNumberToObject(body, "level_pct",      TTYPE_FLOAT);
    JAddNumberToObject(body, "level_avg_pct",  TTYPE_FLOAT);
    JAddNumberToObject(body, "pump1_amps_avg", TTYPE_FLOAT);
    JAddNumberToObject(body, "pump2_amps_avg", TTYPE_FLOAT);
    JAddNumberToObject(body, "pump1_run_min",  TTYPE_FLOAT);
    JAddNumberToObject(body, "pump2_run_min",  TTYPE_FLOAT);
    JAddBoolToObject(body,   "float_sw",       TTYPE_BOOL);
    JAddNumberToObject(body, "alert_count",    TTYPE_INT16);
    JAddNumberToObject(body, "level_faults",   TTYPE_INT16);
    JAddNumberToObject(body, "ct1_faults",     TTYPE_INT16);
    JAddNumberToObject(body, "ct2_faults",     TTYPE_INT16);
    if (!notecard.sendRequestWithRetry(req, 10)) {
        Serial.println("[CONFIG] note.template (summary) failed");
        ok = false;
    }

    return ok;
}

// ---------------------------------------------------------------------------
// fetchEnvOverrides — pull Notehub env vars each wake; keep current settings
//   on any network error, parse failure, or out-of-range value rather than
//   reverting to compile-time defaults.
//
// env var                  range          notes
//   pump_on_amps           0.1–100.0 A
//   high_level_pct         1.0–100.0 %   lower bound > 0: a 0 % threshold
//                                        makes fail-to-start fire every cycle
//   rising_rate_pct        0.1– 50.0 %   lower bound > 0: a 0 % rate threshold
//                                        fires the clog rule every cycle
//   summary_interval_min   1–1440 min
//   inbound_interval_min   1–1440 min
//
// Each branch first calls parseFloat/parseLong (strtof/strtol + end-pointer)
// to reject non-numeric strings; only then is the value range-checked and,
// if valid, persisted into the cfg_* shadow in g_state.
// ---------------------------------------------------------------------------
void fetchEnvOverrides(void) {
    J *rsp = notecard.requestAndResponse(notecard.newRequest("env.get"));
    if (rsp == NULL) return;

    // Discard the response on a Notecard protocol error. Without this check,
    // a response containing an "err" field bypasses the NULL guard and can
    // deliver a partially-formed body with unexpected field values.
    if (notecard.responseError(rsp)) {
        notecard.deleteResponse(rsp);
        return;
    }

    J *b = JGetObject(rsp, "body");
    if (b) {
        const char *v;
        float fraw;
        long  lraw;

        if ((v = JGetString(b, "pump_on_amps")) && *v) {
            if (parseFloat(v, &fraw)) {
                float p = clampF(fraw, 0.1f, 100.0f, g_pump_on_amps);
                g_pump_on_amps           = p;
                g_state.cfg_pump_on_amps = p;
            }
        }
        if ((v = JGetString(b, "high_level_pct")) && *v) {
            if (parseFloat(v, &fraw)) {
                // Lower bound is 1.0 %, not 0.0 %: a threshold of 0 would
                // make fail-to-start fire on every cycle regardless of level.
                float p = clampF(fraw, 1.0f, 100.0f, g_high_level_pct);
                g_high_level_pct           = p;
                g_state.cfg_high_level_pct = p;
            }
        }
        if ((v = JGetString(b, "rising_rate_pct")) && *v) {
            if (parseFloat(v, &fraw)) {
                // Lower bound is 0.1 %, not 0.0 %: a rate of 0 would trigger
                // the clog rule on any sample where the level is not dropping.
                float p = clampF(fraw, 0.1f, 50.0f, g_rising_rate_pct);
                g_rising_rate_pct           = p;
                g_state.cfg_rising_rate_pct = p;
            }
        }
        if ((v = JGetString(b, "summary_interval_min")) && *v) {
            if (parseLong(v, &lraw)) {
                uint32_t p = clampU32(lraw, 1, 1440, g_summary_interval_min);
                g_summary_interval_min           = p;
                g_state.cfg_summary_interval_min = p;
            }
        }
        if ((v = JGetString(b, "inbound_interval_min")) && *v) {
            if (parseLong(v, &lraw)) {
                uint32_t p = clampU32(lraw, 1, 1440, g_inbound_interval_min);
                g_inbound_interval_min           = p;
                g_state.cfg_inbound_interval_min = p;
            }
        }
    }
    notecard.deleteResponse(rsp);
}

// ---------------------------------------------------------------------------
// applyHubSetIfChanged — re-issue hub.set when summary_interval_min or
//   inbound_interval_min changes.
//
// PRODUCT_UID is included in every hub.set so that a cold-boot failure of
// notecardConfigure() does not leave the device permanently unassociated with
// the intended Notehub project: whichever hub.set succeeds first (cold-boot or
// a cadence-update retry) binds the device to the correct project.
//
// applied_* start at 0 on cold boot (impossible for valid intervals ≥ 1 min),
// guaranteeing hub.set is issued on the very first wake. They are updated only
// after hub.set confirms success so a transient I²C error does not permanently
// suppress the retry.
// ---------------------------------------------------------------------------
void applyHubSetIfChanged(void) {
    if (g_state.applied_outbound_min == g_summary_interval_min &&
        g_state.applied_inbound_min  == g_inbound_interval_min) return;

    J *req = notecard.newRequest("hub.set");
    JAddStringToObject(req, "product",  PRODUCT_UID);   // always include so any
                                                         // successful hub.set binds
                                                         // the device to Notehub
    JAddStringToObject(req, "mode",     "periodic");
    JAddNumberToObject(req, "outbound", (int)g_summary_interval_min);
    JAddNumberToObject(req, "inbound",  (int)g_inbound_interval_min);
    // Only persist updated cadence after hub.set confirms success. If the
    // request fails, applied_* remain at their previous values and the update
    // will be retried on the next wake.
    if (notecard.sendRequest(req)) {
        g_state.applied_outbound_min = g_summary_interval_min;
        g_state.applied_inbound_min  = g_inbound_interval_min;
        Serial.print("[CONFIG] hub.set outbound=");
        Serial.print(g_summary_interval_min);
        Serial.print(" inbound=");
        Serial.print(g_inbound_interval_min);
        Serial.println(" min");
    } else {
        Serial.println("[CONFIG] hub.set failed; will retry next wake.");
    }
}

// ---------------------------------------------------------------------------
// readLevelPct — 4-20 mA pressure transducer via 150 Ω shunt → ADC → 0..100 %
//
// Sets *valid_out to false and returns LEVEL_INVALID_SENTINEL when the averaged
// ADC count falls outside [LEVEL_ADC_FAULT_LO, LEVEL_ADC_FAULT_HI], indicating
// an open loop (sensor unplugged, broken wire — reads ≈ 0 counts) or a short-
// circuit fault (reads ≈ 4095 counts). Clamping these extremes to the
// calibrated 4–20 mA endpoints would turn sensor faults into plausible
// 0 %/100 % readings, potentially suppressing real alarms or generating false
// ones; the sentinel value lets operators and downstream analytics distinguish
// a faulted sensor from a genuinely empty wet well.
//
// Callers must check *valid_out before trusting the returned percentage.
// ---------------------------------------------------------------------------
float readLevelPct(bool *valid_out) {
    long sum = 0;
    for (int i = 0; i < LEVEL_SAMPLES; i++) {
        sum += analogRead(PIN_LEVEL_SENSOR);
        delayMicroseconds(500);
    }
    // Divide as floating-point to preserve the fractional benefit of averaging
    // 64 samples; integer division would discard up to 63 counts of precision.
    float adc = (float)sum / (float)LEVEL_SAMPLES;

    // Detect sensor faults before any clamping. An open loop produces ≈ 0
    // counts (well below LEVEL_ADC_FAULT_LO ≈ 645); a short produces ≈ 4095
    // (well above LEVEL_ADC_FAULT_HI ≈ 3823).
    if (adc < (float)LEVEL_ADC_FAULT_LO || adc > (float)LEVEL_ADC_FAULT_HI) {
        *valid_out = false;
        return LEVEL_INVALID_SENTINEL;  // not 0.0f: sentinel distinguishes fault
                                        // from a legitimately empty wet well
    }

    *valid_out = true;
    // Clamp residual calibration noise into the exact 4–20 mA window, then
    // map to percent. In-range readings may sit a few counts outside the ideal
    // endpoints due to component tolerances.
    adc = constrain(adc, (float)LEVEL_ADC_MIN, (float)LEVEL_ADC_MAX);
    return (adc - LEVEL_ADC_MIN) / (float)(LEVEL_ADC_MAX - LEVEL_ADC_MIN) * 100.0f;
}

// ---------------------------------------------------------------------------
// readPumpAmps — SCT-013-030 CT with 2× 10 kΩ bias divider and 10 µF cap
//
// The 10 kΩ/10 kΩ divider centres the AC signal at VREF/2 (≈1.65 V). We
// first measure the DC bias point over CT_BIAS_SAMPLES, then compute RMS
// of (sample – bias) over CT_RMS_SAMPLES. The SCT-013-030 produces 1 V RMS
// per 30 A RMS, so Amps = Vrms × 30.
//
// Fault detection — sets *valid_out = false and returns CT_INVALID_SENTINEL
// when either of the following conditions is detected:
//
//   Bias out of window [CT_BIAS_MIN..CT_BIAS_MAX]:
//     The 10 kΩ/10 kΩ divider should centre the ADC at ~2048 counts.
//     A reading outside [1024..3072] indicates a broken or missing bias
//     resistor, a CT terminal shorted to VCC or GND, or a wiring fault.
//
//   Rail saturation during the RMS window:
//     Any individual sample within CT_RAIL_MARGIN counts of 0 or 4095
//     indicates ADC clipping — consistent with a shorted CT secondary or
//     a grossly over-ranged signal.
//
// Limitation: an open CT secondary winding with an otherwise intact bias
// network reads ≈ VREF/2 with near-zero variance, which is indistinguishable
// in software from a legitimately idle pump. Operators should treat a
// sustained ct*_faults count combined with an unexplained high-water event
// as a prompt to inspect the CT wiring physically.
//
// Callers must check *valid_out before using the returned value in any
// pump-state or fault-detection logic.
// ---------------------------------------------------------------------------
float readPumpAmps(uint8_t pin, bool *valid_out) {
    // Step 1: measure DC bias (mid-rail, ideally 2048 counts at 12-bit)
    long bias_sum = 0;
    for (int i = 0; i < CT_BIAS_SAMPLES; i++) {
        bias_sum += analogRead(pin);
    }
    float bias = (float)bias_sum / (float)CT_BIAS_SAMPLES;

    // Bias validity check: a reading outside [CT_BIAS_MIN..CT_BIAS_MAX]
    // indicates a broken bias network or a shorted CT terminal.
    if (bias < (float)CT_BIAS_MIN || bias > (float)CT_BIAS_MAX) {
        *valid_out = false;
        return CT_INVALID_SENTINEL;
    }

    // Step 2: RMS integration over CT_RMS_SAMPLES.
    // Track rail saturation: a sample within CT_RAIL_MARGIN counts of 0 or
    // 4095 means the ADC is clipping, indicating a shorted secondary or a
    // severely over-ranged input.
    double sq_sum  = 0.0;
    bool   rail_hit = false;
    for (int i = 0; i < CT_RMS_SAMPLES; i++) {
        int raw = analogRead(pin);
        if (raw < CT_RAIL_MARGIN || raw > (4095 - CT_RAIL_MARGIN)) {
            rail_hit = true;
        }
        float s = (float)raw - bias;
        sq_sum += (double)s * s;
    }
    if (rail_hit) {
        *valid_out = false;
        return CT_INVALID_SENTINEL;
    }

    *valid_out = true;
    // Convert counts → volts → amps
    float v_rms = (float)sqrt(sq_sum / CT_RMS_SAMPLES) * (3.3f / 4095.0f);
    return v_rms * CT_AMPS_PER_VOLT;
}

// ---------------------------------------------------------------------------
// readFloatSwitch — debounced active-low read (true = float triggered / alarm)
// ---------------------------------------------------------------------------
bool readFloatSwitch(void) {
    uint8_t low_count = 0;
    for (int i = 0; i < 5; i++) {
        if (digitalRead(PIN_FLOAT_SWITCH) == LOW) low_count++;
        delay(10);
    }
    return (low_count >= 3);  // majority vote: true = high-water alarm active
}
