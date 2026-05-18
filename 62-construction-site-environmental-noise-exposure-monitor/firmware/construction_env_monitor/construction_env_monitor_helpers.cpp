/*******************************************************************************
  construction_env_monitor_helpers.cpp — Sensor helpers, Notecard config
  helpers, and note-send helpers for the Construction Site Environmental &
  Noise Exposure Monitor.

  Global objects and runtime-config variables are defined in
  construction_env_monitor.ino and accessed here via the extern declarations
  in construction_env_monitor_helpers.h.
*******************************************************************************/

#include "construction_env_monitor_helpers.h"

// ── Notecard initialisation (runs once on first boot only) ───────────────────
// Sends hub.set with the product UID to handle the cold-boot I²C race via
// sendRequestWithRetry.  applyCardConfig() is called immediately after and
// re-sends hub.set with requestAndResponse (because lastReportMin is
// initialised to 0), so the outbound cadence is confirmed even if this
// first-boot attempt fails.
void notecardConfigure(void) {
    // Runtime guard: a blank PRODUCT_UID means the sketch was never
    // customised.  Attempting hub.set with an empty product string silently
    // misroutes data; bail early with a visible diagnostic instead.
    if (PRODUCT_UID[0] == '\0') {
#ifdef DEBUG_SERIAL
        DEBUG_SERIAL.println("[cfg] PRODUCT_UID is empty — skipping Notecard "
                             "configuration. Populate PRODUCT_UID before deploying.");
#endif
        return;
    }

    // hub.set — periodic mode.  Summary notes accumulate and sync every
    // cfgReportMin minutes.  Alert notes with sync:true bypass this window.
    J *req = notecard.newRequest("hub.set");
    if (!req) {
#ifdef DEBUG_SERIAL
        DEBUG_SERIAL.println("[cfg] hub.set request allocation failed on first boot.");
#endif
        return;
    }
    JAddStringToObject(req, "product",  PRODUCT_UID);
    JAddStringToObject(req, "mode",     "periodic");
    JAddNumberToObject(req, "outbound", (int)cfgReportMin);
    JAddNumberToObject(req, "inbound",  120);   // poll for env-var updates every 2 h
    // sendRequestWithRetry handles the cold-boot I²C race condition.
    // card.location.mode and note.template are NOT issued here — both are
    // handled by applyCardConfig() and defineTemplates(), which are called on
    // every wake, so a transient failure on first boot is retried automatically.
    notecard.sendRequestWithRetry(req, 5);
}

// ── Re-apply hub outbound cadence and GPS mode if env vars changed ────────────
// Called from both setup() and the loop() fallback path so that Notehub
// operator changes to report_interval_min or gps_interval_sec take effect
// without re-flashing.  State fields are advanced only after a confirmed
// successful requestAndResponse so a transient failure retries on the next wake.
void applyCardConfig(void) {
    // ── Re-apply hub outbound cadence if report_interval_min changed ───────
    // Fires on first boot (lastReportMin == 0) and any subsequent wake where
    // a previous attempt failed.  product is included in every hub.set so
    // the device is guaranteed to be provisioned even if the notecardConfigure()
    // call at first boot suffered a transient I²C failure.
    if (cfgReportMin != state.lastReportMin) {
        if (PRODUCT_UID[0] == '\0') {
#ifdef DEBUG_SERIAL
            DEBUG_SERIAL.println("[cfg] PRODUCT_UID is empty — skipping hub.set.");
#endif
        } else {
            J *req = notecard.newRequest("hub.set");
            if (!req) {
#ifdef DEBUG_SERIAL
                DEBUG_SERIAL.println("[cfg] hub.set allocation failed; will retry next wake.");
#endif
            } else {
                JAddStringToObject(req, "product",  PRODUCT_UID);
                JAddStringToObject(req, "mode",     "periodic");
                JAddNumberToObject(req, "outbound", (int)cfgReportMin);
                JAddNumberToObject(req, "inbound",  120);
                J *rsp = notecard.requestAndResponse(req);
                if (rsp != NULL && !notecard.responseError(rsp)) {
                    // Flush the current accumulation window before adopting the new
                    // cadence so env_summary.qo notes always represent exactly one
                    // configured interval.  sendSummary() returns true for an empty
                    // window (safe on first boot).  If the flush fails, lastReportMin
                    // stays at the old value and the full hub.set + flush sequence
                    // retries on the next wake, keeping state self-consistent.
                    if (sendSummary()) {
                        state.lastReportMin   = cfgReportMin;
                        state.reportCountdown = cfgReportMin * 60;
                    }
#ifdef DEBUG_SERIAL
                    else {
                        DEBUG_SERIAL.println("[cfg] window flush failed; hub.set will "
                                             "retry with flush on next wake.");
                    }
#endif
                }
#ifdef DEBUG_SERIAL
                else {
                    DEBUG_SERIAL.println("[cfg] hub.set failed; will retry next wake.");
                }
#endif
                if (rsp) notecard.deleteResponse(rsp);
            }
        }
    }

    // ── Re-apply GNSS acquisition cadence if gps_interval_sec changed ─────
    // Fires on every wake where lastGpsSec != cfgGpsSec — including first boot
    // (lastGpsSec == 0) and any wake where a previous attempt failed.  On
    // success, clamp gpsCountdown to the new interval so the new cadence takes
    // effect immediately.
    if (cfgGpsSec != state.lastGpsSec) {
        J *req = notecard.newRequest("card.location.mode");
        if (!req) {
#ifdef DEBUG_SERIAL
            DEBUG_SERIAL.println("[cfg] card.location.mode allocation failed; will retry next wake.");
#endif
        } else {
            JAddStringToObject(req, "mode",    "periodic");
            JAddNumberToObject(req, "seconds", (int)cfgGpsSec);
            J *rsp = notecard.requestAndResponse(req);
            if (rsp != NULL && !notecard.responseError(rsp)) {
                state.lastGpsSec = cfgGpsSec;
                if (state.gpsCountdown > cfgGpsSec) {
                    state.gpsCountdown = cfgGpsSec;
                }
            }
#ifdef DEBUG_SERIAL
            else {
                DEBUG_SERIAL.println("[cfg] card.location.mode failed; will retry next wake.");
            }
#endif
            if (rsp) notecard.deleteResponse(rsp);
        }
    }
}

// ── Note templates — fixed-width records shrink on-wire payload ~3–5× ────────
// Called on every wake (idempotent): the Notecard silently ignores a duplicate
// template definition for an already-registered notefile, so retrying on every
// boot costs only one I²C round-trip while guaranteeing templates are applied
// even after a transient failure on a previous wake.
void defineTemplates(void) {
    // env_summary.qo — rolling exposure summary per cfgReportMin window.
    J *req = notecard.newRequest("note.template");
    if (!req) {
#ifdef DEBUG_SERIAL
        DEBUG_SERIAL.println("[cfg] note.template allocation failed for "
                             NOTEFILE_SUMMARY "; will retry next wake.");
#endif
    } else {
        JAddStringToObject(req, "file", NOTEFILE_SUMMARY);
        JAddNumberToObject(req, "port", 50);
        J *body = JAddObjectToObject(req, "body");
        JAddNumberToObject(body, "pm25_avg",       14.1);  // 4-byte float
        JAddNumberToObject(body, "pm25_peak",      14.1);
        JAddNumberToObject(body, "pm10_avg",       14.1);
        JAddNumberToObject(body, "pm10_peak",      14.1);
        JAddNumberToObject(body, "db_a_avg",       14.1);
        JAddNumberToObject(body, "db_a_peak",      14.1);
        JAddNumberToObject(body, "samples",         22);   // 2-byte unsigned int
        JAddNumberToObject(body, "pm_samples",      22);   // 0 = all PM reads failed
        JAddNumberToObject(body, "voltage",        14.1);
        JAddNumberToObject(body, "lat",            14.1);
        JAddNumberToObject(body, "lon",            14.1);
        // location_valid mirrors gpsBootConfirmed: false until the GNSS has
        // confirmed a fresh fix at the current site (i.e. the fix timestamp
        // advanced since boot).  Downstream consumers should treat lat/lon as
        // potentially a cached position from a previous deployment site until
        // this field is true.  Per the note-c template spec, a TBOOL field is
        // declared by passing the literal boolean value 'true' — there is no
        // numeric type-hint for booleans.  See:
        // https://dev.blues.io/notecard/notecard-walkthrough/low-bandwidth-design/#understanding-template-data-types
        JAddBoolToObject(body,   "location_valid",  true);
        J *rsp = notecard.requestAndResponse(req);
        if (rsp == NULL || notecard.responseError(rsp)) {
#ifdef DEBUG_SERIAL
            DEBUG_SERIAL.println("[cfg] note.template for " NOTEFILE_SUMMARY " failed.");
#endif
        }
        if (rsp) notecard.deleteResponse(rsp);
    }

    // env_alert.qo — immediate-sync threshold-breach alert.
    //
    // IMPORTANT: note-c sizes a string template field by the byte length of
    // the sample string, not by any numeric comment.  The placeholder must be
    // at least as long as the longest actual alert value sent by sendAlert().
    // Alert values are: "pm25_high" (9), "pm10_high" (9), "db_a_high" (9).
    // "pm25_high___" (12 chars) gives three bytes of margin for future types.
    req = notecard.newRequest("note.template");
    if (!req) {
#ifdef DEBUG_SERIAL
        DEBUG_SERIAL.println("[cfg] note.template allocation failed for "
                             NOTEFILE_ALERT "; will retry next wake.");
#endif
    } else {
        JAddStringToObject(req, "file", NOTEFILE_ALERT);
        JAddNumberToObject(req, "port", 51);
        J *body = JAddObjectToObject(req, "body");
        JAddStringToObject(body, "alert",          "pm25_high___"); // 12-char field
        JAddNumberToObject(body, "value",          14.1);
        JAddNumberToObject(body, "threshold",      14.1);
        JAddNumberToObject(body, "lat",            14.1);
        JAddNumberToObject(body, "lon",            14.1);
        JAddBoolToObject(body,   "location_valid", true);  // TBOOL; see env_summary.qo
        J *rsp = notecard.requestAndResponse(req);
        if (rsp == NULL || notecard.responseError(rsp)) {
#ifdef DEBUG_SERIAL
            DEBUG_SERIAL.println("[cfg] note.template for " NOTEFILE_ALERT " failed.");
#endif
        }
        if (rsp) notecard.deleteResponse(rsp);
    }
}

// ── Fetch environment variable overrides from Notehub ────────────────────────
// env.get with no arguments returns all project/fleet/device env vars in
// the 'body' object.  Notehub delivers environment variables as strings, so
// each key is read with JGetString() and converted with strtoul() / strtof()
// before clamping.  Keys absent from the body return NULL from JGetString();
// those variables are left at their current (default or previously-set) values.
void fetchEnvOverrides(void) {
    J *req = notecard.newRequest("env.get");
    if (!req) return;
    J *rsp = notecard.requestAndResponse(req);
    if (rsp == NULL) return;

    J *body = JGetObject(rsp, "body");
    if (body != NULL) {
        const char *s;

        s = JGetString(body, "sample_interval_sec");
        if (s && s[0] != '\0') cfgSampleSec = clampU((uint32_t)strtoul(s, NULL, 10), 60, 3600);

        s = JGetString(body, "report_interval_min");
        if (s && s[0] != '\0') cfgReportMin = clampU((uint32_t)strtoul(s, NULL, 10), 5, 1440);

        s = JGetString(body, "pm25_alert_ug_m3");
        if (s && s[0] != '\0') cfgPm25Alert = clampF(strtof(s, NULL), 5.0f, 500.0f);

        s = JGetString(body, "pm10_alert_ug_m3");
        if (s && s[0] != '\0') cfgPm10Alert = clampF(strtof(s, NULL), 5.0f, 1000.0f);

        s = JGetString(body, "db_a_alert");
        if (s && s[0] != '\0') cfgDbAlert = clampF(strtof(s, NULL), 60.0f, 130.0f);

        s = JGetString(body, "gps_interval_sec");
        if (s && s[0] != '\0') cfgGpsSec = clampU((uint32_t)strtoul(s, NULL, 10), 3600, 86400);

        // db_cal_offset may legitimately be 0 — apply whenever the key is present.
        s = JGetString(body, "db_cal_offset");
        if (s && s[0] != '\0') cfgDbCalOffset = clampF(strtof(s, NULL), -20.0f, 20.0f);
    }
    notecard.deleteResponse(rsp);
}

// ── GPS position update from the Notecard's onboard GNSS ─────────────────────
// Returns true only when a boot-confirmed fresh fix is available.  Returns
// false on transient failures, when no fix has been acquired yet, and on the
// FIRST successful card.location response after a fresh power-on — that first
// response may be a cached position from a previous deployment site retained
// in the Notecard's GNSS hardware memory.
//
// Fix freshness is determined by comparing the GPS 'time' field (the epoch
// timestamp of the acquired fix) across successive calls:
//
//   • gpsBootSeenTime == 0 (first successful response since boot): record the
//     fix timestamp as a baseline and store the coordinates, but do NOT mark
//     gpsBootConfirmed — the fix may be stale from a previous site.  Return
//     false so gpsCountdown stays at zero and the next wake retries.
//
//   • fixTime > gpsBootSeenTime: the timestamp advanced, meaning the GNSS
//     acquired a genuinely new fix at the current site.  Mark gpsBootConfirmed,
//     update coordinates, return true so the caller can arm gpsCountdown.
//
//   • fixTime == gpsBootSeenTime: same cached fix, no change needed.
//
// Coordinates (siteLat/siteLon) are always updated on a valid non-zero
// response so the payload body reflects the best available position.  The
// companion 'location_valid' payload field mirrors gpsBootConfirmed so
// downstream consumers can distinguish a confirmed site fix from a
// potentially stale cached position.
bool updateGPS(void) {
    J *req = notecard.newRequest("card.location");
    if (!req) return false;
    J *rsp = notecard.requestAndResponse(req);
    if (rsp == NULL) return false;

    bool got_fix = false;
    const char *err = JGetString(rsp, "err");
    if (err == NULL || err[0] == '\0') {
        float    lat     = (float)JGetNumber(rsp, "lat");
        float    lon     = (float)JGetNumber(rsp, "lon");
        uint32_t fixTime = (uint32_t)JGetNumber(rsp, "time");
        if (lat != 0.0f || lon != 0.0f) {
            // Always store the best available coordinates so the payload
            // reflects the most recent position, even while unconfirmed.
            state.siteLat  = lat;
            state.siteLon  = lon;
            state.gpsValid = true;

            if (state.gpsBootSeenTime == 0) {
                // First successful GPS response since boot: record the fix
                // timestamp as a baseline.  Do not mark boot-confirmed yet —
                // this position may be a cached fix retained from a previous
                // deployment site.  Use 1 if fixTime is 0 to distinguish
                // "seen but unconfirmed" from "never seen" (gpsBootSeenTime==0).
                state.gpsBootSeenTime = (fixTime > 0) ? fixTime : 1;
                // got_fix stays false: gpsCountdown remains 0 so the next
                // wake calls card.location again and checks whether the
                // GNSS has acquired a newer fix at the current site.
            } else if (fixTime > state.gpsBootSeenTime) {
                // Timestamp advanced — the GNSS acquired a genuinely new fix
                // since boot.  The device is confirmed at this site.
                state.gpsBootSeenTime  = fixTime;
                state.gpsBootConfirmed = true;
                got_fix                = true;
            }
            // fixTime == gpsBootSeenTime: same cached fix, no update needed.
        }
    }
    notecard.deleteResponse(rsp);
    return got_fix;
}

// ── Read PM2.5 and PM10 from the Adafruit PMSA003I ───────────────────────────
// Returns true if at least one valid reading was obtained.
// pm25Out / pm10Out in µg/m³, standard-atmosphere correction.
bool readPmSensor(float &pm25Out, float &pm10Out) {
    float pm25Sum = 0.0f, pm10Sum = 0.0f;
    int   valid   = 0;

    for (int i = 0; i < PM_SAMPLE_COUNT; i++) {
        PM25_AQI_Data data;
        if (aqiSensor.read(&data)) {
            // pm25_standard / pm10_standard use standard-atmosphere corrections;
            // they provide a stable, comparable PM metric for trend monitoring.
            // Neither output is suitable for silica-compliance reporting.
            pm25Sum += (float)data.pm25_standard;
            pm10Sum += (float)data.pm10_standard;
            valid++;
        }
        delay(PM_SAMPLE_INTERVAL_MS);
    }

    if (valid == 0) return false;
    pm25Out = pm25Sum / (float)valid;
    pm10Out = pm10Sum / (float)valid;
    return true;
}

// ── Read mean dB(A) from the DFRobot SEN0232 over SOUND_SAMPLE_MS ────────────
// SEN0232 operating range: 3.3–5 V; powered from V+ (~3.7–4.2 V LiPo) the
// supply is within spec.  Vout 0.6–2.6 V maps to 30–130 dB (5 V calibration).
// Apply 'db_cal_offset' to zero-trim against a reference meter at commissioning.
// Note: V+ is not gated by the ATTN sleep path — the sensor draws quiescent
// current continuously; see file-header power notes in the .ino.
//
// Samples are accumulated as acoustic energy (10^(dB/10)) rather than as dB
// values before averaging.  Arithmetic-averaging dB values underestimates the
// true mean when levels vary: a brief 100 dB event is 100× more intense than
// 80 dB but a plain arithmetic mean of those two readings gives 90 dB instead
// of the energy-correct 97 dB.  Converting back to dB at the end
// (10·log10(meanEnergy)) gives the correct mean sound pressure level.
float readSoundLevelDb(void) {
    float    energySum = 0.0f;
    uint32_t count     = 0;
    uint32_t tEnd      = millis() + SOUND_SAMPLE_MS;

    while (millis() < tEnd) {
        float volts = (float)analogRead(PIN_SOUND_LEVEL)
                      * (ADC_REF_VOLTAGE / ADC_RESOLUTION);
        // Clamp to sensor's valid output range before applying the linear map.
        volts = constrain(volts, SEN0232_V_LOW, SEN0232_V_HIGH);
        float db = SEN0232_DB_LOW +
                   (volts - SEN0232_V_LOW) *
                   (SEN0232_DB_HIGH - SEN0232_DB_LOW) /
                   (SEN0232_V_HIGH - SEN0232_V_LOW);
        db += cfgDbCalOffset;
        // Accumulate in the linear energy domain.
        energySum += powf(10.0f, db / 10.0f);
        count++;
        delay(SOUND_SAMPLE_INTERVAL_MS);
    }
    if (count == 0) return 0.0f;
    // Convert mean energy back to dB(A).
    return 10.0f * log10f(energySum / (float)count);
}

// ── Send rolling exposure summary to env_summary.qo ──────────────────────────
// Returns true when the Note was successfully queued.  Window accumulators are
// reset only on success so that a failed queue retains the full window for
// retry on the next wake.  Returns true with no send when the window is empty
// (sampleCount == 0) so the caller still advances the report countdown.
bool sendSummary(void) {
    if (state.sampleCount == 0) return true;

    // Read battery voltage from the Notecard's onboard ADC.
    float voltage = 0.0f;
    J *voltReq = notecard.newRequest("card.voltage");
    if (voltReq) {
        J *rsp = notecard.requestAndResponse(voltReq);
        if (rsp != NULL) {
            voltage = (float)JGetNumber(rsp, "value");
            notecard.deleteResponse(rsp);
        }
    }

    // PM averages use pmSampleCount (valid sensor reads only) so that failed
    // reads do not dilute the window mean.  When no valid reads occurred in
    // the window, emit -9999.0 as an explicit invalid-data sentinel rather
    // than 0.0 — zero is a plausible environmental reading (clean air) and
    // would be misread as valid data by consumers that skip pm_samples.
    // dB average converts from the accumulated energy sum back to decibels.
    const float kPmInvalid = -9999.0f;
    float pm25Avg = (state.pmSampleCount > 0)
                    ? state.pm25Sum / (float)state.pmSampleCount : kPmInvalid;
    float pm10Avg = (state.pmSampleCount > 0)
                    ? state.pm10Sum / (float)state.pmSampleCount : kPmInvalid;
    float dbAvg   = (state.sampleCount > 0)
                    ? 10.0f * log10f(state.dbEnergySum / (float)state.sampleCount)
                    : 0.0f;

    J *req = notecard.newRequest("note.add");
    if (!req) {
#ifdef DEBUG_SERIAL
        DEBUG_SERIAL.println("[note] note.add allocation failed for env_summary.qo; "
                             "retaining window for retry.");
#endif
        return false;
    }
    JAddStringToObject(req, "file", NOTEFILE_SUMMARY);
    // full:true bypasses the omitempty rule that templated Notefiles enforce
    // on Notehub-side JSON serialisation.  Without it, fields whose value is
    // false / 0 / 0.0 / "" are stripped from the body — which would silently
    // erase 'location_valid: false' (the explicit "GPS not yet confirmed at
    // this site" signal), 'pm_samples: 0' (the "every PM read failed" sentinel
    // companion to pm25_avg/pm10_avg = -9999.0), and 'lat: 0.0 / lon: 0.0'
    // (no fix yet).  Setting full:true preserves all field values and keeps
    // the documented sentinel semantics intact.  See:
    // https://dev.blues.io/notecard/notecard-walkthrough/low-bandwidth-design/#use-of-in-templates
    JAddBoolToObject(req,   "full", true);
    J *body = JAddObjectToObject(req, "body");
    JAddNumberToObject(body, "pm25_avg",  pm25Avg);
    JAddNumberToObject(body, "pm25_peak", state.pm25Peak);
    JAddNumberToObject(body, "pm10_avg",  pm10Avg);
    JAddNumberToObject(body, "pm10_peak", state.pm10Peak);
    JAddNumberToObject(body, "db_a_avg",  dbAvg);
    JAddNumberToObject(body, "db_a_peak", state.dbPeak);
    JAddNumberToObject(body, "samples",    (int)state.sampleCount);
    // pm_samples is the count of wake cycles in which the PM sensor returned
    // at least one valid read.  When pm_samples == 0, every read in the window
    // failed; pm25_avg and pm10_avg are set to -9999.0 — an explicit sentinel
    // distinguishable from genuinely particulate-free air (0.0 µg/m³).
    JAddNumberToObject(body, "pm_samples", (int)state.pmSampleCount);
    JAddNumberToObject(body, "voltage",        voltage);
    JAddNumberToObject(body, "lat",            state.siteLat);
    JAddNumberToObject(body, "lon",            state.siteLon);
    // location_valid: false until the GNSS confirms a new fix at this site
    // (fix timestamp advanced since boot).  Treat lat/lon as potentially a
    // cached position from a previous deployment site while this is false.
    JAddBoolToObject(body,   "location_valid", state.gpsBootConfirmed);

    J *rsp = notecard.requestAndResponse(req);
    bool ok = (rsp != NULL) && !notecard.responseError(rsp);
    if (rsp) notecard.deleteResponse(rsp);

    if (ok) {
        // Reset window accumulators only after a confirmed successful queue.
        state.pm25Sum       = 0.0f;
        state.pm10Sum       = 0.0f;
        state.dbEnergySum   = 0.0f;
        state.pm25Peak      = 0.0f;
        state.pm10Peak      = 0.0f;
        state.dbPeak        = 0.0f;
        state.sampleCount   = 0;
        state.pmSampleCount = 0;
    }
#ifdef DEBUG_SERIAL
    else {
        DEBUG_SERIAL.println("[note] env_summary.qo queue failed; retaining window for retry.");
    }
#endif
    return ok;
}

// ── Send an immediate-sync alert to env_alert.qo ─────────────────────────────
// Returns true when the Note was successfully queued.  The caller arms the
// per-alert cooldown only on true so a transient I²C or allocation failure
// does not suppress the next alert for 30 minutes with nothing actually sent.
bool sendAlert(const char *type, float value, float threshold) {
    J *req = notecard.newRequest("note.add");
    if (!req) {
#ifdef DEBUG_SERIAL
        DEBUG_SERIAL.print("[note] note.add allocation failed for alert '");
        DEBUG_SERIAL.print(type);
        DEBUG_SERIAL.println("'; cooldown not armed, eligible for retry.");
#endif
        return false;
    }
    JAddStringToObject(req, "file", NOTEFILE_ALERT);
    JAddBoolToObject(req,   "sync", true);
    // full:true bypasses templated-Notefile omitempty so 'location_valid:
    // false' and any zero-valued lat/lon are preserved on Notehub.  See
    // sendSummary() for the full rationale.
    JAddBoolToObject(req,   "full", true);
    J *body = JAddObjectToObject(req, "body");
    JAddStringToObject(body, "alert",          type);
    JAddNumberToObject(body, "value",          value);
    JAddNumberToObject(body, "threshold",      threshold);
    JAddNumberToObject(body, "lat",            state.siteLat);
    JAddNumberToObject(body, "lon",            state.siteLon);
    JAddBoolToObject(body,   "location_valid", state.gpsBootConfirmed);

    J *rsp = notecard.requestAndResponse(req);
    bool ok = (rsp != NULL) && !notecard.responseError(rsp);
    if (rsp) notecard.deleteResponse(rsp);

#ifdef DEBUG_SERIAL
    if (!ok) {
        DEBUG_SERIAL.print("[note] alert '");
        DEBUG_SERIAL.print(type);
        DEBUG_SERIAL.println("' queue failed; cooldown not armed, eligible for retry.");
    }
#endif
    return ok;
}

// ── One complete sample / accumulate / alert / report cycle ──────────────────
// Cycle ordering: (1) age timers, (2) GPS re-acquire if due, (3) read sound
// level via ADC, (4) init and warm up PMSA003I, (5) read PM, (6) accumulate,
// (7) alert checks, (8) send summary if due.  Sound is sampled BEFORE PM
// sensor warm-up so the PMSA003I fan acoustic signature does not contaminate
// the dB(A) window — see the readSoundLevelDb() block below and README §9.
//
// PM_WARMUP_MS is applied on every cycle after a successful begin_I2C().
// Whether the ATTN sleep path cuts the Qwiic/3V3 rail between wakes is
// carrier-implementation-specific; always applying the full warm-up delay
// ensures readings are valid regardless of the carrier's power topology.
//
// All per-alert cooldowns and the GPS re-acquire countdown are aged (decremented)
// at the START of each cycle, before any rule evaluation, so that a timer freshly
// armed later in the same cycle is not immediately shortened — keeping effective
// cooldown and re-acquire intervals aligned with their configured values.
void runOneSampleCycle(void) {

    // ── Age timers before any rule evaluation ─────────────────────────────
    // Decrement alert cooldowns and the GPS countdown first, at the top of
    // the cycle, so that a timer freshly armed later in this same cycle (e.g.
    // an alert cooldown set a few lines below) is not immediately reduced.
    // This keeps effective dedup windows and GPS re-acquire intervals aligned
    // with their configured values rather than being one sample interval short.
    // gpsCountdown is clamped at zero (not allowed to wrap) to preserve the
    // "retry immediately until a valid fix is obtained" policy on first boot.
    state.pm25AlertCooldown = (state.pm25AlertCooldown > cfgSampleSec)
                               ? state.pm25AlertCooldown - cfgSampleSec : 0;
    state.pm10AlertCooldown = (state.pm10AlertCooldown > cfgSampleSec)
                               ? state.pm10AlertCooldown - cfgSampleSec : 0;
    state.dbAlertCooldown   = (state.dbAlertCooldown   > cfgSampleSec)
                               ? state.dbAlertCooldown   - cfgSampleSec : 0;
    state.gpsCountdown      = (state.gpsCountdown      > cfgSampleSec)
                               ? state.gpsCountdown      - cfgSampleSec : 0;

    // ── GPS re-acquire if due ─────────────────────────────────────────────
    // gpsCountdown is held at zero until a valid fix is confirmed, so the
    // host retries on consecutive wakes rather than waiting a full interval
    // with absent coordinates in the payload.
    if (state.gpsCountdown == 0) {
        if (updateGPS()) {
            state.gpsCountdown = cfgGpsSec;
        }
        // On no fix: gpsCountdown stays 0, next wake retries immediately.
    }

    // ── Read sound level FIRST ────────────────────────────────────────────
    // The SEN0232 is always powered from V+ (not gated by the ATTN sleep
    // path) and can be sampled immediately after the GPS update.  Sampling
    // sound before calling begin_I2C() and the PM warm-up delay bounds the
    // PM fan's acoustic contribution to at most the host boot + setup() time
    // (~5–15 s); this minimises but does not eliminate contamination.
    // Whether the PMSA003I's Qwiic 3V3 rail is cut during ATTN sleep is
    // carrier-implementation-specific: if it is not cut, the fan runs
    // continuously and the audio channel is contaminated throughout.
    // Complete elimination requires an explicit GPIO-controlled load switch
    // to gate PMSA003I power — see README §9.
    float dbA = readSoundLevelDb();

    // ── Initialise PM sensor over I²C ────────────────────────────────────
    // begin_I2C() is called after the sound window completes so the PM
    // sensor's warm-up phase does not overlap the dB(A) sample.
    // It is still called on every cycle to detect whether the sensor is
    // responsive before committing the warm-up delay.
    bool pmReady = aqiSensor.begin_I2C();
    if (!pmReady) {
#ifdef DEBUG_SERIAL
        DEBUG_SERIAL.println("[pm] begin_I2C() failed; will retry next wake.");
#endif
    }

    // ── Read PM2.5 / PM10 ─────────────────────────────────────────────────
    float pm25 = -1.0f, pm10 = -1.0f;
    if (pmReady) {
        // Always allow the PMSA003I fan and laser to stabilise before reading.
        // Whether the ATTN sleep path cuts the Qwiic 3V3 rail between wakes is
        // carrier-implementation-specific; applying PM_WARMUP_MS unconditionally
        // guarantees valid readings regardless of whether the sensor cold-started
        // or was already running.
        delay(PM_WARMUP_MS);
        readPmSensor(pm25, pm10);
    }

    // ── Accumulate window stats ───────────────────────────────────────────
    if (pm25 >= 0.0f) {
        // pm25 and pm10 come from the same sensor read: both valid together.
        state.pm25Sum += pm25;
        if (pm25 > state.pm25Peak) state.pm25Peak = pm25;
        state.pm10Sum += pm10;
        if (pm10 > state.pm10Peak) state.pm10Peak = pm10;
        state.pmSampleCount++;      // count valid PM reads independently
    }
    // Accumulate dB as acoustic energy so sendSummary() can compute a
    // physically correct mean sound pressure level (see readSoundLevelDb()).
    state.dbEnergySum += powf(10.0f, dbA / 10.0f);
    if (dbA > state.dbPeak) state.dbPeak = dbA;
    state.sampleCount++;            // count every cycle for dB average

    // ── Per-sample alert checks ───────────────────────────────────────────
    // Arm the cooldown only after a confirmed successful queue so a transient
    // failure does not suppress the next alert for 30 minutes with nothing
    // actually transmitted.  Cooldowns are aged at the top of this function,
    // so a freshly armed value will not be decremented again until next cycle.
    if (pm25 >= 0.0f && pm25 >= cfgPm25Alert && state.pm25AlertCooldown == 0) {
        if (sendAlert("pm25_high", pm25, cfgPm25Alert)) {
            state.pm25AlertCooldown = ALERT_COOLDOWN_SEC;
        }
    }
    if (pm10 >= 0.0f && pm10 >= cfgPm10Alert && state.pm10AlertCooldown == 0) {
        if (sendAlert("pm10_high", pm10, cfgPm10Alert)) {
            state.pm10AlertCooldown = ALERT_COOLDOWN_SEC;
        }
    }
    if (dbA >= cfgDbAlert && state.dbAlertCooldown == 0) {
        if (sendAlert("db_a_high", dbA, cfgDbAlert)) {
            state.dbAlertCooldown = ALERT_COOLDOWN_SEC;
        }
    }

    // ── Send summary if report window has elapsed ─────────────────────────
    // reportCountdown is either fired (reset to cfgReportMin * 60) or
    // decremented by cfgSampleSec — never both in the same cycle — so no
    // separate aging pass is needed here.  saveStateAndSleep() trims the
    // sleep duration by g_lastActiveSec, making the total cycle time (active
    // + sleep) equal cfgSampleSec, so decrementing by cfgSampleSec tracks
    // elapsed wall time correctly.  Advancing the countdown only after a
    // confirmed successful queue means a transient note.add failure retries
    // on the very next wake rather than skipping a window.
    if (state.reportCountdown <= cfgSampleSec) {
        if (sendSummary()) {
            state.reportCountdown = cfgReportMin * 60;
        }
    } else {
        state.reportCountdown -= cfgSampleSec;
    }
}

// ── Serialise state to Notecard flash and cut host power via card.attn ────────
// sleepSec is the configured sample interval minus the seconds spent awake
// this cycle, so the Notecard wakes the host at the intended cadence rather
// than (cfgSampleSec + activeSec) seconds after the cycle started.
void saveStateAndSleep(uint32_t sleepSec) {
    NotePayloadDesc payload = {0, 0, 0};
    NotePayloadAddSegment(&payload, STATE_SEG_ID, &state, sizeof(state));
    // Persists state to Notecard flash; card.attn cuts the host power rail for
    // sleepSec seconds.  Next wake restores state via NotePayloadRetrieveAfterSleep.
    NotePayloadSaveAndSleep(&payload, sleepSec, NULL);
    // Returns here only if the ATTN path is absent or the power rail was not cut.
}

// ── Helpers ───────────────────────────────────────────────────────────────────
float clampF(float v, float lo, float hi) {
    return (v < lo) ? lo : (v > hi) ? hi : v;
}
uint32_t clampU(uint32_t v, uint32_t lo, uint32_t hi) {
    return (v < lo) ? lo : (v > hi) ? hi : v;
}
