/***************************************************************************
  construction_equipment_anti_theft_helpers.cpp

  Library for Blues Notecard interaction — Construction Equipment Anti-Theft.

  This library encapsulates all Notecard functionality for the
  construction_equipment_anti_theft project.
  This is specific to your project and is NOT A GENERAL PURPOSE LIBRARY.

  THIS FILE SHOULD BE EDITED AFTER GENERATION.
  IT IS PROVIDED AS A STARTING POINT FOR THE USER TO EDIT AND EXTEND.
***************************************************************************/

#include "construction_equipment_anti_theft_helpers.h"
#include <math.h>

const char kStateSegID[] = "APP";

// ─── Internal helper: checked request/response ────────────────────────────────
// Sends a request via requestAndResponse() and validates both the I/O result
// and the Notecard-level err field.  Returns true only when the response is
// non-null and carries no 'err' key.  Logs any Notecard API error string to
// serial so failures surface in debug output rather than being silently ignored.
// Use this for all security-sensitive writes: note.add, note.template, hub.set,
// and card.location.mode. Keep sendRequestWithRetry() for the cold-boot race.
static bool sendRequestChecked(Notecard &nc, J *req)
{
    J *rsp = nc.requestAndResponse(req);
    if (rsp == NULL) return false;
    bool ok = (JGetObjectItem(rsp, "err") == NULL);
    if (!ok) {
        const char *e = JGetString(rsp, "err");
        if (e) { LOG("[APP] WARN API err: "); LOGLN(e); }
    }
    nc.deleteResponse(rsp);
    return ok;
}

// ─── Idempotent Notecard configuration ───────────────────────────────────────
// Checks the cfg_* flags in AppState and re-applies only the configuration steps
// that have not yet been confirmed.  Safe to call on every wake — a no-op when
// all four flags are already true.
//
// hub.set failure is hard-blocking for uplink: without a valid product UID and
// mode the Notecard cannot associate to Notehub.  Template failure is
// hard-blocking on Skylo: without compact templates, satellite-path notes are
// rejected.  GPS and accelerometer config failures are non-fatal (WARN logged)
// — they degrade tracking but do not break uplink.
//
// Inbound cadence is taken from s.inbound_s (default INBOUND_MIN * 60 = 4 min)
// so commands and env-var changes reach the Notecard promptly regardless of the
// current host sleep interval.
bool ensureConfigured(Notecard &nc, const char *product_uid, AppState &s)
{
    bool all_ok = true;

    if (!s.cfg_hub_ok) {
        // Context-aware inbound: use the short active interval only when
        // after-hours or moving; during daytime stopped, match the stopped wake
        // interval to avoid idle inbound sessions while the equipment is parked.
        // On cold boot, s.last_afterhrs and s.last_moving are false (safe default:
        // daytime stopped) — the short inbound is applied on the next transition.
        bool ctx_after = s.last_afterhrs;
        bool ctx_mov   = s.last_moving;
        int inbound_min = (ctx_after || ctx_mov)
            ? (s.inbound_s / 60)
            : (s.heartbeat_stopped_s / 60);
        if (inbound_min < 1) inbound_min = 1;

        // hub.set: periodic mode.
        // Switch to "continuous" during development for real-time JSON debugging;
        // use "periodic" for field deployment to minimise cellular sessions.
        // sendRequestWithRetry handles the cold-boot I2C race where the Notecard
        // may not be ready immediately after the host powers up, and also
        // re-establishes the Notehub association after an independent Notecard reset.
        J *req = nc.newRequest("hub.set");
        JAddStringToObject(req, "product",  product_uid);
        JAddStringToObject(req, "mode",     "periodic");
        JAddNumberToObject(req, "outbound", s.outbound_s / 60);
        JAddNumberToObject(req, "inbound",  inbound_min);
        if (nc.sendRequestWithRetry(req, 5)) {
            s.cfg_hub_ok = true;
            LOGLN("[APP] hub.set confirmed.");
        } else {
            LOGLN("[APP] ERROR: hub.set failed — will retry next wake. "
                  "Notes will not reach Notehub until this succeeds.");
            all_ok = false;
        }
    }

    if (!s.cfg_gps_ok) {
        // GPS: periodic mode, motion-gated. threshold:4 requires 4 accelerometer
        // events before spinning up GNSS — keeps the radio off during parked hours.
        J *req = nc.newRequest("card.location.mode");
        JAddStringToObject(req, "mode",      "periodic");
        JAddNumberToObject(req, "seconds",   s.heartbeat_moving_s);
        JAddNumberToObject(req, "threshold", 4);
        if (sendRequestChecked(nc, req)) {
            s.cfg_gps_ok = true;
        } else {
            LOGLN("[APP] WARN: card.location.mode config failed — "
                  "will retry next wake.");
        }
    }

    if (!s.cfg_motion_ok) {
        // Accelerometer: 25 Hz, ±4 G, 60-second event buckets.
        // motion:3 → 3 events per bucket classifies the unit as "moving".
        J *req = nc.newRequest("card.motion.mode");
        JAddBoolToObject(req,   "start",       true);
        JAddNumberToObject(req, "sensitivity", 2);    // 25 Hz, ±4 G
        JAddNumberToObject(req, "seconds",     60);
        JAddNumberToObject(req, "motion",      3);
        if (sendRequestChecked(nc, req)) {
            s.cfg_motion_ok = true;
        } else {
            LOGLN("[APP] WARN: card.motion.mode config failed — "
                  "will retry next wake.");
        }
    }

    if (!s.cfg_templates_ok) {
        if (defineTemplates(nc)) {
            s.cfg_templates_ok = true;
        } else {
            LOGLN("[APP] ERROR: note.template registration failed — "
                  "will retry next wake. Satellite notes may be rejected "
                  "until templates are confirmed.");
            all_ok = false;
        }
    }

    return all_ok;
}

// ─── Compact note templates ───────────────────────────────────────────────────
// format:"compact" + port are mandatory for Notecard for Skylo outbound Notefiles.
// Without them, satellite-path notes may be rejected by the Notecard firmware.
// Type hints: 14.1 = 4-byte float, 12 = 2-byte signed int, "N" = string of ≤N chars.
// Returns true only when both templates register successfully.  Each template is
// retried up to kMaxAttempts times; a transient Notecard-side error must not leave
// the device running without compact templates (notes would be rejected on Skylo).
bool defineTemplates(Notecard &nc)
{
    const int kMaxAttempts = 3;
    bool all_ok = true;

    // ── tracker.qo — periodic heartbeat ──────────────────────────────────────
    {
        bool ok = false;
        for (int attempt = 0; attempt < kMaxAttempts && !ok; attempt++) {
            J *req = nc.newRequest("note.template");
            JAddStringToObject(req, "file",   TRACKER_NOTEFILE);
            JAddStringToObject(req, "format", "compact");
            JAddNumberToObject(req, "port",   TRACKER_PORT);
            J *body = JAddObjectToObject(req, "body");
            JAddNumberToObject(body, "lat",         14.1);
            JAddNumberToObject(body, "lon",         14.1);
            // loc_valid: 1 when lat/lon carry a real GNSS fix, 0 when no fix is
            // available (lat/lon will be 0,0). Downstream routes should filter
            // loc_valid==0 before rendering map pins so null-island (0°N, 0°E)
            // is never shown as a real position.
            JAddNumberToObject(body, "loc_valid",   12);
            JAddNumberToObject(body, "ignition",    12);
            JAddNumberToObject(body, "moving",      12);
            JAddNumberToObject(body, "immobilized", 12);
            JAddNumberToObject(body, "bat_v",       14.1);
            JAddNumberToObject(body, "fence_ok",    12);
            // fix_age_s: seconds since the GNSS fix was last acquired (>= 0), or
            // -1 when card.time was unavailable and the age cannot be determined.
            // Downstream consumers must treat -1 as "age unknown".
            JAddNumberToObject(body, "fix_age_s",   14.1);
            ok = sendRequestChecked(nc, req);
            if (!ok && attempt < kMaxAttempts - 1) {
                LOG("[APP] WARN: note.template tracker.qo attempt ");
                LOG(attempt + 1);
                LOGLN(" failed — retrying.");
                delay(200);
            }
        }
        if (!ok) {
            LOGLN("[APP] ERROR: note.template tracker.qo failed after all retries.");
            all_ok = false;
        }
    }

    // ── alert.qo — event-driven alert ────────────────────────────────────────
    {
        bool ok = false;
        for (int attempt = 0; attempt < kMaxAttempts && !ok; attempt++) {
            J *req = nc.newRequest("note.template");
            JAddStringToObject(req, "file",   ALERT_NOTEFILE);
            JAddStringToObject(req, "format", "compact");
            JAddNumberToObject(req, "port",   ALERT_PORT);
            J *body = JAddObjectToObject(req, "body");
            // Template placeholder: the string length of this sample defines the
            // maximum field width in the compact binary record.  Use 24 underscores
            // so the field accommodates the longest emitted alert name
            // ("ignition_on_immobilized" = 23 chars) with one byte of margin.
            // Using the two-character "24" was a bug — it allocated only a 2-char
            // field, silently truncating every real alert string at the Notecard.
            JAddStringToObject(body, "alert",       "________________________");  // 24-char sample → field width 24
            JAddNumberToObject(body, "lat",         14.1);
            JAddNumberToObject(body, "lon",         14.1);
            // loc_valid: same sentinel scheme as tracker.qo (see above).
            JAddNumberToObject(body, "loc_valid",   12);
            JAddNumberToObject(body, "ignition",    12);
            JAddNumberToObject(body, "immobilized", 12);
            JAddNumberToObject(body, "bat_v",       14.1);
            JAddNumberToObject(body, "fix_age_s",   14.1);  // age of cached GPS fix at alert time
            ok = sendRequestChecked(nc, req);
            if (!ok && attempt < kMaxAttempts - 1) {
                LOG("[APP] WARN: note.template alert.qo attempt ");
                LOG(attempt + 1);
                LOGLN(" failed — retrying.");
                delay(200);
            }
        }
        if (!ok) {
            LOGLN("[APP] ERROR: note.template alert.qo failed after all retries.");
            all_ok = false;
        }
    }

    return all_ok;
}

// ─── Environment variable overrides ──────────────────────────────────────────
// Returns true when Notehub delivered new env-var values (env_modified advanced);
// returns false when nothing changed or on error.  The caller should call
// applyHubCadence() after a true return so the Notecard's outbound/inbound
// session windows stay in sync with any updated heartbeat intervals.
//
// IMPORTANT — Notehub env vars are ALWAYS JSON strings, never JSON numbers or
// booleans.  JGetNumber() returns 0 for any string-valued field, so using it
// directly silently discards every fleet-level override the operator sets.
// All fields are therefore read with JGetString() and converted explicitly:
//   • floating-point  →  strtod()  (fence_lat, fence_lon, fence_radius_m)
//   • integer         →  strtol()  (hour bounds, cadence intervals)
//   • boolean flag    →  strcmp()  (fence_enabled: "1" or "true")
// A value is applied to AppState only when the parse succeeds
// (endptr is advanced past at least one character).
bool fetchEnvOverrides(Notecard &nc, AppState &s)
{
    J *req = nc.newRequest("env.get");
    // time delta: only return vars changed since s.env_modified epoch.
    JAddNumberToObject(req, "time", (double)s.env_modified);
    J *rsp = nc.requestAndResponse(req);
    if (rsp == NULL) return false;
    // env.get returns err when nothing has changed since s.env_modified — normal
    // during most wake cycles.  Log all error strings at debug level so that
    // genuine faults (e.g. I2C timeout, Notecard not ready) are still visible
    // in serial output instead of being silently discarded.
    if (JGetObjectItem(rsp, "err")) {
        const char *err_str = JGetString(rsp, "err");
        if (err_str != NULL) {
            LOG("[APP] DBG env.get: "); LOGLN(err_str);
        }
        nc.deleteResponse(rsp);
        return false;
    }

    s.env_modified = (uint32_t)JGetNumber(rsp, "time");
    J *env = JGetObjectItem(rsp, "body");
    if (env == NULL) { nc.deleteResponse(rsp); return false; }

    // ── Geofence center ──────────────────────────────────────────────────────
    // fence_enabled=1 is the preferred commissioning path: it applies fence_lat
    // and fence_lon regardless of their numeric value, removing the ambiguity
    // where 0.0 simultaneously means "unset" and "on the equator/prime meridian".
    // The legacy non-zero fallback is retained for backward compatibility with
    // existing fleet configs that set fence_lat/fence_lon without fence_enabled.
    //
    // All env-var fields arrive from Notehub as JSON strings; coordinates are
    // parsed with strtod() and the fence_enabled flag with strcmp() so that a
    // string "0" is not misread as the numeric 0 that JGetNumber() returns for
    // any absent or non-numeric field.
    {
        const char *fe_str  = JGetString(env, "fence_enabled");
        const char *lat_str = JGetString(env, "fence_lat");
        const char *lon_str = JGetString(env, "fence_lon");

        // Accept "1" or "true"; anything else (including absent) → false.
        bool fence_enabled_flag = (fe_str != NULL &&
                                   (strcmp(fe_str, "1") == 0 ||
                                    strcmp(fe_str, "true") == 0));

        double env_lat = 0.0;
        bool   lat_ok  = false;
        if (lat_str != NULL && *lat_str != '\0') {
            char  *endptr = NULL;
            double v      = strtod(lat_str, &endptr);
            if (endptr != lat_str) { env_lat = v; lat_ok = true; }
        }

        double env_lon = 0.0;
        bool   lon_ok  = false;
        if (lon_str != NULL && *lon_str != '\0') {
            char  *endptr = NULL;
            double v      = strtod(lon_str, &endptr);
            if (endptr != lon_str) { env_lon = v; lon_ok = true; }
        }

        if (fence_enabled_flag && lat_ok && lon_ok) {
            // Explicit flag — apply coordinates regardless of their numeric value.
            s.fence_lat = env_lat;
            s.fence_lon = env_lon;
            s.fence_set = true;
        } else if (!fence_enabled_flag && lat_ok && lon_ok &&
                   (env_lat != 0.0 || env_lon != 0.0)) {
            // Legacy fallback: non-zero check.  Does not support geofences centered
            // exactly on latitude 0° or longitude 0° — use fence_enabled=1 instead.
            s.fence_lat = env_lat;
            s.fence_lon = env_lon;
            s.fence_set = true;
        }
    }

    // ── Fence radius ──────────────────────────────────────────────────────────
    {
        const char *str = JGetString(env, "fence_radius_m");
        if (str != NULL && *str != '\0') {
            char  *endptr = NULL;
            double v      = strtod(str, &endptr);
            if (endptr != str && v > 0.0) s.fence_radius_m = (float)v;
        }
    }

    // ── After-hours window ────────────────────────────────────────────────────
    {
        const char *str = JGetString(env, "after_hours_start");
        if (str != NULL && *str != '\0') {
            char *endptr = NULL;
            long  v      = strtol(str, &endptr, 10);
            if (endptr != str && v > 0 && v < 24)
                s.after_hours_start = (int8_t)v;
        }
    }
    {
        const char *str = JGetString(env, "after_hours_end");
        if (str != NULL && *str != '\0') {
            char *endptr = NULL;
            long  v      = strtol(str, &endptr, 10);
            if (endptr != str && v >= 0 && v < 24)
                s.after_hours_end = (int8_t)v;
        }
    }

    // ── Cadence intervals ─────────────────────────────────────────────────────
    // Clamp every cadence value to a sane operational range before storing.
    // This prevents a mis-typed Notehub fleet variable from creating an interval
    // so long it effectively disables monitoring, or so short it creates a
    // pathological duty cycle that drains the battery.
    {
        const char *str = JGetString(env, "heartbeat_stopped_min");
        if (str != NULL && *str != '\0') {
            char *endptr = NULL;
            long  v      = strtol(str, &endptr, 10);
            if (endptr != str && v > 0)
                s.heartbeat_stopped_s = constrain((int)v, MIN_ENV_INTERVAL_MIN, MAX_HEARTBEAT_STOPPED_MIN) * 60;
        }
    }
    {
        const char *str = JGetString(env, "heartbeat_moving_min");
        if (str != NULL && *str != '\0') {
            char *endptr = NULL;
            long  v      = strtol(str, &endptr, 10);
            if (endptr != str && v > 0)
                s.heartbeat_moving_s = constrain((int)v, MIN_ENV_INTERVAL_MIN, MAX_HEARTBEAT_MOVING_MIN) * 60;
        }
    }
    {
        const char *str = JGetString(env, "heartbeat_afterhours_min");
        if (str != NULL && *str != '\0') {
            char *endptr = NULL;
            long  v      = strtol(str, &endptr, 10);
            if (endptr != str && v > 0)
                s.heartbeat_afterhours_s = constrain((int)v, MIN_ENV_INTERVAL_MIN, MAX_HEARTBEAT_AFTERHOURS_MIN) * 60;
        }
    }
    {
        const char *str = JGetString(env, "alert_cooldown_min");
        if (str != NULL && *str != '\0') {
            char *endptr = NULL;
            long  v      = strtol(str, &endptr, 10);
            if (endptr != str && v > 0)
                s.alert_cooldown_s = constrain((int)v, MIN_ENV_INTERVAL_MIN, MAX_ALERT_COOLDOWN_MIN) * 60;
        }
    }
    {
        // inbound_min: how often the Notecard polls Notehub for commands and env updates.
        // Clamped to MIN_ENV_INTERVAL_MIN..MAX_INBOUND_MIN so operators cannot
        // accidentally set it so large that immobilize commands are delayed for hours.
        const char *str = JGetString(env, "inbound_min");
        if (str != NULL && *str != '\0') {
            char *endptr = NULL;
            long  v      = strtol(str, &endptr, 10);
            if (endptr != str && v > 0)
                s.inbound_s = constrain((int)v, MIN_ENV_INTERVAL_MIN, MAX_INBOUND_MIN) * 60;
        }
    }
    {
        // outbound_min clamps to the same range as heartbeat_stopped_min — the two
        // share upper bound MAX_HEARTBEAT_STOPPED_MIN (24 h) since both represent
        // a time-to-transmission budget, not a safety-critical latency.
        const char *str = JGetString(env, "outbound_min");
        if (str != NULL && *str != '\0') {
            char *endptr = NULL;
            long  v      = strtol(str, &endptr, 10);
            if (endptr != str && v > 0)
                s.outbound_s = constrain((int)v, MIN_ENV_INTERVAL_MIN, MAX_HEARTBEAT_STOPPED_MIN) * 60;
        }
    }

    nc.deleteResponse(rsp);
    return true;
}

// ─── Hub cadence sync ─────────────────────────────────────────────────────────
// Reissues hub.set (outbound/inbound) AND card.location.mode (GNSS acquisition
// seconds) derived from the current AppState cadence fields and wake context.
//
// Inbound is context-aware (see applyHubCadence declaration in the header):
//   • after-hours or moving  → s.inbound_s (short, default 4 min)
//   • daytime stopped        → s.heartbeat_stopped_s (long, default 60 min)
//
// Outbound uses s.outbound_s (default 240 min, set by outbound_min env var)
// — decoupled from the host wake interval so multiple heartbeats can accumulate
// per outbound session without degrading detection latency.
void applyHubCadence(Notecard &nc, const AppState &s, bool moving, bool afterhrs)
{
    // Context-aware inbound: short during active-monitoring windows (after-hours,
    // moving); long when parked and daytime to avoid idle cellular sessions.
    int inbound_min = (afterhrs || moving)
        ? (s.inbound_s / 60)
        : (s.heartbeat_stopped_s / 60);
    if (inbound_min < 1) inbound_min = 1;

    J *req = nc.newRequest("hub.set");
    JAddStringToObject(req, "mode",     "periodic");
    JAddNumberToObject(req, "outbound", s.outbound_s / 60);
    JAddNumberToObject(req, "inbound",  inbound_min);
    if (!sendRequestChecked(nc, req)) {
        LOGLN("[APP] WARN: hub.set cadence update failed.");
    }

    // Retune GNSS acquisition cadence to match the moving heartbeat interval.
    // Without this, card.location.mode stays at the compile-time default even
    // when heartbeat_moving_min is changed via Notehub environment variables.
    req = nc.newRequest("card.location.mode");
    JAddStringToObject(req, "mode",      "periodic");
    JAddNumberToObject(req, "seconds",   s.heartbeat_moving_s);
    JAddNumberToObject(req, "threshold", 4);
    if (!sendRequestChecked(nc, req)) {
        LOGLN("[APP] WARN: card.location.mode cadence update failed.");
    }
}

// ─── Inbound command queue ────────────────────────────────────────────────────
// Drains the entire immobilize.qi queue on each wake cycle (last command wins).
// Looping until note.get returns an error guarantees that if the operator posts
// `immobilize` followed by `release` (or vice versa) before the device wakes,
// the final intent is applied atomically on the same wake — not delayed by hours.
// The acknowledgment note (immobilize_armed) is emitted once by runCycle() after
// this function returns, based on the was_pending / immobilize_pending delta.
//
// Returns true when the command path is healthy (queue drained or confirmed empty).
// Returns false when note.get failed after all retries, or when it returned a
// non-empty-queue error string — the caller should emit a diagnostic alert so
// operators can distinguish 'no command queued' from 'command path unhealthy'.
bool checkAndHandleCommand(Notecard &nc, AppState &s)
{
    const int kMaxRetries = 3;

    for (;;) {
        // Retry note.get on NULL response (transient I2C fault) before giving up.
        // A single transport error must not silently defer an immobilize or release
        // command until the next host wake, which may be hours away in the stopped
        // daytime state.
        J *rsp = NULL;
        for (int attempt = 0; attempt < kMaxRetries && rsp == NULL; attempt++) {
            J *req = nc.newRequest("note.get");
            JAddStringToObject(req, "file",   COMMAND_NOTEFILE);
            JAddBoolToObject(req,   "delete", true);
            rsp = nc.requestAndResponse(req);
            if (rsp == NULL) {
                LOG("[APP] WARN: note.get I/O failure on attempt ");
                LOG(attempt + 1);
                LOGLN(".");
                if (attempt < kMaxRetries - 1) delay(100);
            }
        }
        if (rsp == NULL) {
            // All retries exhausted — command path is unhealthy.
            LOGLN("[APP] ERROR: note.get failed after all retries — "
                  "command path unhealthy; caller will emit diagnostic.");
            return false;
        }

        // note.get returns "file.note.quiet" when the queue is empty — normal
        // exit.  Any other error string indicates a real command-path fault;
        // return false so the caller can emit a diagnostic alert and operators
        // can distinguish "no command queued" from "path unhealthy".
        if (JGetObjectItem(rsp, "err")) {
            const char *err_str = JGetString(rsp, "err");
            nc.deleteResponse(rsp);
            if (err_str != NULL && strstr(err_str, "quiet") != NULL) {
                break;  // Confirmed empty queue — healthy exit.
            }
            // Non-quiet error: real command-path fault.
            LOG("[APP] ERROR: note.get returned unexpected error: ");
            LOGLN(err_str ? err_str : "(null)");
            return false;
        }

        J *body = JGetObjectItem(rsp, "body");
        if (body != NULL) {
            const char *cmd = JGetString(body, "cmd");
            if (cmd != NULL && strcmp(cmd, "immobilize") == 0) {
                s.immobilize_pending = true;
            } else if (cmd != NULL && strcmp(cmd, "release") == 0) {
                s.immobilize_pending = false;
                if (s.immobilized) {
                    releaseRelay();
                    s.immobilized = false;
                }
            } else {
                // Unknown or malformed command: log clearly so operator mistakes
                // surface in serial output rather than disappearing silently.
                LOG("[APP] WARN: unknown command ignored: \"");
                LOG(cmd != NULL ? cmd : "(null)");
                LOGLN("\"");
            }
        }
        nc.deleteResponse(rsp);
    }
    return true;
}

// ─── Sensor reads ─────────────────────────────────────────────────────────────
bool getIgnitionState(bool last_state)
{
    // Read A2 with analogRead() and average multiple samples to filter automotive
    // line noise and battery-sag transients during engine crank.
    // Voltage divider: 12V × 10/(33+10) ≈ 2.79V → ~3462 counts on 3.3V / 12-bit ADC.
    //
    // Hysteresis thresholds (12-bit, 3.3V reference):
    //   kOnThreshold  ~2.50V (3103 counts) — divider output must EXCEED this to latch ON.
    //   kOffThreshold ~2.00V (2482 counts) — divider output must FALL BELOW this to latch OFF.
    // The ~0.5V hysteresis band spans the typical battery-sag floor during cranking
    // (~10.5V → ~2.44V divider output) while remaining well above ground-referenced
    // line noise. Adjust if your specific ignition circuit has different sag depth.
    //
    // NOTE: add TVS protection and an RC low-pass filter (R ≈ 1kΩ, C ≈ 100nF) ahead of
    // A2 before deploying on live equipment (see hardware note in the .ino header).
    const int kSamples      = 4;
    const int kOnThreshold  = 3103;   // ≥ ~2.50 V → declare ON  (rising edge)
    const int kOffThreshold = 2482;   // <  ~2.00 V → declare OFF (falling edge)

    int32_t sum = 0;
    for (int i = 0; i < kSamples; i++) {
        sum += analogRead(PIN_IGNITION_SENSE);
        if (i < kSamples - 1) delay(5);  // brief inter-sample pause to decorrelate noise
    }
    int avg = (int)(sum / kSamples);

    // Apply hysteresis: only toggle state when the averaged reading clearly crosses
    // the opposite threshold. Within the band (kOffThreshold ≤ avg < kOnThreshold),
    // hold the previous state to prevent chattering near the logic boundary.
    if (!last_state && avg >= kOnThreshold)  return true;
    if ( last_state && avg <  kOffThreshold) return false;
    return last_state;
}

bool getIsMoving(Notecard &nc)
{
    J *rsp = nc.requestAndResponse(nc.newRequest("card.motion"));
    if (rsp == NULL) return false;
    if (JGetObjectItem(rsp, "err")) { nc.deleteResponse(rsp); return false; }
    const char *mode_str = JGetString(rsp, "mode");
    bool moving = (mode_str != NULL && strcmp(mode_str, "moving") == 0);
    nc.deleteResponse(rsp);
    return moving;
}

bool getLocation(Notecard &nc, double &lat, double &lon, uint32_t &fix_time)
{
    J *rsp = nc.requestAndResponse(nc.newRequest("card.location"));
    if (rsp == NULL) return false;
    if (JGetObjectItem(rsp, "err")) { nc.deleteResponse(rsp); return false; }
    lat      = JGetNumber(rsp, "lat");
    lon      = JGetNumber(rsp, "lon");
    // "time" is the epoch of the last acquired GNSS fix (not the current time).
    // The caller computes fix_age_s = now - fix_time and includes it in payloads
    // so operators can see whether geofence decisions used fresh or stale data.
    fix_time = (uint32_t)JGetNumber(rsp, "time");
    nc.deleteResponse(rsp);
    return (lat != 0.0 || lon != 0.0);
}

float getBatteryVoltage(Notecard &nc)
{
    J *rsp = nc.requestAndResponse(nc.newRequest("card.voltage"));
    if (rsp == NULL) return 0.0f;
    if (JGetObjectItem(rsp, "err")) { nc.deleteResponse(rsp); return 0.0f; }
    float v = (float)JGetNumber(rsp, "value");
    nc.deleteResponse(rsp);
    return v;
}

uint32_t getEpochTime(Notecard &nc)
{
    J *rsp = nc.requestAndResponse(nc.newRequest("card.time"));
    if (rsp == NULL) return 0;
    if (JGetObjectItem(rsp, "err")) { nc.deleteResponse(rsp); return 0; }
    uint32_t t = (uint32_t)JGetNumber(rsp, "time");
    nc.deleteResponse(rsp);
    return t;
}

// ─── After-hours window check (UTC) ──────────────────────────────────────────
bool isAfterHours(uint32_t epoch, int8_t start_h, int8_t end_h)
{
    if (epoch == 0) return false;
    int hour = (int)((epoch / 3600UL) % 24UL);
    // Overnight span (e.g. start=18, end=6) crosses midnight.
    if (start_h > end_h) {
        return (hour >= start_h || hour < end_h);
    }
    return (hour >= start_h && hour < end_h);
}

// ─── Haversine great-circle distance (meters) ────────────────────────────────
float haversineDistanceM(double lat1, double lon1, double lat2, double lon2)
{
    const double R = 6371000.0;
    double dLat = (lat2 - lat1) * M_PI / 180.0;
    double dLon = (lon2 - lon1) * M_PI / 180.0;
    double a = sin(dLat / 2) * sin(dLat / 2)
             + cos(lat1 * M_PI / 180.0) * cos(lat2 * M_PI / 180.0)
             * sin(dLon / 2) * sin(dLon / 2);
    return (float)(R * 2.0 * atan2(sqrt(a), sqrt(1.0 - a)));
}

// ─── Geofence flash persistence ───────────────────────────────────────────────
// Writes the commissioned fence center and radius to a named note in fence.db so
// the coordinates survive a full power loss.  On the next cold boot, the firmware
// calls loadFenceFromFlash() first — before GPS auto-anchor is allowed — so a
// battery-disconnect/reconnect during a theft event cannot silently re-home the
// fence to the thief's current location.
// Retries up to kMaxAttempts on transient I2C or Notecard-side failures.
// Returns true on success; false when all retries fail — the caller must log a
// distinct diagnostic and must NOT treat the fence as durably commissioned across
// reboots until a subsequent call succeeds.
bool saveFenceToFlash(Notecard &nc, const AppState &s)
{
    const int kMaxAttempts = 3;
    for (int attempt = 0; attempt < kMaxAttempts; attempt++) {
        J *req = nc.newRequest("note.update");
        JAddStringToObject(req, "file", FENCE_NOTEFILE);
        JAddStringToObject(req, "note", "home");
        J *body = JAddObjectToObject(req, "body");
        JAddNumberToObject(body, "lat",    s.fence_lat);
        JAddNumberToObject(body, "lon",    s.fence_lon);
        JAddNumberToObject(body, "radius", (double)s.fence_radius_m);
        if (sendRequestChecked(nc, req)) {
            return true;
        }
        LOG("[APP] WARN: saveFenceToFlash attempt ");
        LOG(attempt + 1);
        LOGLN(" failed.");
        if (attempt < kMaxAttempts - 1) delay(200);
    }
    LOGLN("[APP] ERROR: saveFenceToFlash failed after all retries — "
          "fence coordinates are not durably stored. A power cycle may "
          "allow auto-anchor to re-home to a wrong location.");
    return false;
}

// Reads the previously-persisted fence back from fence.db.  Returns true and
// populates s.fence_lat, s.fence_lon, s.fence_radius_m, and s.fence_set on
// success.
//
// Returns false in two distinct cases, disambiguated by io_error:
//   io_error == false: record positively confirmed absent (safe to auto-anchor)
//   io_error == true:  transport / API failure after all retries; fence presence
//                      is unknown — the caller must NOT auto-anchor.
//
// Retries up to kMaxAttempts on NULL response or non-"not-found" API errors so
// a single transient I2C hiccup on boot cannot silently allow re-homing.
bool loadFenceFromFlash(Notecard &nc, AppState &s, bool &io_error)
{
    io_error = false;
    const int kMaxAttempts = 3;

    for (int attempt = 0; attempt < kMaxAttempts; attempt++) {
        J *req = nc.newRequest("note.get");
        JAddStringToObject(req, "file", FENCE_NOTEFILE);
        JAddStringToObject(req, "note", "home");
        J *rsp = nc.requestAndResponse(req);

        if (rsp == NULL) {
            // I2C / transport failure — cannot confirm absence; mark and retry.
            io_error = true;
            LOG("[APP] WARN: loadFenceFromFlash I/O failure on attempt ");
            LOGLN(attempt + 1);
            if (attempt < kMaxAttempts - 1) delay(200);
            continue;
        }

        J *errObj = JGetObjectItem(rsp, "err");
        if (errObj != NULL) {
            const char *err_str = JGetString(rsp, "err");
            // Distinguish "note does not exist" from all other API/transport errors.
            // Only clear io_error (permit auto-anchor) when the Notecard positively
            // confirms there is no persisted record in the file.
            bool not_found = (err_str != NULL &&
                              (strstr(err_str, "does not exist") != NULL ||
                               strstr(err_str, "not found")      != NULL));
            nc.deleteResponse(rsp);
            if (not_found) {
                io_error = false;   // confirmed absent — auto-anchor is safe
                return false;
            }
            // Other API error — could be transient; retry.
            io_error = true;
            LOG("[APP] WARN: loadFenceFromFlash API err (attempt ");
            LOG(attempt + 1);
            LOG("): ");
            if (err_str) LOGLN(err_str);
            if (attempt < kMaxAttempts - 1) delay(200);
            continue;
        }

        // Successful response — parse the body.
        J *body = JGetObjectItem(rsp, "body");
        if (body == NULL) {
            // Response arrived but body is absent — record is malformed.
            // Treat as an error (not confirmed-absent) so auto-anchor is suppressed
            // rather than silently permitted on a corrupt or schema-mismatched note.
            nc.deleteResponse(rsp);
            io_error = true;
            LOG("[APP] WARN: loadFenceFromFlash body missing (attempt ");
            LOG(attempt + 1);
            LOGLN(") — treating as I/O error; auto-anchor suppressed.");
            if (attempt < kMaxAttempts - 1) { delay(200); continue; }
            return false;
        }
        double lat = JGetNumber(body, "lat");
        double lon = JGetNumber(body, "lon");
        // lat/lon of exactly 0,0 is a corrupt or uninitialized record — no
        // legitimate job-site fence would be anchored at null island.  Treat as a
        // malformed record (io_error = true) so auto-anchor is suppressed and the
        // operator must explicitly re-commission the fence to recover.
        if (lat == 0.0 && lon == 0.0) {
            nc.deleteResponse(rsp);
            io_error = true;
            LOGLN("[APP] ERROR: loadFenceFromFlash lat/lon are 0,0 — "
                  "record is corrupt or uninitialized. Auto-anchor "
                  "suppressed; re-commission the geofence to recover.");
            return false;   // no retry — record is present but bad; looping won't help
        }
        s.fence_lat = lat;
        s.fence_lon = lon;
        float r = (float)JGetNumber(body, "radius");
        if (r > 0.0f) s.fence_radius_m = r;
        s.fence_set = true;
        nc.deleteResponse(rsp);
        io_error = false;
        return true;
    }

    // All attempts exhausted with transport errors — suppress auto-anchor.
    // io_error remains true.
    return false;
}

// ─── Periodic heartbeat ───────────────────────────────────────────────────────
bool sendHeartbeat(Notecard &nc, const AppState &s,
                   double lat, double lon, bool loc_valid,
                   bool moving, int8_t fence_ok,
                   bool ignition_on, int32_t fix_age_s)
{
    float bat = getBatteryVoltage(nc);
    J *req  = nc.newRequest("note.add");
    JAddStringToObject(req, "file", TRACKER_NOTEFILE);
    // No sync:true — heartbeats queue and flush on the hub.set outbound cadence.
    J *body = JAddObjectToObject(req, "body");
    JAddNumberToObject(body, "lat",         lat);
    JAddNumberToObject(body, "lon",         lon);
    // loc_valid signals to downstream consumers whether lat/lon carry a real fix.
    // When loc_valid==0, lat/lon are 0,0 (no fix) and should not be rendered on a map.
    JAddNumberToObject(body, "loc_valid",   loc_valid      ? 1 : 0);
    JAddNumberToObject(body, "ignition",    ignition_on    ? 1 : 0);
    JAddNumberToObject(body, "moving",      moving         ? 1 : 0);
    JAddNumberToObject(body, "immobilized", s.immobilized  ? 1 : 0);
    JAddNumberToObject(body, "bat_v",       (double)bat);
    // fence_ok: -1 = unknown (no valid fix or fence not yet anchored),
    //            0 = geofence breach, 1 = in-fence.
    JAddNumberToObject(body, "fence_ok",    (double)fence_ok);
    // fix_age_s: >=0 = real elapsed seconds; -1 = age unknown (card.time unavailable).
    JAddNumberToObject(body, "fix_age_s",   (double)fix_age_s);
    if (!sendRequestChecked(nc, req)) {
        LOGLN("[APP] WARN: heartbeat note.add failed.");
        return false;
    }
    return true;
}

// ─── Event alert (immediate sync) ────────────────────────────────────────────
bool sendAlert(Notecard &nc, const AppState &s,
               const char *alert_type, double lat, double lon, bool loc_valid,
               bool ignition, bool immobilized, int32_t fix_age_s)
{
    // Fetch battery voltage once before the retry loop; it will not change
    // meaningfully across attempts and avoids extra I2C transactions.
    float bat = getBatteryVoltage(nc);

    // Retry loop: a transient I2C or Notecard error must not silently drop a
    // security-critical alert (geofence breach, immobilize ack, release ack).
    // Uses requestAndResponse() so the Notecard-level err field is inspected on
    // every attempt — sendRequest() cannot distinguish an API-level rejection
    // from a successful queue.
    const int kMaxRetries = 3;
    for (int attempt = 0; attempt < kMaxRetries; attempt++) {
        J *req  = nc.newRequest("note.add");
        JAddStringToObject(req, "file", ALERT_NOTEFILE);
        JAddBoolToObject(req, "sync", true);   // wake radio, transmit now
        J *body = JAddObjectToObject(req, "body");
        JAddStringToObject(body, "alert",       alert_type);
        JAddNumberToObject(body, "lat",         lat);
        JAddNumberToObject(body, "lon",         lon);
        // loc_valid: 0 when no GNSS fix is available — downstream routes must not
        // map 0,0 as a real incident location.
        JAddNumberToObject(body, "loc_valid",   loc_valid   ? 1 : 0);
        JAddNumberToObject(body, "ignition",    ignition    ? 1 : 0);
        JAddNumberToObject(body, "immobilized", immobilized ? 1 : 0);
        JAddNumberToObject(body, "bat_v",       (double)bat);
        // fix_age_s: >=0 = real elapsed seconds; -1 = age unknown (card.time unavailable).
        JAddNumberToObject(body, "fix_age_s",   (double)fix_age_s);
        J *rsp = nc.requestAndResponse(req);
        if (rsp == NULL) {
            LOG("[APP] WARN: alert I/O failure on attempt ");
            LOGLN(attempt + 1);
        } else {
            bool ok = (JGetObjectItem(rsp, "err") == NULL);
            if (!ok) {
                const char *e = JGetString(rsp, "err");
                if (e) {
                    LOG("[APP] WARN alert API err (attempt ");
                    LOG(attempt + 1);
                    LOG("): ");
                    LOGLN(e);
                }
            }
            nc.deleteResponse(rsp);
            if (ok) return true;
        }
        if (attempt < kMaxRetries - 1) delay(500);
    }
    LOG("[APP] ERROR: alert \"");
    LOG(alert_type);
    LOGLN("\" failed after all retries.");
    return false;
}

// ─── Relay control ────────────────────────────────────────────────────────────
void assertRelay()
{
    // HIGH → BSS138 gate at 3.3 V (within R_DS(on)-characterized region per datasheet,
    // max 3.5 Ω at V_GS = 2.5 V) → relay coil energized → NC contact opens → ignition cut.
    digitalWrite(PIN_RELAY_DRIVER, HIGH);
}

void releaseRelay()
{
    // LOW → BSS138 gate pulled to GND by 10 kΩ pulldown → MOSFET off →
    // coil de-energized → NC contact closes → ignition circuit restored.
    digitalWrite(PIN_RELAY_DRIVER, LOW);
}
