/*
 * grease_interceptor_monitor_helpers.cpp
 *
 * Utility functions for the Hydromechanical (HGI) and Batch-Collection Grease Interceptor Level Monitor.
 * See grease_interceptor_monitor_helpers.h for the shared constants, State
 * type, and forward declarations.
 *
 * `notecard` is defined as a global in grease_interceptor_monitor.ino and
 * accessed here via `extern`.
 */

#include "grease_interceptor_monitor_helpers.h"

extern Notecard notecard;  // defined in grease_interceptor_monitor.ino

// ===========================================================================
// Ultrasonic sensor read
//
// The A02YYUW (SEN0311) emits a continuous stream of 4-byte UART packets:
//   Byte 0: 0xFF  (start byte)
//   Byte 1: Distance high byte
//   Byte 2: Distance low byte
//   Byte 3: Checksum = (0xFF + high + low) & 0xFF
//   Distance (mm) = (high << 8) | low
//
// Returns the measured distance in mm, or -1.0 on error / bad checksum.
// ===========================================================================
float readDistanceMm(void) {
    // Flush stale bytes that may have accumulated while the host was sleeping
    while (Serial1.available()) Serial1.read();

    uint8_t  buf[SENSOR_PACKET_LEN];
    uint32_t t_start = millis();

    // Scan for the start byte (0xFF)
    while (millis() - t_start < SENSOR_WAIT_MS) {
        if (Serial1.available()) {
            if (Serial1.peek() == SENSOR_START_BYTE) break;
            Serial1.read();  // discard non-start bytes
        }
        delay(1);
    }
    if (!Serial1.available()) return -1.0f;  // timed out waiting for header

    // Collect the full 4-byte packet
    int      idx   = 0;
    uint32_t t_pkt = millis();
    while (idx < SENSOR_PACKET_LEN && millis() - t_pkt < SENSOR_WAIT_MS) {
        if (Serial1.available()) buf[idx++] = (uint8_t)Serial1.read();
        delay(1);
    }
    if (idx < SENSOR_PACKET_LEN) return -1.0f;  // incomplete packet

    // Validate start byte and checksum
    if (buf[0] != SENSOR_START_BYTE) return -1.0f;
    uint8_t expected_cs = (uint8_t)((buf[0] + buf[1] + buf[2]) & 0xFF);
    if (expected_cs != buf[3]) return -1.0f;  // checksum mismatch

    uint16_t dist_mm = ((uint16_t)buf[1] << 8) | buf[2];

    // Sensor's rated range is 30–4500 mm; reject out-of-range values
    if (dist_mm < 30 || dist_mm > 4500) return -1.0f;

    return (float)dist_mm;
}

// ===========================================================================
// Compute the median of an array via insertion sort (n ≤ NUM_READINGS = 5).
// For odd n the middle element is returned. For even n the two middle
// elements are averaged so the result is always an unbiased median estimate.
// Requires n >= 1. Callers guarantee n >= 2 before invoking this function.
// ===========================================================================
float medianOf(float *arr, int n) {
    for (int i = 1; i < n; i++) {
        float key = arr[i];
        int   j   = i - 1;
        while (j >= 0 && arr[j] > key) {
            arr[j + 1] = arr[j--];
        }
        arr[j + 1] = key;
    }
    if (n % 2 == 0) {
        return (arr[n / 2 - 1] + arr[n / 2]) * 0.5f;
    }
    return arr[n / 2];
}

// ===========================================================================
// Convert sensor distance to fill percentage.
//
// The ultrasonic sensor is mounted at the top of the interceptor, pointing
// down. As the interceptor fills, the liquid/FOG surface rises and the
// measured distance to that surface decreases.
//   fill_pct = (1 - distance / depth) * 100
// Clamped to [0, 100].
//
// depth_mm is the sensor-to-surface distance immediately after pump-out
// (0% fill), set per installation via the interceptor_depth_mm env var.
// On HGI and batch-collection units without a fixed outlet weir, fill_pct
// rises predictably with accumulation and is a valid pump-out indicator.
// ===========================================================================
float distanceToFillPct(float distance_mm, float depth_mm) {
    if (depth_mm <= 0.0f) return 0.0f;
    float pct = (1.0f - distance_mm / depth_mm) * 100.0f;
    if (pct < 0.0f)   pct = 0.0f;
    if (pct > 100.0f) pct = 100.0f;
    return pct;
}

// ===========================================================================
// Get current epoch time from the Notecard.
// Returns 0 if the Notecard has not yet synced time or if the request fails.
// ===========================================================================
uint32_t getEpochTime(void) {
    J *rsp = notecard.requestAndResponse(notecard.newRequest("card.time"));
    if (!notecardResponseOk(rsp)) {
        notecard.deleteResponse(rsp);
        return 0;
    }
    uint32_t t = (uint32_t)JGetNumber(rsp, "time");
    notecard.deleteResponse(rsp);
    return t;
}

// ===========================================================================
// Validate a Notecard response: non-NULL and no err field present.
// Logs the Notecard error string when debug output is enabled.
// notecard.deleteResponse() must still be called by the caller.
// ===========================================================================
bool notecardResponseOk(J *rsp) {
    if (rsp == NULL) {
#ifdef usbSerial
        usbSerial.println("[ERR] Notecard: NULL response");
#endif
        return false;
    }
    const char *err = JGetString(rsp, "err");
    if (err != NULL && err[0] != '\0') {
#ifdef usbSerial
        usbSerial.print("[ERR] Notecard: ");
        usbSerial.println(err);
#endif
        return false;
    }
    return true;
}

// ===========================================================================
// Daily summary note — queued in the Notecard's on-device store; transmitted
// on the next scheduled outbound sync (typically every 24 hours).
// Returns true if the Notecard accepted the note; false on I2C or API error.
// The caller resets window accumulators only on a true return.
//
// "full":true is required for templated notes whose fields can legitimately
// be zero. Notecard templates default to omitempty at Notehub serialization,
// stripping any field whose value is 0/false/null/"". Without "full":true a
// freshly pumped-out interceptor (0 % fill) would have fill_pct_avg,
// fill_pct_peak, and fill_pct_now all silently dropped from the Notehub body
// — only valid_samples would appear, leaving the consumer unable to
// distinguish "0 % fill" from "field never sent".
// ===========================================================================
bool sendSummary(const State &state) {
    float avg = state.fill_pct_sum / (float)state.valid_samples;

    J *req = notecard.newRequest("note.add");
    JAddStringToObject(req, "file", "grease_summary.qo");
    JAddBoolToObject(req,   "full", true);
    J *body = JAddObjectToObject(req, "body");
    JAddNumberToObject(body, "fill_pct_avg",  avg);
    JAddNumberToObject(body, "fill_pct_peak", state.fill_pct_peak);
    // fill_pct_now carries the most recent valid instantaneous reading
    // persisted in State across sleep cycles — not the window average. The
    // caller gates sendSummary on valid_samples > 0, which guarantees
    // fill_pct_last_valid has been updated to a real (>= 0) value before we
    // reach this point, so we can include it unconditionally.
    JAddNumberToObject(body, "fill_pct_now", state.fill_pct_last_valid);
    JAddNumberToObject(body, "valid_samples", state.valid_samples);
    J *rsp = notecard.requestAndResponse(req);
    bool ok = notecardResponseOk(rsp);
    notecard.deleteResponse(rsp);
    return ok;
}

// ===========================================================================
// Threshold alert note — sync:true causes the Notecard to bypass the
// outbound queue and open a cellular session immediately.
// A single attempt is made. On failure the caller does NOT advance
// last_alert_epoch, so the next wake will retry naturally as long as
// fill_pct remains above the threshold and the cooldown has elapsed.
// Retrying immediately risks duplicate alert Notes if the first request
// reached the Notecard but the I2C response was lost (note.add is not
// idempotent), which would create spurious double-alerts for a single
// threshold crossing.
// ===========================================================================
bool sendAlert(float fill_pct, float threshold_pct) {
    J *req = notecard.newRequest("note.add");
    JAddStringToObject(req, "file", "grease_alert.qo");
    JAddBoolToObject(req,   "sync", true);
    J *body = JAddObjectToObject(req, "body");
    JAddStringToObject(body, "alert",        "fill_threshold_exceeded");
    JAddNumberToObject(body, "fill_pct",      fill_pct);
    JAddNumberToObject(body, "threshold_pct", threshold_pct);
    J *rsp = notecard.requestAndResponse(req);
    bool ok = notecardResponseOk(rsp);
    notecard.deleteResponse(rsp);
    return ok;
}
