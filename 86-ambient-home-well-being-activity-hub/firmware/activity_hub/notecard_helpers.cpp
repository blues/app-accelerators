/*
 * notecard_helpers.cpp — Notecard configuration, time utilities, and
 *                         environment-variable overrides.
 *
 * This file is specific to this project and is NOT a general-purpose library.
 */
#include "notecard_helpers.h"

// ---------------------------------------------------------------------------
// Runtime-configurable globals (definitions)
// ---------------------------------------------------------------------------
int g_sample_interval_sec     = DEFAULT_SAMPLE_INTERVAL_SEC;
int g_summary_interval_min    = DEFAULT_SUMMARY_INTERVAL_MIN;
int g_morning_start_hour      = DEFAULT_MORNING_START_HOUR;
int g_morning_end_hour        = DEFAULT_MORNING_END_HOUR;
int g_sleep_start_hour        = DEFAULT_SLEEP_START_HOUR;
int g_sleep_end_hour          = DEFAULT_SLEEP_END_HOUR;
int g_night_bathroom_limit    = DEFAULT_NIGHT_BATHROOM_LIMIT;
int g_bed_threshold           = DEFAULT_BED_THRESHOLD;
int g_utc_offset_hours        = DEFAULT_UTC_OFFSET_HOURS;
int g_quiet_minutes_for_alert = DEFAULT_QUIET_MINUTES_FOR_ALERT;

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------

// Returns v clamped to [lo, hi].
static int clampInt(int v, int lo, int hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

// Parses a decimal integer from s into *out. Returns true only when s is
// non-empty and consists entirely of a valid integer (no trailing junk).
// On failure the caller retains its current value instead of silently
// accepting the zero that atoi() would produce for strings like "abc".
static bool safeParseInt(const char *s, long *out) {
    if (!s || !*s) return false;
    char *endp;
    long v = strtol(s, &endp, 10);
    if (endp == s || *endp != '\0') return false;
    *out = v;
    return true;
}

// ---------------------------------------------------------------------------
// Notecard one-time setup
// ---------------------------------------------------------------------------

// Returns a djb2 hash of the compile-time PRODUCT_UID string.
// Persisted in AppState so the caller can detect firmware reflashes that
// supply a different (or newly-set) ProductUID and force hub.set to re-run.
uint32_t productUidHash() {
    uint32_t h = 5381;
    const char *s = PRODUCT_UID;
    while (*s) h = h * 33u ^ (uint8_t)*s++;
    return h;
}

// hub.set — establishes product association and outbound/inbound cadence.
// Returns true when the Notecard confirms success so the caller can persist
// the result and skip the call on subsequent wakes.
// Returns false immediately (without contacting the Notecard) when
// PRODUCT_UID is empty — the hub cannot associate with a Notehub project
// until a valid ProductUID is compiled in, so hub_configured must remain
// false until then so every subsequent wake retries.
bool hubConfigure() {
    if (!PRODUCT_UID[0]) {
#ifdef DEBUG_SERIAL
        Serial.println("PRODUCT_UID is empty -- set it in app_state.h before flashing");
#endif
        return false;
    }
    J *req = notecard.newRequest("hub.set");
    if (!req) return false;
    JAddStringToObject(req, "product", PRODUCT_UID);
    // periodic mode: Notecard connects on a schedule rather than staying
    // always-on, which is appropriate for a battery-friendly home device.
    // Change to "continuous" while debugging for immediate event visibility.
    JAddStringToObject(req, "mode",     "periodic");
    JAddNumberToObject(req, "outbound", DEFAULT_SUMMARY_INTERVAL_MIN);
    JAddNumberToObject(req, "inbound",  DEFAULT_SUMMARY_INTERVAL_MIN * 2);
    // sendRequestWithRetry on first contact handles the cold-boot I2C race.
    bool ok = notecard.sendRequestWithRetry(req, 10);
#ifdef DEBUG_SERIAL
    if (!ok) Serial.println("hub.set failed -- will retry on next wake");
#endif
    return ok;
}

// note.template — registers fixed-length binary encoding for the summary
// Notefile (~3–5× bandwidth saving over free-form JSON).
bool defineTemplates() {
    J *req = notecard.newRequest("note.template");
    if (!req) return false;
    JAddStringToObject(req, "file", "activity_summary.qo");
    JAddNumberToObject(req, "port", 50);
    J *body = JAddObjectToObject(req, "body");
    JAddNumberToObject(body, "pir_count",        12);    // 2-byte signed int
    JAddNumberToObject(body, "door_count",        12);    // 2-byte signed int
    JAddNumberToObject(body, "humidity_pct",      14.1);  // 4-byte float
    JAddNumberToObject(body, "temp_c",            14.1);  // 4-byte float
    JAddNumberToObject(body, "bed_motion_pct",    11);    // 1-byte signed int (%)
    JAddNumberToObject(body, "night_bath_count",  11);    // 1-byte signed int
    JAddBoolToObject(body,   "morning_activity",  true);
    J *rsp = notecard.requestAndResponse(req);
    if (!rsp) return false;
    bool ok = !notecard.responseError(rsp);
    notecard.deleteResponse(rsp);
#ifdef DEBUG_SERIAL
    if (!ok) Serial.println("note.template failed -- will retry on next wake");
#endif
    // activity_alert.qo is intentionally not templated: alert notes are
    // low-volume and benefit from free-form JSON so each alert type can carry
    // different supporting fields without a separate template per rule.
    return ok;
}

// card.motion.mode stop — suppresses accelerometer interrupts so bench
// power traces (Mojo) are not polluted by ISR wakes during validation.
bool motionStop() {
    J *req = notecard.newRequest("card.motion.mode");
    if (!req) return false;
    JAddBoolToObject(req, "stop", true);
    J *rsp = notecard.requestAndResponse(req);
    if (!rsp) return false;
    bool ok = !notecard.responseError(rsp);
    notecard.deleteResponse(rsp);
#ifdef DEBUG_SERIAL
    if (!ok) Serial.println("card.motion.mode stop failed -- will retry on next wake");
#endif
    return ok;
}

// ---------------------------------------------------------------------------
// Environment-variable overrides
// ---------------------------------------------------------------------------

void fetchEnvOverrides(AppState &state) {
    // Reset to compile-time defaults before applying env var overrides.
    // In the Stop-mode architecture, globals persist between cycles; resetting
    // here ensures that removing a variable from Notehub reverts the setting
    // to its default rather than leaving the stale env-var value active.
    g_sample_interval_sec     = DEFAULT_SAMPLE_INTERVAL_SEC;
    g_summary_interval_min    = DEFAULT_SUMMARY_INTERVAL_MIN;
    g_morning_start_hour      = DEFAULT_MORNING_START_HOUR;
    g_morning_end_hour        = DEFAULT_MORNING_END_HOUR;
    g_sleep_start_hour        = DEFAULT_SLEEP_START_HOUR;
    g_sleep_end_hour          = DEFAULT_SLEEP_END_HOUR;
    g_night_bathroom_limit    = DEFAULT_NIGHT_BATHROOM_LIMIT;
    g_bed_threshold           = DEFAULT_BED_THRESHOLD;
    g_utc_offset_hours        = DEFAULT_UTC_OFFSET_HOURS;
    g_quiet_minutes_for_alert = DEFAULT_QUIET_MINUTES_FOR_ALERT;

    J *rsp = notecard.requestAndResponse(notecard.newRequest("env.get"));
    if (!rsp) return;
    if (notecard.responseError(rsp)) { notecard.deleteResponse(rsp); return; }

    J *b = JGetObject(rsp, "body");
    if (b) {
        const char *s;
        long v;

        // Every value is parsed with safeParseInt() and clamped to a sane range.
        // An invalid env var (non-numeric, empty, or out-of-range) leaves the
        // current in-memory value unchanged rather than silently accepting a
        // zero or clamped value that could alter cadence or alert behaviour.
        if ((s = JGetString(b, "sample_interval_sec")) && safeParseInt(s, &v))
            g_sample_interval_sec = clampInt((int)v, 30, 3600);

        if ((s = JGetString(b, "morning_start_hour")) && safeParseInt(s, &v))
            g_morning_start_hour = clampInt((int)v, 0, 23);
        if ((s = JGetString(b, "morning_end_hour")) && safeParseInt(s, &v))
            g_morning_end_hour = clampInt((int)v, 0, 23);
        if ((s = JGetString(b, "sleep_start_hour")) && safeParseInt(s, &v))
            g_sleep_start_hour = clampInt((int)v, 0, 23);
        if ((s = JGetString(b, "sleep_end_hour")) && safeParseInt(s, &v))
            g_sleep_end_hour = clampInt((int)v, 0, 23);

        // Equal start/end would zero-length a window and silently suppress its
        // alert class for the rest of the deployment. Revert to defaults.
        if (g_morning_start_hour == g_morning_end_hour) {
            g_morning_start_hour = DEFAULT_MORNING_START_HOUR;
            g_morning_end_hour   = DEFAULT_MORNING_END_HOUR;
#ifdef DEBUG_SERIAL
            Serial.println("morning_start_hour == morning_end_hour: reset to defaults");
#endif
        }
        if (g_sleep_start_hour == g_sleep_end_hour) {
            g_sleep_start_hour = DEFAULT_SLEEP_START_HOUR;
            g_sleep_end_hour   = DEFAULT_SLEEP_END_HOUR;
#ifdef DEBUG_SERIAL
            Serial.println("sleep_start_hour == sleep_end_hour: reset to defaults");
#endif
        }

        if ((s = JGetString(b, "night_bathroom_limit")) && safeParseInt(s, &v))
            g_night_bathroom_limit = clampInt((int)v, 1, 30);
        if ((s = JGetString(b, "bed_threshold")) && safeParseInt(s, &v))
            g_bed_threshold = clampInt((int)v, 0, 4095);
        if ((s = JGetString(b, "utc_offset_hours")) && safeParseInt(s, &v))
            g_utc_offset_hours = clampInt((int)v, -12, 14);
        if ((s = JGetString(b, "quiet_minutes_for_alert")) && safeParseInt(s, &v))
            g_quiet_minutes_for_alert = clampInt((int)v, 5, 120);

        if ((s = JGetString(b, "summary_interval_min")) && safeParseInt(s, &v)) {
            g_summary_interval_min = clampInt((int)v, 5, 1440);
        }

        // Always compare the effective summary cadence (env override when present,
        // compile-time default when absent) against what was last confirmed applied
        // to the Notecard. Because g_summary_interval_min is a global that
        // re-initialises to DEFAULT_SUMMARY_INTERVAL_MIN on every host wake, this
        // comparison catches both env-var changes AND removals: when the var is
        // removed the global falls back to the default, and if applied_summary_interval_min
        // holds a different value, hub.set is reissued to restore the default cadence.
        if (g_summary_interval_min != (int)state.applied_summary_interval_min) {
            J *req = notecard.newRequest("hub.set");
            if (req) {
                JAddStringToObject(req, "mode",     "periodic");
                JAddNumberToObject(req, "outbound", g_summary_interval_min);
                JAddNumberToObject(req, "inbound",  g_summary_interval_min * 2);
                if (notecard.sendRequestWithRetry(req, 5)) {
                    state.applied_summary_interval_min = (uint16_t)g_summary_interval_min;
                }
                // On Notecard failure applied_summary_interval_min stays mismatched;
                // the next wake retries automatically.
            }
        }
    }
    notecard.deleteResponse(rsp);
}

// ---------------------------------------------------------------------------
// Time helpers
// ---------------------------------------------------------------------------

// Returns the current UTC epoch from the Notecard, or 0 if not yet synced.
uint32_t notecardTime() {
    J *rsp = notecard.requestAndResponse(notecard.newRequest("card.time"));
    uint32_t t = 0;
    if (rsp) {
        if (!notecard.responseError(rsp)) {
            t = (uint32_t)JGetNumber(rsp, "time");
        }
        notecard.deleteResponse(rsp);
    }
    return t;
}

// Convert UTC epoch to local hour-of-day applying g_utc_offset_hours.
// NOTE: Only whole-hour offsets are supported. Deployments in half-hour or
// 45-minute offset regions (e.g. India UTC+5:30, Iran UTC+3:30, Nepal
// UTC+5:45) will show an incorrect local hour for time-of-day window checks.
// Set utc_offset_hours to the nearest whole-hour value as an approximation,
// or extend the firmware to support a utc_offset_minutes variable.
uint8_t localHour(uint32_t epochUtc) {
    if (epochUtc == 0) return 0;
    int32_t local = (int32_t)epochUtc + (int32_t)(g_utc_offset_hours * 3600);
    local = ((local % 86400) + 86400) % 86400;
    return (uint8_t)(local / 3600);
}

// Returns true if hour falls within [start_h, end_h).
// Handles overnight windows that wrap around midnight (e.g., 22–6).
// Equal start/end is always false (disabled/zero-length window).
bool inWindow(uint8_t hour, uint8_t start_h, uint8_t end_h) {
    if (start_h == end_h) return false;
    if (start_h < end_h)  return (hour >= start_h && hour < end_h);
    // Overnight wrap: e.g., 22–6 → hour >= 22 OR hour < 6
    return (hour >= start_h || hour < end_h);
}

// Returns true if the per-alert cooldown has elapsed since last firing.
bool cooldownExpired(const AppState &s, int idx, uint32_t now) {
    return (now == 0) || ((now - s.last_alert_time[idx]) >= ALERT_COOLDOWN_SEC);
}
