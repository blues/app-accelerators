/***************************************************************************
  apiary_hive_monitor_helpers.cpp — Sensor driver and Notecard helper
  implementations for the Remote Apiary Hive Health Monitor.

  Contains:
    notecardConfigure() — hub.set (daily outbound / weekly inbound),
                          accelerometer disable; called once on first boot.
    defineTemplates()   — registers compact Note templates for
                          hive_summary.qo (port 10) and hive_alert.qo
                          (port 11); required for Starnote NTN operation.
    fetchEnvOverrides() — pulls operator thresholds from Notehub env vars,
                          applies individual range clamps, then validates the
                          temp_low_c / temp_high_c pair together before storing.
    readWeightKg()      — HX711 10-sample average; sleeps chip after read.
    readTempHumidity()  — SHT31-D single I2C measurement with NaN guard.
    readAudioFeatures() — two-pass ZCR/RMS/peak extraction over 12 × 256
                          samples; per-window DC offset computed from data,
                          ZCR derived from measured wall-clock time.
    sendAlert()         — note.add with sync:true; retries once on failure.
    sendSummary()       — daily aggregated Note; uses safeAvg() sentinels.
    safeAvg()           — returns -9999.0 when count == 0.
***************************************************************************/

#include <Arduino.h>
#include "apiary_hive_monitor_helpers.h"

// DEBUG_SERIAL is controlled by the commented-out define in
// apiary_hive_monitor_helpers.h (included above). Both this translation unit
// and the .ino see the same flag because both include the shared header.

// Audio constants
// AUDIO_SAMPLE_RATE_HZ is a nominal design target only.  The actual
// acquisition rate on any given board is determined by analogRead() latency
// plus the inter-sample delay, so it differs from this figure.  ZCR is
// derived from the wall-clock duration measured with micros() during each
// window's pass-1 collection (see readAudioFeatures), not from this constant.
static constexpr int   AUDIO_SAMPLE_RATE_HZ   = 4000;  // nominal; not used in ZCR calc
static constexpr int   AUDIO_SAMPLE_PERIOD_US  = 225;  // 250 µs - ~25 µs read time
static constexpr int   AUDIO_WINDOW_SAMPLES    = 256;
static constexpr int   AUDIO_NUM_WINDOWS       = 12;
// Half-range of the 12-bit ADC: used to normalise centred samples to [-1, 1].
// This is NOT used as a DC-bias offset — the per-window DC mean is computed
// from the actual samples before each feature calculation window.
static constexpr int   AUDIO_ADC_HALFRANGE     = 2048;
static constexpr int   PIN_MIC_LOCAL           = A0;

// Audio health-check thresholds.
// The MAX9814 biases its output at ~1.65 V (half of VCC), so the ADC idle
// reading should be near the 12-bit midpoint (2048).  A DC offset outside
// the plausible band [AUDIO_DC_PLAUSIBLE_MIN, AUDIO_DC_PLAUSIBLE_MAX] indicates
// a floating input, a disconnected microphone, or a severely mis-biased circuit.
// AUDIO_CLIP_THRESH defines the LSB distance from either rail (0 or 4095) within
// which a sample is counted as clipped; AUDIO_CLIP_FRAC_MAX is the fraction of
// samples in a window that may be clipped before the whole window is rejected.
// AUDIO_MIN_GOOD_WINDOWS is the minimum number of per-window validity checks
// that must pass for the whole sample to be accepted.
// AUDIO_RMS_MIN is a normalized-amplitude floor (0.0 = silence, 1.0 = full-scale);
// values below this indicate a dead or shorted microphone even if DC/clip checks
// pass (e.g., A0 tied to a stable mid-rail voltage with no AC content).
static constexpr int   AUDIO_DC_PLAUSIBLE_MIN  = 256;   // ~6.25 % of 4096 from rail
static constexpr int   AUDIO_DC_PLAUSIBLE_MAX  = 3840;  // ~93.75 % of 4096
static constexpr int   AUDIO_CLIP_THRESH       = 32;    // within 32 LSB of 0 or 4095
static constexpr float AUDIO_CLIP_FRAC_MAX     = 0.30f; // > 30 % clipped → reject window
static constexpr int   AUDIO_MIN_GOOD_WINDOWS  = 7;     // need ≥ 7 of 12 valid windows
static constexpr float AUDIO_RMS_MIN           = 0.002f;// normalized RMS floor

// ===========================================================================
// notecardConfigure — hub.set for daily outbound / weekly inbound
// Returns true on success; false if the critical hub.set fails (retry next wake).
// ===========================================================================
bool notecardConfigure(bool freshBoot, const char *productUID) {
    if (!freshBoot) return true;

    // Retry loop handles the cold-boot I2C race on the Notecarrier CX.
    // requestAndResponse() lets us inspect the Notecard-side response so that a
    // rejected hub.set (err field present) is treated as a real failure rather
    // than as transport-level success; first_boot must only clear when the
    // Notecard confirms the configuration was accepted without error.
    bool hubSetOk = false;
    J *req = NULL;
    for (int attempt = 0; attempt < 5 && !hubSetOk; attempt++) {
        if (attempt > 0) delay(1000);
        req = notecard.newRequest("hub.set");
        JAddStringToObject(req, "product",  productUID);
        JAddStringToObject(req, "mode",     "periodic");
        JAddNumberToObject(req, "outbound", 1440);   // daily sync (minutes)
        JAddNumberToObject(req, "inbound",  10080);  // weekly inbound (satellite budget)
        J *rsp = notecard.requestAndResponse(req);
        if (rsp != NULL) {
            const char *err = JGetString(rsp, "err");
            hubSetOk = (!err || !*err);
            notecard.deleteResponse(rsp);
        }
    }
    if (!hubSetOk) {
        // hub.set was rejected or timed out — leave first_boot dirty so the sketch retries.
        return false;
    }

    // Disable accelerometer — reduces idle interrupt noise on Mojo power trace.
    // Non-critical; failure does not prevent Notecard from operating normally.
    req = notecard.newRequest("card.motion.mode");
    JAddBoolToObject(req, "stop", true);
    notecard.sendRequest(req);

    // This design does not configure or use device location — no
    // card.location.mode call is issued.  The Starnote's integrated GPS
    // receiver is used internally by the satellite stack for timing and
    // ephemeris only; location data is not read or transmitted by the host.
    return true;
}

// ===========================================================================
// defineTemplates — compact templates required for Starnote NTN operation
// Returns true only if both templates registered without a Notecard error.
// ===========================================================================
bool defineTemplates(void) {
    // hive_summary.qo — daily aggregated summary
    J *req = notecard.newRequest("note.template");
    JAddStringToObject(req, "file",   "hive_summary.qo");
    JAddNumberToObject(req, "port",   10);
    JAddStringToObject(req, "format", "compact");
    J *body = JAddObjectToObject(req, "body");
    JAddNumberToObject(body, "weight_kg",    14.1);  // 4-byte float
    JAddNumberToObject(body, "weight_delta", 14.1);
    JAddNumberToObject(body, "temp_c_avg",   14.1);
    JAddNumberToObject(body, "humidity_avg", 14.1);
    JAddNumberToObject(body, "zcr_avg",      12);    // 2-byte signed int
    JAddNumberToObject(body, "rms_avg",      14.1);
    JAddNumberToObject(body, "peak_avg",     14.1);
    JAddNumberToObject(body, "samples",      12);
    JAddNumberToObject(body, "batt_mv",      12);
    {
        J *rsp = notecard.requestAndResponse(req);
        if (rsp == NULL) return false;
        const char *err = JGetString(rsp, "err");
        if (err && *err) { notecard.deleteResponse(rsp); return false; }
        notecard.deleteResponse(rsp);
    }

    // hive_alert.qo — immediate threshold alerts
    req = notecard.newRequest("note.template");
    JAddStringToObject(req, "file",   "hive_alert.qo");
    JAddNumberToObject(req, "port",   11);
    JAddStringToObject(req, "format", "compact");
    body = JAddObjectToObject(req, "body");
    JAddStringToObject(body, "alert",   "audio_anomaly");  // longest name sets max field width
    JAddNumberToObject(body, "value1",  14.1);
    JAddNumberToObject(body, "value2",  14.1);
    JAddNumberToObject(body, "batt_mv", 12);
    {
        J *rsp = notecard.requestAndResponse(req);
        if (rsp == NULL) return false;
        const char *err = JGetString(rsp, "err");
        if (err && *err) { notecard.deleteResponse(rsp); return false; }
        notecard.deleteResponse(rsp);
    }

    return true;
}

// ===========================================================================
// fetchEnvOverrides — pull operator thresholds from Notehub environment vars
// ===========================================================================
void fetchEnvOverrides(uint16_t &sampleMin, uint16_t &reportHr,
                       float &weightDropKg, float &tempLow,
                       float &tempHigh, uint16_t &audioZcr,
                       float &calibration, float &zeroOffsetKg,
                       bool &resetSeen) {
    resetSeen = false;
    J *req = notecard.newRequest("env.get");
    J *rsp = notecard.requestAndResponse(req);
    if (rsp == NULL) return;
    const char *envErr = JGetString(rsp, "err");
    if (envErr && *envErr) { notecard.deleteResponse(rsp); return; }

    J *body = JGetObject(rsp, "body");
    if (body != NULL) {
        const char *v;
        if ((v = JGetString(body, "sample_interval_min")) && *v) {
            uint16_t n = (uint16_t)atoi(v);
            if (n >= 1 && n <= 1440) sampleMin = n;
        }
        if ((v = JGetString(body, "report_interval_hr")) && *v) {
            uint16_t n = (uint16_t)atoi(v);
            if (n >= 1 && n <= 168) reportHr = n;
        }
        if ((v = JGetString(body, "weight_alert_kg_drop")) && *v) {
            float f = strtof(v, NULL);
            if (f > 0.0f && f < 50.0f) weightDropKg = f;
        }
        // Parse temp thresholds into locals first; validate the pair together
        // before committing them.  Raising temp_low_c in Notehub without also
        // updating temp_high_c is a valid operator action, but if it produces a
        // band where newTempLow >= newTempHigh the alert rule
        //   (temp_c < tempLow || temp_c > tempHigh)
        // becomes permanently true for every reading.  Reject the whole override
        // set and keep the caller's values (compile-time defaults) instead.
        float newTempLow  = tempLow;   // start from caller-supplied value (default)
        float newTempHigh = tempHigh;  // start from caller-supplied value (default)
        if ((v = JGetString(body, "temp_low_c")) && *v) {
            float f = strtof(v, NULL);
            if (f > 0.0f && f < 50.0f) newTempLow = f;
        }
        if ((v = JGetString(body, "temp_high_c")) && *v) {
            float f = strtof(v, NULL);
            if (f > 0.0f && f < 50.0f) newTempHigh = f;
        }
        if (newTempLow < newTempHigh) {
            tempLow  = newTempLow;
            tempHigh = newTempHigh;
        } else {
#ifdef DEBUG_SERIAL
            Serial.print("[APP] env: temp band inverted (low=");
            Serial.print(newTempLow);
            Serial.print(", high=");
            Serial.print(newTempHigh);
            Serial.println(") — keeping compile-time defaults");
#endif
            // tempLow / tempHigh unchanged; caller's defaults remain in effect.
        }
        if ((v = JGetString(body, "audio_zcr_alert")) && *v) {
            uint16_t n = (uint16_t)atoi(v);
            if (n >= 100 && n <= 5000) audioZcr = n;
        }
        if ((v = JGetString(body, "hx711_calibration")) && *v) {
            float f = strtof(v, NULL);
            if (f > 100.0f && f < 100000.0f) calibration = f;
        }
        if ((v = JGetString(body, "hx711_zero_offset_kg")) && *v) {
            float f = strtof(v, NULL);
            if (f >= 0.0f && f < 250.0f) zeroOffsetKg = f;
        }
        // Commissioning reset: report the raw env var state to the caller.
        // resetSeen = true means reset_state is currently "1" in Notehub.
        // The caller compares resetSeen against HiveState::last_reset_token to
        // ensure the reset fires only on the 0→1 transition (one-shot); the
        // operator does not need to manually clear the variable to prevent
        // repeated resets on subsequent wakes.
        if ((v = JGetString(body, "reset_state")) && *v) {
            if (strcmp(v, "1") == 0) resetSeen = true;
        }
    }
    notecard.deleteResponse(rsp);
}

// ===========================================================================
// readWeightKg — HX711 10-sample average; returns -1.0 on timeout/error
// ===========================================================================
float readWeightKg(float calibration) {
    scale.power_up();
    scale.set_scale(calibration);

    unsigned long deadline = millis() + 500;
    while (!scale.is_ready() && millis() < deadline) delay(5);

    if (!scale.is_ready()) {
#ifdef DEBUG_SERIAL
        Serial.println("[APP] HX711 timeout");
#endif
        scale.power_down();
        return -1.0f;
    }

    float raw = scale.get_units(10);  // average 10 readings
    if (raw < 0.0f)   raw = 0.0f;
    if (raw > 250.0f) raw = 250.0f;
    return raw;
}

// ===========================================================================
// readTempHumidity — SHT31-D single I2C measurement; false on sensor error
// ===========================================================================
bool readTempHumidity(float &temp_c, float &humidity_pct) {
    temp_c       = sht31.readTemperature();
    humidity_pct = sht31.readHumidity();
    if (isnan(temp_c) || isnan(humidity_pct)) {
#ifdef DEBUG_SERIAL
        Serial.println("[APP] SHT31-D read failed");
#endif
        return false;
    }
    return true;
}

// ===========================================================================
// readAudioFeatures — two-pass ZCR/RMS/peak extraction with hardware-validity
// gating; returns false and zeroes outputs when the audio signal is implausible.
//
// Processes AUDIO_NUM_WINDOWS × AUDIO_WINDOW_SAMPLES samples (~0.75 s total).
// Each 256-sample window is collected into a small stack buffer so that the
// per-window DC mean can be computed and subtracted before ZCR/RMS/peak
// analysis.  Measuring the actual DC operating point rather than assuming the
// 12-bit midpoint (2048) compensates for MAX9814 AGC and board-to-board bias
// variation, making the ZCR threshold more portable across installations.
// ZCR in counts/second reflects colony acoustic activity level; an anomalous
// ZCR indicates a measurable behavioral change whose specific biological cause
// requires physical inspection and cannot be inferred from ZCR alone.
//
// Validity gates (per window):
//   DC plausibility — dc_offset must be within [AUDIO_DC_PLAUSIBLE_MIN,
//                     AUDIO_DC_PLAUSIBLE_MAX].  A floating or disconnected
//                     input typically reads near 0 or 4095; a shorted input
//                     reads a constant mid-rail value with the wrong polarity.
//   Clipping        — if > AUDIO_CLIP_FRAC_MAX of samples are within
//                     AUDIO_CLIP_THRESH LSB of either ADC rail, the signal
//                     is railed (severe overload or shorted supply rail).
// Windows that fail either gate are skipped; if fewer than AUDIO_MIN_GOOD_WINDOWS
// pass, the entire sample is rejected and the function returns false.
//
// Final gate (whole sample):
//   RMS floor       — if the overall normalized RMS is below AUDIO_RMS_MIN the
//                     microphone is dead or shorted even though DC/clip checks
//                     passed (e.g., A0 tied to a stable mid-rail DC source).
// ===========================================================================
bool readAudioFeatures(float &zcr_mean, float &rms_mean, float &peak_mean) {
    // Explicitly configure 12-bit ADC resolution so that AUDIO_ADC_HALFRANGE
    // (2048) matches the actual hardware output range (0–4095).  STM32duino
    // defaults to 12-bit on STM32 targets, but the explicit call documents the
    // dependency and keeps the code portable to boards that default to 10-bit.
    analogReadResolution(12);

    float zcr_accum  = 0.0f, rms_accum  = 0.0f, peak_accum  = 0.0f;
    int   bad_windows = 0;

    for (int w = 0; w < AUDIO_NUM_WINDOWS; w++) {
        // --- Pass 1: collect raw ADC samples, sum for DC mean, count clips --
        // 256 × int16_t = 512 bytes; allocated once per window on the stack.
        // t0 / t1 bracket the collection so ZCR is derived from the actual
        // elapsed wall-clock time rather than the nominal 4 kHz design rate.
        // This keeps the counts/second figure accurate regardless of MCU speed
        // or core-specific analogRead() latency.
        int16_t samples[AUDIO_WINDOW_SAMPLES];
        int32_t dc_sum  = 0;
        int     clipped = 0;

        unsigned long t0 = micros();
        samples[0] = (int16_t)analogRead(PIN_MIC_LOCAL);
        dc_sum = samples[0];
        for (int i = 1; i < AUDIO_WINDOW_SAMPLES; i++) {
            delayMicroseconds(AUDIO_SAMPLE_PERIOD_US);
            samples[i] = (int16_t)analogRead(PIN_MIC_LOCAL);
            dc_sum += samples[i];
        }
        unsigned long t1 = micros();
        int32_t dc_offset = dc_sum / AUDIO_WINDOW_SAMPLES;

        // Count rail-clipped samples (absolute ADC limits, before DC removal).
        // A floating or railed input produces many samples near 0 or 4095.
        for (int i = 0; i < AUDIO_WINDOW_SAMPLES; i++) {
            if (samples[i] < AUDIO_CLIP_THRESH ||
                samples[i] > (4095 - AUDIO_CLIP_THRESH)) clipped++;
        }

        // --- Per-window validity gate ------------------------------------
        // Reject windows where the DC bias is outside the plausible mid-rail
        // band or where the signal is predominantly clipped/railed.
        bool window_ok =
            (dc_offset >= AUDIO_DC_PLAUSIBLE_MIN &&
             dc_offset <= AUDIO_DC_PLAUSIBLE_MAX) &&
            ((float)clipped / (float)AUDIO_WINDOW_SAMPLES <= AUDIO_CLIP_FRAC_MAX);

        if (!window_ok) {
            bad_windows++;
            continue;   // skip Pass 2; do not accumulate features for this window
        }

        // Actual window duration; guard against a zero difference on a very
        // fast core to avoid division by zero.
        float dur_s = (t1 > t0) ? (float)(t1 - t0) * 1e-6f
                                 : (float)AUDIO_WINDOW_SAMPLES / (float)AUDIO_SAMPLE_RATE_HZ;

        // --- Pass 2: compute ZCR / RMS / peak with measured DC removed ----
        uint32_t crossings = 0;
        float    sum_sq    = 0.0f;
        float    peak_abs  = 0.0f;

        int32_t prev = (int32_t)samples[0] - dc_offset;
        for (int i = 1; i < AUDIO_WINDOW_SAMPLES; i++) {
            int32_t cur = (int32_t)samples[i] - dc_offset;

            if ((prev < 0 && cur >= 0) || (prev >= 0 && cur < 0)) crossings++;

            // Normalise to [-1, 1] using the ADC half-range as the divisor.
            float norm = (float)cur / (float)AUDIO_ADC_HALFRANGE;
            sum_sq += norm * norm;
            float abs_n = (norm < 0.0f) ? -norm : norm;
            if (abs_n > peak_abs) peak_abs = abs_n;
            prev = cur;
        }

        zcr_accum  += (float)crossings / dur_s;
        rms_accum  += sqrtf(sum_sq / (float)(AUDIO_WINDOW_SAMPLES - 1));
        peak_accum += peak_abs;
    }

    // --- Whole-sample validity gate: minimum good-window count -----------
    int good_windows = AUDIO_NUM_WINDOWS - bad_windows;
    if (good_windows < AUDIO_MIN_GOOD_WINDOWS) {
#ifdef DEBUG_SERIAL
        Serial.print("[APP] Audio rejected: only ");
        Serial.print(good_windows);
        Serial.println(" of 12 windows passed DC/clip check");
#endif
        zcr_mean = 0.0f; rms_mean = 0.0f; peak_mean = 0.0f;
        return false;
    }

    zcr_mean  = zcr_accum  / (float)good_windows;
    rms_mean  = rms_accum  / (float)good_windows;
    peak_mean = peak_accum / (float)good_windows;

    // --- Whole-sample validity gate: RMS floor ---------------------------
    // Near-zero RMS means the microphone is dead or shorted even if DC and
    // clip checks passed (e.g., A0 pulled to a stable mid-rail supply).
    if (rms_mean < AUDIO_RMS_MIN) {
#ifdef DEBUG_SERIAL
        Serial.print("[APP] Audio rejected: RMS ");
        Serial.print(rms_mean, 6);
        Serial.println(" below minimum — mic dead or shorted?");
#endif
        zcr_mean = 0.0f; rms_mean = 0.0f; peak_mean = 0.0f;
        return false;
    }

    return true;
}

// ===========================================================================
// sendAlert — note.add with sync:true; bypasses outbound timer immediately.
// Retries once on the same wake for transient failures.
// Returns true only when the Notecard acknowledges without error.
// ===========================================================================
bool sendAlert(const char *alertType, float value1, float value2) {
    // Read battery voltage once; use -9999 sentinel so downstream consumers
    // can distinguish a failed read from a legitimately-low real measurement.
    int batt_mv = -9999;
    {
        J *rsp = notecard.requestAndResponse(notecard.newRequest("card.voltage"));
        if (rsp != NULL) {
            const char *err = JGetString(rsp, "err");
            if (!err || !*err) {
                batt_mv = (int)(JGetNumber(rsp, "value") * 1000.0);
            }
            notecard.deleteResponse(rsp);
        }
    }

    // Attempt note.add up to two times on the same wake cycle.
    for (int attempt = 0; attempt < 2; attempt++) {
        J *req = notecard.newRequest("note.add");
        if (req == NULL) return false;
        JAddStringToObject(req, "file", "hive_alert.qo");
        JAddBoolToObject(req, "sync", true);
        J *body = JAddObjectToObject(req, "body");
        if (body != NULL) {
            JAddStringToObject(body, "alert",   alertType);
            JAddNumberToObject(body, "value1",  value1);
            JAddNumberToObject(body, "value2",  value2);
            JAddNumberToObject(body, "batt_mv", batt_mv);
        }
        J *rsp = notecard.requestAndResponse(req);
        if (rsp != NULL) {
            const char *err = JGetString(rsp, "err");
            bool ok = (!err || !*err);
#ifdef DEBUG_SERIAL
            if (!ok) {
                Serial.print("[APP] Alert note.add failed (attempt ");
                Serial.print(attempt + 1);
                Serial.print("): ");
                Serial.println(err);
            } else {
                Serial.print("[APP] Alert sent: "); Serial.println(alertType);
            }
#endif
            notecard.deleteResponse(rsp);
            if (ok) return true;
        }
    }
    return false;
}

// ===========================================================================
// sendSummary — daily aggregated summary Note.
// Retries once on the same wake for transient failures.
// Returns true only when the Notecard acknowledges without error.
// ===========================================================================
bool sendSummary(const HiveState &st) {
    // Read battery voltage once; use -9999 sentinel so downstream consumers
    // can distinguish a failed read from a legitimately-low real measurement.
    int batt_mv = -9999;
    {
        J *rsp = notecard.requestAndResponse(notecard.newRequest("card.voltage"));
        if (rsp != NULL) {
            const char *err = JGetString(rsp, "err");
            if (!err || !*err) {
                batt_mv = (int)(JGetNumber(rsp, "value") * 1000.0);
            }
            notecard.deleteResponse(rsp);
        }
    }

    float weight_avg   = safeAvg(st.weight_sum_kg, st.weight_sample_count);
    // Both weight sentinels are -1.0f; >= 0.0f confirms a valid reading exists,
    // including a true-zero reading for an empty or removed platform.
    float weight_delta = (st.weight_first_kg >= 0.0f && st.weight_last_kg >= 0.0f)
                         ? (st.weight_last_kg - st.weight_first_kg) : -9999.0f;

    // Attempt note.add up to two times on the same wake cycle.
    for (int attempt = 0; attempt < 2; attempt++) {
        J *req = notecard.newRequest("note.add");
        if (req == NULL) return false;
        JAddStringToObject(req, "file", "hive_summary.qo");
        J *body = JAddObjectToObject(req, "body");
        if (body != NULL) {
            JAddNumberToObject(body, "weight_kg",    weight_avg);
            JAddNumberToObject(body, "weight_delta", weight_delta);
            JAddNumberToObject(body, "temp_c_avg",   safeAvg(st.temp_sum_c,      st.temp_valid_count));
            JAddNumberToObject(body, "humidity_avg", safeAvg(st.humidity_sum_pct, st.temp_valid_count));
            // audio_sample_count is 0 or 1 per window (snapshot taken once daily)
            JAddNumberToObject(body, "zcr_avg",      (int)safeAvg(st.zcr_sum,    st.audio_sample_count));
            JAddNumberToObject(body, "rms_avg",      safeAvg(st.rms_sum,         st.audio_sample_count));
            JAddNumberToObject(body, "peak_avg",     safeAvg(st.peak_sum,        st.audio_sample_count));
            JAddNumberToObject(body, "samples",      st.sample_count);
            JAddNumberToObject(body, "batt_mv",      batt_mv);
        }
        J *rsp = notecard.requestAndResponse(req);
        if (rsp != NULL) {
            const char *err = JGetString(rsp, "err");
            bool ok = (!err || !*err);
#ifdef DEBUG_SERIAL
            if (!ok) {
                Serial.print("[APP] Summary note.add failed (attempt ");
                Serial.print(attempt + 1);
                Serial.print("): ");
                Serial.println(err);
            } else {
                Serial.print("[APP] Summary sent — samples: "); Serial.print(st.sample_count);
                Serial.print("  zcr_avg: ");  Serial.print(safeAvg(st.zcr_sum,  st.audio_sample_count));
                Serial.print("  rms_avg: ");  Serial.print(safeAvg(st.rms_sum,  st.audio_sample_count), 4);
                Serial.print("  peak_avg: "); Serial.println(safeAvg(st.peak_sum, st.audio_sample_count), 4);
            }
#endif
            notecard.deleteResponse(rsp);
            if (ok) return true;
        }
    }
    return false;
}

// ===========================================================================
// safeAvg — returns -9999.0 sentinel so downstream can detect missing data
// ===========================================================================
float safeAvg(float sum, uint16_t count) {
    return (count > 0) ? (sum / (float)count) : -9999.0f;
}
