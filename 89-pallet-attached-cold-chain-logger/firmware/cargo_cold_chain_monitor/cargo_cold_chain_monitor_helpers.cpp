/*
 * cargo_cold_chain_monitor_helpers.cpp
 *
 * Notecard configuration, sensor reads, alert/summary/log emission, and
 * shipment-state detection helpers for the cargo cold-chain monitor.
 *
 * Application flow (setup, loop) lives in the .ino; this file contains
 * every function declared in cargo_cold_chain_monitor_helpers.h.
 */

#include <Arduino.h>
#include <string.h>
#include "cargo_cold_chain_monitor_helpers.h"

// ===========================================================================
// Notecard request helpers
// ===========================================================================

bool ncSend(J *req) {
    J *rsp = notecard.requestAndResponse(req);
    if (!rsp) {
        Serial.println("[cargo] Notecard request returned NULL");
        return false;
    }
    if (notecard.responseError(rsp)) {
        Serial.print("[cargo] Notecard error: ");
        Serial.println(JGetString(rsp, "err"));
        notecard.deleteResponse(rsp);
        return false;
    }
    notecard.deleteResponse(rsp);
    return true;
}

J *ncQuery(J *req) {
    J *rsp = notecard.requestAndResponse(req);
    if (!rsp) {
        Serial.println("[cargo] Notecard request returned NULL");
        return NULL;
    }
    if (notecard.responseError(rsp)) {
        Serial.print("[cargo] Notecard error: ");
        Serial.println(JGetString(rsp, "err"));
        notecard.deleteResponse(rsp);
        return NULL;
    }
    return rsp;
}

// alertCooldownOk: returns true when an alert type is eligible to fire.
//   last == 0  → never fired; eligible immediately (even pre-sync).
//   last == 1  → fired before valid time (pre-sync sentinel); not eligible
//                while now is still 0.  On the first post-sync wake the
//                sentinel is promoted to now in evaluateAlerts() before this
//                function is called, so the normal elapsed-time path applies.
//   last > 1   → normal timestamp; eligible once ALERT_COOLDOWN_SEC elapses.
bool alertCooldownOk(uint32_t last, uint32_t now) {
    if (last == 0) return true;
    if (now  == 0) return false;
    if (now  <= last) return false;
    return (now - last) >= ALERT_COOLDOWN_SEC;
}

// ===========================================================================
// Notecard configuration
// ===========================================================================
void notecardConfigure() {
    J *req = notecard.newRequest("hub.set");
    if (PRODUCT_UID[0]) JAddStringToObject(req, "product", PRODUCT_UID);
    JAddStringToObject(req, "mode",     "periodic");
    // Use the persisted outbound cadence when it has been set so that a
    // dwell-mode extension applied on a previous wake is preserved across warm
    // boots.  On first configuration (last_outbound_min == 0) fall back to the
    // compiled default; applyDynamicOutbound() will adjust if the current state
    // calls for an extended cadence.
    uint32_t ob = (gState.last_outbound_min > 0) ? gState.last_outbound_min
                                                  : OUTBOUND_INTERVAL_MIN;
    JAddNumberToObject(req, "outbound", (double)ob);
    JAddNumberToObject(req, "inbound",  INBOUND_INTERVAL_MIN);
    J *rsp = notecard.requestAndResponseWithRetry(req, 5);
    if (rsp && !notecard.responseError(rsp)) {
        gState.hub_configured = true;
        if (gState.last_outbound_min == 0) {
            gState.last_outbound_min = OUTBOUND_INTERVAL_MIN;
        }
        Serial.println("[cargo] hub.set configured");
    } else {
        Serial.println("[cargo] hub.set failed — will retry on next wake");
    }
    if (rsp) notecard.deleteResponse(rsp);

    // card.transport — enable the automatic WiFi → cellular → Skylo satellite
    // (NTN) fallback that the README promises.  NTN is OFF by default on the
    // Notecard for Skylo (NOTE-NBGLWX): the factory transport never reaches the
    // satellite radio, so without this request queued alerts would sit in flash
    // forever once the pallet leaves terrestrial coverage.  "wifi-cell-ntn"
    // prefers WiFi where a provisioned AP is reachable (a connected DC), falls
    // back to cellular over road and rail, and finally to Skylo NTN where
    // neither is available and the antenna has sky exposure — with no firmware
    // branching.
    //
    // An initial non-NTN (cellular or WiFi) sync is required first so the device
    // can associate with Notehub and register templates before NTN is usable;
    // the cold-boot hub.set above triggers that first terrestrial sync, so
    // commission each unit where it has cellular/WiFi coverage.  The Notecard
    // persists this setting in its own flash, so issuing it once per cold boot
    // is sufficient.
    if (!gState.transport_configured) {
        req = notecard.newRequest("card.transport");
        JAddStringToObject(req, "method", "wifi-cell-ntn");
        if (ncSend(req)) {
            gState.transport_configured = true;
            Serial.println("[cargo] card.transport configured (wifi-cell-ntn)");
        } else {
            Serial.println("[cargo] card.transport failed — will retry on next wake");
        }
    }

    if (!gState.motion_configured) {
        req = notecard.newRequest("card.motion.mode");
        JAddBoolToObject(req,   "start",       true);
        JAddNumberToObject(req, "sensitivity", 2);
        if (ncSend(req)) {
            gState.motion_configured = true;
            Serial.println("[cargo] card.motion.mode configured");
        } else {
            Serial.println("[cargo] card.motion.mode failed — will retry on next wake");
        }
    }
}

// applyDynamicOutbound: compute the desired hub.set outbound cadence based on
// the current shipment state and re-issue hub.set only when the cadence differs
// from the last applied value.
//
// During confirmed DWELL, the outbound cadence is extended to
//   OUTBOUND_INTERVAL_MIN * gDwellBatchFactor (capped at 1440 min / 24 hr)
// so the Notecard syncs outbound only every few hours instead of every hour —
// directly reducing the number of Skylo NTN sessions during long warehouse or
// DC stays.  Alert and state-change notes marked sync:true bypass the outbound
// queue entirely and trigger an immediate session regardless of this cadence.
//
// On transition out of DWELL the cadence is restored to OUTBOUND_INTERVAL_MIN
// so the device resumes normal hourly syncs during transit and handling.
void applyDynamicOutbound() {
    uint32_t desired = OUTBOUND_INTERVAL_MIN;
    if (gState.shipment_state == SHIP_STATE_DWELL) {
        uint32_t ext = OUTBOUND_INTERVAL_MIN * gDwellBatchFactor;
        desired = (ext > 1440U) ? 1440U : ext;
    }
    if (desired == gState.last_outbound_min) return;  // already at the right cadence

    J *req = notecard.newRequest("hub.set");
    JAddStringToObject(req, "mode",     "periodic");
    JAddNumberToObject(req, "outbound", (double)desired);
    JAddNumberToObject(req, "inbound",  INBOUND_INTERVAL_MIN);
    J *rsp = notecard.requestAndResponseWithRetry(req, 3);
    if (rsp && !notecard.responseError(rsp)) {
        gState.last_outbound_min = desired;
        Serial.print("[cargo] outbound cadence -> ");
        Serial.print(desired); Serial.println(" min");
    } else {
        Serial.println("[cargo] hub.set (dynamic outbound) failed — will retry next wake");
    }
    if (rsp) notecard.deleteResponse(rsp);
}

// loadOrIncrementBootSeg: read the boot-segment counter and the tilt-baseline
// orientation from the Notecard local notefile chain_boot.dbx, increment the
// counter, write both back, and store the new boot_seg in gState.boot_seg.
// Called once on every cold boot (not on warm boot — the sleep payload
// preserves both fields across planned sleep/wake cycles).
//
// chain_boot.dbx is a local-only (.dbx) DB Notefile stored in Notecard flash.
// It is NOT synced to Notehub.  This provides cold-boot-resilient persistence
// independent of the NotePayloadSaveAndSleep mechanism:
//   - Planned sleep/wake: boot_seg and baseline_orientation are preserved in
//     the sleep payload (the warm-boot path; this function is not called).
//   - Uncontrolled cold boot (power loss, brown-out): the sleep payload is
//     absent, but chain_boot.dbx retains both values so the boot counter
//     increments correctly and the tilt baseline is restored from the
//     orientation captured at logger activation rather than being re-seeded
//     from the post-boot orientation.
//
// If the Notecard is unreachable on cold boot, boot_seg defaults to 1 for the
// first boot and increments from the stored value when the Notecard recovers.
// A missing baseline_orientation (first-ever activation) leaves
// gState.baseline_orientation empty; setup() will populate it on the first
// successful card.motion read and call persistBaselineOrientation() to save it.
// writeChainBootNote: upsert the chain_boot.dbx "v1" note containing the
// current boot_seg and (if set) baseline_orientation.  Tries note.update first
// — that's the steady-state path once the note has been created.  On failure
// (most commonly because the note does not yet exist on the very first cold
// boot) falls back to note.add to create it.  Without this fallback the
// counter and the tilt baseline would never persist to local flash and the
// uncontrolled cold-boot resilience guarantee would be lost.
static bool writeChainBootNote() {
    for (int attempt = 0; attempt < 2; attempt++) {
        const char *op = (attempt == 0) ? "note.update" : "note.add";
        J *req = notecard.newRequest(op);
        JAddStringToObject(req, "file", "chain_boot.dbx");
        JAddStringToObject(req, "note", "v1");
        J *body = JAddObjectToObject(req, "body");
        JAddNumberToObject(body, "boot_seg", (double)gState.boot_seg);
        if (gState.baseline_orientation[0]) {
            JAddStringToObject(body, "baseline_orientation", gState.baseline_orientation);
        }
        if (ncSend(req)) return true;
    }
    return false;
}

uint16_t loadOrIncrementBootSeg() {
    uint16_t stored = 0;
    J *req = notecard.newRequest("note.get");
    JAddStringToObject(req, "file", "chain_boot.dbx");
    JAddStringToObject(req, "note", "v1");
    J *rsp = notecard.requestAndResponse(req);
    if (rsp) {
        if (!notecard.responseError(rsp)) {
            J *b = JGetObject(rsp, "body");
            if (b) {
                stored = (uint16_t)JGetNumber(b, "boot_seg");
                // Restore baseline_orientation so tilt detection continues
                // comparing against the activation-time baseline even after
                // an uncontrolled power loss.
                const char *orient = JGetString(b, "baseline_orientation");
                if (orient && orient[0]) {
                    strncpy(gState.baseline_orientation, orient,
                            sizeof(gState.baseline_orientation) - 1);
                    gState.baseline_orientation[sizeof(gState.baseline_orientation) - 1] = '\0';
                    Serial.print("[cargo] cold boot — orientation baseline restored: ");
                    Serial.println(gState.baseline_orientation);
                }
            }
        }
        notecard.deleteResponse(rsp);
    }

    uint16_t newSeg = stored + 1;
    gState.boot_seg = newSeg;

    // Persist the incremented counter back to Notecard local flash.
    // Also carry forward the baseline_orientation if it was already stored so
    // a subsequent cold boot can restore it again.
    if (!writeChainBootNote()) {
        Serial.println("[cargo] chain_boot.dbx write failed — boot_seg and orientation "
                       "baseline may not survive the next uncontrolled cold boot");
    }

    Serial.print("[cargo] cold boot — boot_seg="); Serial.println(newSeg);
    return newSeg;
}

// persistBaselineOrientation: write the current baseline_orientation to
// chain_boot.dbx so it survives an uncontrolled cold boot (power loss,
// brown-out).  Called once from setup() the first time the baseline is set.
// The baseline_orientation is also preserved on every subsequent cold boot by
// loadOrIncrementBootSeg(), which carries it forward in the write-back.
//
// No-op if baseline_orientation is empty (nothing to persist yet).
void persistBaselineOrientation() {
    if (!gState.baseline_orientation[0]) return;
    if (!writeChainBootNote()) {
        Serial.println("[cargo] chain_boot.dbx: failed to persist orientation baseline — "
                       "baseline may re-seed from current orientation on next "
                       "uncontrolled cold boot");
    }
}

// ===========================================================================
// Note templates
// ===========================================================================
void defineTemplates() {
    bool allOk = true;

    // ── cargo_data.qo — compact hourly summary ───────────────────────────────
    // "compact" strips location metadata and reduces the on-wire record from
    // ~200 bytes (free JSON) to ~50 bytes.  _time is included so each summary
    // carries its own audit timestamp.
    //
    // Field type codes from note-c (<Notecard.h>):
    //   TUINT32  = 24   — 4-byte unsigned integer (epoch, motion count)
    //   TFLOAT32 = 14.1 — 4-byte IEEE-754 float, 1 decimal place
    //   TUINT16  = 22   — 2-byte unsigned integer (small counters)
    J *req = notecard.newRequest("note.template");
    JAddStringToObject(req, "file",   NOTE_SUMMARY);
    JAddNumberToObject(req, "port",   50);
    JAddStringToObject(req, "format", "compact");
    J *body = JAddObjectToObject(req, "body");
    JAddNumberToObject(body, "_time",        TUINT32);
    JAddNumberToObject(body, "temp_mean_c",  TFLOAT32);
    JAddNumberToObject(body, "temp_min_c",   TFLOAT32);
    JAddNumberToObject(body, "temp_max_c",   TFLOAT32);
    JAddNumberToObject(body, "rh_mean_pct",  TFLOAT32);
    JAddNumberToObject(body, "rh_min_pct",   TFLOAT32);
    JAddNumberToObject(body, "rh_max_pct",   TFLOAT32);
    JAddNumberToObject(body, "lux_max",      TFLOAT32);
    JAddNumberToObject(body, "motion_total", TUINT32);
    JAddNumberToObject(body, "motion_valid", TUINT16);
    JAddNumberToObject(body, "samples",      TUINT16);
    if (!ncSend(req)) {
        Serial.println("[cargo] cargo_data.qo template failed — will retry on next wake");
        allOk = false;
    }

    // ── cargo_log.qo — compact per-sample tamper-evident log ─────────────────
    // One record per sample cycle.  Not synced immediately — entries batch with
    // the regular outbound window (port 51) so no extra satellite session is
    // consumed per sample.  The chain_crc field lets downstream systems verify
    // that no records have been inserted, deleted, or modified.
    //
    // _time is always written: the valid epoch when available, or 0 as a
    // documented pre-sync sentinel.  Writing 0 (rather than omitting the field)
    // keeps the compact-template body consistent with the registered schema so
    // every entry is encoded correctly regardless of time-sync state.
    // Downstream consumers should treat _time == 0 as pre-sync and use
    // Notehub's event receive-time as the best available approximation.
    //
    // motion_valid distinguishes "no motion occurred" (motion=0, motion_valid=1)
    // from "card.motion was unavailable" (motion=0, motion_valid=0) so the
    // audit log retains its compliance semantics even when the accelerometer
    // interface is temporarily unreachable.
    req = notecard.newRequest("note.template");
    JAddStringToObject(req, "file",   NOTE_LOG);
    JAddNumberToObject(req, "port",   51);
    JAddStringToObject(req, "format", "compact");
    body = JAddObjectToObject(req, "body");
    JAddNumberToObject(body, "_time",        TUINT32);   // sample epoch; 0 = pre-sync sentinel
    JAddNumberToObject(body, "seq",          TUINT32);   // monotonic sequence counter
    JAddNumberToObject(body, "temp_c",       TFLOAT32);  // instantaneous PT100 temperature
    JAddNumberToObject(body, "rh_pct",       TFLOAT32);  // instantaneous relative humidity
    JAddNumberToObject(body, "lux",          TFLOAT32);  // instantaneous interior lux
    JAddNumberToObject(body, "motion",       TUINT32);   // motion events this interval (0 when motion_valid=0)
    JAddNumberToObject(body, "motion_valid", TUINT16);   // 1 = card.motion available; 0 = data unavailable
    JAddNumberToObject(body, "state",        TUINT16);   // shipment state (SHIP_STATE_*)
    JAddNumberToObject(body, "boot_seg",     TUINT16);   // boot-segment counter
    JAddNumberToObject(body, "chain_crc",    TUINT32);   // rolling integrity hash
    if (!ncSend(req)) {
        Serial.println("[cargo] cargo_log.qo template failed — will retry on next wake");
        allOk = false;
    }

    if (allOk) {
        gState.templates_registered = true;
        Serial.println("[cargo] templates registered");
    }
}

// ===========================================================================
// Environment variable overrides — every wake
// ===========================================================================

float envFloat(J *body, const char *key, float fallback) {
    J *item = JGetObjectItem(body, key);
    if (!item) return fallback;
    double n = JGetNumber(body, key);
    if (n != 0.0) return (float)n;
    const char *s = JGetString(body, key);
    if (!s) return 0.0f;
    char *endptr = NULL;
    double v = strtod(s, &endptr);
    if (endptr && endptr != s) return (float)v;
    return fallback;
}

void fetchEnvOverrides() {
    J *rsp = ncQuery(notecard.newRequest("env.get"));
    if (!rsp) return;

    J *b = JGetObject(rsp, "body");
    if (b) {
        // Temperature limits (PT100 physical range well within these bounds)
        float tmin = constrain(envFloat(b, "temp_min_c",  gTempMinC), -40.0f, 85.0f);
        float tmax = constrain(envFloat(b, "temp_max_c",  gTempMaxC), -40.0f, 85.0f);
        if (tmin < tmax) {
            gTempMinC = tmin;
            gTempMaxC = tmax;
        } else {
            Serial.println("[cargo] temp_min_c >= temp_max_c — ignoring");
        }

        gHumidityMax = constrain(
            envFloat(b, "humidity_max_pct", gHumidityMax), 0.0f, 100.0f);

        gLightLux = constrain(
            envFloat(b, "light_open_lux", gLightLux), 0.0f, 120000.0f);

        float sc = envFloat(b, "shock_events", (float)gShockCount);
        if (sc >= 1.0f && sc <= 1000.0f) {
            gShockCount = (uint32_t)sc;
        } else {
            Serial.println("[cargo] shock_events out of range [1,1000] — ignoring");
        }

        // Sample interval: clamped to whole minutes for exact motion-bucket alignment
        float ss = envFloat(b, "sample_interval_sec", (float)gSampleSec);
        if (ss >= 60.0f && ss <= 3600.0f) {
            uint32_t aligned = ((uint32_t)ss / 60U) * 60U;
            if (aligned < 60U) aligned = 60U;
            if (aligned != (uint32_t)ss) {
                Serial.print("[cargo] sample_interval_sec rounded to ");
                Serial.print(aligned);
                Serial.println(" s (whole-minute boundary)");
            }
            gSampleSec = aligned;
        } else {
            Serial.println("[cargo] sample_interval_sec out of range [60,3600] — ignoring");
        }

        float sm = envFloat(b, "summary_interval_min", (float)gSummaryMin);
        if (sm >= 1.0f && sm <= 1440.0f) {
            gSummaryMin = (uint32_t)sm;
        } else {
            Serial.println("[cargo] summary_interval_min out of range [1,1440] — ignoring");
        }

        // ── Shipment-state model thresholds ──────────────────────────────────
        float tm = envFloat(b, "transit_motion_min", (float)gTransitMotion);
        if (tm >= 1.0f && tm <= 100.0f) {
            gTransitMotion = (uint32_t)tm;
        } else {
            Serial.println("[cargo] transit_motion_min out of range [1,100] — ignoring");
        }

        float dc = envFloat(b, "dwell_confirm_samples", (float)gDwellConfirm);
        if (dc >= 1.0f && dc <= 20.0f) {
            gDwellConfirm = (uint32_t)dc;
        } else {
            Serial.println("[cargo] dwell_confirm_samples out of range [1,20] — ignoring");
        }

        float tc = envFloat(b, "transit_confirm_samples", (float)gTransitConfirm);
        if (tc >= 1.0f && tc <= 20.0f) {
            gTransitConfirm = (uint32_t)tc;
        } else {
            Serial.println("[cargo] transit_confirm_samples out of range [1,20] — ignoring");
        }

        float dbf = envFloat(b, "dwell_batch_factor", (float)gDwellBatchFactor);
        if (dbf >= 1.0f && dbf <= 10.0f) {
            gDwellBatchFactor = (uint32_t)dbf;
        } else {
            Serial.println("[cargo] dwell_batch_factor out of range [1,10] — ignoring");
        }
    }
    notecard.deleteResponse(rsp);
}

// ===========================================================================
// Sensor reads
// ===========================================================================

// Returns true only if the primary sensors (MAX31865, SHT41) both returned
// valid readings.  VEML7700 faults set lux=INVALID_F but do NOT affect the
// return value — a missing light sensor must not suppress shock or tilt
// alert evaluation.
bool readSensors(float *temp_c, float *rh_pct, float *lux) {
    bool ok = true;

    // ── MAX31865: NIST-traceable PT100 temperature ────────────────────────────
    // begin() re-initializes the MAX31865 on every wake because the SPI device
    // is re-powered with the host.  MAX31865_4WIRE matches the specified
    // Omega PR-21C 4-wire PT100 probe; the Adafruit #3328 breakout jumper must
    // be set to the 4-wire position (desolder the 2/3-wire default bridge and
    // close the 4-wire pads — see the Adafruit product guide).
    rtd.begin(MAX31865_4WIRE);
    float t = rtd.temperature(MAX31865_RNOMINAL, MAX31865_RREF);
    uint8_t fault = rtd.readFault();
    rtd.clearFault();
    if (fault) {
        // Common faults: RTDINLOW = open-circuit probe; REFINLOW/REFINHIGH =
        // reference resistor issue; HIGHTHRESH/LOWTHRESH = out-of-range RTD.
        Serial.print("[cargo] MAX31865 fault 0x"); Serial.println(fault, HEX);
        *temp_c = INVALID_F;
        ok = false;
    } else if (isnan(t) || t < -200.0f || t > 200.0f) {
        Serial.println("[cargo] MAX31865 temperature out of valid range");
        *temp_c = INVALID_F;
        ok = false;
    } else {
        *temp_c = t;
        Serial.print("[cargo] T="); Serial.print(*temp_c, 2);
        Serial.println(" C (PT100)");
    }

    // ── SHT41: relative humidity (humidity channel only) ──────────────────────
    // The PT100/MAX31865 is the primary temperature source; the SHT41 is used
    // exclusively for relative humidity.  The integrated heater prevents
    // condensation-induced RH drift in cold, humid refrigerated environments.
    if (!sht4x.begin(&Wire)) {
        Serial.println("[cargo] SHT41 not found on I2C bus");
        *rh_pct = INVALID_F;
        ok = false;
    } else {
        sht4x.setPrecision(SHT4X_HIGH_PRECISION);
        sensors_event_t hum, temp_unused;
        if (!sht4x.getEvent(&hum, &temp_unused)) {
            Serial.println("[cargo] SHT41 measurement failed");
            *rh_pct = INVALID_F;
            ok = false;
        } else {
            *rh_pct = isnan(hum.relative_humidity) ? INVALID_F
                                                    : hum.relative_humidity;
            Serial.print("[cargo] RH="); Serial.print(*rh_pct, 1);
            Serial.println(" %");
        }
    }

    // ── VEML7700: interior cargo-bay light detection ──────────────────────────
    // The sensor is mounted facing inward into the cargo space so it reads
    // near-zero lux when the reefer or container is sealed.  When the cargo
    // door or container lid is opened, light floods the interior and the sensor
    // reports above the light_open_lux threshold — triggering a light_exposure
    // alert and a HANDLING state transition.
    // A missing VEML7700 returns INVALID_F (not 0.0) so a sensor fault is
    // distinguishable from genuine darkness.
    if (!veml7700.begin(&Wire)) {
        Serial.println("[cargo] VEML7700 not found on I2C bus");
        *lux = INVALID_F;
    } else {
        veml7700.setGain(VEML7700_GAIN_2);
        veml7700.setIntegrationTime(VEML7700_IT_100MS);
        float raw = veml7700.readLux();
        if (isnan(raw) || raw < 0.0f) {
            Serial.println("[cargo] VEML7700 readLux invalid");
            *lux = INVALID_F;
        } else {
            *lux = raw;
            Serial.print("[cargo] lux="); Serial.println(*lux, 1);
        }
    }

    return ok;
}

// ===========================================================================
// Motion count and orientation from Notecard's built-in accelerometer
// ===========================================================================

bool readMotionCount(uint32_t *count_out, char *orient_out, size_t orient_max) {
    J *req = notecard.newRequest("card.motion");
    uint32_t minutes = gSampleSec / 60U;
    if (minutes < 1U) minutes = 1U;
    JAddNumberToObject(req, "minutes", (double)minutes);

    J *rsp = ncQuery(req);
    if (!rsp) {
        Serial.println("[cargo] card.motion unavailable — motion data skipped this cycle");
        return false;
    }

    // Parse comma-separated bucket counts with strtoul to handle multi-digit
    // values (e.g., "10" must not be read as 1).
    uint32_t count = 0;
    const char *mv = JGetString(rsp, "movements");
    if (mv) {
        const char *p = mv;
        while (*p) {
            char *end;
            unsigned long val = strtoul(p, &end, 10);
            if (end == p) break;
            count += (uint32_t)val;
            if (*end == ',') { p = end + 1; }
            else break;
        }
    }

    if (orient_out && orient_max > 0) {
        const char *orient = JGetString(rsp, "orientation");
        if (orient && orient[0]) {
            strncpy(orient_out, orient, orient_max - 1);
            orient_out[orient_max - 1] = '\0';
        }
    }

    Serial.print("[cargo] motion="); Serial.println(count);
    notecard.deleteResponse(rsp);
    *count_out = count;
    return true;
}

// ===========================================================================
// Threshold evaluation — fires alerts when readings exceed configured limits
// ===========================================================================

void evaluateAlerts(float temp_c, float rh_pct, float lux,
                    uint32_t motion, bool motionOk,
                    const char *orientation, uint32_t now) {
    // Pre-sync sentinel promotion: when a pre-sync sentinel (value 1) is stored
    // and valid time is now available, promote it to the current epoch so the
    // 30-minute cooldown runs from time-sync rather than from epoch 0.
    if (now > 0) {
        if (gState.last_temp_low_alert  == 1) gState.last_temp_low_alert  = now;
        if (gState.last_temp_high_alert == 1) gState.last_temp_high_alert = now;
        if (gState.last_rh_alert        == 1) gState.last_rh_alert        = now;
        if (gState.last_shock_alert     == 1) gState.last_shock_alert     = now;
        if (gState.last_light_alert     == 1) gState.last_light_alert     = now;
        if (gState.last_tilt_alert      == 1) gState.last_tilt_alert      = now;
    }

    uint32_t motionVal = motionOk ? motion : MOTION_INVALID;

    // ── Temperature excursions ───────────────────────────────────────────────
    if (temp_c != INVALID_F) {
        if (temp_c < gTempMinC &&
                alertCooldownOk(gState.last_temp_low_alert, now)) {
            if (sendAlert("temp_low", temp_c, rh_pct, lux, motionVal)) {
                gState.last_temp_low_alert = (now > 0) ? now : 1;
            }
        }
        if (temp_c > gTempMaxC &&
                alertCooldownOk(gState.last_temp_high_alert, now)) {
            if (sendAlert("temp_high", temp_c, rh_pct, lux, motionVal)) {
                gState.last_temp_high_alert = (now > 0) ? now : 1;
            }
        }
    }

    // ── Humidity excursion ───────────────────────────────────────────────────
    if (rh_pct != INVALID_F && rh_pct > gHumidityMax) {
        if (alertCooldownOk(gState.last_rh_alert, now)) {
            if (sendAlert("humidity_high", temp_c, rh_pct, lux, motionVal)) {
                gState.last_rh_alert = (now > 0) ? now : 1;
            }
        }
    }

    // ── Shock: skip entirely when card.motion was unavailable ────────────────
    if (motionOk && motion >= gShockCount) {
        if (alertCooldownOk(gState.last_shock_alert, now)) {
            if (sendAlert("shock_detected", temp_c, rh_pct, lux, motionVal)) {
                gState.last_shock_alert = (now > 0) ? now : 1;
            }
        }
    }

    // ── Interior light / cargo bay opened ───────────────────────────────────
    // The VEML7700 faces inward into the cargo space.  Light at or above
    // light_open_lux means the cargo door or container lid has been opened.
    // INVALID_F means sensor is absent — skip.
    if (lux != INVALID_F && lux >= gLightLux) {
        if (alertCooldownOk(gState.last_light_alert, now)) {
            if (sendAlert("light_exposure", temp_c, rh_pct, lux, motionVal)) {
                gState.last_light_alert = (now > 0) ? now : 1;
            }
        }
    }

    // ── Tilt detection ───────────────────────────────────────────────────────
    if (orientation && orientation[0] && gState.baseline_orientation[0]) {
        bool tilted = (strncmp(orientation, gState.baseline_orientation,
                               sizeof(gState.baseline_orientation)) != 0);
        if (tilted && alertCooldownOk(gState.last_tilt_alert, now)) {
            if (sendTiltAlert(gState.baseline_orientation, orientation,
                              temp_c, rh_pct, lux, motionVal)) {
                gState.last_tilt_alert = (now > 0) ? now : 1;
            }
        }
    }
}

// ===========================================================================
// Alert notes — immediate sync via sync:true
// ===========================================================================

bool sendAlert(const char *type, float temp_c, float rh_pct,
               float lux, uint32_t motion) {
    Serial.print("[cargo] ALERT -> "); Serial.println(type);

    J *req = notecard.newRequest("note.add");
    JAddStringToObject(req, "file", NOTE_ALERT);
    JAddBoolToObject(req,   "sync", true);
    J *body = JAddObjectToObject(req, "body");
    JAddStringToObject(body, "alert", type);
    if (temp_c != INVALID_F)       JAddNumberToObject(body, "temp_c",  (double)temp_c);
    if (rh_pct != INVALID_F)       JAddNumberToObject(body, "rh_pct",  (double)rh_pct);
    if (lux    != INVALID_F)       JAddNumberToObject(body, "lux",     (double)lux);
    if (motion != MOTION_INVALID)  JAddNumberToObject(body, "motion",  (double)motion);
    return ncSend(req);
}

bool sendTiltAlert(const char *prev_orient, const char *cur_orient,
                   float temp_c, float rh_pct, float lux, uint32_t motion) {
    Serial.print("[cargo] ALERT -> tilt_detected (");
    Serial.print(prev_orient); Serial.print(" -> ");
    Serial.print(cur_orient);  Serial.println(")");

    J *req = notecard.newRequest("note.add");
    JAddStringToObject(req, "file", NOTE_ALERT);
    JAddBoolToObject(req,   "sync", true);
    J *body = JAddObjectToObject(req, "body");
    JAddStringToObject(body, "alert",            "tilt_detected");
    JAddStringToObject(body, "orientation_from", prev_orient);
    JAddStringToObject(body, "orientation_to",   cur_orient);
    if (temp_c != INVALID_F)       JAddNumberToObject(body, "temp_c",  (double)temp_c);
    if (rh_pct != INVALID_F)       JAddNumberToObject(body, "rh_pct",  (double)rh_pct);
    if (lux    != INVALID_F)       JAddNumberToObject(body, "lux",     (double)lux);
    if (motion != MOTION_INVALID)  JAddNumberToObject(body, "motion",  (double)motion);
    return ncSend(req);
}

// ===========================================================================
// Sample accumulation
// ===========================================================================

void accumulateSample(float temp_c, float rh_pct, float lux,
                      uint32_t motion, bool motionOk) {
    if (temp_c != INVALID_F) {
        gState.temp_sum += temp_c;
        if (temp_c < gState.temp_min) gState.temp_min = temp_c;
        if (temp_c > gState.temp_max) gState.temp_max = temp_c;
        gState.temp_n++;
    }
    if (rh_pct != INVALID_F) {
        gState.rh_sum += rh_pct;
        if (rh_pct < gState.rh_min) gState.rh_min = rh_pct;
        if (rh_pct > gState.rh_max) gState.rh_max = rh_pct;
        gState.rh_n++;
    }
    if (lux != INVALID_F) {
        if (lux > gState.lux_max) gState.lux_max = lux;
        gState.lux_n++;
    }
    if (motionOk) {
        gState.motion_total += motion;
        gState.motion_n++;
    }
    gState.summary_n++;
}

// ===========================================================================
// Summary snapshot and send
// ===========================================================================

void snapshotSummary(uint32_t epoch) {
    gState.pending_epoch     = epoch;
    gState.pending_temp_mean = (gState.temp_n > 0)
        ? gState.temp_sum / (float)gState.temp_n : INVALID_F;
    gState.pending_temp_min  = (gState.temp_n > 0) ? gState.temp_min : INVALID_F;
    gState.pending_temp_max  = (gState.temp_n > 0) ? gState.temp_max : INVALID_F;
    gState.pending_rh_mean   = (gState.rh_n   > 0)
        ? gState.rh_sum   / (float)gState.rh_n   : INVALID_F;
    gState.pending_rh_min    = (gState.rh_n   > 0) ? gState.rh_min  : INVALID_F;
    gState.pending_rh_max    = (gState.rh_n   > 0) ? gState.rh_max  : INVALID_F;
    gState.pending_lux_max   = (gState.lux_n  > 0) ? gState.lux_max : INVALID_F;
    gState.pending_motion    = (gState.motion_n > 0)
        ? gState.motion_total : MOTION_INVALID;
    gState.pending_samples   = gState.summary_n;
}

bool sendPendingSummary() {
    J *req = notecard.newRequest("note.add");
    JAddStringToObject(req, "file", NOTE_SUMMARY);
    J *body = JAddObjectToObject(req, "body");
    JAddNumberToObject(body, "_time",        (double)gState.pending_epoch);
    JAddNumberToObject(body, "temp_mean_c",  (double)gState.pending_temp_mean);
    JAddNumberToObject(body, "temp_min_c",   (double)gState.pending_temp_min);
    JAddNumberToObject(body, "temp_max_c",   (double)gState.pending_temp_max);
    JAddNumberToObject(body, "rh_mean_pct",  (double)gState.pending_rh_mean);
    JAddNumberToObject(body, "rh_min_pct",   (double)gState.pending_rh_min);
    JAddNumberToObject(body, "rh_max_pct",   (double)gState.pending_rh_max);
    JAddNumberToObject(body, "lux_max",      (double)gState.pending_lux_max);
    {
        uint32_t motionTotal = (gState.pending_motion != MOTION_INVALID)
            ? gState.pending_motion : 0;
        uint16_t motionValid = (gState.pending_motion != MOTION_INVALID) ? 1 : 0;
        JAddNumberToObject(body, "motion_total", (double)motionTotal);
        JAddNumberToObject(body, "motion_valid", (double)motionValid);
    }
    JAddNumberToObject(body, "samples",      (double)gState.pending_samples);

    bool ok = ncSend(req);
    if (ok) {
        Serial.print("[cargo] summary sent — samples=");
        Serial.println(gState.pending_samples);
    } else {
        Serial.println("[cargo] summary note.add failed — will retry next wake");
    }
    return ok;
}

void resetAccumulators() {
    gState.temp_sum = gState.rh_sum = gState.lux_max = 0.0f;
    gState.temp_min = gState.rh_min = 999.0f;
    gState.temp_max = gState.rh_max = -999.0f;
    gState.temp_n = gState.rh_n = gState.lux_n = 0;
    gState.motion_total = gState.motion_n = 0;
    gState.summary_n = 0;
}

// ===========================================================================
// Current Unix epoch from Notecard
// ===========================================================================

uint32_t currentEpoch() {
    J *rsp = notecard.requestAndResponse(notecard.newRequest("card.time"));
    if (!rsp) return 0;
    uint32_t t = (uint32_t)JGetNumber(rsp, "time");
    notecard.deleteResponse(rsp);
    return t;
}

// ===========================================================================
// Tamper-evident per-sample log
// ===========================================================================

// chainUpdate: djb2-rotate-based hash chain.  Each call mixes the previous
// hash with the current sample's raw field values to produce a new hash.
// Float fields are mixed as their IEEE-754 bit patterns so INVALID_F is
// hashed consistently regardless of sensor availability.  Downstream
// consumers replay this chain from seq=1 (seed=0) to detect any insertion,
// deletion, or modification in the remote log.
static uint32_t chainUpdate(uint32_t prev, uint32_t seq, uint16_t boot_seg,
                             float temp_c, float rh_pct, float lux,
                             uint32_t motion, uint8_t state) {
    #define ROTMIX(h, v) ((h) = (((h) << 5) | ((h) >> 27)) ^ (v))
    uint32_t h = prev ^ 0x5A827999UL;
    ROTMIX(h, seq);
    ROTMIX(h, (uint32_t)boot_seg);
    uint32_t bits;
    memcpy(&bits, &temp_c, sizeof(bits)); ROTMIX(h, bits);
    memcpy(&bits, &rh_pct, sizeof(bits)); ROTMIX(h, bits);
    memcpy(&bits, &lux,    sizeof(bits)); ROTMIX(h, bits);
    ROTMIX(h, motion);
    ROTMIX(h, (uint32_t)state);
    #undef ROTMIX
    return h;
}

// sendLogEntry: append one per-sample record to cargo_log.qo.
// seq and chain_crc are advanced before the note.add so the chain represents
// the physical event sequence.  A failed note.add burns the seq number —
// the resulting gap in Notehub is itself evidence of a dropped transmission.
//
// _time is ALWAYS written.  When a valid epoch is available (now > 0) the
// real timestamp is used.  When the Notecard has not yet obtained time from
// Notehub (now == 0) the value 0 is written as a documented pre-sync
// sentinel.  Writing 0 explicitly (rather than omitting the field) keeps the
// compact-template body consistent with the registered schema for every entry.
// Downstream consumers should treat _time == 0 as pre-sync and use Notehub's
// event receive-time as the best available approximation for those records.
//
// motionOk controls the motion_valid flag.  When motionOk is false the motion
// field is written as 0, but motion_valid is set to 0 so downstream consumers
// can distinguish "card.motion unavailable" from "no motion occurred" (which
// would be motion=0, motion_valid=1).  The chain hash uses the raw motion value
// (0 when unavailable) in both cases so replay is straightforward from the
// stored fields.
bool sendLogEntry(uint32_t now, float temp_c, float rh_pct,
                  float lux, uint32_t motion, bool motionOk, uint8_t state) {
    gState.seq++;
    gState.chain_crc = chainUpdate(gState.chain_crc, gState.seq, gState.boot_seg,
                                   temp_c, rh_pct, lux, motion, state);

    J *req = notecard.newRequest("note.add");
    JAddStringToObject(req, "file", NOTE_LOG);
    // No sync:true — log entries batch with the regular outbound window so
    // no extra satellite session is consumed per sample cycle.
    J *body = JAddObjectToObject(req, "body");
    // Always write _time: real epoch or 0 as pre-sync sentinel.
    JAddNumberToObject(body, "_time",        (double)now);
    JAddNumberToObject(body, "seq",          (double)gState.seq);
    // INVALID_F is written as-is; downstream interprets -9999 as sensor fault
    // (consistent with summary convention) and uses the same value for the
    // chain hash so the chain is reproducible from the logged values.
    JAddNumberToObject(body, "temp_c",       (double)temp_c);
    JAddNumberToObject(body, "rh_pct",       (double)rh_pct);
    JAddNumberToObject(body, "lux",          (double)lux);
    JAddNumberToObject(body, "motion",       (double)(motionOk ? motion : 0U));
    JAddNumberToObject(body, "motion_valid", (double)(motionOk ? 1U : 0U));
    JAddNumberToObject(body, "state",        (double)state);
    JAddNumberToObject(body, "boot_seg",     (double)gState.boot_seg);
    JAddNumberToObject(body, "chain_crc",    (double)gState.chain_crc);

    bool ok = ncSend(req);
    if (!ok) {
        Serial.print("[cargo] log entry seq="); Serial.print(gState.seq);
        Serial.println(" note.add failed — seq gap will appear in remote log");
    }
    return ok;
}

// ===========================================================================
// Shipment-state detection
// ===========================================================================

// detectShipmentState: update gState.shipment_state based on current readings.
// Returns true when the state transitions to a new value; the caller should
// call sendStateChange() on a true return.
//
// HANDLING: interior lux at or above light_open_lux — cargo door / container
//   opened.  Takes priority over motion-based transitions.
// IN_TRANSIT: gTransitConfirm consecutive samples with motion >= gTransitMotion.
// DWELL: gDwellConfirm consecutive samples with motion < gTransitMotion.
//   UNKNOWN → DWELL transition also emits a state-change note so the first
//   confirmed-stationary event is recorded in chain-of-custody.
bool detectShipmentState(uint32_t motion, bool motionOk,
                          float lux, uint32_t now) {
    if (!motionOk && lux == INVALID_F) return false;

    uint8_t prev_state = gState.shipment_state;

    // ── HANDLING: cargo bay opened ────────────────────────────────────────────
    if (lux != INVALID_F && lux >= gLightLux) {
        gState.dwell_count   = 0;
        gState.transit_count = 0;
        if (prev_state != SHIP_STATE_HANDLING) {
            gState.shipment_state    = SHIP_STATE_HANDLING;
            gState.state_since_epoch = now;
            return true;
        }
        return false;
    }

    if (!motionOk) return false;

    // ── Motion counters ───────────────────────────────────────────────────────
    if (motion >= gTransitMotion) {
        if (gState.transit_count < UINT16_MAX) gState.transit_count++;
        gState.dwell_count = 0;
    } else {
        if (gState.dwell_count < UINT16_MAX) gState.dwell_count++;
        gState.transit_count = 0;
    }

    // ── IN_TRANSIT ────────────────────────────────────────────────────────────
    if (gState.transit_count >= (uint16_t)gTransitConfirm &&
            prev_state != SHIP_STATE_TRANSIT) {
        gState.shipment_state    = SHIP_STATE_TRANSIT;
        gState.state_since_epoch = now;
        return true;
    }

    // ── DWELL (including UNKNOWN → DWELL seeding) ─────────────────────────────
    if (gState.dwell_count >= (uint16_t)gDwellConfirm &&
            prev_state != SHIP_STATE_DWELL) {
        gState.shipment_state    = SHIP_STATE_DWELL;
        gState.state_since_epoch = now;
        return true;
    }

    return false;
}

// sendStateChange: emit cargo_state.qo with sync:true on any state transition.
bool sendStateChange(uint8_t prev_state, uint8_t new_state, uint32_t now) {
    static const char * const stateNames[] = {
        "unknown", "dwell", "in_transit", "handling"
    };
    const char *from = (prev_state <= 3) ? stateNames[prev_state] : "unknown";
    const char *to   = (new_state  <= 3) ? stateNames[new_state]  : "unknown";

    Serial.print("[cargo] STATE "); Serial.print(from);
    Serial.print(" -> "); Serial.println(to);

    J *req = notecard.newRequest("note.add");
    JAddStringToObject(req, "file", NOTE_STATE);
    JAddBoolToObject(req,   "sync", true);
    J *body = JAddObjectToObject(req, "body");
    JAddStringToObject(body, "state_from", from);
    JAddStringToObject(body, "state_to",   to);
    if (now > 0) JAddNumberToObject(body, "_time", (double)now);
    return ncSend(req);
}
