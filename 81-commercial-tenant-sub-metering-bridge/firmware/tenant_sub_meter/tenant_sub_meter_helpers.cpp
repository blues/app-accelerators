/*
 * tenant_sub_meter_helpers.cpp — Helper implementations for
 *                                Commercial Tenant Energy Monitoring Bridge
 *
 * All functions declared in tenant_sub_meter_helpers.h are implemented here.
 * Globals (notecard, state, cfg) are defined in tenant_sub_meter.ino and
 * referenced via the extern declarations in the shared header.
 *
 * SPDX-License-Identifier: MIT
 */
#include "tenant_sub_meter_helpers.h"
#include <math.h>

// =============================================================================
// V×I channel measurement (sequential interleaved ADC reads)
// =============================================================================
// Interleaves ADC_SAMPLES voltage (transducer on VOLTAGE_PIN) and current
// (Rogowski+integrator output on current_pin) samples.  Both signals are
// AC-coupled to 1.65 V by external bias networks; DC is removed in software.
//
// Active power = mean(v_centred[n] × i_centred[n]), scaled by both sensor
// calibration constants.  Voltage is read first in each pair; current follows
// ~50 µs later.  At 60 Hz, 50 µs corresponds to δ = 2π × 60 × 50 µs ≈ 1.08°.
// The relative active-power error is approximately tan(φ)·sin(δ) ≈ tan(φ) × 0.019,
// where φ = arccos(PF) is the load power-factor angle — about 1 % at PF 0.9
// and about 2 % at PF 0.7.  See README §6.3 for the full derivation.
//
// Each call produces one ~200 ms active-power snapshot.  The caller accumulates
// these snapshots into TenantState.demand_window_mwh / demand_window_sec; peak
// demand is derived from completed DEMAND_INTERVAL_SEC windows, not from a single
// snapshot maximum.
//
// Three categories of fault check run on every call:
//   • Current-channel bias range       → FAULT_BIAS_RANGE
//   • Current-channel saturation       → FAULT_SATURATED
//   • Shared voltage-reference path    → FAULT_VOLTAGE_REF
//     (bias, saturation, and line-voltage plausibility)
// Low RMS current (< MIN_SIGNAL_AMPS) is logged to Serial as a commissioning
// diagnostic but is NOT placed in m.fault: a legitimately unloaded tenant
// circuit is indistinguishable from a disconnected Rogowski coil by current alone.
// =============================================================================
ChannelMeasurement measureChannel(uint8_t current_pin) {
    // Static buffers avoid large stack frames; single-threaded so this is safe.
    static int16_t v_buf[ADC_SAMPLES];
    static int16_t i_buf[ADC_SAMPLES];

    // Interleaved burst — voltage read first in each pair so any systematic
    // phase offset between V and I samples is consistent across all pairs.
    for (int n = 0; n < ADC_SAMPLES; n++) {
        v_buf[n] = (int16_t)analogRead(VOLTAGE_PIN);
        i_buf[n] = (int16_t)analogRead(current_pin);
    }

    // Compute DC offsets (dominated by the 1.65 V half-rail bias point).
    int32_t v_sum = 0, i_sum = 0;
    for (int n = 0; n < ADC_SAMPLES; n++) {
        v_sum += v_buf[n];
        i_sum += i_buf[n];
    }
    float v_dc = (float)v_sum / (float)ADC_SAMPLES;
    float i_dc = (float)i_sum / (float)ADC_SAMPLES;

    // Compute RMS² sums and cross-product sum over DC-removed samples.
    float v_sq = 0.0f, i_sq = 0.0f, cross = 0.0f;
    for (int n = 0; n < ADC_SAMPLES; n++) {
        float vd = (float)v_buf[n] - v_dc;
        float id = (float)i_buf[n] - i_dc;
        v_sq  += vd * vd;
        i_sq  += id * id;
        cross += vd * id;
    }

    // Convert from ADC-count domain to physical units.
    //   scale = V_ref / full_scale = volts per count
    //   i_rms_adc  = RMS of (ADC counts) × scale  →  ADC-pin V_rms
    //   p_adc      = mean(vd×id) × scale²          →  ADC-side mean product (V²)
    const float scale   = ADC_VREF / ADC_FULL_SCALE;
    float i_rms_adc = sqrtf(i_sq / (float)ADC_SAMPLES) * scale;
    float p_adc     = (cross / (float)ADC_SAMPLES) * scale * scale;

    ChannelMeasurement m;
    m.rms_amps = i_rms_adc * cfg.rogowski_amps_per_volt;
    // Active power W = mean(v_physical × i_physical)
    //   = (cross/N × scale²) × volt_scale × rogowski_amps_per_volt
    m.watts    = p_adc * cfg.volt_scale * cfg.rogowski_amps_per_volt;

    // ── Per-channel sanity checks ─────────────────────────────────────────────
    // These guards detect hardware faults that would otherwise silently produce
    // zeroed or inflated energy readings in billing data.  All checks operate
    // on the raw ADC-count domain before unit conversion so they are independent
    // of the calibration constants.  Non-zero fault bits are OR'd into
    // TenantState.fault_accum over the summary period and reported in fault_mask.
    m.fault = 0;

    // ── Current-channel bias check ────────────────────────────────────────────
    // DC offset must sit within the expected 1.40–1.90 V half-rail window.
    // A reading outside this range indicates a disconnected bias resistor,
    // shorted decoupling capacitor, or absent 3.3 V supply on one half of the
    // divider.
    if (i_dc < BIAS_MIN_COUNTS || i_dc > BIAS_MAX_COUNTS) {
        m.fault |= FAULT_BIAS_RANGE;
    }

    // ── Current-channel saturation check ─────────────────────────────────────
    // Trips when the DC-removed current signal's RMS exceeds the RMS-equivalent of
    // 85 % of the 1.65 V half-rail peak amplitude (≈ 0.99 V RMS, ≈ 1230 ADC counts;
    // see SATURATION_RMS_COUNTS rationale in helpers.h).  Crossing this threshold
    // means the signal is nearing ADC rail clipping, which would distort both the
    // active-power computation and the RMS current result.
    float i_rms_counts = sqrtf(i_sq / (float)ADC_SAMPLES);
    if (i_rms_counts > SATURATION_RMS_COUNTS) {
        m.fault |= FAULT_SATURATED;
    }

    // ── Shared voltage-reference checks (FAULT_VOLTAGE_REF) ──────────────────
    // The voltage transducer path is shared by all tenant channels: a failed or
    // drifting voltage reference silently corrupts every channel's watt and Wh
    // values while per-channel current checks remain clean.  These three checks
    // mirror the current-channel guards and additionally verify that the measured
    // line voltage is plausible.  FAULT_VOLTAGE_REF is propagated into m.fault
    // for every channel on the same wake so downstream billing can identify
    // periods where all power calculations are suspect.
    float v_rms_counts = sqrtf(v_sq / (float)ADC_SAMPLES);
    float v_rms_adc    = v_rms_counts * scale;
    float line_v_rms   = v_rms_adc * cfg.volt_scale;

    if (v_dc < BIAS_MIN_COUNTS || v_dc > BIAS_MAX_COUNTS) {
        // Voltage-path bias out of range: disconnected transducer or failed divider.
        m.fault |= FAULT_VOLTAGE_REF;
    }
    if (v_rms_counts > SATURATION_RMS_COUNTS) {
        // Voltage-path approaching saturation: signal nearing ADC rail clipping
        // on the transducer output (same threshold rationale as the current
        // channel — see SATURATION_RMS_COUNTS in helpers.h).
        m.fault |= FAULT_VOLTAGE_REF;
    }
    if (line_v_rms < VOLTAGE_MIN_V_RMS) {
        // Line voltage implausibly low: disconnected transducer, open burden
        // resistor, or failed bias network rather than a normal brownout.
        m.fault |= FAULT_VOLTAGE_REF;
    }

    // ── Low-signal commissioning diagnostic ───────────────────────────────────
    // RMS current below MIN_SIGNAL_AMPS may indicate an open-circuit Rogowski
    // coil or disconnected integrator, but is indistinguishable from a
    // legitimately unloaded tenant circuit.  NOT propagated to m.fault to
    // prevent valid zero-usage intervals from being quarantined by downstream
    // billing.  Visible on Serial for commissioning and bench validation only.
    if (m.rms_amps < MIN_SIGNAL_AMPS) {
        Serial.println("[diag] low-current channel — unloaded tenant or disconnected sensor?");
    }

    return m;
}

// =============================================================================
// Notecard cold-boot readiness handshake
// =============================================================================
// sendRequestWithRetry MUST be the first Notecard transaction on every cold
// boot.  It handles the I2C race condition where the STM32L433 host comes up
// several hundred milliseconds before the Notecard is ready to accept requests.
// On warm wakes the function returns almost immediately.
//
// sendRequestWithRetry returns bool (true = acknowledged, false = timeout).
// It does NOT return a J* response pointer.  card.version is used here because
// it is lightweight and read-only; the response is discarded — the sole purpose
// of this call is to establish that the I2C bus is responsive before any other
// transaction is attempted.
// =============================================================================
bool notecardReady(uint32_t timeout_sec) {
    J *req = notecard.newRequest("card.version");
    if (!req) return false;
    bool ok = notecard.sendRequestWithRetry(req, timeout_sec);
    if (!ok) Serial.println("[notecard] readiness check timed out");
    return ok;
}

// =============================================================================
// Environment variable fetch
// =============================================================================
// Called on every wake cycle.  Firmware defaults are applied first; Notehub
// values overwrite only the variables that have been explicitly set.
// Upper and lower bounds clamp values to safe operating ranges so a typo or
// hostile env var cannot brick the device.
//
// notecardReady() must be called before this function so that the I2C cold-
// boot race has already been resolved.
// =============================================================================
void fetchEnvOverrides(void) {
    cfg.sample_interval_sec    = DEFAULT_SAMPLE_INTERVAL_SEC;
    cfg.summary_interval_min   = DEFAULT_SUMMARY_INTERVAL_MIN;
    cfg.rogowski_amps_per_volt = DEFAULT_ROGOWSKI_AMPS_PER_VOLT;
    cfg.volt_scale             = DEFAULT_VOLT_SCALE;
    cfg.num_tenants            = DEFAULT_NUM_TENANTS;

    J *req = notecard.newRequest("env.get");
    if (!req) return;
    J *rsp = notecard.requestAndResponse(req);
    if (!rsp) return;
    if (notecard.responseError(rsp)) {
        notecard.deleteResponse(rsp);
        return;
    }

    J *body = JGetObject(rsp, "body");
    if (body) {
        const char *v;
        if ((v = JGetString(body, "sample_interval_sec")) && *v)
            cfg.sample_interval_sec = (uint32_t)constrain(
                atol(v), (long)MIN_SAMPLE_INTERVAL_SEC, (long)MAX_SAMPLE_INTERVAL_SEC);
        if ((v = JGetString(body, "summary_interval_min")) && *v)
            cfg.summary_interval_min = (uint32_t)constrain(
                atol(v), (long)MIN_SUMMARY_INTERVAL_MIN, (long)MAX_SUMMARY_INTERVAL_MIN);
        if ((v = JGetString(body, "rogowski_amps_per_volt")) && *v) {
            float f = (float)atof(v);
            if (f > 0.0f && f <= 10000.0f) cfg.rogowski_amps_per_volt = f;
        }
        if ((v = JGetString(body, "volt_scale")) && *v) {
            float f = (float)atof(v);
            if (f > 0.0f && f <= 10000.0f) cfg.volt_scale = f;
        }
        if ((v = JGetString(body, "num_tenants")) && *v)
            cfg.num_tenants = (uint8_t)constrain(atoi(v), 1, 4);
    }
    notecard.deleteResponse(rsp);
}

// =============================================================================
// Hub configuration (shared by initNotecard and reissueHubSet)
// =============================================================================
// Issues hub.set using sendRequestWithRetry to handle the cold-boot I2C race
// where the Cygnet host comes up before the Notecard is ready.  The outbound
// cadence is set to cfg.summary_interval_min so the Notecard flushes queued
// notes roughly as often as new summaries are created.
//
// sendRequestWithRetry returns bool (true = acknowledged without error).
// hub.set has no meaningful response body, so the bool return is sufficient;
// there is no response-returning retry variant needed here.
//
// Returns true only when the Notecard acknowledges the request without error.
// =============================================================================
static bool configureHub(void) {
    J *req = notecard.newRequest("hub.set");
    if (!req) return false;
    JAddStringToObject(req, "product",  PRODUCT_UID);
    JAddStringToObject(req, "mode",     "periodic");
    JAddNumberToObject(req, "outbound", (double)cfg.summary_interval_min);
    JAddNumberToObject(req, "inbound",  (double)INBOUND_MINUTES);
    bool ok = notecard.sendRequestWithRetry(req, 10);
    if (!ok) Serial.println("[notecard] hub.set failed");
    return ok;
}

// First-boot Notecard configuration.
// fetchEnvOverrides() must be called first so cfg.summary_interval_min is
// valid before the outbound cadence is written to hub.set.
bool initNotecard(void) {
    return configureHub();
}

// Re-issues hub.set after the summary cadence changes via env var.
bool reissueHubSet(void) {
    return configureHub();
}

// =============================================================================
// Note template registration
// =============================================================================
// Registers the binary-packed template for meter_summary.qo.  Returns true
// only when the Notecard acknowledges without error; the caller latches
// notecard_configured only after a true return so a failed registration is
// retried automatically on the next wake.
//
// Type hint 14.1 = 4-byte IEEE-754 float (TFLOAT32).
// =============================================================================
bool defineTemplates(void) {
    J *req = notecard.newRequest("note.template");
    if (!req) return false;
    JAddStringToObject(req, "file", SUMMARY_NOTEFILE);
    J *body = JAddObjectToObject(req, "body");
    char key[28];
    for (int t = 1; t <= 4; t++) {
        snprintf(key, sizeof(key), "t%d_wh",       t);
        JAddNumberToObject(body, key, 14.1);  // estimated interval energy, TFLOAT32
        snprintf(key, sizeof(key), "t%d_demand_w", t);
        JAddNumberToObject(body, key, 14.1);  // peak DEMAND_INTERVAL_SEC avg demand W, TFLOAT32
    }
    // fault_mask: 4-bit nibble per tenant channel (T1 in bits 3:0, T4 in bits 15:12).
    // Bits within each nibble: 0 = FAULT_BIAS_RANGE, 1 = FAULT_SATURATED,
    // 2 = reserved (was FAULT_NO_SIGNAL, retired), 3 = FAULT_VOLTAGE_REF.
    // FAULT_VOLTAGE_REF is set in all active channel nibbles simultaneously when
    // the shared voltage reference is suspect.  0 = no faults detected.
    // TFLOAT32 represents the full uint16 range (0–65535) without loss of precision.
    JAddNumberToObject(body, "fault_mask", 14.1);
    J *rsp = notecard.requestAndResponse(req);
    bool ok = rsp && !notecard.responseError(rsp);
    if (!ok) Serial.println("[init] note.template for meter_summary.qo failed — will retry");
    notecard.deleteResponse(rsp);
    return ok;
}

// =============================================================================
// Hourly summary emission
// =============================================================================
// Uses requestAndResponse so the return code can be checked.
// Returns true only when the Notecard acknowledges the note without error.
//
// The caller MUST NOT clear per-tenant accumulators unless this returns true.
// A false return preserves accumulated energy data so the next wake retries
// with the complete (growing) total, preventing silent data loss.
//
// Inactive tenants (index >= cfg.num_tenants) emit 0 so the Notehub schema
// remains consistent and downstream systems never see stale channel data.
// =============================================================================
bool sendSummary(void) {
    Serial.println("[summary] queuing meter_summary.qo");
    J *req = notecard.newRequest("note.add");
    if (!req) return false;

    JAddStringToObject(req, "file", SUMMARY_NOTEFILE);
    // No sync:true — the note batches with the Notecard's periodic outbound
    // window.  This keeps the cellular radio silent on sample wakes that fall
    // between syncs.
    J *body = JAddObjectToObject(req, "body");
    uint8_t active = (cfg.num_tenants <= 4) ? cfg.num_tenants : 4;
    char key[28];
    for (uint8_t t = 0; t < 4; t++) {
        float wh       = (t < active)
                         ? (float)((double)state.tenant[t].accum_wh_milli / 1000.0)
                         : 0.0f;
        float demand_w = (t < active)
                         ? ((float)state.tenant[t].peak_demand_cw / 100.0f)
                         : 0.0f;
        snprintf(key, sizeof(key), "t%d_wh",       t + 1);
        JAddNumberToObject(body, key, (double)wh);
        snprintf(key, sizeof(key), "t%d_demand_w", t + 1);
        JAddNumberToObject(body, key, (double)demand_w);
    }

    // Pack per-channel fault flags: 4-bit nibble per tenant channel.
    // T1 occupies bits 3:0, T4 bits 15:12.  Bit positions within each nibble:
    //   0 = FAULT_BIAS_RANGE, 1 = FAULT_SATURATED, 3 = FAULT_VOLTAGE_REF.
    //   Bit 2 reserved (FAULT_NO_SIGNAL retired — see measureChannel()).
    // FAULT_VOLTAGE_REF is set simultaneously in all active channel nibbles when
    // the shared voltage reference is suspect.  Inactive channels contribute 0.
    // Downstream billing should reject or quarantine any summary where fault_mask != 0.
    uint16_t fault_mask = 0;
    for (uint8_t t = 0; t < 4; t++) {
        uint8_t f = (t < active) ? state.tenant[t].fault_accum : 0U;
        fault_mask |= (uint16_t)(f & 0x0FU) << (t * 4U);
    }
    JAddNumberToObject(body, "fault_mask", (double)fault_mask);

    J *rsp = notecard.requestAndResponse(req);
    bool ok = rsp && !notecard.responseError(rsp);
    notecard.deleteResponse(rsp);
    return ok;
}

// =============================================================================
// Time helper
// =============================================================================

// Returns the current UTC epoch from the Notecard, or 0 if unavailable
// (Notecard not yet time-synced, I2C error, or error response).
uint32_t getEpochSec(void) {
    J *req = notecard.newRequest("card.time");
    if (!req) return 0;
    J *rsp = notecard.requestAndResponse(req);
    if (!rsp) return 0;
    if (notecard.responseError(rsp)) {
        notecard.deleteResponse(rsp);
        return 0;
    }
    uint32_t t = (uint32_t)JGetNumber(rsp, "time");
    notecard.deleteResponse(rsp);
    return t;
}
