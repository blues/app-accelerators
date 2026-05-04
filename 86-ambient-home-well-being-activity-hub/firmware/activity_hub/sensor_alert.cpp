/*
 * sensor_alert.cpp — Sensor reads and outbound Note helpers.
 *
 * This file is specific to this project and is NOT a general-purpose library.
 */
#include "sensor_alert.h"

// ---------------------------------------------------------------------------
// Sensor helpers
// ---------------------------------------------------------------------------

// Reads temperature and relative humidity from the SHT31.
// Returns false on sensor error (NaN); outTempC and outHumidPct are
// unchanged on false so callers can keep the last-known values.
bool readHumidity(float &outTempC, float &outHumidPct) {
    float t = sht31.readTemperature();
    float h = sht31.readHumidity();
    if (isnan(t) || isnan(h)) return false;
    outTempC    = t;
    outHumidPct = h;
    return true;
}

// Reads the piezo bed-vibration sensor over a 500 ms measurement window.
// Returns true when peak-to-peak ADC amplitude exceeds g_bed_threshold.
//
// IMPORTANT — this is a vibration signal, NOT a bed-occupancy measurement:
//   true  → detectable micro-vibration was present at the sensor point.
//   false → the sensor was quiet at this location during the window.
//           This does NOT mean the bed is unoccupied. An immobile occupant
//           (deeply asleep, post-surgery, or heavily medicated) produces
//           low-amplitude vibration that may fall below threshold and return
//           false even when physically present. An empty mattress is also
//           electrically quiet. Callers must not infer "bed empty" from false.
//
// ADC resolution is fixed at 12 bits in setup() so amplitude spans 0–4095
// and matches the bed_threshold tuning range documented in app_state.h.
// Enable both CALIBRATION_MODE and DEBUG_SERIAL in app_state.h to log the
// raw amplitude to Serial for threshold calibration.
bool readBedMotion() {
    const int kSamples = 100;
    int vMax = 0, vMin = 4095;
    for (int i = 0; i < kSamples; i++) {
        int v = analogRead(PIN_BED);
        if (v > vMax) vMax = v;
        if (v < vMin) vMin = v;
        delay(5);   // 5 ms × 100 = 500 ms total window
    }
#if defined(CALIBRATION_MODE) && defined(DEBUG_SERIAL)
    Serial.print("Bed peak-to-peak (ADC counts): ");
    Serial.println(vMax - vMin);
#endif
    return ((vMax - vMin) >= g_bed_threshold);
}

// ---------------------------------------------------------------------------
// Outbound Note helpers
// ---------------------------------------------------------------------------

// Queues one activity_summary.qo Note. Returns true only when the Notecard
// confirms the Note was accepted. The caller must NOT reset summary window
// counters on false — doing so would permanently discard the hour's data.
bool sendSummary(AppState &state) {
    uint8_t bed_motion_pct = (state.bed_samples > 0)
        ? (uint8_t)((100UL * state.bed_motion_samples) / state.bed_samples) : 0;

    J *req = notecard.newRequest("note.add");
    if (!req) return false;
    JAddStringToObject(req, "file", "activity_summary.qo");
    J *body = JAddObjectToObject(req, "body");
    JAddNumberToObject(body, "pir_count",        (int)state.pir_count);
    JAddNumberToObject(body, "door_count",        (int)state.door_count);
    JAddNumberToObject(body, "humidity_pct",      state.humidity_last);
    JAddNumberToObject(body, "temp_c",            state.temp_last);
    JAddNumberToObject(body, "bed_motion_pct",    (int)bed_motion_pct);
    JAddNumberToObject(body, "night_bath_count",  (int)state.night_bathroom_count);
    JAddBoolToObject(body,   "morning_activity",  state.morning_activity);
    J *rsp = notecard.requestAndResponse(req);
    if (!rsp) return false;
    bool ok = !notecard.responseError(rsp);
    notecard.deleteResponse(rsp);
    return ok;
}

// Queues one activity_alert.qo Note with sync:true (bypasses the outbound
// interval for immediate delivery). Returns true only when the Notecard
// confirms success. The caller must NOT update alert latches on false —
// leaving the rule armed allows it to retry on the next wake cycle.
bool sendAlert(const char *alertType, const char *detail) {
    J *req = notecard.newRequest("note.add");
    if (!req) return false;
    JAddStringToObject(req, "file", "activity_alert.qo");
    JAddBoolToObject(req,   "sync", true);
    J *body = JAddObjectToObject(req, "body");
    JAddStringToObject(body, "alert",  alertType);
    JAddStringToObject(body, "detail", detail);
    J *rsp = notecard.requestAndResponse(req);
    if (!rsp) return false;
    bool ok = !notecard.responseError(rsp);
    notecard.deleteResponse(rsp);
    return ok;
}
