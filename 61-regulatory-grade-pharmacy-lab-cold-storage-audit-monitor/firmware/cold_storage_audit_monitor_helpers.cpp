/*
  cold_storage_audit_monitor_helpers.cpp

  Environment-variable parsing, sensor reads, Note emission, and sleep helpers
  for the cold-storage audit monitor. See cold_storage_audit_monitor_helpers.h
  for shared type definitions and extern declarations.

  Temperature is read from the production PT1000 RTD probe (Adafruit 3984) via
  the MAX31865 SPI amplifier (Adafruit 3648).  The MAX31865 runs in continuous-
  conversion mode after begin(); readTemperatureC() reads the most recent value
  on each call — no dataReady() poll is required.
*/
#include "cold_storage_audit_monitor_helpers.h"

// ===========================================================================
// fetchEnvOverrides — pull threshold and cadence overrides from Notehub
// ===========================================================================
// All variables are optional; existing values are preserved when a variable
// is absent, empty, or malformed. strtoul/strtof with end-pointer validation
// is used throughout: a value whose end pointer matches the start (no digits)
// or does not land on '\0' (trailing garbage) is silently discarded, so a
// Notehub typo cannot corrupt a previously valid threshold.
void fetchEnvOverrides() {
    J *req = notecard.newRequest("env.get");
    J *names = JAddArrayToObject(req, "names");
    JAddItemToArray(names, JCreateString("sample_interval_sec"));
    JAddItemToArray(names, JCreateString("temp_high_alert_c"));
    JAddItemToArray(names, JCreateString("temp_low_alert_c"));
    JAddItemToArray(names, JCreateString("door_open_alert_min"));
    JAddItemToArray(names, JCreateString("alert_cooldown_min"));
    JAddItemToArray(names, JCreateString("door_lux_threshold"));

    J *rsp = notecard.requestAndResponse(req);
    if (rsp == NULL) return;
    if (notecard.responseError(rsp)) { notecard.deleteResponse(rsp); return; }

    J *body = JGetObject(rsp, "body");
    if (body != NULL) {
        const char *v;
        char *end;

        v = JGetString(body, "sample_interval_sec");
        if (v && v[0]) {
            unsigned long val = strtoul(v, &end, 10);
            if (end != v && *end == '\0' && val >= 60 && val <= 3600)
                state.sample_interval_sec = (uint32_t)val;
        }

        // Parse high and low together; reject the pair atomically when
        // inconsistent (low >= high) so a partial update cannot disable all
        // temperature alerts.
        float new_high = state.temp_high_c;
        float new_low  = state.temp_low_c;
        v = JGetString(body, "temp_high_alert_c");
        if (v && v[0]) {
            float val = strtof(v, &end);
            if (end != v && *end == '\0' && val > -60.0f && val < 120.0f)
                new_high = val;
        }
        v = JGetString(body, "temp_low_alert_c");
        if (v && v[0]) {
            float val = strtof(v, &end);
            if (end != v && *end == '\0' && val > -60.0f && val < 120.0f)
                new_low = val;
        }
        if (new_low < new_high) {
            state.temp_high_c = new_high;
            state.temp_low_c  = new_low;
        }

        v = JGetString(body, "door_open_alert_min");
        if (v && v[0]) {
            unsigned long val = strtoul(v, &end, 10);
            if (end != v && *end == '\0' && val >= 1 && val <= 1440)
                state.door_alert_min = (uint32_t)val;
        }

        v = JGetString(body, "alert_cooldown_min");
        if (v && v[0]) {
            unsigned long val = strtoul(v, &end, 10);
            if (end != v && *end == '\0' && val >= 1 && val <= 1440)
                state.alert_cooldown_sec = (uint32_t)val * 60;
        }

        v = JGetString(body, "door_lux_threshold");
        if (v && v[0]) {
            float val = strtof(v, &end);
            if (end != v && *end == '\0' && val >= 0.0f && val <= 200000.0f)
                state.lux_threshold = val;
        }
    }

    notecard.deleteResponse(rsp);
}

// ===========================================================================
// Sensor helpers
// ===========================================================================

// Read temperature from the PT1000 RTD via the MAX31865 amplifier.
// Returns NAN when the amplifier was not initialised, a fault is detected,
// or the reading falls outside the expected calibrated range (-60°C to 120°C).
//
// The MAX31865 runs in continuous-conversion mode after begin() — no
// dataReady() poll is needed. temperature() retrieves the most recent value;
// readFault() checks the MAX31865 fault register and returns non-zero on any
// RTD open/short or over/under-voltage condition.
//
// Conversion constants: PT1000 R0 = 1000 Ω; Adafruit board Rref = 4300 Ω.
float readTemperatureC() {
    if (!tempSensorOk) return NAN;

    float temp = rtdAmp.temperature(1000, 4300.0);
    uint8_t fault = rtdAmp.readFault();
    if (fault) {
        rtdAmp.clearFault();
#if ENABLE_DEBUG
        Serial.print("[WARN] MAX31865 fault: 0x");
        Serial.println(fault, HEX);
#endif
        return NAN;
    }
    if (isnan(temp) || temp < -60.0f || temp > 120.0f) {
#if ENABLE_DEBUG
        Serial.println("[WARN] MAX31865 out-of-range");
#endif
        return NAN;
    }
    return temp;
}

// Read ambient illuminance from VEML7700. Returns NAN on init failure or fault.
// Gain (VEML7700_GAIN_1) and integration time (VEML7700_IT_100MS) are
// configured once in setup(); readLux() uses those fixed settings.
float readLightLux() {
    if (!lightSensorOk) return NAN;
    float lux = lightSensor.readLux();
    if (lux < 0.0f) {
#if ENABLE_DEBUG
        Serial.println("[WARN] VEML7700 invalid");
#endif
        return NAN;
    }
    return lux;
}

// Read door switch with 10 ms software debounce. Returns true when the door
// is OPEN (D5 HIGH: reed contacts open, magnet absent from the switch body).
bool readDoorOpen() {
    bool a = (digitalRead(DOOR_SWITCH_PIN) == HIGH);
    delay(10);
    bool b = (digitalRead(DOOR_SWITCH_PIN) == HIGH);
    return (a && b);
}

// Return the current UTC epoch from the Notecard RTC, or 0 when time is not
// yet synced with Notehub. Retries up to three times to tolerate transient
// I2C hiccups; distinguishes a transient fault (retry) from RTC-not-synced
// (t == 0, no error — no point retrying).
uint32_t getEpochTime() {
    for (int attempt = 0; attempt < 3; attempt++) {
        if (attempt > 0) delay(250);
        J *rsp = notecard.requestAndResponse(notecard.newRequest("card.time"));
        if (rsp == NULL) continue;
        bool had_error = notecard.responseError(rsp);
        uint32_t t = (uint32_t)JGetNumber(rsp, "time");
        notecard.deleteResponse(rsp);
        if (!had_error && t > 0) return t;
        if (!had_error) break;   // t == 0, no error: RTC not yet synced
        // had_error: transient fault, loop will retry
    }
#if ENABLE_DEBUG
    Serial.println("[WARN] getEpochTime: card.time unavailable this cycle");
#endif
    return 0;
}

// ===========================================================================
// sendReading — enqueue one per-sample templated Note
// ===========================================================================
// Retries up to three times to survive transient Notecard/I2C faults.
// Returns true on success (and resets the drop counters now that they have
// been reported in the Note body). Returns false when all retries fail;
// the caller is responsible for persisting the payload and accounting for
// the loss.
//
// sample_epoch: UTC epoch captured at sensor-read time. Written as an explicit
// body field so retried Notes (whose Notecard envelope reflects retry time)
// still carry the authoritative original sample timestamp for audit lineage.
// Pass 0 together with time_valid:false when the Notecard RTC has not yet
// synced — downstream systems can distinguish pre-sync data explicitly.
bool sendReading(float temp_c, float lux, bool door_open,
                 uint32_t door_open_sec, bool time_valid, uint32_t sample_epoch) {
    for (int attempt = 0; attempt < 3; attempt++) {
        if (attempt > 0) delay(250);

        J *req = notecard.newRequest("note.add");
        JAddStringToObject(req, "file", NOTEFILE_READING);
        J *body = JAddObjectToObject(req, "body");
        JAddNumberToObject(body, "temp_c",
                           (double)(isnan(temp_c) ? SENTINEL_NO_DATA : temp_c));
        JAddNumberToObject(body, "lux",
                           (double)(isnan(lux) ? SENTINEL_LUX_NO_DATA : lux));
        JAddBoolToObject(body,   "door_open",         door_open);
        JAddNumberToObject(body, "door_open_sec",     (int)door_open_sec);
        // Authoritative sample time for audit lineage. On first-attempt sends
        // this matches the Notecard envelope time; on retried sends it preserves
        // the original sample epoch despite the envelope reflecting retry time.
        JAddNumberToObject(body, "sample_epoch",      (double)sample_epoch);
        JAddBoolToObject(body,   "time_valid",        time_valid);
        // Cumulative drop counters — visible in Notehub without a separate channel
        JAddNumberToObject(body, "dropped_readings",  (double)state.dropped_readings);
        JAddNumberToObject(body, "dropped_alerts",    (double)state.dropped_alerts);
        // Reading Notes ride the regular outbound cadence; no sync:true needed.

        J *rsp = notecard.requestAndResponse(req);
        if (rsp == NULL) {
#if ENABLE_DEBUG
            Serial.print("[WARN] sendReading: no response (attempt ");
            Serial.print(attempt + 1); Serial.println(")");
#endif
            continue;
        }
        if (notecard.responseError(rsp)) {
#if ENABLE_DEBUG
            Serial.print("[WARN] sendReading: "); Serial.println(JGetString(rsp, "err"));
#endif
            notecard.deleteResponse(rsp);
            continue;
        }
        notecard.deleteResponse(rsp);
        // Success: clear accumulated drop counters now that they have been reported.
        state.dropped_readings = 0;
        state.dropped_alerts   = 0;
        return true;
    }
#if ENABLE_DEBUG
    Serial.println("[ERR] sendReading: all retries failed");
#endif
    return false;
}

// ===========================================================================
// sendAlert — enqueue an immediate-sync alert Note
// ===========================================================================
// sync:true tells the Notecard to open a cellular session immediately rather
// than waiting for the next scheduled outbound window. Retries up to three
// times to survive transient faults.
// Returns true on success; false when all retries are exhausted.
// On false the caller must NOT advance the cooldown timestamp (keeping the
// alert eligible for retry) and should persist the payload in AppState.
//
// event_epoch: the UTC epoch when the alert condition was first detected.
// note.add has no timestamp-override field, so the Notecard stamps a retried
// Note with retry time in the envelope. event_epoch is always written as an
// explicit body field so downstream audit queries can use the original trigger
// time regardless of whether this is a first-attempt or retried send.
bool sendAlert(const char *alert_type, float temp_c, float lux,
               bool door_open, uint32_t door_open_sec, bool time_valid,
               uint32_t event_epoch) {
    for (int attempt = 0; attempt < 3; attempt++) {
        if (attempt > 0) delay(250);

        J *req = notecard.newRequest("note.add");
        JAddStringToObject(req, "file", NOTEFILE_ALERT);
        JAddBoolToObject(req, "sync", true);
        J *body = JAddObjectToObject(req, "body");
        JAddStringToObject(body, "alert",         alert_type);
        JAddNumberToObject(body, "temp_c",
                           (double)(isnan(temp_c) ? SENTINEL_NO_DATA : temp_c));
        JAddNumberToObject(body, "lux",
                           (double)(isnan(lux) ? SENTINEL_LUX_NO_DATA : lux));
        JAddBoolToObject(body,   "door_open",      door_open);
        JAddNumberToObject(body, "door_open_sec",  (int)door_open_sec);
        JAddBoolToObject(body,   "time_valid",     time_valid);
        // Authoritative trigger time for audit lineage. On first-attempt sends
        // this matches the Notecard envelope time; on retried sends it
        // preserves the original event epoch despite the envelope reflecting
        // retry time.
        JAddNumberToObject(body, "event_epoch",    (double)event_epoch);

        J *rsp = notecard.requestAndResponse(req);
        if (rsp == NULL) {
#if ENABLE_DEBUG
            Serial.print("[WARN] sendAlert: no response (attempt ");
            Serial.print(attempt + 1); Serial.println(")");
#endif
            continue;
        }
        if (notecard.responseError(rsp)) {
#if ENABLE_DEBUG
            Serial.print("[WARN] sendAlert: "); Serial.println(JGetString(rsp, "err"));
#endif
            notecard.deleteResponse(rsp);
            continue;
        }
        notecard.deleteResponse(rsp);
        return true;
    }
#if ENABLE_DEBUG
    Serial.println("[ERR] sendAlert: all retries failed");
#endif
    return false;
}

// ===========================================================================
// goToSleep — serialise AppState to Notecard flash and cut Cygnet power
// ===========================================================================
// NotePayloadSaveAndSleep writes AppState into the Notecard's flash-backed
// segment store and issues card.attn sleep. The ATTN pin drives the
// Notecarrier CX enable gate, cutting power to the Cygnet for
// sample_interval_sec seconds. On the next wake, setup() calls
// NotePayloadRetrieveAfterSleep to restore the struct.
void goToSleep() {
    NotePayloadDesc payload = {0, 0, 0};
    NotePayloadAddSegment(&payload, STATE_SEG_ID, &state, sizeof(state));
    NotePayloadSaveAndSleep(&payload, state.sample_interval_sec, NULL);
    // If execution reaches here, ATTN-based power control is not wired.
    // The delay in loop() provides a fallback bench polling cadence.
}
