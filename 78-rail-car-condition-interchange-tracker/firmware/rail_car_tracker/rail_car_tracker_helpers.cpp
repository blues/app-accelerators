/***************************************************************************
  rail_car_tracker_helpers.cpp — Helper implementations for Rail Car
  Condition & Interchange Tracker

  Encapsulates all Notecard interactions, sensor reads, and alert/summary
  note emission. Called from setup() in rail_car_tracker.ino.

  THIS FILE SHOULD BE EDITED AFTER GENERATION.
  IT IS PROVIDED AS A STARTING POINT FOR THE USER TO EDIT AND EXTEND.
***************************************************************************/

#include "rail_car_tracker_helpers.h"

// ─────────────────────────────────────────────────────────────────────────────
// notecardReady — per-boot I²C readiness ping with retry
//
// The host MCU can power up before the Notecard's I²C stack is ready. A
// lightweight card.version with sendRequestWithRetry idles for up to 10 s
// to let the bus settle before any real request. Returns true when the
// Notecard acknowledges; false if every retry times out. Callers must abort
// the cycle on false — no further Notecard interactions should be attempted.
// ─────────────────────────────────────────────────────────────────────────────
bool notecardReady() {
    J *req = notecard.newRequest("card.version");
    bool ok = notecard.sendRequestWithRetry(req, 10);
    if (!ok) {
        debugSerial.println("[error] Notecard did not respond to card.version within 10 s");
    }
    return ok;
}

// ─────────────────────────────────────────────────────────────────────────────
// configureNotecard — hub.set + voltage-variable sync policy
//
// Called at every boot (idempotent) so that a PRODUCT_UID change in the
// firmware sketch takes effect on the very next wake without requiring
// CONFIG_VERSION to be bumped. Returns false if the request fails; the caller
// logs the error but continues — the Notecard retains its previous hub.set
// configuration so queued notes are not lost. Voltage-variable
// outbound/inbound schedules conserve battery and satellite airtime when
// solar input drops.
// ─────────────────────────────────────────────────────────────────────────────
bool configureNotecard() {
    J *req = notecard.newRequest("hub.set");
    JAddStringToObject(req, "product", PRODUCT_UID);
    JAddStringToObject(req, "mode",    "periodic");
    // Sync every 4 h at normal solar voltage; stretch to 8 h when low;
    // pause entirely at dead-battery to preserve modem startup energy.
    JAddStringToObject(req, "voutbound",
                       "usb:60;high:120;normal:240;low:480;dead:0");
    JAddStringToObject(req, "vinbound",
                       "usb:120;high:240;normal:480;low:720;dead:0");
    bool ok = notecard.sendRequestWithRetry(req, 10);
    if (!ok) debugSerial.println("[warn] hub.set failed");
    return ok;
}

// ─────────────────────────────────────────────────────────────────────────────
// defineTemplates — register compact note.template for all three Notefiles
//
// "compact" format is required for satellite (NTN/Skylo) transmission; it
// strips metadata and dramatically reduces per-note wire size.
//
// All three templates declare _lat/_lon/_ltime as reserved compact fields.
// The Notecard injects the last known GPS fix into these fields when the
// note is queued, if a fix is available. Before the first fix is acquired,
// these fields may be absent or zero — the host never needs to query
// card.location or supply coordinates directly.
//
//   Port 10 — railcar_status.qo  : periodic condition summary
//   Port 11 — railcar_alert.qo   : edge-triggered alert notes
//   Port 12 — railcar_location.qo: dedicated position stream
//
// Returns false if any template registration reports an error.
// ─────────────────────────────────────────────────────────────────────────────
bool defineTemplates() {
    bool ok = true;

    // ── Status summary template (port 10) ─────────────────────────────────────
    // pressure_psi and tank_temp_c are compiled in only for TANK_CAR builds.
    {
        J *req = notecard.newRequest("note.template");
        JAddStringToObject(req, "file",   FILE_STATUS);
        JAddNumberToObject(req, "port",   10);
        JAddStringToObject(req, "format", "compact");
        J *body = JAddObjectToObject(req, "body");
        JAddNumberToObject(body, "_lat",          TFLOAT32);
        JAddNumberToObject(body, "_lon",          TFLOAT32);
        JAddNumberToObject(body, "_ltime",        TINT32);
#ifdef TANK_CAR
        JAddNumberToObject(body, "pressure_psi",  TFLOAT32);  // MPRLS absolute pressure (PSI)
        JAddNumberToObject(body, "tank_temp_c",   TFLOAT32);  // DS18B20 cargo temperature (°C)
#endif
        JAddBoolToObject  (body, "coupled",       true);
        JAddBoolToObject  (body, "moving",        true);
        JAddNumberToObject(body, "shock_peak_g",  TFLOAT32);
        JAddNumberToObject(body, "shock_windows", TINT16);
        J *rsp = notecard.requestAndResponse(req);
        if (rsp != NULL) {
            const char *err = JGetString(rsp, "err");
            if (err && *err) {
                debugSerial.print("[warn] note.template status: ");
                debugSerial.println(err);
                ok = false;
            }
            notecard.deleteResponse(rsp);
        } else {
            debugSerial.println("[warn] note.template status: no response");
            ok = false;
        }
    }

    // ── Alert template (port 11) ──────────────────────────────────────────────
    // _lat/_lon/_ltime are injected by the Notecard from the last known GPS
    // fix when available. This eliminates host-side card.location queries;
    // before the first fix is acquired the fields may be absent or zero.
    {
        J *req = notecard.newRequest("note.template");
        JAddStringToObject(req, "file",   FILE_ALERT);
        JAddNumberToObject(req, "port",   11);
        JAddStringToObject(req, "format", "compact");
        J *body = JAddObjectToObject(req, "body");
        // String fields in compact templates use a numeric string as the
        // character-count reservation — the value is the maximum byte length,
        // not a literal string (see dev.blues.io low-bandwidth-design guide,
        // "Working with Note Templates"). "14" reserves 14 characters, which
        // comfortably fits the longest alert names in this firmware:
        //   "tank_temp_high" — 14 chars
        //   "pressure_drop"  — 13 chars
        //   "pressure_high"  — 13 chars
        // This matches the pattern used by the Blues reference accelerators
        // (e.g., accelerator 54 uses "14"; accelerator 53 uses "20").
        JAddStringToObject(body, "alert",  "14");
        JAddNumberToObject(body, "value",  TFLOAT32);
        JAddNumberToObject(body, "_lat",   TFLOAT32);
        JAddNumberToObject(body, "_lon",   TFLOAT32);
        JAddNumberToObject(body, "_ltime", TINT32);
        J *rsp = notecard.requestAndResponse(req);
        if (rsp != NULL) {
            const char *err = JGetString(rsp, "err");
            if (err && *err) {
                debugSerial.print("[warn] note.template alert: ");
                debugSerial.println(err);
                ok = false;
            }
            notecard.deleteResponse(rsp);
        } else {
            debugSerial.println("[warn] note.template alert: no response");
            ok = false;
        }
    }

    // ── Location note template (port 12) ──────────────────────────────────────
    // Compact, low-byte-count note for the dedicated position stream.
    // Fired on motion-state transitions (stopped ↔ moving) and while moving
    // at the configured location_interval_min cadence. The small body keeps
    // satellite airtime cost negligible even at a 30-minute transmit rate.
    // _lat/_lon/_ltime are injected from the last known Notecard GPS fix when
    // available; before the first fix is acquired these fields may be absent
    // or zero. moving and coupled give downstream interchange-detection
    // pipelines the operational context needed alongside each position fix.
    {
        J *req = notecard.newRequest("note.template");
        JAddStringToObject(req, "file",   FILE_LOCATION);
        JAddNumberToObject(req, "port",   12);
        JAddStringToObject(req, "format", "compact");
        J *body = JAddObjectToObject(req, "body");
        JAddNumberToObject(body, "_lat",    TFLOAT32);
        JAddNumberToObject(body, "_lon",    TFLOAT32);
        JAddNumberToObject(body, "_ltime",  TINT32);
        JAddBoolToObject  (body, "moving",  true);
        JAddBoolToObject  (body, "coupled", true);
        J *rsp = notecard.requestAndResponse(req);
        if (rsp != NULL) {
            const char *err = JGetString(rsp, "err");
            if (err && *err) {
                debugSerial.print("[warn] note.template location: ");
                debugSerial.println(err);
                ok = false;
            }
            notecard.deleteResponse(rsp);
        } else {
            debugSerial.println("[warn] note.template location: no response");
            ok = false;
        }
    }

    return ok;
}

// ─────────────────────────────────────────────────────────────────────────────
// configureMotionAndGPS — enable Notecard internal accelerometer + GPS
//
// card.motion.mode with motion:3 flags the car as moving when ≥3 motion
// events occur in a 60 s bucket.
//
// card.location.mode is set to periodic WITHOUT a motion threshold so that a
// newly installed tracker on a stationary rail car can acquire an initial GPS
// fix in a yard or at an interchange. Once setup() confirms a valid fix via
// card.location (state.locationAcquired), it calls applyGPSMotionGate() to
// add threshold:1, keeping the GNSS radio off during yard dwell and protecting
// the solar power budget. The motion gate is restored after every firmware
// update that bumps CONFIG_VERSION because locationAcquired is reset to false
// whenever configuration is reapplied.
//
// Returns false if either request reports an error.
// ─────────────────────────────────────────────────────────────────────────────
bool configureMotionAndGPS() {
    bool ok = true;

    // Motion tracking: 25 Hz / ±16G Notecard internal accelerometer,
    // 60-second buckets; ≥3 motion events per bucket → "moving"
    {
        J *req = notecard.newRequest("card.motion.mode");
        JAddBoolToObject  (req, "start",       true);
        JAddNumberToObject(req, "sensitivity", 1);   // 25 Hz, ±16G
        JAddNumberToObject(req, "seconds",     60);
        JAddNumberToObject(req, "motion",      3);   // ≥3 events → "moving"
        J *rsp = notecard.requestAndResponse(req);
        if (rsp != NULL) {
            const char *err = JGetString(rsp, "err");
            if (err && *err) {
                debugSerial.print("[warn] card.motion.mode: ");
                debugSerial.println(err);
                ok = false;
            }
            notecard.deleteResponse(rsp);
        } else {
            debugSerial.println("[warn] card.motion.mode: no response");
            ok = false;
        }
    }

    // Periodic GPS: attempt a fix every 5 minutes. threshold is intentionally
    // omitted so the GNSS radio runs unconditionally until setup() confirms the
    // first fix. applyGPSMotionGate() then adds threshold:1 to gate the radio
    // on motion, saving solar energy during long stationary yard dwell periods.
    {
        J *req = notecard.newRequest("card.location.mode");
        JAddStringToObject(req, "mode",    "periodic");
        JAddNumberToObject(req, "seconds", 300); // 5-minute GPS fix interval
        // threshold intentionally omitted: GPS runs regardless of motion until
        // the initial fix is confirmed and applyGPSMotionGate() is called.
        J *rsp = notecard.requestAndResponse(req);
        if (rsp != NULL) {
            const char *err = JGetString(rsp, "err");
            if (err && *err) {
                debugSerial.print("[warn] card.location.mode: ");
                debugSerial.println(err);
                ok = false;
            }
            notecard.deleteResponse(rsp);
        } else {
            debugSerial.println("[warn] card.location.mode: no response");
            ok = false;
        }
    }

    return ok;
}

// ─────────────────────────────────────────────────────────────────────────────
// applyGPSMotionGate — add motion gating to the periodic GPS configuration
//
// Re-issues card.location.mode with threshold:1 so the GNSS radio only wakes
// when the Notecard internal accelerometer detects at least one motion event.
// Called from setup() once a valid GPS fix has been confirmed in card.location,
// replacing the no-threshold "initial acquisition" mode set by
// configureMotionAndGPS(). The Notecard retains this configuration across
// host power cycles until it is overwritten.
//
// Returns false if the request reports an error; the caller leaves
// state.locationAcquired false so the gate is retried next wake.
// ─────────────────────────────────────────────────────────────────────────────
bool applyGPSMotionGate() {
    J *req = notecard.newRequest("card.location.mode");
    JAddStringToObject(req, "mode",      "periodic");
    JAddNumberToObject(req, "seconds",   300);
    JAddNumberToObject(req, "threshold", 1); // require ≥1 motion event before waking GPS
    J *rsp = notecard.requestAndResponse(req);
    if (rsp == NULL) {
        debugSerial.println("[warn] card.location.mode (motion gate): no response");
        return false;
    }
    const char *err = JGetString(rsp, "err");
    if (err && *err) {
        debugSerial.print("[warn] card.location.mode (motion gate): ");
        debugSerial.println(err);
        notecard.deleteResponse(rsp);
        return false;
    }
    notecard.deleteResponse(rsp);
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// fetchEnvOverrides — pull Notehub environment variables and clamp values
//
// Called every wake so fleet operators can retune thresholds without
// reflashing. Explicitly checks the err field so env.get failures are
// distinguishable from "no overrides set" during commissioning.
// On any failure the caller's default values remain unchanged.
//
// Recognised environment variables:
//   sample_interval_min   — host wake interval (5–60 min)
//   report_interval_min   — status summary cadence (sampleMin–1440 min)
//   location_interval_min — max gap between location notes while moving
//                           (sampleMin–240 min)
//   shock_threshold_g     — impact alert threshold (0.5–20 G)
//   shock_cooldown_min    — minimum gap between shock alerts (1–60 min)
//   pressure_max_psi      — tank overpressure alert (1–25 PSI; TANK_CAR only)
//   pressure_drop_psi     — tank leak-drop alert (0.5–25 PSI; TANK_CAR only)
//   tank_temp_min_c       — cargo low-temperature alert (−60–25 °C; TANK_CAR only)
//   tank_temp_max_c       — cargo high-temperature alert (20–100 °C; TANK_CAR only)
// ─────────────────────────────────────────────────────────────────────────────
void fetchEnvOverrides(uint32_t &sampleMin, uint32_t &reportMin,
                       float &shockThreshG, uint32_t &shockCoolMin,
                       uint32_t &locationIntervalMin,
                       float &pressMaxPsi, float &pressDropPsi,
                       float &tankTempMinC, float &tankTempMaxC) {
    J *rsp = notecard.requestAndResponse(notecard.newRequest("env.get"));
    if (rsp == NULL) {
        debugSerial.println("[warn] env.get: no response — using defaults");
        return;
    }
    const char *err = JGetString(rsp, "err");
    if (err && *err) {
        debugSerial.print("[warn] env.get: ");
        debugSerial.println(err);
        notecard.deleteResponse(rsp);
        return;
    }
    J *body = JGetObject(rsp, "body");
    if (body == NULL) {
        // No environment variables have been set yet — defaults remain in effect.
        notecard.deleteResponse(rsp);
        return;
    }

    const char *v;
    if ((v = JGetString(body, "sample_interval_min")) && *v)
        sampleMin           = (uint32_t)constrain(atol(v), 5, 60);
    if ((v = JGetString(body, "report_interval_min")) && *v)
        reportMin           = (uint32_t)constrain(atol(v), sampleMin, 1440);
    if ((v = JGetString(body, "location_interval_min")) && *v)
        locationIntervalMin = (uint32_t)constrain(atol(v), sampleMin, 240);
    if ((v = JGetString(body, "shock_threshold_g")) && *v)
        shockThreshG        = constrain(atof(v), 0.5f, 20.0f);
    if ((v = JGetString(body, "shock_cooldown_min")) && *v)
        shockCoolMin        = (uint32_t)constrain(atol(v), 1, 60);
#ifdef TANK_CAR
    if ((v = JGetString(body, "pressure_max_psi")) && *v)
        pressMaxPsi         = constrain(atof(v), 1.0f, 25.0f);   // MPRLS 0–25 PSI range
    if ((v = JGetString(body, "pressure_drop_psi")) && *v)
        pressDropPsi        = constrain(atof(v), 0.5f, 25.0f);   // MPRLS 0–25 PSI range
    if ((v = JGetString(body, "tank_temp_min_c")) && *v)
        tankTempMinC        = constrain(atof(v), -60.0f, 25.0f); // DS18B20 range; threshold may be positive
    if ((v = JGetString(body, "tank_temp_max_c")) && *v)
        tankTempMaxC        = constrain(atof(v), 20.0f, 100.0f); // DS18B20 upper limit 125 °C; practical max 100 °C
#else
    (void)pressMaxPsi;
    (void)pressDropPsi;
    (void)tankTempMinC;
    (void)tankTempMaxC;
#endif

    notecard.deleteResponse(rsp);
}

// ─────────────────────────────────────────────────────────────────────────────
// readCouplerState — 5-sample majority-vote debounce
//
// Returns true if the coupler is engaged (reed switch closed = LOW).
// The 5 × 20 ms sampling window (100 ms total) is short relative to the
// 15-minute sample interval but sufficient to filter brief vibration-induced
// contact chatter.
// ─────────────────────────────────────────────────────────────────────────────
bool readCouplerState() {
    pinMode(PIN_COUPLER, INPUT_PULLUP);
    uint8_t lowCount = 0;
    for (uint8_t i = 0; i < 5; i++) {
        if (digitalRead(PIN_COUPLER) == LOW) lowCount++;
        delay(20);
    }
    return (lowCount >= 3); // majority vote: ≥3 LOW reads → coupled
}

// ─────────────────────────────────────────────────────────────────────────────
// adxl345Begin — initialize ADXL345 via direct I²C writes
//
// Returns false if either register write fails; the caller must skip all
// subsequent shock reads for this wake rather than feeding garbage into the
// impact-scoring math.
// ─────────────────────────────────────────────────────────────────────────────
bool adxl345Begin() {
    // Wake device and enable measurement mode (Power Control bit 3)
    Wire.beginTransmission(ADXL345_ADDR);
    Wire.write(ADXL_REG_POWER);
    Wire.write(0x08);  // bit3: Measure
    if (Wire.endTransmission() != 0) return false;

    // Full-resolution ±16G mode (Data Format: FULL_RES | range 11b)
    Wire.beginTransmission(ADXL345_ADDR);
    Wire.write(ADXL_REG_FORMAT);
    Wire.write(0x0B);  // FULL_RES | ±16G
    if (Wire.endTransmission() != 0) return false;

    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// adxl345ReadG — read X/Y/Z axes as floats in G
//
// Returns false on any I²C error or short read so callers can discard the
// sample rather than feeding garbage bytes into the axis math.
// ─────────────────────────────────────────────────────────────────────────────
bool adxl345ReadG(float &gx, float &gy, float &gz) {
    Wire.beginTransmission(ADXL345_ADDR);
    Wire.write(ADXL_REG_DATAX0);
    if (Wire.endTransmission(false) != 0) return false;
    uint8_t n = Wire.requestFrom((uint8_t)ADXL345_ADDR, (uint8_t)6);
    if (n < 6) return false;
    int16_t rx = (int16_t)(Wire.read() | (Wire.read() << 8));
    int16_t ry = (int16_t)(Wire.read() | (Wire.read() << 8));
    int16_t rz = (int16_t)(Wire.read() | (Wire.read() << 8));
    gx = rx * ADXL_SCALE_16G;
    gy = ry * ADXL_SCALE_16G;
    gz = rz * ADXL_SCALE_16G;
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// readPeakShockG — burst-sample the ADXL345 and return peak resultant G
//
// SHOCK_SAMPLES reads at ~100 Hz; tracks the highest instantaneous vector
// magnitude so single-sample spikes are not missed. Resultant magnitude
// includes gravity (~1.0 G on z-axis at rest); impacts appear as spikes
// well above that baseline.
//
// Returns NAN if every read in the burst fails (I²C fault), so alert logic
// can skip the sample rather than acting on a manufactured magnitude.
// ─────────────────────────────────────────────────────────────────────────────
float readPeakShockG() {
    float   peakG        = 0.0f;
    uint8_t validSamples = 0;
    for (uint8_t i = 0; i < SHOCK_SAMPLES; i++) {
        float gx, gy, gz;
        if (!adxl345ReadG(gx, gy, gz)) {
            delay(10);
            continue; // discard bad reads; do not feed error bytes into magnitude math
        }
        float mag = sqrtf(gx * gx + gy * gy + gz * gz);
        if (mag > peakG) peakG = mag;
        validSamples++;
        delay(10); // ~100 Hz effective sample rate
    }
    return (validSamples > 0) ? peakG : NAN;
}

// ─────────────────────────────────────────────────────────────────────────────
// sendAlert — emit an alert note to FILE_ALERT (compact, satellite-safe)
//
// Location is NOT included in the host-side body. Instead, _lat/_lon/_ltime
// reserved compact template fields are declared in defineTemplates() so the
// Notecard injects the last known GPS fix at queue time when available.
// Before the first fix is acquired, those fields may be absent or zero.
//
// sync:true is NOT set here. The caller issues a single hub.sync after all
// alerts for the cycle are queued, avoiding redundant modem activations.
//
// Returns true if the note was accepted by the Notecard without error.
// Callers must only advance alert-delivery state on true.
// ─────────────────────────────────────────────────────────────────────────────
bool sendAlert(const char *alertType, float value) {
    J *req = notecard.newRequest("note.add");
    JAddStringToObject(req, "file", FILE_ALERT);
    J *body = JAddObjectToObject(req, "body");
    JAddStringToObject(body, "alert", alertType);
    JAddNumberToObject(body, "value", value);
    // _lat/_lon/_ltime are injected by the Notecard template engine from the
    // last known fix when available; may be absent before first fix is acquired
    J *rsp = notecard.requestAndResponse(req);
    if (rsp == NULL) {
        debugSerial.print("[warn] note.add alert (");
        debugSerial.print(alertType);
        debugSerial.println("): no response");
        return false;
    }
    const char *err = JGetString(rsp, "err");
    if (err && *err) {
        debugSerial.print("[warn] note.add alert (");
        debugSerial.print(alertType);
        debugSerial.print("): ");
        debugSerial.println(err);
        notecard.deleteResponse(rsp);
        return false;
    }
    notecard.deleteResponse(rsp);
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// sendSummary — emit the periodic status note (compact template, port 10)
//
// GPS coordinates are injected from the last known fix via the _lat/_lon/
// _ltime compact template fields when available — no host GPS query needed.
// Before the first fix is acquired those fields may be absent or zero.
//
// pressure_psi and tank_temp_c are only included in TANK_CAR builds (MPRLS
// and DS18B20 sensors fitted). In all other builds those fields are absent
// from the compact template so no sentinel bytes are wasted over satellite.
//
// Emits -9999 as a sentinel for any sensor that produced no valid reading
// so downstream analytics can distinguish "no data" from a real near-zero.
//
// shock_windows is the count of sample windows (not individual impacts)
// whose peak G exceeded shock_threshold_g during this summary period.
//
// Returns true if the note was accepted without error. Callers must only
// reset the accumulation window (peakShockG, shockWindowCount, elapsedMin)
// when this function returns true; otherwise the window is preserved for retry.
// ─────────────────────────────────────────────────────────────────────────────
bool sendSummary(float pressurePsi, float tankTempC, bool coupled, bool moving) {
    J *req = notecard.newRequest("note.add");
    JAddStringToObject(req, "file", FILE_STATUS);
    J *body = JAddObjectToObject(req, "body");
#ifdef TANK_CAR
    JAddNumberToObject(body, "pressure_psi",  isnan(pressurePsi) ? -9999.0f : pressurePsi);
    JAddNumberToObject(body, "tank_temp_c",   isnan(tankTempC)   ? -9999.0f : tankTempC);
#else
    (void)pressurePsi;
    (void)tankTempC;
#endif
    JAddBoolToObject  (body, "coupled",       coupled);
    JAddBoolToObject  (body, "moving",        moving);
    JAddNumberToObject(body, "shock_peak_g",  state.peakShockG);
    JAddNumberToObject(body, "shock_windows", state.shockWindowCount);
    J *rsp = notecard.requestAndResponse(req);
    if (rsp == NULL) {
        debugSerial.println("[warn] note.add summary: no response");
        return false;
    }
    const char *err = JGetString(rsp, "err");
    if (err && *err) {
        debugSerial.print("[warn] note.add summary: ");
        debugSerial.println(err);
        notecard.deleteResponse(rsp);
        return false;
    }
    notecard.deleteResponse(rsp);
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// sendLocationNote — emit a compact position snapshot to FILE_LOCATION
//
// Provides the dedicated position stream needed for interchange-detection and
// geofencing. Called on two triggers:
//
//   1. Motion-state transition (stopped ↔ moving): captures yard arrival and
//      departure events with an immediate hub.sync so the timestamp reaches
//      Notehub before the car moves out of connectivity range.
//
//   2. While moving, every location_interval_min minutes: fills the gap
//      between periodic status summaries with a continuous position record
//      dense enough for downstream interchange-boundary detection.
//
// Body contains only moving and coupled; _lat/_lon/_ltime are injected by
// the Notecard template engine from the last known GPS fix when available;
// before the first fix is acquired those fields may be absent or zero.
//
// Returns true if the note was accepted by the Notecard without error.
// Callers must only reset locationElapsedMin and advance lastMovingState on
// a true return so that failed sends are retried on the next wake.
// ─────────────────────────────────────────────────────────────────────────────
bool sendLocationNote(bool coupled, bool moving) {
    J *req = notecard.newRequest("note.add");
    JAddStringToObject(req, "file", FILE_LOCATION);
    J *body = JAddObjectToObject(req, "body");
    JAddBoolToObject(body, "moving",  moving);
    JAddBoolToObject(body, "coupled", coupled);
    // _lat/_lon/_ltime are injected by the Notecard template engine from the
    // last known fix when available; may be absent before first fix is acquired
    J *rsp = notecard.requestAndResponse(req);
    if (rsp == NULL) {
        debugSerial.println("[warn] note.add location: no response");
        return false;
    }
    const char *err = JGetString(rsp, "err");
    if (err && *err) {
        debugSerial.print("[warn] note.add location: ");
        debugSerial.println(err);
        notecard.deleteResponse(rsp);
        return false;
    }
    notecard.deleteResponse(rsp);
    return true;
}
