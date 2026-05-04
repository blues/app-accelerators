// ============================================================================
// cabinet_battery_sentinel_helpers.cpp
//
// Helper function implementations for Blues Application Accelerator #84 —
// Remote Cabinet Backup Battery Sentinel.
//
// See cabinet_battery_sentinel_helpers.h for all types, constants, extern
// globals, and function prototypes.
// ============================================================================

#include "cabinet_battery_sentinel_helpers.h"

// ─── Full initialisation — clean boot and invalid-segment recovery ────────────
// Called from both the true first-power-on path and the "state segment layout
// changed" recovery path so Notecard configuration, template registration, and
// window state are always consistent after either event.
void doFirstBoot(void) {
    memset(&state, 0, sizeof(state));
    state.voltMin     = VOLT_MIN_INIT;
    state.currMin     = CURR_MIN_INIT;
    state.tempMax     = TEMP_MAX_INIT;
    state.socPct      = DEFAULT_SOC_PCT_INIT;  // -1.0 = not yet commissioned
    state.lastSocInit = -1.0f;                 // no soc_pct_init has been applied yet
    // g_sampleSec and g_summaryMin are at compile-time defaults here;
    // fetchEnvOverrides() runs immediately after in setup() and hub.set is
    // re-applied if the operator has changed either value in Notehub.
    notecardConfigure();
    state.lastSummaryMin = g_summaryMin;
    state.lastSampleSec  = g_sampleSec;
}

// ─── One-time Notecard configuration ─────────────────────────────────────────
// hub.set: periodic mode.  Summary notes queue and ship every g_summaryMin
// minutes.  Alert notes with sync:true bypass this timer immediately.
void notecardConfigure(void) {
    J *req = notecard.newRequest("hub.set");
    JAddStringToObject(req, "product",  PRODUCT_UID);
    JAddStringToObject(req, "mode",     "periodic");
    JAddNumberToObject(req, "outbound", (int)g_summaryMin);
    JAddNumberToObject(req, "inbound",  120);   // check env-var updates every 2 h
    // sendRequestWithRetry covers the cold-boot I2C race where the host comes
    // up before the Notecard has finished its own initialisation sequence.
    if (notecard.sendRequestWithRetry(req, 5)) {
        state.hubConfigured = true;
    } else {
        notecard.logDebug("hub.set failed during notecardConfigure\n");
    }

    // Disable the Notecard's internal accelerometer.  In default configuration
    // it generates interrupt-processing blips visible on a Mojo trace, obscuring
    // the sleep-current baseline during bench energy measurements.
    req = notecard.newRequest("card.motion.mode");
    JAddBoolToObject(req, "stop", true);
    notecard.sendRequest(req);

    defineTemplate();
}

// ─── Register Note template for battery_summary.qo ───────────────────────────
// Templates encode the schema as fixed-length wire records rather than
// free-form JSON, shrinking per-Note wire size ~3–5×.  The numeric placeholders
// encode type and precision: 14.2 = 4-byte IEEE-754 float, 2 decimal places;
// 12 = 2-byte signed integer.
void defineTemplate(void) {
    J *req = notecard.newRequest("note.template");
    JAddStringToObject(req, "file", NOTEFILE_SUMMARY);
    JAddNumberToObject(req, "port", 50);
    J *body = JAddObjectToObject(req, "body");
    JAddNumberToObject(body, "volt_v",      14.2);  // window-average battery-terminal voltage (V)
    JAddNumberToObject(body, "curr_ma",     14.1);  // window-average current (mA)
    JAddNumberToObject(body, "power_mw",    14.1);  // window-average power (mW)
    JAddNumberToObject(body, "charge_ah",   14.3);  // net coulombs this window (Ah) — per-window delta
    JAddNumberToObject(body, "soc_pct",     14.1);  // running SoC estimate (%); -9999 = not commissioned
    JAddNumberToObject(body, "temp_c",      14.1);  // window-average temperature (°C)
    JAddNumberToObject(body, "volt_min_v",  14.2);  // minimum voltage in window
    JAddNumberToObject(body, "curr_min_ma", 14.1);  // deepest discharge current
    JAddNumberToObject(body, "temp_max_c",  14.1);  // peak temperature in window
    JAddNumberToObject(body, "samples",     12);    // valid voltage/current samples
    if (notecard.sendRequest(req)) {
        state.templateDefined = true;
    } else {
        notecard.logDebug("note.template registration failed\n");
    }
}

// ─── Environment variable fetch (every wake) ─────────────────────────────────
// Fetching on every wake means threshold or cadence changes pushed from Notehub
// propagate automatically on the next inbound sync — no truck roll required.
void fetchEnvOverrides(void) {
    J *req = notecard.newRequest("env.get");
    J *rsp = notecard.requestAndResponse(req);
    if (rsp == NULL) return;

    const char *err = JGetString(rsp, "err");
    if (err && *err) {
        notecard.logDebug("env.get error: ");
        notecard.logDebug(err);
        notecard.logDebug("\n");
        notecard.deleteResponse(rsp);
        return;
    }

    J *body = JGetObject(rsp, "body");
    if (body != NULL) {
        const char *s;
        if ((s = JGetString(body, "sample_interval_sec"))  && *s) g_sampleSec        = (uint32_t)atol(s);
        if ((s = JGetString(body, "summary_interval_min")) && *s) g_summaryMin       = (uint32_t)atol(s);
        if ((s = JGetString(body, "volt_min_v"))           && *s) g_voltMinV         = (float)atof(s);
        if ((s = JGetString(body, "volt_max_v"))           && *s) g_voltMaxV         = (float)atof(s);
        if ((s = JGetString(body, "float_current_hi_ma"))  && *s) g_floatCurrHiMa   = (float)atof(s);
        if ((s = JGetString(body, "temp_alert_c"))         && *s) g_tempAlertC       = (float)atof(s);
        if ((s = JGetString(body, "discharge_ma"))         && *s) g_dischargeMa      = (float)atof(s);
        if ((s = JGetString(body, "usable_capacity_ah"))   && *s) g_usableCapacityAh = (float)atof(s);
        if ((s = JGetString(body, "soc_low_pct"))          && *s) g_socLowPct        = (float)atof(s);
        if ((s = JGetString(body, "soc_pct_init"))         && *s) g_socPctInit       = (float)atof(s);
    }
    notecard.deleteResponse(rsp);

    // ── Cadence clamps ────────────────────────────────────────────────────
    if (g_sampleSec  < 30)    g_sampleSec  = 30;
    if (g_sampleSec  > 3600)  g_sampleSec  = 3600;
    if (g_summaryMin < 5)     g_summaryMin = 5;
    if (g_summaryMin > 1440)  g_summaryMin = 1440;  // max 24 h

    // ── Alert threshold validation ────────────────────────────────────────
    // A misconfigured env-var string parses to 0 via atof/atol, which can
    // invert or disable alert logic entirely (e.g., discharge_ma=0 would
    // make every float sample look like a power outage, volt_min_v >= volt_max_v
    // would make the voltage window logically impossible).  Clamp and enforce
    // relationships here so field operator mistakes degrade gracefully.

    // Voltage window: 9–32 V covers 12 V and 24 V positive-referenced bus systems.
    // The INA228 measures up to 85 V; 32 V comfortably covers a 24 V VRLA or LFP
    // charger bus at absorption (~29 V) with margin for end-of-charge transients.
    if (g_voltMinV < 9.0f)   g_voltMinV = 9.0f;
    if (g_voltMinV > 32.0f)  g_voltMinV = 32.0f;
    if (g_voltMaxV < 9.0f)   g_voltMaxV = 9.0f;
    if (g_voltMaxV > 32.0f)  g_voltMaxV = 32.0f;
    // Enforce volt_min_v < volt_max_v; revert both to compile-time defaults
    // so the device keeps meaningful alert coverage rather than firing
    // either every sample or never.
    if (g_voltMinV >= g_voltMaxV) {
        notecard.logDebug("fetchEnvOverrides: volt_min_v >= volt_max_v — "
                          "reverting both voltage thresholds to defaults\n");
        g_voltMinV = DEFAULT_VOLT_MIN_V;
        g_voltMaxV = DEFAULT_VOLT_MAX_V;
    }

    // float_current_hi_ma is a float-state (positive-region) threshold;
    // a negative value would fire on every non-discharge sample.
    if (g_floatCurrHiMa < 0.0f) g_floatCurrHiMa = 0.0f;

    // Temperature alert: 20–80 °C is the physically plausible trigger range
    // for an equipment cabinet battery.
    if (g_tempAlertC < 20.0f)  g_tempAlertC = 20.0f;
    if (g_tempAlertC > 80.0f)  g_tempAlertC = 80.0f;

    // discharge_ma must be strictly negative: it separates the float-current
    // (positive) region from the outage (negative) region.  A zero or positive
    // value would collapse both regions and disable all float-state alerts.
    // Also clamp the magnitude to prevent an absurdly large threshold from
    // silently disabling the power-outage alert.
    if (g_dischargeMa >= 0.0f)      g_dischargeMa = -1.0f;
    if (g_dischargeMa < -10000.0f)  g_dischargeMa = -10000.0f;

    // SoC estimation parameters.
    // usable_capacity_ah must be a positive non-zero value; a zero or negative
    // result from atof (e.g. mis-typed env var) would produce division by zero
    // or an inverted delta — clamp to a safe minimum.
    if (g_usableCapacityAh <= 0.0f) g_usableCapacityAh = DEFAULT_USABLE_CAPACITY_AH;

    // soc_low_pct is a percentage in [1, 99]; clamp to a physically meaningful
    // range so a misconfiguration doesn't disable the alert permanently (> 100)
    // or fire it on every sample (≤ 0).
    if (g_socLowPct <= 0.0f)   g_socLowPct = 1.0f;
    if (g_socLowPct >= 100.0f) g_socLowPct = 99.0f;
}

// ─── INA228 initialisation (runs every wake) ─────────────────────────────────
// The Cygnet's power rail is cut during sleep, which also powers off the INA228.
// Re-initialise the chip on every wake — its internal registers reset at POR.
bool initINA228(void) {
    if (!ina228.begin(INA228_I2C_ADDR, &Wire)) {
        notecard.logDebug("INA228: begin() failed — check Qwiic wiring and address.\n");
        return false;
    }
    // Calibrate for the onboard 15 mΩ shunt at 8 A full-scale.
    ina228.setShunt(INA228_SHUNT_OHMS, INA228_MAX_CURRENT_A);
    notecard.logDebug("INA228: ready — 15 mΩ shunt, 8 A full-scale.\n");
    return true;
}

// ─── Sensor reads ─────────────────────────────────────────────────────────────

// Returns true battery-terminal voltage in V.
//
// With Battery(+) wired to INA228 V+ and the load/charger bus wired to V−,
// the INA228 bus-voltage register samples V− (load side), not the battery
// terminal.  readShuntVoltage() returns the differential V+ − V− in mV
// (positive during discharge, negative during float/charge), so adding it
// to the bus voltage yields the true battery-terminal voltage:
//
//   V_terminal = V_bus + V_shunt_mV / 1000.0
//
// At float current (≤ 500 mA) the shunt drop across 15 mΩ is ≤ 7.5 mV —
// negligible.  At heavy discharge (e.g. 3.2 A) it reaches +48 mV, which
// is material for float-voltage alert accuracy and summary fidelity.
float readBatteryVoltage(void) {
    // readBusVoltage() → V at V−; readShuntVoltage() → mV across shunt (V+−V−).
    return ina228.readBusVoltage() + ina228.readShuntVoltage() / 1000.0f;
}

// Returns current in mA.
//
// Sign convention (firmware-level, after negating the raw INA228 reading):
//   Positive = charger supplying float current (normal operation).
//   Negative = battery discharging into the load (power outage or load spike).
//
// The INA228 is wired with Battery(+) on IN+ and the load bus on IN−, so the
// chip reports a *positive* raw value when current flows battery→load
// (discharge direction).  Negating here makes every downstream check use the
// intuitive convention: positive = charger present, negative = outage event.
// See §4 of the README for the wiring diagram.
//
// API note: readCurrent() is the primary virtual method in the Adafruit_INA2xx
// base class; it returns milliamps (the implementation multiplies the raw
// register value by _current_lsb × 1000).  getCurrent_mA() is a thin wrapper
// around the same call included for INA21x compatibility — readCurrent() is
// used here to call the virtual directly.
float readBatteryCurrent(void) {
    return -(ina228.readCurrent());   // negate: positive=float, negative=discharge
                                      // readCurrent() returns mA; no unit conversion needed
}

// Reads pack surface temperature from the NTC thermistor using the β equation.
// Topology: +3V3 → 10 kΩ (pull-up) → A0 → NTC → GND
// Returns °C, or NAN if the probe appears open or shorted.
float readPackTempC(void) {
    // Average 16 ADC samples to reduce quantisation noise on the 12-bit ADC.
    int32_t raw = 0;
    for (int i = 0; i < 16; i++) raw += analogRead(NTC_PIN);
    raw /= 16;

    float v = (float)raw * (NTC_VCC / (float)ADC_FULL);

    // Reject rail-clamp readings that indicate an open or shorted probe.
    if (v <= 0.05f || v >= (NTC_VCC - 0.05f)) return NAN;

    float r_ntc = NTC_R_PULLUP * v / (NTC_VCC - v);
    float t_inv = (1.0f / NTC_T0_K) + (1.0f / NTC_BETA) * logf(r_ntc / NTC_R0);
    return (1.0f / t_inv) - 273.15f;
}

// ─── Send hourly summary note ─────────────────────────────────────────────────
// Returns true on successful delivery.  Resets metric accumulators on success
// so that a transient Notecard I2C fault preserves window data for a retry.
//
// When both INA228 counts are zero (sensor was unreachable all window), the note
// is still emitted — all INA228 fields carry SUMMARY_INVALID_SENTINEL and
// `samples` is 0 — so Notehub shows a visible fault window rather than a silent
// gap.  The ina228_unreachable alert (fired from setup()) provides the
// immediate notification; the sentinel-filled summary preserves the time-series
// continuity for downstream analytics.
bool sendSummary(void) {
    float voltAvg = state.voltCount > 0
                    ? state.voltSum / (float)state.voltCount : SUMMARY_INVALID_SENTINEL;
    float currAvg = state.currCount > 0
                    ? state.currSum / (float)state.currCount : SUMMARY_INVALID_SENTINEL;
    float tempAvg = state.tempCount > 0
                    ? state.tempSum / (float)state.tempCount : SUMMARY_INVALID_SENTINEL;
    float voltMin = state.voltCount > 0 ? state.voltMin : SUMMARY_INVALID_SENTINEL;
    float currMin = state.currCount > 0 ? state.currMin : SUMMARY_INVALID_SENTINEL;
    float tempMax = state.tempCount > 0 ? state.tempMax : SUMMARY_INVALID_SENTINEL;
    // power_mw is meaningful only when both averages are valid.
    float powerMw = (state.voltCount > 0 && state.currCount > 0)
                    ? voltAvg * currAvg : SUMMARY_INVALID_SENTINEL;

    // Retry up to 3 times; only reset accumulators on confirmed delivery.
    bool sent = false;
    for (int attempt = 0; attempt < 3 && !sent; attempt++) {
        J *req = notecard.newRequest("note.add");
        JAddStringToObject(req, "file", NOTEFILE_SUMMARY);
        // No sync:true — summary notes ride the next periodic outbound session.
        J *body = JAddObjectToObject(req, "body");
        JAddNumberToObject(body, "volt_v",      voltAvg);
        JAddNumberToObject(body, "curr_ma",     currAvg);
        JAddNumberToObject(body, "power_mw",    powerMw);
        // charge_ah is derived entirely from current samples.  If no valid
        // current samples were collected (INA228 unreachable all window),
        // emit SUMMARY_INVALID_SENTINEL — not 0.0, which is a valid reading
        // for a window where charge and discharge balanced exactly.
        JAddNumberToObject(body, "charge_ah",   state.currCount > 0
                                                ? state.chargeAh
                                                : SUMMARY_INVALID_SENTINEL);
        // soc_pct carries SUMMARY_INVALID_SENTINEL (-9999) when SoC has not
        // been commissioned (operator has not yet set soc_pct_init).  Once
        // commissioned, it reflects the coulomb-counted running estimate.
        JAddNumberToObject(body, "soc_pct",     state.socPct >= 0.0f
                                                ? state.socPct
                                                : SUMMARY_INVALID_SENTINEL);
        JAddNumberToObject(body, "temp_c",      tempAvg);
        JAddNumberToObject(body, "volt_min_v",  voltMin);
        JAddNumberToObject(body, "curr_min_ma", currMin);
        JAddNumberToObject(body, "temp_max_c",  tempMax);
        // Emit the smaller of the two INA228 valid-sample counts so the field
        // accurately reflects the number of complete voltage+current samples.
        JAddNumberToObject(body, "samples",     (int)(state.voltCount < state.currCount
                                                      ? state.voltCount : state.currCount));
        sent = notecard.sendRequest(req);
        if (!sent && attempt < 2) {
            notecard.logDebug("note.add (summary) failed, retrying\n");
            delay(500);
        }
    }
    if (!sent) {
        notecard.logDebug("note.add (summary) failed after 3 attempts — window data preserved\n");
        return false;
    }

    // Reset metric accumulators for the next window only after confirmed delivery.
    state.voltSum   = state.currSum   = state.tempSum = 0.0f;
    state.voltCount = state.currCount = state.tempCount = 0;
    state.chargeAh  = 0.0f;
    state.voltMin   = VOLT_MIN_INIT;
    state.currMin   = CURR_MIN_INIT;
    state.tempMax   = TEMP_MAX_INIT;
    return true;
}

// ─── Send an immediate-sync alert note ───────────────────────────────────────
// sync:true causes the Notecard to wake the radio immediately rather than
// waiting for the next outbound window.  Used for threshold trips and for the
// ina228_unreachable sensor-fault notification.
//
// Returns true when note.add was accepted by the Notecard.  The caller is
// responsible for arming the per-alert cooldown ONLY on a true return; if
// every attempt fails the cooldown must NOT be set so the next wake retries
// the alert instead of suppressing it for 30 minutes without queuing anything.
bool sendAlert(const char *alertType, float volt, float curr, float temp) {
    bool sent = false;
    for (int attempt = 0; attempt < 3 && !sent; attempt++) {
        J *req = notecard.newRequest("note.add");
        JAddStringToObject(req, "file", NOTEFILE_ALERT);
        JAddBoolToObject(req,   "sync", true);
        J *body = JAddObjectToObject(req, "body");
        JAddStringToObject(body, "alert",   alertType);
        // Use SUMMARY_INVALID_SENTINEL instead of NAN for any metric that was
        // unavailable at alert time (NAN is not valid JSON).
        JAddNumberToObject(body, "volt_v",  isnan(volt) ? SUMMARY_INVALID_SENTINEL : volt);
        JAddNumberToObject(body, "curr_ma", isnan(curr) ? SUMMARY_INVALID_SENTINEL : curr);
        JAddNumberToObject(body, "temp_c",  isnan(temp) ? SUMMARY_INVALID_SENTINEL : temp);
        // Include current SoC in every alert note when commissioned — it is
        // particularly useful context for power_outage and soc_low events.
        // battery_alert.qo is free-form JSON (not template-backed), so adding
        // this field only when valid is safe and keeps note sizes small.
        if (state.socPct >= 0.0f) {
            JAddNumberToObject(body, "soc_pct", state.socPct);
        }
        sent = notecard.sendRequest(req);
        if (!sent && attempt < 2) {
            notecard.logDebug("note.add (alert) failed, retrying\n");
            delay(500);
        }
    }
    if (!sent) {
        notecard.logDebug("note.add (alert) failed after 3 attempts\n");
    }
    return sent;
}

// ─── Serialise state and cut host power until the next sample interval ───────
void sleepHost(void) {
    NotePayloadDesc payload = {0, 0, 0};
    NotePayloadAddSegment(&payload, STATE_SEG_ID, &state, sizeof(state));
    // NotePayloadSaveAndSleep writes the payload to Notecard flash, then issues
    // card.attn in "sleep" mode.  The ATTN pin cuts Cygnet power for g_sampleSec
    // seconds; the Notecard idles at ~8 µA in the meantime.
    NotePayloadSaveAndSleep(&payload, g_sampleSec, NULL);

    // Should not reach here.  If we do, ATTN is not switching the host power
    // rail — verify the Notecarrier CX is being used (not a bare Notecarrier X).
    delay(15000);
}
