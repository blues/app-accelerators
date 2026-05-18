/*
 * livestock_water_tank_monitor_helpers.cpp
 * Blues Application Example — Off-Grid Livestock Water Tank Monitor
 *
 * Sensor-read, env-var parsing, alert, and summary helper implementations.
 * All globals these functions access are defined in the sketch (.ino) and
 * declared extern in livestock_water_tank_monitor_helpers.h.
 */
#include "livestock_water_tank_monitor_helpers.h"

// =============================================================================
// Clamp helpers — guard every env-var against out-of-range values.
// A value outside [minv, maxv] returns the caller-supplied fallback unchanged.
uint32_t clampU32(long v, uint32_t minv, uint32_t maxv, uint32_t fallback) {
    if (v < (long)minv || v > (long)maxv) return fallback;
    return (uint32_t)v;
}

float clampF(double v, float minv, float maxv, float fallback) {
    if ((float)v < minv || (float)v > maxv) return fallback;
    return (float)v;
}

// =============================================================================
// Fetch all Notehub environment variables in a single env.get request.
// Returns true when env.get responded without error and the body was valid;
// false on any I²C, allocation, or response error.
//
// On success: g_* globals are updated and the persisted env-var cache
// (g.env*) is written back so that a future wake with a failed env.get
// uses these values rather than reverting to compile-time defaults.
// On failure: g_* are unchanged (the caller pre-loaded them from g.env*).
//
// All parsed values are clamped to safe ranges so that an invalid Notehub
// entry cannot create tight-loop sleeps, impossible calibrations, or
// sample-counter overflows.
bool fetchEnvOverrides(void) {
    J *req = notecard.newRequest("env.get");
    if (!req) return false;
    J *rsp = notecard.requestAndResponse(req);
    if (!rsp) return false;
    if (notecard.responseError(rsp)) { notecard.deleteResponse(rsp); return false; }

    J *body = JGetObject(rsp, "body");
    if (!body) { notecard.deleteResponse(rsp); return false; }

    const char *v;
    if ((v = JGetString(body, "tank_depth_mm")) && *v)
        g_tankDepthMm = clampU32(atol(v),
                                 ENV_TANK_DEPTH_MM_MIN, ENV_TANK_DEPTH_MM_MAX,
                                 g_tankDepthMm);
    if ((v = JGetString(body, "sensor_min_mm")) && *v)
        g_sensorMinMm = clampU32(atol(v),
                                 ENV_SENSOR_MIN_MM_MIN, ENV_SENSOR_MIN_MM_MAX,
                                 g_sensorMinMm);
    if ((v = JGetString(body, "level_alert_pct")) && *v)
        g_levelAlertPct = (uint8_t)clampU32(atol(v),
                                             ENV_LEVEL_PCT_MIN, ENV_LEVEL_PCT_MAX,
                                             g_levelAlertPct);
    if ((v = JGetString(body, "level_critical_pct")) && *v)
        g_levelCriticalPct = (uint8_t)clampU32(atol(v),
                                                ENV_LEVEL_PCT_MIN, ENV_LEVEL_PCT_MAX,
                                                g_levelCriticalPct);
    if ((v = JGetString(body, "pump_on_amps")) && *v)
        g_pumpOnAmps = clampF(atof(v),
                              ENV_PUMP_AMPS_MIN, ENV_PUMP_AMPS_MAX,
                              g_pumpOnAmps);
    if ((v = JGetString(body, "battery_alert_v")) && *v)
        g_batteryAlertV = clampF(atof(v),
                                 ENV_BATT_V_MIN, ENV_BATT_V_MAX,
                                 g_batteryAlertV);
    if ((v = JGetString(body, "sample_interval_sec")) && *v)
        g_sampleIntervalSec = clampU32(atol(v),
                                       ENV_SAMPLE_SEC_MIN, ENV_SAMPLE_SEC_MAX,
                                       g_sampleIntervalSec);
    if ((v = JGetString(body, "summary_interval_min")) && *v)
        g_summaryIntervalMin = clampU32(atol(v),
                                        ENV_SUMMARY_MIN_MIN, ENV_SUMMARY_MIN_MAX,
                                        g_summaryIntervalMin);
    if ((v = JGetString(body, "alert_cooldown_sec")) && *v)
        g_alertCooldownSec = clampU32(atol(v),
                                      ENV_COOLDOWN_SEC_MIN, ENV_COOLDOWN_SEC_MAX,
                                      g_alertCooldownSec);

    // Sanity-check the two level thresholds after both have been parsed.
    // If level_critical_pct > level_alert_pct, the severity ordering is
    // inverted: "critical" would fire less often than "low", and the
    // mutual-exclusion guard in evaluateAlerts() would suppress the low
    // alert whenever the (misordered) critical threshold is in effect.
    // Clamp critical down to alert to restore correct ordering; the
    // operator can fix the Notehub values without a firmware change.
    if (g_levelCriticalPct > g_levelAlertPct) {
        g_levelCriticalPct = g_levelAlertPct;
    }

    // Cross-validate summary and sample intervals.
    // A summary window shorter than one sample wake produces empty windows:
    // no accumulator entries land in the window and the emitted averages
    // become -1.0 sentinels rather than real measurements. Enforce
    //   summary_interval_min >= ceil(sample_interval_sec / 60)
    // by clamping summary_interval_min up to the minimum that guarantees
    // at least one sample per window. Both values have already been parsed
    // above, so changing either one in Notehub produces the correct result
    // on the next inbound sync without a firmware change.
    {
        uint32_t minSummaryMin = (g_sampleIntervalSec + 59u) / 60u;
        if (g_summaryIntervalMin < minSummaryMin) {
            g_summaryIntervalMin = minSummaryMin;
        }
    }

    // Persist the just-applied values so a future wake with a failed env.get
    // still uses these rather than compile-time defaults.
    g.envTankDepthMm        = g_tankDepthMm;
    g.envSensorMinMm        = g_sensorMinMm;
    g.envLevelAlertPct      = g_levelAlertPct;
    g.envLevelCriticalPct   = g_levelCriticalPct;
    g.envPumpOnAmps         = g_pumpOnAmps;
    g.envBatteryAlertV      = g_batteryAlertV;
    g.envSampleIntervalSec  = g_sampleIntervalSec;
    g.envSummaryIntervalMin = g_summaryIntervalMin;
    g.envAlertCooldownSec   = g_alertCooldownSec;

    notecard.deleteResponse(rsp);
    return true;
}

// =============================================================================
// Read raw distance from the MB7389 ultrasonic sensor (analog output mode).
// The sensor continuously outputs Vcc/5120 per mm; no trigger pulse needed.
// Returns distance in mm, or -1.0 if the reading falls outside the valid range.
float readDistanceMm(void) {
    long sum = 0;
    for (int i = 0; i < LEVEL_ADC_SAMPLES; i++) {
        sum += analogRead(PIN_LEVEL_SENSOR);
        delayMicroseconds(200);
    }
    float distMm = ((float)sum / LEVEL_ADC_SAMPLES) * MB7389_MM_PER_COUNT;

    // Reject readings outside the sensor's documented valid range.
    if (distMm < MB7389_MIN_MM || distMm > MB7389_MAX_MM) return -1.0f;
    return distMm;
}

// =============================================================================
// Convert a raw distance measurement to a tank fill percentage.
//   distanceMm == sensor_min_mm (water at full)  →  100%
//   distanceMm == tank_depth_mm (water at empty) →    0%
// Returns -1.0 if the distance is invalid or the calibration values make
// no physical sense (sensor_min_mm >= tank_depth_mm).
float readLevelPct(float distanceMm) {
    if (distanceMm < 0.0f) return -1.0f;
    if (g_tankDepthMm <= g_sensorMinMm) return -1.0f;  // misconfigured

    float rangeMm  = (float)(g_tankDepthMm - g_sensorMinMm);
    float levelPct = 100.0f * ((float)g_tankDepthMm - distanceMm) / rangeMm;

    // Clamp to [0, 100] to absorb minor calibration offsets at the extremes.
    if (levelPct < 0.0f)   levelPct = 0.0f;
    if (levelPct > 100.0f) levelPct = 100.0f;
    return levelPct;
}

// =============================================================================
// Measure pump RMS current via the SCT-013-030 CT and 2×10kΩ bias circuit.
//
// The CT output is an AC voltage (50/60 Hz) riding on a ~1.65V DC midpoint
// set by the bias divider. Two passes over the ADC are needed:
//   Pass 1 (CT_BIAS_SAMPLES): estimate the DC midpoint (mean of raw counts).
//   Pass 2 (CT_RMS_SAMPLES):  compute RMS deviation from that midpoint.
// RMS voltage × 30 A/V → RMS current at the pump.
//
// Returns 0.0 if the computed amps are below the noise floor (pump off).
// Returns -1.0 if the bias midpoint is outside the plausible midscale band —
// an open or shorted divider shifts it to the ADC rails and would corrupt
// the RMS deviation; callers must exclude this wake from the accumulator.
float readPumpAmps(void) {
    // Pass 1: estimate DC midpoint set by the 2×10kΩ bias divider.
    long biasSum = 0;
    for (int i = 0; i < CT_BIAS_SAMPLES; i++) {
        biasSum += analogRead(PIN_PUMP_CT);
        delayMicroseconds(50);
    }
    float bias = (float)biasSum / (float)CT_BIAS_SAMPLES;

    // Reject if bias is outside the plausible midscale band. A healthy
    // 2×10kΩ divider at Vcc=3.3V produces ~2048 counts (1.65V). Values
    // below CT_BIAS_MIN_COUNTS or above CT_BIAS_MAX_COUNTS indicate an
    // open resistor, solder bridge, or missing bypass capacitor; the RMS
    // calculation would produce a meaningless result.
    if (bias < (float)CT_BIAS_MIN_COUNTS || bias > (float)CT_BIAS_MAX_COUNTS) {
        return -1.0f;
    }

    // Pass 2: sum squares of deviations from the bias midpoint over ~20 cycles.
    float sumSq = 0.0f;
    for (int i = 0; i < CT_RMS_SAMPLES; i++) {
        float s = (float)analogRead(PIN_PUMP_CT) - bias;
        sumSq  += s * s;
        delayMicroseconds(50);
    }
    float rmsAdc   = sqrtf(sumSq / (float)CT_RMS_SAMPLES);
    float rmsVolts = rmsAdc * (3.3f / 4095.0f);
    float amps     = rmsVolts * CT_AMPS_PER_VOLT;

    return (amps < CT_NOISE_FLOOR_A) ? 0.0f : amps;
}

// =============================================================================
// Read solar battery voltage via the switched 47kΩ/10kΩ voltage divider on A2.
// The BSS84 P-channel MOSFET (high-side switch, enabled via PIN_BATT_EN) is
// turned on just before sampling and off immediately after. This eliminates
// the quiescent divider draw from the 12V bus during sleep and prevents the
// A2 pin from being back-driven through its input-protection diode when the
// host MCU is unpowered (MCU off → PIN_BATT_EN floating → NPN off →
// 100 kΩ pullup holds PMOS gate at 12V → PMOS off → A2 at GND through 10kΩ).
//
// Returns -1.0 if the computed voltage falls outside the plausible range for
// a 12V solar system. An open divider or shorted R1 is caught here; the
// caller's battV > 0.0f guard excludes the sentinel from the accumulator,
// alert gate, and adaptive-sleep multiplier.
float readBatteryV(void) {
    // Enable the BSS84 PMOS switch; allow 1 ms for PMOS turn-on (BSS84 Qg
    // ≈ 1.6 nC; through the NPN collector path the gate swings from 12V to
    // ~0V in ≪ 1 ms, but the conservative margin ensures full enhancement
    // before sampling begins).
    digitalWrite(PIN_BATT_EN, HIGH);
    delay(1);

    long sum = 0;
    for (int i = 0; i < BATT_ADC_SAMPLES; i++) {
        sum += analogRead(PIN_BATTERY);
        delayMicroseconds(200);
    }

    // Disable immediately after sampling — leaves A2 pulled to GND through
    // the low-side 10kΩ once the PMOS turns off (≈ 100 µs via 100kΩ pullup).
    digitalWrite(PIN_BATT_EN, LOW);

    float vAdc  = ((float)sum / BATT_ADC_SAMPLES) * (3.3f / 4095.0f);
    float battV = vAdc / BATT_DIVIDER_RATIO;
    if (battV < BATT_PLAUSIBLE_V_MIN || battV > BATT_PLAUSIBLE_V_MAX) return -1.0f;
    return battV;
}

// =============================================================================
// Evaluate all three alert conditions and emit a sync:true Note for each that
// fires. Level alerts are cooldown-gated; battery_low is edge-triggered.
// Skips all checks when now == 0 (Notecard not yet time-synced) to prevent
// spurious alerts on the first cold boot.
// Cooldown timestamps and the alert counter are updated only after a confirmed
// successful Note queue operation; a Notecard transaction failure leaves state
// unchanged so the next wake can retry.
// alertsSinceLastSummary is saturated at UINT16_MAX rather than plain ++ to
// prevent counter wrap in extreme configurations (see GlobalState comment).
void evaluateAlerts(float levelPct, float pumpAmps,
                    float battV,   uint32_t now) {
    if (now == 0) return;

    // Level alerts — critical takes priority; level_low fires only when not critical.
    if (levelPct >= 0.0f) {
        if (levelPct < (float)g_levelCriticalPct) {
            if ((now - g.lastAlertEpoch[ALERT_LEVEL_CRIT]) >= g_alertCooldownSec) {
                if (sendAlert(ALERT_CODE_LEVEL_CRIT, levelPct, pumpAmps, battV)) {
                    g.lastAlertEpoch[ALERT_LEVEL_CRIT] = now;
                    if (g.alertsSinceLastSummary < UINT16_MAX) g.alertsSinceLastSummary++;
                }
            }
        } else if (levelPct < (float)g_levelAlertPct) {
            if ((now - g.lastAlertEpoch[ALERT_LEVEL_LOW]) >= g_alertCooldownSec) {
                if (sendAlert(ALERT_CODE_LEVEL_LOW, levelPct, pumpAmps, battV)) {
                    g.lastAlertEpoch[ALERT_LEVEL_LOW] = now;
                    if (g.alertsSinceLastSummary < UINT16_MAX) g.alertsSinceLastSummary++;
                }
            }
        }
    }

    // Solar battery voltage alert — edge-triggered.
    // Fires once when voltage first crosses below battery_alert_v, then
    // suppresses further alerts until it recovers above
    // battery_alert_v + BATTERY_LOW_HYSTERESIS_V. This prevents a persistently
    // low battery from triggering a sync:true radio session on every 60-minute
    // emergency-sleep wake, which would worsen the depletion it is warning about.
    if (battV > 0.0f) {
        if (battV < g_batteryAlertV) {
            if (!g.batteryLowActive) {
                // First sample in this low-battery episode: fire the alert.
                if (sendAlert(ALERT_CODE_BATTERY_LOW, levelPct, pumpAmps, battV)) {
                    g.lastAlertEpoch[ALERT_BATTERY_LOW] = now;
                    if (g.alertsSinceLastSummary < UINT16_MAX) g.alertsSinceLastSummary++;
                    g.batteryLowActive = true;
                }
            }
            // else: already in a low-battery episode — suppress repeated alerts.
        } else if (battV >= g_batteryAlertV + BATTERY_LOW_HYSTERESIS_V) {
            // Battery has recovered above threshold + hysteresis: re-arm.
            g.batteryLowActive = false;
        }
    }
}

// =============================================================================
// Emit an immediate alert Note with sync:true to bypass the outbound interval.
// Uses the compact template registered for tank_alert.qo (port 51, "compact"
// format) so the Note is satellite-safe on the NOTE-NBGLWX Starnote/NTN path.
// alertCode is an integer (ALERT_CODE_*) stored in the "alert_code" field;
// fixed-length binary encoding means pump_amps = 0.0 (pump off at alert time)
// is transmitted faithfully — compact encoding has no omitempty-style drop
// for zero-valued numeric fields.
// Guards on g.templatesInstalled: sending a free-form note.add to a templated
// Notefile would corrupt the binary-encoded Notefile. setup() retries
// defineTemplates() on every wake until both templates are confirmed, so an
// alert arriving before templates are confirmed is held until the next wake.
// Returns true if the Note was successfully queued; callers must not update
// cooldown state or counters unless true is returned.
bool sendAlert(int alertCode, float levelPct,
               float pumpAmps, float battV) {
    if (!g.templatesInstalled) return false;
    J *req = notecard.newRequest("note.add");
    if (!req) return false;
    JAddStringToObject(req, "file", NOTEFILE_ALERT);
    JAddBoolToObject(req,   "sync", true);
    J *body = JAddObjectToObject(req, "body");
    if (!body) { JDelete(req); return false; }
    JAddNumberToObject(body, "alert_code", alertCode);
    JAddNumberToObject(body, "level_pct",  levelPct);
    JAddNumberToObject(body, "pump_amps",  pumpAmps);
    JAddNumberToObject(body, "battery_v",  battV);
    return notecard.sendRequest(req);
}

// =============================================================================
// Queue a periodic summary Note using the template-encoded Notefile.
// The Notecard batches this with other queued Notes and ships at the next
// scheduled outbound sync; no immediate radio wake is required.
// pump_on is derived from the window-average current: true when pumpAmps
// (the mean of all valid samples in the window) is at or above g_pumpOnAmps.
// Returns false (without queuing anything) when template registration has not
// yet succeeded — sending a free-form note.add to a templated Notefile would
// defeat the binary-encoding that keeps wire size small for satellite links,
// and setup() already retries defineTemplates() on every wake until it sticks.
// Returns true if the Note was successfully queued; callers must not reset
// accumulators unless true is returned.
bool sendSummary(float levelPct, float distMm,
                 float pumpAmps, float battV) {
    if (!g.templatesInstalled) return false;
    J *req = notecard.newRequest("note.add");
    if (!req) return false;
    JAddStringToObject(req, "file", NOTEFILE_SUMMARY);
    J *body = JAddObjectToObject(req, "body");
    if (!body) { JDelete(req); return false; }
    JAddNumberToObject(body, "level_pct",   levelPct);
    JAddNumberToObject(body, "distance_mm", distMm);
    JAddNumberToObject(body, "pump_amps",   pumpAmps);
    JAddBoolToObject(body,   "pump_on",     pumpAmps >= g_pumpOnAmps);
    JAddNumberToObject(body, "battery_v",   battV);
    JAddNumberToObject(body, "alerts",      (int)g.alertsSinceLastSummary);
    return notecard.sendRequest(req);
}
