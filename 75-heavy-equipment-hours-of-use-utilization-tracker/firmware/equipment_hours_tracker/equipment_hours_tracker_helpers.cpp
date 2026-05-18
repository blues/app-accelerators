/*******************************************************************************
 * equipment_hours_tracker_helpers.cpp
 *
 * Global variable definitions and helper-function implementations for the
 * Heavy Equipment Hours-of-Use & Utilization Tracker firmware.
 * See equipment_hours_tracker_helpers.h for types and prototypes.
 ******************************************************************************/

#include "equipment_hours_tracker_helpers.h"

// ── Global definitions ────────────────────────────────────────────────────────
Notecard          notecard;
Adafruit_LSM6DSOX sox;

PersistState g_s;
const char   SEG_ID[] = "EQHRS";

// Runtime env overrides — static initialisers provide the compile-time defaults,
// but fetchEnvOverrides() seeds these from g_s.applied_* on every wake before
// calling env.get, so a transient miss never reverts a previously-applied value.
float    g_vib_run_mg           = VIB_RUN_MG_DEFAULT;
float    g_vib_cv_max           = VIB_CV_MAX_DEFAULT;
uint32_t g_summary_interval_min = SUMMARY_INTERVAL_MIN;
float    g_fence_lat            = 0.0f;
float    g_fence_lon            = 0.0f;
uint32_t g_fence_radius_m       = 0;

// ── Tri-state env result ──────────────────────────────────────────────────────
// ENV_OK    — a non-empty text field was returned; parsed value written to *out.
// ENV_UNSET — variable is explicitly deleted in Notehub (request succeeded but
//             text field is absent or empty); caller should revert to the
//             compile-time default so Notehub deletions actually take effect.
// ENV_ERROR — transport / I²C failure (no response, or "err" field present);
//             caller should leave the last-good seeded value unchanged.
//
// Collapsing ENV_UNSET and ENV_ERROR into one 'false' path (the prior design)
// made thresholds, geofence parameters, and summary cadence sticky indefinitely:
// deleting a Notehub variable could never revert a setting to its default.
typedef enum : int8_t { ENV_ERROR = -1, ENV_UNSET = 0, ENV_OK = 1 } EnvResult;

// ── File-local env-read helper ────────────────────────────────────────────────
// Issues a single env.get request.  Frees the response unconditionally; writes
// the parsed double to *out only on ENV_OK.
//
// IMPORTANT: JGetString returns a pointer into the JSON object's heap.
// atof(v) MUST be called before deleteResponse(rsp) frees that heap; calling
// atof on a dangling pointer after the free is undefined behaviour that can
// produce intermittent garbage values and make env overrides appear flaky.
static EnvResult envReadDouble(const char *name, double &out) {
    J *req = notecard.newRequest("env.get");
    JAddStringToObject(req, "name", name);
    J *rsp = notecard.requestAndResponse(req);
    if (!rsp) return ENV_ERROR;
    const char *err = JGetString(rsp, "err");
    bool has_err = (err && *err != '\0');
    const char *v = JGetString(rsp, "text");
    bool has_val  = (v && *v != '\0');
    double parsed = has_val ? atof(v) : 0.0;  // parse before freeing the JSON object
    notecard.deleteResponse(rsp);              // v is now invalid — do not use after this line
    if (has_err) return ENV_ERROR;
    if (has_val) { out = parsed; return ENV_OK; }
    return ENV_UNSET;
}

// ── Response-checking helper ──────────────────────────────────────────────────
// Sends a pre-built request and returns true only if the Notecard replies
// without an "err" field.  Frees the response.  Used for all critical
// configuration and data-path requests where a silent failure is unacceptable.
bool checkedRequest(J *req) {
    J *rsp = notecard.requestAndResponse(req);
    if (!rsp) return false;
    const char *err = JGetString(rsp, "err");
    bool ok = (!err || *err == '\0');
    if (!ok) { Serial.print("[ERR] "); Serial.println(err); }
    notecard.deleteResponse(rsp);
    return ok;
}

// ── Env-var clamping helpers ──────────────────────────────────────────────────
// A bad fleet-level env var must never make the classifier unusable or drive an
// integer outside a safe operating range; return fallback when out of bounds.
float clampF(double v, float minv, float maxv, float fallback) {
    if (v < (double)minv || v > (double)maxv) return fallback;
    return (float)v;
}
uint32_t clampU32(long v, uint32_t minv, uint32_t maxv, uint32_t fallback) {
    if (v < (long)minv || v > (long)maxv) return fallback;
    return (uint32_t)v;
}

// ── One-time Notecard configuration (cold boot only) ─────────────────────────
// Returns true only after every request is confirmed by the Notecard so that
// g_s.configured is not set when a transient I²C failure leaves configuration
// incomplete.
bool notecardConfigure(void) {
    // Runtime guard: an empty PRODUCT_UID is a first-light misconfiguration.
    // A compile-time #pragma message is easy to miss; a loud serial error is not.
    if (*PRODUCT_UID == '\0') {
        Serial.println("[CFG] PRODUCT_UID is empty — set it to your Notehub ProductUID before flashing");
        return false;
    }

    // hub.set: sendRequestWithRetry handles the cold-boot I²C race condition
    // where the Notecard may not yet be ready to accept commands.  The return
    // value is checked so that a failed hub.set causes a retry on the next wake
    // rather than leaving the device with an invalid project association.
    J *req = notecard.newRequest("hub.set");
    JAddStringToObject(req, "product",  PRODUCT_UID);
    JAddStringToObject(req, "mode",     "periodic");
    JAddNumberToObject(req, "outbound", SUMMARY_INTERVAL_MIN); // daily outbound
    JAddNumberToObject(req, "inbound",  480);                   // 8-hour env-var pull
    if (!notecard.sendRequestWithRetry(req, 10)) {
        Serial.println("[CFG] hub.set failed");
        return false;
    }

    // Periodic GPS: acquire a fix every GPS_PERIOD_SECONDS (15 min).
    req = notecard.newRequest("card.location.mode");
    JAddStringToObject(req, "mode",    "periodic");
    JAddNumberToObject(req, "seconds", GPS_PERIOD_SECONDS);
    if (!checkedRequest(req)) return false;

    // Heartbeat tracking: emit _track.qo every GPS_HEARTBEAT_HOURS even when
    // stationary so the asset remains visible on the map.
    req = notecard.newRequest("card.location.track");
    JAddBoolToObject(req, "start",     true);
    JAddBoolToObject(req, "heartbeat", true);
    JAddNumberToObject(req, "hours",   GPS_HEARTBEAT_HOURS);
    if (!checkedRequest(req)) return false;

    // Optionally disable the Notecard's internal accelerometer to avoid
    // interference with the external LSM6DSOX and save ~0.5 mA.
    // IMPORTANT: the interaction between card.motion.mode {"stop":true} and
    // periodic GPS / geofencing on the Notecard for Skylo (NOTE-NBGLWX) has not
    // been bench-validated.  The macro is undefined by default; enable it only
    // after confirming that all periodic/geofence behaviour remains intact on
    // your target Notecard firmware.  See DISABLE_NOTECARD_MOTION in helpers.h.
#ifdef DISABLE_NOTECARD_MOTION
    req = notecard.newRequest("card.motion.mode");
    JAddBoolToObject(req, "stop", true);
    if (!checkedRequest(req)) return false;
#endif

    return true;
}

// ── Note templates — compact format required for Notecard for Skylo ───────────
// Compact templates store notes as fixed-length binary records, reducing wire
// size ~3–5× vs. free-form JSON.  Satellite packets are limited to 256 bytes.
// _lat / _lon are compact-template keywords auto-filled by the Notecard GPS.
// Returns true only after both templates are confirmed by the Notecard.
bool defineTemplates(void) {
    // equip_summary.qo — daily engine-hours summary
    // 14.1 = 4-byte IEEE-754 float; 12.1 = 2-byte float; 12 = 2-byte integer
    J *req = notecard.newRequest("note.template");
    JAddStringToObject(req, "file",   "equip_summary.qo");
    JAddNumberToObject(req, "port",    50);
    JAddStringToObject(req, "format", "compact");
    J *body = JAddObjectToObject(req, "body");
    JAddNumberToObject(body, "run_h",       14.1);
    JAddNumberToObject(body, "run_h_total", 14.1);
    JAddNumberToObject(body, "transport_h", 14.1);
    JAddNumberToObject(body, "bat_v",       12.1);
    JAddNumberToObject(body, "fault_ct",    12);   // event-queue overflow counter (2-byte int)
    JAddNumberToObject(body, "_lat",        14.1);
    JAddNumberToObject(body, "_lon",        14.1);
    if (!checkedRequest(req)) return false;

    // equip_event.qo — state-change events; hub.sync is issued after each
    // successful note.add to request prompt delivery.
    // The sample string for "event" must be the longest value that will ever be
    // stored so the compact template reserves exactly the right number of bytes.
    // "transport_start" (15 chars) is the longest of the four event names.
    // 14.1 = 4-byte IEEE-754 float; 14 = 4-byte signed integer (fits Unix epoch).
    req = notecard.newRequest("note.template");
    JAddStringToObject(req, "file",   "equip_event.qo");
    JAddNumberToObject(req, "port",    51);
    JAddStringToObject(req, "format", "compact");
    body = JAddObjectToObject(req, "body");
    JAddStringToObject(body, "event",       "transport_start");  // longest event name
    JAddNumberToObject(body, "session_min", 14.1);
    JAddNumberToObject(body, "run_h_total", 14.1);
    JAddNumberToObject(body, "epoch",       14);    // 4-byte int: Unix transition timestamp (s)
    JAddNumberToObject(body, "_lat",        14.1);
    JAddNumberToObject(body, "_lon",        14.1);
    if (!checkedRequest(req)) return false;

    return true;
}

// ── Env-var overrides — refetched on every wake ───────────────────────────────
//
// Step 1 — Seed runtime globals from the last-good env reads persisted in
// g_s.applied_*.  This ensures that a transient env.get miss on any wake
// leaves previously-applied tuning and fence parameters intact instead of
// reverting to compile-time defaults.  Seeded values are range-checked so
// that stale PersistState bytes from a firmware upgrade cannot produce
// out-of-range behaviour.
//
// Step 2 — Issue env.get for each tunable.  A successful read with a valid
// value overwrites the seed AND updates g_s.applied_* so the next wake
// inherits it.  A failed or empty read leaves the seeded value unchanged.
void fetchEnvOverrides(void) {
    // ── Step 1: seed from last-good env reads ─────────────────────────────────
    g_vib_run_mg = (g_s.applied_vib_run_mg >= 1.0f &&
                    g_s.applied_vib_run_mg <= 500.0f)
                   ? g_s.applied_vib_run_mg : VIB_RUN_MG_DEFAULT;

    g_vib_cv_max = (g_s.applied_vib_cv_max >= 0.05f &&
                    g_s.applied_vib_cv_max <= 2.0f)
                   ? g_s.applied_vib_cv_max : VIB_CV_MAX_DEFAULT;

    g_summary_interval_min = (g_s.applied_summary_interval_min >= 60 &&
                               g_s.applied_summary_interval_min <= 44640)
                              ? g_s.applied_summary_interval_min : SUMMARY_INTERVAL_MIN;

    // Fence seeds: lat/lon range-checked; radius capped at max (0 is valid sentinel).
    g_fence_lat = (g_s.applied_env_fence_lat >= -90.0f &&
                   g_s.applied_env_fence_lat <=  90.0f)
                  ? g_s.applied_env_fence_lat : 0.0f;
    g_fence_lon = (g_s.applied_env_fence_lon >= -180.0f &&
                   g_s.applied_env_fence_lon <=  180.0f)
                  ? g_s.applied_env_fence_lon : 0.0f;
    g_fence_radius_m = (g_s.applied_env_fence_radius_m <= GEOFENCE_RADIUS_MAX_M)
                       ? g_s.applied_env_fence_radius_m : 0;

    // ── Step 2: fetch fresh values ────────────────────────────────────────────
    // ENV_OK    → apply and persist the new value in applied_*.
    // ENV_UNSET → variable was explicitly deleted in Notehub; clear applied_*
    //             and revert the runtime global to the compile-time default.
    // ENV_ERROR → I²C / transport failure; leave the seeded value untouched.
    double val;
    EnvResult er;

    er = envReadDouble("vib_run_mg", val);
    if (er == ENV_OK) {
        float clamped          = clampF(val, 1.0f, 500.0f, VIB_RUN_MG_DEFAULT);
        g_vib_run_mg           = clamped;
        g_s.applied_vib_run_mg = clamped;
    } else if (er == ENV_UNSET) {
        g_vib_run_mg           = VIB_RUN_MG_DEFAULT;
        g_s.applied_vib_run_mg = 0.0f;  // 0 = "never set; use compile-time default"
    }

    er = envReadDouble("vib_cv_max", val);
    if (er == ENV_OK) {
        // CV = σ/μ; below ~0.05 is unreachably stable; above 2.0 mis-classifies
        // nearly all transport as running.
        float clamped          = clampF(val, 0.05f, 2.0f, VIB_CV_MAX_DEFAULT);
        g_vib_cv_max           = clamped;
        g_s.applied_vib_cv_max = clamped;
    } else if (er == ENV_UNSET) {
        g_vib_cv_max           = VIB_CV_MAX_DEFAULT;
        g_s.applied_vib_cv_max = 0.0f;
    }

    // Geofence params: each env var is persisted independently on a successful
    // read so a partial read (e.g. only lat arrived) doesn't corrupt the others.
    // applyGeofenceIfChanged() validates that all three are coherent before
    // issuing a Notecard call.
    er = envReadDouble("geofence_lat", val);
    if (er == ENV_OK && val >= -90.0 && val <= 90.0) {
        g_fence_lat               = (float)val;
        g_s.applied_env_fence_lat = (float)val;
    } else if (er == ENV_UNSET) {
        g_fence_lat               = 0.0f;
        g_s.applied_env_fence_lat = 0.0f;
    }

    er = envReadDouble("geofence_lon", val);
    if (er == ENV_OK && val >= -180.0 && val <= 180.0) {
        g_fence_lon               = (float)val;
        g_s.applied_env_fence_lon = (float)val;
    } else if (er == ENV_UNSET) {
        g_fence_lon               = 0.0f;
        g_s.applied_env_fence_lon = 0.0f;
    }

    er = envReadDouble("geofence_radius_m", val);
    if (er == ENV_OK) {
        // Validate signedness BEFORE any unsigned cast.  A negative value
        // such as -1 wraps to 0xFFFFFFFF when cast directly to uint32_t; that
        // huge number then clamps to GEOFENCE_RADIUS_MAX_M (50 km), silently
        // activating the maximum-size fence on what was an invalid operator input.
        if (val < 0.0) {
            Serial.print("[ENV] geofence_radius_m="); Serial.print(val, 1);
            Serial.println(" is negative — ignored");
        } else {
            uint32_t r = (uint32_t)val;
            // Clamp non-zero radii to the declared operating range.  Zero is the
            // sentinel meaning "no fence" and is never clamped.  An out-of-range
            // value (e.g. a typo in a fleet env var) is forced to the nearest bound
            // and logged so the operator can diagnose it without guessing.
            if (r > 0) {
                if (r < GEOFENCE_RADIUS_MIN_M) {
                    Serial.print("[ENV] geofence_radius_m="); Serial.print(r);
                    Serial.print(" below minimum — clamped to "); Serial.println(GEOFENCE_RADIUS_MIN_M);
                    r = GEOFENCE_RADIUS_MIN_M;
                } else if (r > GEOFENCE_RADIUS_MAX_M) {
                    Serial.print("[ENV] geofence_radius_m="); Serial.print(r);
                    Serial.print(" above maximum — clamped to "); Serial.println(GEOFENCE_RADIUS_MAX_M);
                    r = GEOFENCE_RADIUS_MAX_M;
                }
            }
            g_fence_radius_m               = r;
            g_s.applied_env_fence_radius_m = r;
        }
    } else if (er == ENV_UNSET) {
        g_fence_radius_m               = 0;
        g_s.applied_env_fence_radius_m = 0;
    }

    // Outbound cadence: only issue hub.set when the value actually changes, and
    // only persist the new cadence after the Notecard confirms the request so
    // the local summary timer and the Notecard outbound cadence stay in sync.
    er = envReadDouble("summary_interval_min", val);
    if (er == ENV_OK) {
        uint32_t new_interval = clampU32((long)val, 60, 44640, g_summary_interval_min);
        if (new_interval != g_summary_interval_min) {
            J *hreq = notecard.newRequest("hub.set");
            JAddNumberToObject(hreq, "outbound", (int)new_interval);
            if (checkedRequest(hreq)) {
                g_summary_interval_min           = new_interval;
                g_s.applied_summary_interval_min = new_interval;
                Serial.print("[ENV] summary_interval_min updated to "); Serial.println(new_interval);
            }
        }
    } else if (er == ENV_UNSET) {
        // Variable deleted in Notehub: revert to the compile-time default and
        // update the Notecard outbound cadence if it had been changed.
        if (g_summary_interval_min != SUMMARY_INTERVAL_MIN) {
            J *hreq = notecard.newRequest("hub.set");
            JAddNumberToObject(hreq, "outbound", SUMMARY_INTERVAL_MIN);
            if (checkedRequest(hreq)) {
                g_summary_interval_min           = SUMMARY_INTERVAL_MIN;
                g_s.applied_summary_interval_min = 0;  // 0 = "never set; use default"
                Serial.println("[ENV] summary_interval_min reverted to compile-time default");
            }
        }
    }
}

// ── Geofence — reconfigure Notecard if env-var fence parameters have changed ──
// Three env vars must ALL be set together: geofence_lat, geofence_lon, and
// geofence_radius_m.  Leaving lat/lon at the default 0,0 while setting a
// non-zero radius is treated as misconfigured and the fence is not applied.
//
// The fence centre (lat/lon) AND radius are persisted in PersistState so that
// a radius-only change is detected and re-applied on the next wake.
//
// When geofence_radius_m is set to 0 after a fence was active, this function
// explicitly re-issues card.location.mode without lat/lon/max parameters, which
// removes the geofence configuration from the Notecard.  Without this explicit
// clear, a previously-applied fence would remain active even after the operator
// sets geofence_radius_m=0.
//
// Cached state (fence_lat / fence_lon / fence_radius_m / fence_was_active) is
// only updated after the Notecard confirms the request so that a transient
// failure causes a retry on the next wake rather than silently sticking.
void applyGeofenceIfChanged(void) {
    if (g_fence_radius_m == 0) {
        if (g_s.fence_was_active) {
            // Re-apply periodic mode without fence parameters to clear the fence.
            J *req = notecard.newRequest("card.location.mode");
            JAddStringToObject(req, "mode",    "periodic");
            JAddNumberToObject(req, "seconds", GPS_PERIOD_SECONDS);
            if (checkedRequest(req)) {
                g_s.fence_was_active = false;
                g_s.fence_lat        = 0.0f;
                g_s.fence_lon        = 0.0f;
                g_s.fence_radius_m   = 0;
                Serial.println("[GEO] Fence cleared");
            }
        }
        return;
    }

    // Validate lat/lon: both must be non-zero and within WGS-84 range.
    // The defaults (0,0) map to the Gulf of Guinea — applying a fence there is
    // almost certainly a misconfiguration.  Require all three parameters.
    bool lat_ok = (fabsf(g_fence_lat) > 0.0001f) &&
                  (g_fence_lat >= -90.0f) && (g_fence_lat <= 90.0f);
    bool lon_ok = (fabsf(g_fence_lon) > 0.0001f) &&
                  (g_fence_lon >= -180.0f) && (g_fence_lon <= 180.0f);
    if (!lat_ok || !lon_ok) {
        Serial.println("[GEO] Skipped: set geofence_lat, geofence_lon, and geofence_radius_m together");
        return;
    }

    // Skip if all three cached values match — avoids redundant Notecard calls.
    if (fabsf(g_fence_lat - g_s.fence_lat) < 0.0001f &&
        fabsf(g_fence_lon - g_s.fence_lon) < 0.0001f &&
        g_fence_radius_m == g_s.fence_radius_m) return;

    J *req = notecard.newRequest("card.location.mode");
    JAddStringToObject(req, "mode",    "periodic");
    JAddNumberToObject(req, "seconds", GPS_PERIOD_SECONDS);
    JAddNumberToObject(req, "lat",     g_fence_lat);
    JAddNumberToObject(req, "lon",     g_fence_lon);
    JAddNumberToObject(req, "max",     (int)g_fence_radius_m);
    JAddNumberToObject(req, "minutes", 2);  // confirm outside fence for 2 min
    if (checkedRequest(req)) {
        g_s.fence_lat        = g_fence_lat;
        g_s.fence_lon        = g_fence_lon;
        g_s.fence_radius_m   = g_fence_radius_m;
        g_s.fence_was_active = true;
        Serial.print("[GEO] Fence: lat="); Serial.print(g_fence_lat, 5);
        Serial.print(" lon=");            Serial.print(g_fence_lon, 5);
        Serial.print(" r=");              Serial.println(g_fence_radius_m);
    }
}

// ── Vibration classifier ──────────────────────────────────────────────────────
// Collects VIB_SAMPLE_COUNT samples at 104 Hz (~2 s).
// Computes residual acceleration magnitude (|a| − 1g) in milli-g,
// then derives RMS and CV = σ/μ over the window.
//
// Engine idle (diesel, 700 RPM): periodic ~11.7 Hz vibration → low CV (0.10–0.25)
// Transport (truck, road): aperiodic shock → high CV (0.50–1.0+)
EquipState classifyVibration(void) {
    const float G = 9.806f;
    float sum = 0.0f, sum_sq = 0.0f;

    for (int i = 0; i < VIB_SAMPLE_COUNT; i++) {
        sensors_event_t accel, gyro, temp;
        sox.getEvent(&accel, &gyro, &temp);
        float ax = accel.acceleration.x, ay = accel.acceleration.y, az = accel.acceleration.z;
        float delta_mg = fabsf(sqrtf(ax*ax + ay*ay + az*az) - G) * 1000.0f / G;
        sum    += delta_mg;
        sum_sq += delta_mg * delta_mg;
        // sox.getEvent() takes ~1–2 ms; budget ~7.5 ms more → ~104 Hz net rate
        delayMicroseconds(7500);
    }

    const float n  = (float)VIB_SAMPLE_COUNT;
    float mean     = sum / n;
    float var      = (sum_sq / n) - (mean * mean);
    float cv       = (mean > 1.0f) ? sqrtf(var > 0.0f ? var : 0.0f) / mean : 1.0f;
    float rms      = sqrtf(sum_sq / n);

    const char *label = (rms < g_vib_run_mg) ? "IDLE"
                      : (cv  < g_vib_cv_max)  ? "RUNNING" : "TRANSPORT";
    Serial.print("[VIB] rms="); Serial.print(rms, 1);
    Serial.print("mg cv=");     Serial.print(cv, 3);
    Serial.print(" → ");        Serial.println(label);

    if (rms < g_vib_run_mg) return ST_IDLE;
    return (cv < g_vib_cv_max) ? ST_RUNNING : ST_TRANSPORT;
}

// ── Hour accumulator — credits prev_state for the actual elapsed time ─────────
// Using the real wall-clock delta between wakes (rather than the nominal
// SAMPLE_INTERVAL_SEC) keeps the software hour meter aligned with elapsed time.
// Each cycle actually runs slightly longer than the 30 s sleep due to ~2 s of
// accelerometer capture and additional I²C/Notecard overhead; accumulating
// nominal sleep durations therefore systematically undercounts active time by
// several percent — material for a billing / warranty / maintenance application.
//
// Fall-back to SAMPLE_INTERVAL_SEC when:
//   • clock is unavailable (now == 0, i.e. no cellular/GPS sync yet)
//   • no previous sample epoch is recorded (first wake after cold boot)
//   • the computed delta is implausibly large (time-sync jump or stale epoch
//     after a reboot) — capped at 3× nominal to protect against runaway credits
void updateHourAccumulator(uint32_t now) {
    uint32_t elapsed_sec = SAMPLE_INTERVAL_SEC;  // nominal fallback
    if (now > 0 && g_s.last_sample_epoch > 0 && now > g_s.last_sample_epoch) {
        uint32_t diff = now - g_s.last_sample_epoch;
        if (diff <= (uint32_t)SAMPLE_INTERVAL_SEC * 3u) elapsed_sec = diff;
    }
    const float delta_h = (float)elapsed_sec / 3600.0f;
    if      (g_s.prev_state == ST_RUNNING)   { g_s.run_h_today += delta_h; g_s.run_h_total += delta_h; }
    else if (g_s.prev_state == ST_TRANSPORT) { g_s.transport_h_today += delta_h; }
    if (now > 0) g_s.last_sample_epoch = now;
}

// ── Pending-event ring-buffer helpers ─────────────────────────────────────────

// Adds a new transition record to the tail of the ring buffer.
// If the buffer is already full (PENDING_QUEUE_DEPTH consecutive wakes without
// a Notecard acknowledgement), the overflow is logged and the new record is
// dropped rather than overwriting the oldest billable evidence.
// Returns true if the event was successfully enqueued.
bool enqueueEvent(const char *tag, uint32_t epoch,
                  float session_min, float run_h_total) {
    if (g_s.evq_count >= PENDING_QUEUE_DEPTH) {
        Serial.print("[EVENT] queue full — dropping "); Serial.println(tag);
        return false;
    }
    uint8_t tail = (g_s.evq_head + g_s.evq_count) % PENDING_QUEUE_DEPTH;
    PendingEvent &e = g_s.evq[tail];
    strncpy(e.tag, tag, sizeof(e.tag) - 1);
    e.tag[sizeof(e.tag) - 1] = '\0';
    e.epoch       = epoch;
    e.session_min = session_min;
    e.run_h_total = run_h_total;
    g_s.evq_count++;
    return true;
}

// Attempts to deliver the oldest pending event (ring-buffer head) to the
// Notecard.  On success the head pointer advances and the count decrements.
// Returns true if the queue was empty (nothing to send) or the oldest event
// was successfully acknowledged.  Returns false if the send failed; the head
// is not advanced and the event will be retried on the next wake.
bool sendNextPendingEvent(void) {
    if (g_s.evq_count == 0) return true;

    PendingEvent &e = g_s.evq[g_s.evq_head];

    // note.add on a .qo Notefile is a pure append — the Notecard API does not
    // provide a deduplication field for outgoing queue notes.  Omit sync:true
    // to decouple local note acceptance from the network session.  A lost I²C
    // ACK (note accepted by the Notecard but the host never received the reply)
    // will cause the host to retry on the next wake, producing a duplicate
    // entry in the Notecard's queue.  This is an acknowledged edge case: the
    // ring-buffer design ensures at-least-once delivery; the "epoch" field
    // transmitted in each event body is the Unix timestamp of the actual state
    // transition, captured when the event was enqueued.  Server-side
    // deduplication on the downstream route (keying on epoch + tag) is the
    // recommended mitigation for billing-critical deployments because a
    // retransmitted duplicate will carry the same epoch and event tag as the
    // original.  A separate hub.sync call below requests prompt delivery
    // without tying it to the note.add retry logic.
    for (int attempt = 0; attempt < 2; attempt++) {
        J *req = notecard.newRequest("note.add");
        JAddStringToObject(req, "file", "equip_event.qo");
        J *body = JAddObjectToObject(req, "body");
        JAddStringToObject(body, "event",       e.tag);
        JAddNumberToObject(body, "session_min", e.session_min);
        JAddNumberToObject(body, "run_h_total", e.run_h_total);
        JAddNumberToObject(body, "epoch",       (JNUMBER)e.epoch);  // transition timestamp
        J *rsp = notecard.requestAndResponse(req);
        if (!rsp) {
            Serial.print("[EVENT] no response (attempt "); Serial.print(attempt + 1); Serial.println(")");
            continue;
        }
        const char *err = JGetString(rsp, "err");
        bool ok = (!err || *err == '\0');
        if (!ok) { Serial.print("[EVENT] err: "); Serial.println(err); }
        notecard.deleteResponse(rsp);
        if (ok) {
            Serial.print("[EVENT] "); Serial.println(e.tag);
            g_s.evq_head  = (g_s.evq_head + 1) % PENDING_QUEUE_DEPTH;
            g_s.evq_count--;
            // Request prompt delivery as a separate fire-and-forget call so
            // time-critical billing events are not held until the next
            // scheduled outbound window.  Delivery is ultimately the
            // Notecard's responsibility; this call failing is non-fatal.
            J *syncReq = notecard.newRequest("hub.sync");
            J *syncRsp = notecard.requestAndResponse(syncReq);
            if (syncRsp) notecard.deleteResponse(syncRsp);
            return true;
        }
    }
    Serial.print("[EVENT] failed to queue "); Serial.println(e.tag);
    return false;
}

// ── Daily summary — queued for next outbound sync ─────────────────────────────
// Returns true if the note was successfully queued.  The caller updates
// last_summary_epoch and resets the daily accumulators only on success, so a
// failed send retries on the next wake rather than silently dropping the record.
bool sendSummary(void) {
    float bat_v = getBatteryVoltage();
    for (int attempt = 0; attempt < 2; attempt++) {
        J *req = notecard.newRequest("note.add");
        JAddStringToObject(req, "file", "equip_summary.qo");
        J *body = JAddObjectToObject(req, "body");
        JAddNumberToObject(body, "run_h",       g_s.run_h_today);
        JAddNumberToObject(body, "run_h_total", g_s.run_h_total);
        JAddNumberToObject(body, "transport_h", g_s.transport_h_today);
        JAddNumberToObject(body, "bat_v",       bat_v);
        JAddNumberToObject(body, "fault_ct",    g_s.evq_overflow_count);
        J *rsp = notecard.requestAndResponse(req);
        if (!rsp) {
            Serial.print("[SUMMARY] no response (attempt "); Serial.print(attempt + 1); Serial.println(")");
            continue;
        }
        const char *err = JGetString(rsp, "err");
        bool ok = (!err || *err == '\0');
        if (!ok) { Serial.print("[SUMMARY] err: "); Serial.println(err); }
        notecard.deleteResponse(rsp);
        if (ok) {
            Serial.print("[SUMMARY] run_h="); Serial.print(g_s.run_h_today, 2);
            Serial.print(" total=");          Serial.print(g_s.run_h_total, 1);
            Serial.print(" bat_v=");          Serial.println(bat_v, 2);
            return true;
        }
    }
    Serial.println("[SUMMARY] failed to queue summary");
    return false;
}

// ── Utilities ─────────────────────────────────────────────────────────────────
uint32_t getEpoch(void) {
    J *rsp = notecard.requestAndResponse(notecard.newRequest("card.time"));
    uint32_t t = 0;
    if (rsp) {
        const char *err = JGetString(rsp, "err");
        if (!err || *err == '\0') t = (uint32_t)JGetNumber(rsp, "time");
        notecard.deleteResponse(rsp);
    }
    return t;
}

float getBatteryVoltage(void) {
    J *rsp = notecard.requestAndResponse(notecard.newRequest("card.voltage"));
    float v = 0.0f;
    if (rsp) {
        const char *err = JGetString(rsp, "err");
        if (!err || *err == '\0') v = (float)JGetNumber(rsp, "value");
        notecard.deleteResponse(rsp);
    }
    return v;
}

// ── Sleep — persist state and cut host power via ATTN ─────────────────────────
// NotePayloadSaveAndSleep serialises g_s into Notecard flash, then issues
// card.attn mode:sleep.  On Notecarrier CX v1.3, ATTN and EN are separate
// header pins; an external jumper wire from ATTN to EN must be installed for
// the Notecard to gate the Cygnet's 3.3V host rail (see README §4).  With the
// jumper in place, SAMPLE_INTERVAL_SEC later the Cygnet powers up and re-enters
// setup().  Without the jumper the host stays powered and loop() takes over as
// the (higher-power) fallback path.
void goToSleep(void) {
    NotePayloadDesc payload = {0, 0, 0};
    NotePayloadAddSegment(&payload, SEG_ID, &g_s, sizeof(g_s));
    NotePayloadSaveAndSleep(&payload, SAMPLE_INTERVAL_SEC, NULL);
    delay(15000);  // should not return; loop() retries setup() as fallback
}
