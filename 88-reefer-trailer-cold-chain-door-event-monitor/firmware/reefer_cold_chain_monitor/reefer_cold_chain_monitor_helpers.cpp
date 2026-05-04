/*
 * reefer_cold_chain_monitor_helpers.cpp
 *
 * Notecard configuration, sensor drivers, door/temperature state machines,
 * alert/summary emission, and time helpers for the reefer cold-chain monitor.
 *
 * Application flow (setup, loop, runSampleCycle) lives in the .ino; this file
 * contains every function declared in reefer_cold_chain_monitor_helpers.h.
 */

#include <Arduino.h>
#include <OneWire.h>
#include "reefer_cold_chain_monitor_helpers.h"

// =============================================================================
// Notecard configuration (runs on first boot only)
//
// Returns true when hub.set and card.location.mode both succeed.
// Returns false on any NULL allocation, Notecard error, or I²C failure; the
// caller will retry on subsequent wakes until both succeed.
// =============================================================================
bool hubConfigure(void) {
    // hub.set — sendRequestWithRetry guards the cold-boot I²C race where the
    // host MCU starts before the Notecard firmware is ready.
    {
        J *req = notecard.newRequest("hub.set");
        if (req == NULL) return false;
        JAddStringToObject(req, "product",  PRODUCT_UID);
        JAddStringToObject(req, "mode",     "periodic");
        JAddNumberToObject(req, "outbound", (int)DEFAULT_SUMMARY_INTERVAL_MIN);
        JAddNumberToObject(req, "inbound",  120);
        if (!notecard.sendRequestWithRetry(req, 10)) {
            DEBUG_PRINTLN(F("[cfg] hub.set failed"));
            return false;
        }
    }

    // card.location.mode — periodic GNSS tracking every 10 minutes.
    // The Notecard's built-in accelerometer gates the GPS radio so it only
    // acquires a new fix when the trailer is moving.
    {
        J *req = notecard.newRequest("card.location.mode");
        if (req == NULL) return false;
        JAddStringToObject(req, "mode",    "periodic");
        JAddNumberToObject(req, "seconds", 600);
        J *rsp = notecard.requestAndResponse(req);
        if (rsp == NULL) {
            DEBUG_PRINTLN(F("[cfg] card.location.mode failed (null response)"));
            return false;
        }
        if (notecard.responseError(rsp)) {
            DEBUG_PRINTLN(F("[cfg] card.location.mode returned error"));
            notecard.deleteResponse(rsp);
            return false;
        }
        notecard.deleteResponse(rsp);
    }

    return true;
}

// =============================================================================
// Define Note templates — single-file alert + cellular-only log and summary
//
// Three notefiles implement the data transport strategy:
//
//   NOTEFILE_ALERT   (compact, port, no delete:true)
//     Critical alerts.  Compact encoding with a port number satisfies NTN
//     requirements and is supported on cellular too.  No delete:true: a queued
//     alert is never purged — it is delivered over the first available
//     transport (cellular preferred; NTN satellite as automatic fallback).
//
//   NOTEFILE_LOG     (no format/port, delete:true)
//     Per-sample records (temperature + door state every sample cycle).
//     Non-NTN-compatible template (no format/port) + delete:true: the
//     Notecard discards queued notes at sync time when NTN is the active
//     transport — per-sample data never consumes the 10 KB satellite budget.
//     (Blues Satellite Best Practices: dev.blues.io/starnote/
//     satellite-best-practices/)
//
//   NOTEFILE_SUMMARY (no format/port, delete:true)
//     Hourly aggregates (mean/min/max per probe, door event count).
//     Same non-NTN + delete:true pattern: stale summaries queued during
//     NTN-only coverage are discarded at sync time, not routed over satellite.
//
// Returns true when all three templates are registered successfully.
// =============================================================================
bool defineTemplates(void) {
    // ── Alert template (NTN-compatible, delivered over any transport) ─────────
    {
        J *req = notecard.newRequest("note.template");
        if (req == NULL) return false;
        JAddStringToObject(req, "file",   NOTEFILE_ALERT);
        JAddNumberToObject(req, "port",   TEMPLATE_PORT_ALERT);
        JAddStringToObject(req, "format", "compact");
        // No delete:true — alert notes are never purged; they are delivered over
        // whichever transport (cellular or NTN) is available when hub.sync fires.
        J *body = JAddObjectToObject(req, "body");
        // String field: exemplar length = longest expected value
        // ("temp_excursion", 14 chars — ties "door_open_long" at 14 chars).
        JAddStringToObject(body, "alert",         "temp_excursion");
        JAddNumberToObject(body, "t1_c",           TFLOAT32);
        JAddNumberToObject(body, "t2_c",           TFLOAT32);
        JAddBoolToObject(body,   "door_open",      TBOOL);
        JAddNumberToObject(body, "door_open_sec",  TUINT32);
        JAddNumberToObject(body, "lat",            TFLOAT32);
        JAddNumberToObject(body, "lon",            TFLOAT32);
        J *rsp = notecard.requestAndResponse(req);
        if (rsp == NULL) {
            DEBUG_PRINTLN(F("[cfg] note.template alert failed (null)"));
            return false;
        }
        if (notecard.responseError(rsp)) {
            DEBUG_PRINTLN(F("[cfg] note.template alert error"));
            notecard.deleteResponse(rsp);
            return false;
        }
        notecard.deleteResponse(rsp);
    }

    // ── Per-sample log template (cellular/WiFi only) ──────────────────────────
    {
        J *req = notecard.newRequest("note.template");
        if (req == NULL) return false;
        JAddStringToObject(req, "file",   NOTEFILE_LOG);
        JAddBoolToObject(req,   "delete", true);
        J *body = JAddObjectToObject(req, "body");
        JAddNumberToObject(body, "t1_c",      TFLOAT32);
        JAddNumberToObject(body, "t2_c",      TFLOAT32);
        JAddBoolToObject(body,   "door_open", TBOOL);
        J *rsp = notecard.requestAndResponse(req);
        if (rsp == NULL) {
            DEBUG_PRINTLN(F("[cfg] note.template log failed (null)"));
            return false;
        }
        if (notecard.responseError(rsp)) {
            DEBUG_PRINTLN(F("[cfg] note.template log error"));
            notecard.deleteResponse(rsp);
            return false;
        }
        notecard.deleteResponse(rsp);
    }

    // ── Hourly summary template (cellular/WiFi only) ──────────────────────────
    {
        J *req = notecard.newRequest("note.template");
        if (req == NULL) return false;
        JAddStringToObject(req, "file",   NOTEFILE_SUMMARY);
        JAddBoolToObject(req,   "delete", true);
        J *body = JAddObjectToObject(req, "body");
        JAddNumberToObject(body, "t1_c",        TFLOAT32);
        JAddNumberToObject(body, "t2_c",        TFLOAT32);
        JAddNumberToObject(body, "t1_min_c",    TFLOAT32);
        JAddNumberToObject(body, "t1_max_c",    TFLOAT32);
        JAddNumberToObject(body, "t2_min_c",    TFLOAT32);
        JAddNumberToObject(body, "t2_max_c",    TFLOAT32);
        JAddNumberToObject(body, "door_events", TINT16);
        JAddBoolToObject(body,   "door_open",   TBOOL);
        J *rsp = notecard.requestAndResponse(req);
        if (rsp == NULL) {
            DEBUG_PRINTLN(F("[cfg] note.template summary failed (null)"));
            return false;
        }
        if (notecard.responseError(rsp)) {
            DEBUG_PRINTLN(F("[cfg] note.template summary error"));
            notecard.deleteResponse(rsp);
            return false;
        }
        notecard.deleteResponse(rsp);
    }

    return true;
}

// =============================================================================
// Re-apply hub.set if summary_interval_min changed via environment variable
//
// s.summary_interval_min is only updated after a confirmed successful request
// so a transient Notecard error never silently leaves the outbound cadence out
// of sync with the locally-applied summary interval.
// =============================================================================
void applyHubSetIfChanged(AppState &s) {
    if (g_summaryIntervalMin == s.summary_interval_min) return;
    J *req = notecard.newRequest("hub.set");
    if (req == NULL) return;
    JAddNumberToObject(req, "outbound", (int)g_summaryIntervalMin);
    JAddNumberToObject(req, "inbound",  120);
    J *rsp = notecard.requestAndResponse(req);
    if (rsp == NULL) {
        DEBUG_PRINTLN(F("[cfg] hub.set update failed (null response)"));
        return;
    }
    if (notecard.responseError(rsp)) {
        DEBUG_PRINTLN(F("[cfg] hub.set update returned error"));
        notecard.deleteResponse(rsp);
        return;
    }
    notecard.deleteResponse(rsp);
    s.summary_interval_min = g_summaryIntervalMin;
    DEBUG_PRINT(F("[cfg] hub.set outbound updated to "));
    DEBUG_PRINTLN(g_summaryIntervalMin);
}

// =============================================================================
// Fetch Notehub environment variable overrides
//
// All variables are optional; firmware defaults apply for any that are absent.
// Each value is validated with strtof/strtol and an end-pointer check: a clean
// parse requires the entire string to be consumed (endptr points at the NUL
// terminator after conversion).  Non-numeric strings like "banana" leave endptr
// at the start of the string, so they are rejected and the prior value kept.
// Values that parse cleanly are further clamped to a safe operational range.
// =============================================================================
void fetchEnvOverrides(void) {
    J *req = notecard.newRequest("env.get");
    if (req == NULL) return;
    J *rsp = notecard.requestAndResponse(req);
    if (rsp == NULL) return;
    if (notecard.responseError(rsp)) {
        notecard.deleteResponse(rsp);
        return;
    }

    J *body = JGetObjectItem(rsp, "body");
    if (body != NULL) {
        const char *v;
        char       *endp;

        // Parse both temperature thresholds into temporaries and validate the
        // pair before committing.  If only one var is present, the temporary for
        // the absent var retains the current runtime value so a single-threshold
        // update is safe.  If the resulting pair is invalid (min >= max) the
        // current runtime values are kept unchanged; only the bad update is
        // discarded, not the existing fleet-specific thresholds.
        float newTempMax = g_tempMaxC;
        float newTempMin = g_tempMinC;

        v = JGetString(body, "temp_max_c");
        if (v && *v) {
            float fv = strtof(v, &endp);
            if (endp && *endp == '\0')
                newTempMax = clampF((double)fv, -30.0, 50.0, g_tempMaxC);
        }

        v = JGetString(body, "temp_min_c");
        if (v && *v) {
            float fv = strtof(v, &endp);
            if (endp && *endp == '\0')
                newTempMin = clampF((double)fv, -60.0, 20.0, g_tempMinC);
        }

        if (newTempMin < newTempMax) {
            g_tempMinC = newTempMin;
            g_tempMaxC = newTempMax;
        } else if (newTempMin != g_tempMinC || newTempMax != g_tempMaxC) {
            // Warn only when an env-var update actually produced the crossing —
            // no noise on cycles where neither threshold was set remotely.
            DEBUG_PRINTLN(F("[env] temp_min_c >= temp_max_c — ignoring, keeping current thresholds"));
        }

        v = JGetString(body, "door_alert_sec");
        if (v && *v) {
            long lv = strtol(v, &endp, 10);
            if (endp && *endp == '\0')
                g_doorAlertSec = clampU32(lv, 30, 86400, g_doorAlertSec);
        }

        v = JGetString(body, "sample_interval_sec");
        if (v && *v) {
            long lv = strtol(v, &endp, 10);
            if (endp && *endp == '\0')
                g_sampleIntervalSec = clampU32(lv, 10, 3600, g_sampleIntervalSec);
        }

        v = JGetString(body, "summary_interval_min");
        if (v && *v) {
            long lv = strtol(v, &endp, 10);
            if (endp && *endp == '\0')
                g_summaryIntervalMin = clampU32(lv, 1, 1440, g_summaryIntervalMin);
        }

        v = JGetString(body, "alert_cooldown_sec");
        if (v && *v) {
            long lv = strtol(v, &endp, 10);
            if (endp && *endp == '\0')
                g_alertCooldownSec = clampU32(lv, 60, 86400, g_alertCooldownSec);
        }
    }
    notecard.deleteResponse(rsp);
}

// =============================================================================
// Sensor reading helpers
// =============================================================================

// Reads both DS18B20 probes.  Returns true if at least one probe responded.
// requestTemperatures() blocks ~750 ms at 12-bit resolution; a disconnected or
// failed probe returns TEMP_INVALID (-127 °C).
bool readTemperatures(float &t1, float &t2) {
    probes.requestTemperatures();
    t1 = probes.getTempCByIndex(0);
    t2 = probes.getTempCByIndex(1);
    bool ok1 = (t1 > TEMP_INVALID + 1.0f);
    bool ok2 = (t2 > TEMP_INVALID + 1.0f);
    if (!ok1) { t1 = TEMP_INVALID; DEBUG_PRINTLN(F("[sensor] Probe 1 not responding")); }
    if (!ok2) { t2 = TEMP_INVALID; DEBUG_PRINTLN(F("[sensor] Probe 2 not responding")); }
    return (ok1 || ok2);
}

// Returns true when the door is open.
// HIGH = door open  (magnet absent → NO switch open → INPUT_PULLUP high)
// LOW  = door closed (magnet present → switch closed → pin to GND)
bool readDoorState(void) {
    return (digitalRead(DOOR_PIN) == HIGH);
}

// =============================================================================
// Door state machine
//
// Detects three conditions:
//   1. closed → open  : immediate door_open alert
//   2. open → closed  : door_close alert with duration
//   3. still open beyond door_alert_sec : one door_open_long per open event
//
// Pending-alert pattern:
//   Physical door state (door_open, door_opened_epoch) is advanced immediately
//   on transition so the same edge is never re-detected. The alert is not
//   considered delivered until sendAlert() returns true. Until then,
//   pending_door_alert / pending_door_open_sec persist in AppState so the next
//   wake retries the exact same event without loss.
//
//   pending_door_sent tracks whether the alert note was successfully enqueued
//   on a prior wake.  On a retry the note.add is skipped (preventing a
//   duplicate) and only hub.sync is reattempted.
//
//   On each call, any pending alert from a prior cycle is retried first; then
//   new transitions are evaluated so a stuck retry never swallows a close event.
//   If a new transition overwrites a still-pending older one, the newer event
//   takes precedence (e.g. door closes before the open alert is acked — the
//   close alert with full open-duration is more actionable).
//
//   Exception — DOOR_PENDING_LONG retry idempotency:
//   The long-open detection branch is guarded on
//   pending_door_alert == DOOR_PENDING_NONE.  When a DOOR_PENDING_LONG retry
//   fails and execution falls through to the transition-evaluation block, the
//   door is still open so neither the open-transition nor close-transition
//   branch fires; without the guard the long-open branch would re-evaluate,
//   refreshing pending_door_open_sec / lat / lon and calling sendAlert() a
//   second time in the same call — enqueuing a duplicate note and mutating the
//   original payload.  The DOOR_PENDING_NONE guard prevents this; a genuine
//   close event is still caught by the open→closed branch above the guard.
// =============================================================================
void checkDoorEvents(AppState &s, float t1, float t2,
                     bool doorOpen, uint32_t now) {
    // ── Retry any pending alert from a previous cycle ─────────────────────────
    if (s.pending_door_alert != DOOR_PENDING_NONE) {
        const char *retryType  = NULL;
        bool  retryDoorOpen    = false;
        switch (s.pending_door_alert) {
            case DOOR_PENDING_OPEN:
                retryType = "door_open";      retryDoorOpen = true;  break;
            case DOOR_PENDING_CLOSE:
                retryType = "door_close";     retryDoorOpen = false; break;
            case DOOR_PENDING_LONG:
                retryType = "door_open_long"; retryDoorOpen = true;  break;
            default:
                s.pending_door_alert = DOOR_PENDING_NONE; break;
        }
        if (retryType != NULL &&
            sendAlert(retryType, t1, t2, retryDoorOpen, s.pending_door_open_sec,
                      s.pending_door_lat, s.pending_door_lon,
                      s.pending_door_sent)) {
            if (s.pending_door_alert == DOOR_PENDING_LONG) {
                s.door_long_alert_sent = true;
            }
            s.pending_door_alert = DOOR_PENDING_NONE;
            DEBUG_PRINT(F("[door] Pending alert delivered: ")); DEBUG_PRINTLN(retryType);
        }
        // Continue to new-transition evaluation even if retry failed so a close
        // event is never blocked behind a stuck open-alert retry.
    }

    // ── Evaluate new transitions ──────────────────────────────────────────────
    if (doorOpen && !s.door_open) {
        // Transition: closed → open
        s.door_open              = true;
        s.door_opened_epoch      = now;
        s.door_long_alert_sent   = false;
        s.door_events++;
        s.pending_door_open_sec  = 0;
        s.pending_door_sent      = false;   // fresh alert — reset enqueue flag
        getLocation(s.pending_door_lat, s.pending_door_lon);
        s.pending_door_alert     = DOOR_PENDING_OPEN;
        DEBUG_PRINTLN(F("[door] Door opened — alerting"));
        if (sendAlert("door_open", t1, t2, true, 0,
                      s.pending_door_lat, s.pending_door_lon,
                      s.pending_door_sent)) {
            s.pending_door_alert = DOOR_PENDING_NONE;
        }

    } else if (!doorOpen && s.door_open) {
        // Transition: open → closed
        uint32_t openSec = (now > s.door_opened_epoch)
                           ? (now - s.door_opened_epoch) : 0;
        s.door_open              = false;
        s.pending_door_open_sec  = openSec;
        s.pending_door_sent      = false;   // fresh alert — reset enqueue flag
        getLocation(s.pending_door_lat, s.pending_door_lon);
        s.pending_door_alert     = DOOR_PENDING_CLOSE;
        DEBUG_PRINT(F("[door] Door closed after ")); DEBUG_PRINT(openSec);
        DEBUG_PRINTLN(F(" s"));
        if (sendAlert("door_close", t1, t2, false, openSec,
                      s.pending_door_lat, s.pending_door_lon,
                      s.pending_door_sent)) {
            s.pending_door_alert = DOOR_PENDING_NONE;
        }

    } else if (doorOpen && s.door_open && !s.door_long_alert_sent &&
               s.pending_door_alert == DOOR_PENDING_NONE) {
        // Door still open and no long-open alert currently pending —
        // check whether the long-open threshold has been crossed.
        uint32_t openSec = (now > s.door_opened_epoch)
                           ? (now - s.door_opened_epoch) : 0;
        if (openSec >= g_doorAlertSec) {
            s.pending_door_open_sec  = openSec;
            s.pending_door_sent      = false;   // fresh alert — reset enqueue flag
            getLocation(s.pending_door_lat, s.pending_door_lon);
            s.pending_door_alert     = DOOR_PENDING_LONG;
            DEBUG_PRINT(F("[door] Door open too long (")); DEBUG_PRINT(openSec);
            DEBUG_PRINTLN(F(" s) — alerting"));
            if (sendAlert("door_open_long", t1, t2, true, openSec,
                          s.pending_door_lat, s.pending_door_lon,
                          s.pending_door_sent)) {
                s.door_long_alert_sent = true;
                s.pending_door_alert   = DOOR_PENDING_NONE;
            }
        }
    }
}

// =============================================================================
// Temperature excursion detection
//
// Fires one alert per cooldown window when either probe breaches the warm or
// cold threshold.
//
// Pending-alert pattern (mirrors door-event retry):
//   When an excursion is detected the alert is latched into AppState
//   (pending_temp_alert, pending_temp_t1/t2, pending_temp_door_open,
//   pending_temp_lat/lon) before sendAlert() is called.  If sendAlert() fails,
//   the latch persists across sleep so the next wake retries with the original
//   detection readings and location.  This preserves brief excursions that
//   return to normal before the retry wake and whose initial send failed.
//   pending_temp_sent tracks whether the alert note was successfully enqueued
//   on a prior wake, preventing a duplicate note on a hub.sync retry.
//
// Cooldown behaviour:
//   last_temp_alert_epoch == 0 means no alert has been delivered yet; the
//   cooldown check is skipped so the very first excursion is never suppressed.
//   The cooldown epoch is set to "now" only after the note is confirmed
//   enqueued, so delayed delivery (after retries) still correctly gates the
//   next cooldown window from the delivery time.
// =============================================================================
void checkTemperatureExcursion(AppState &s, float t1, float t2, uint32_t now) {
    // ── Retry any pending temperature alert from a previous cycle ─────────────
    if (s.pending_temp_alert != TEMP_PENDING_NONE) {
        const char *retryType = (s.pending_temp_alert == TEMP_PENDING_WARM)
                                ? "temp_excursion" : "temp_cold";
        if (sendAlert(retryType,
                      s.pending_temp_t1, s.pending_temp_t2,
                      s.pending_temp_door_open, 0,
                      s.pending_temp_lat, s.pending_temp_lon,
                      s.pending_temp_sent)) {
            s.last_temp_alert_epoch = now;
            s.pending_temp_alert    = TEMP_PENDING_NONE;
            DEBUG_PRINT(F("[temp] Pending alert delivered: "));
            DEBUG_PRINTLN(retryType);
        }
        // Whether retry succeeded or not, skip new-excursion detection this
        // cycle: the pending event has priority and detecting a new one would
        // overwrite the undelivered latch.
        return;
    }

    // Cooldown: only active after the first alert has been delivered.
    // last_temp_alert_epoch == 0 means no alert has fired yet — skip the check
    // so the very first excursion is never suppressed.
    if (s.last_temp_alert_epoch != 0 &&
        (now - s.last_temp_alert_epoch) < g_alertCooldownSec) {
        return;
    }

    bool t1v = (t1 > TEMP_INVALID + 1.0f);
    bool t2v = (t2 > TEMP_INVALID + 1.0f);

    if ((t1v && t1 > g_tempMaxC) || (t2v && t2 > g_tempMaxC)) {
        DEBUG_PRINT(F("[temp] Warm excursion — t1="));
        DEBUG_PRINT(t1); DEBUG_PRINT(F(" t2=")); DEBUG_PRINTLN(t2);
        // Latch before calling sendAlert so the event survives a failed send.
        s.pending_temp_alert     = TEMP_PENDING_WARM;
        s.pending_temp_t1        = t1;
        s.pending_temp_t2        = t2;
        s.pending_temp_door_open = s.door_open;
        s.pending_temp_sent      = false;
        getLocation(s.pending_temp_lat, s.pending_temp_lon);
        if (sendAlert("temp_excursion", t1, t2, s.door_open, 0,
                      s.pending_temp_lat, s.pending_temp_lon,
                      s.pending_temp_sent)) {
            s.last_temp_alert_epoch = now;
            s.pending_temp_alert    = TEMP_PENDING_NONE;
        }
    } else if ((t1v && t1 < g_tempMinC) || (t2v && t2 < g_tempMinC)) {
        DEBUG_PRINT(F("[temp] Cold excursion — t1="));
        DEBUG_PRINT(t1); DEBUG_PRINT(F(" t2=")); DEBUG_PRINTLN(t2);
        // Latch before calling sendAlert so the event survives a failed send.
        s.pending_temp_alert     = TEMP_PENDING_COLD;
        s.pending_temp_t1        = t1;
        s.pending_temp_t2        = t2;
        s.pending_temp_door_open = s.door_open;
        s.pending_temp_sent      = false;
        getLocation(s.pending_temp_lat, s.pending_temp_lon);
        if (sendAlert("temp_cold", t1, t2, s.door_open, 0,
                      s.pending_temp_lat, s.pending_temp_lon,
                      s.pending_temp_sent)) {
            s.last_temp_alert_epoch = now;
            s.pending_temp_alert    = TEMP_PENDING_NONE;
        }
    }
}

// =============================================================================
// Accumulate samples into the current summary window
// =============================================================================
void accumulateSummary(AppState &s, float t1, float t2) {
    if (t1 > TEMP_INVALID + 1.0f) {
        s.t1_sum_c += t1;
        s.t1_count++;
        if (t1 < s.t1_min_c) s.t1_min_c = t1;
        if (t1 > s.t1_max_c) s.t1_max_c = t1;
    }
    if (t2 > TEMP_INVALID + 1.0f) {
        s.t2_sum_c += t2;
        s.t2_count++;
        if (t2 < s.t2_min_c) s.t2_min_c = t2;
        if (t2 > s.t2_max_c) s.t2_max_c = t2;
    }
}

// =============================================================================
// Send hourly summary note (cellular / WiFi only)
//
// Writes to NOTEFILE_SUMMARY (non-NTN-compatible, delete:true). The Notecard
// discards queued notes from this file at sync time when NTN is the active
// transport, so stale summaries never consume satellite budget.
// No sync:true — regular summaries don't justify waking the radio.
//
// Returns true when the note is successfully enqueued; the caller should reset
// summary accumulators only on a true return.
// =============================================================================
bool sendSummary(AppState &s, bool doorOpen) {
    float t1m = (s.t1_count > 0) ? (s.t1_sum_c / (float)s.t1_count) : TEMP_INVALID;
    float t2m = (s.t2_count > 0) ? (s.t2_sum_c / (float)s.t2_count) : TEMP_INVALID;

    J *req = notecard.newRequest("note.add");
    if (req == NULL) return false;
    JAddStringToObject(req, "file", NOTEFILE_SUMMARY);
    J *body = JAddObjectToObject(req, "body");
    JAddNumberToObject(body, "t1_c",       (double)t1m);
    JAddNumberToObject(body, "t2_c",       (double)t2m);
    JAddNumberToObject(body, "t1_min_c",
        (double)((s.t1_count > 0) ? s.t1_min_c : TEMP_INVALID));
    JAddNumberToObject(body, "t1_max_c",
        (double)((s.t1_count > 0) ? s.t1_max_c : TEMP_INVALID));
    JAddNumberToObject(body, "t2_min_c",
        (double)((s.t2_count > 0) ? s.t2_min_c : TEMP_INVALID));
    JAddNumberToObject(body, "t2_max_c",
        (double)((s.t2_count > 0) ? s.t2_max_c : TEMP_INVALID));
    JAddNumberToObject(body, "door_events", (double)s.door_events);
    JAddBoolToObject(body,   "door_open",   doorOpen);

    J *rsp = notecard.requestAndResponse(req);
    if (rsp == NULL) {
        DEBUG_PRINTLN(F("[summary] note.add failed (null response)"));
        return false;
    }
    if (notecard.responseError(rsp)) {
        DEBUG_PRINTLN(F("[summary] note.add returned error"));
        notecard.deleteResponse(rsp);
        return false;
    }
    notecard.deleteResponse(rsp);
    DEBUG_PRINTLN(F("[summary] Summary queued"));
    return true;
}

// =============================================================================
// Send an immediate-sync alert note
//
// Writes to NOTEFILE_ALERT (compact + port, NTN-compatible) and then issues a
// hub.sync request to wake the radio immediately.  The Notecard selects the
// best available transport — cellular when in range, Skylo NTN satellite as
// automatic fallback — with no firmware involvement.
//
// hub.sync is treated as part of the critical alert path.  If it fails this
// function returns false so the caller keeps the pending-alert latch set and
// the next wake retries.  On the retry the note.add call is skipped (done is
// already true from the prior successful enqueue) and only hub.sync is
// reattempted.
//
// lat/lon must be the GNSS coordinates captured at event-detection time
// (stored in AppState.pending_*_lat/lon).  Passing them as parameters rather
// than calling getLocation() here ensures retry calls carry the location where
// the event occurred, not the trailer's current position after it has moved.
// Both are 0.0 before a GNSS fix is acquired; downstream systems should treat
// (lat == 0.0 && lon == 0.0) as a no-fix sentinel.
//
// done is an in-out flag owned by the caller and persisted in AppState across
// sleep cycles.  It is false when a new alert is first latched and is set to
// true here only on a confirmed successful enqueue.  A retry call skips
// note.add when done is already true, preventing duplicate notes if the prior
// wake enqueued successfully but failed on hub.sync.
//
// Returns true only when the note is enqueued AND hub.sync succeeds.  Callers
// treat a false return as a retriable failure and retry on the next wake.
// =============================================================================
bool sendAlert(const char *type, float t1, float t2,
               bool doorOpen, uint32_t openSec, float lat, float lon,
               bool &done) {

    // ── Enqueue alert note ────────────────────────────────────────────────────
    // Skip if already successfully enqueued on a prior wake (retry path).
    if (!done) {
        J *req = notecard.newRequest("note.add");
        if (req != NULL) {
            JAddStringToObject(req, "file", NOTEFILE_ALERT);
            J *body = JAddObjectToObject(req, "body");
            JAddStringToObject(body, "alert",         type);
            JAddNumberToObject(body, "t1_c",          (double)t1);
            JAddNumberToObject(body, "t2_c",          (double)t2);
            JAddBoolToObject(body,   "door_open",     doorOpen);
            JAddNumberToObject(body, "door_open_sec", (double)openSec);
            JAddNumberToObject(body, "lat",           (double)lat);
            JAddNumberToObject(body, "lon",           (double)lon);
            J *rsp = notecard.requestAndResponse(req);
            if (rsp != NULL) {
                if (!notecard.responseError(rsp)) { done = true; }
                notecard.deleteResponse(rsp);
            }
        }
    }

    // ── Immediate sync ────────────────────────────────────────────────────────
    // Attempt hub.sync once the alert note is confirmed enqueued — whether
    // enqueued on this call or on a prior wake.  Check the response for NULL
    // and responseError: a failed hub.sync returns false so the caller keeps
    // the pending-alert latch set and retries on the next wake.
    bool sync_ok = false;
    if (done) {
        J *req = notecard.newRequest("hub.sync");
        if (req != NULL) {
            J *rsp = notecard.requestAndResponse(req);
            if (rsp != NULL) {
                if (!notecard.responseError(rsp)) {
                    sync_ok = true;
                } else {
                    DEBUG_PRINTLN(F("[alert] hub.sync returned error — will retry"));
                }
                notecard.deleteResponse(rsp);
            } else {
                DEBUG_PRINTLN(F("[alert] hub.sync failed (null response) — will retry"));
            }
        }
    }

    bool ok = done && sync_ok;
    if (ok) {
        DEBUG_PRINT(F("[alert] ")); DEBUG_PRINTLN(type);
    } else {
        DEBUG_PRINT(F("[alert] partial/failed (enqueued="));
        DEBUG_PRINT(done); DEBUG_PRINT(F(" sync="));
        DEBUG_PRINT(sync_ok); DEBUG_PRINT(F("): "));
        DEBUG_PRINTLN(type);
    }
    return ok;
}

// =============================================================================
// Send a per-sample log note (cellular/WiFi only — best effort)
//
// Writes one note to NOTEFILE_LOG on every sample cycle.  The template carries
// delete:true (non-NTN-compatible; no format/port): the Notecard discards
// queued log notes at sync time when NTN is the active transport — per-sample
// data never consumes the 10 KB bundled satellite data budget.
// Log notes accumulate in the Notecard's on-device queue and flush on the next
// outbound session (hourly by default).
//
// A transient note.add failure silently drops the sample.  Log notes are
// best-effort and do not use the pending-alert retry pattern; the minor loss
// of an occasional sample is acceptable for a trend/compliance record.
// =============================================================================
void sendLog(float t1, float t2, bool doorOpen) {
    J *req = notecard.newRequest("note.add");
    if (req == NULL) return;
    JAddStringToObject(req, "file", NOTEFILE_LOG);
    J *body = JAddObjectToObject(req, "body");
    JAddNumberToObject(body, "t1_c",      (double)t1);
    JAddNumberToObject(body, "t2_c",      (double)t2);
    JAddBoolToObject(body,   "door_open", doorOpen);
    J *rsp = notecard.requestAndResponse(req);
    if (rsp != NULL) {
        if (notecard.responseError(rsp)) {
            DEBUG_PRINTLN(F("[log] note.add error — sample dropped"));
        } else {
            DEBUG_PRINTLN(F("[log] Sample logged"));
        }
        notecard.deleteResponse(rsp);
    } else {
        DEBUG_PRINTLN(F("[log] note.add null response — sample dropped"));
    }
}

// =============================================================================
// Get the current UTC epoch time from the Notecard's built-in RTC
//
// The Notecard RTC is synchronised on every cellular or satellite session.
// Returns 0 if the clock is not yet synced or if the request fails.
// =============================================================================
uint32_t getEpochTime(void) {
    J *req = notecard.newRequest("card.time");
    if (req == NULL) return 0;                   // allocation guard (item 7)
    J *rsp = notecard.requestAndResponse(req);
    if (rsp == NULL) return 0;
    if (notecard.responseError(rsp)) {
        notecard.deleteResponse(rsp);
        return 0;
    }
    uint32_t t = (uint32_t)JGetNumber(rsp, "time");
    notecard.deleteResponse(rsp);
    return t;
}

// =============================================================================
// Get the last known location from the Notecard GNSS receiver
//
// card.location.mode is set to "periodic" / 600 s in hubConfigure() so the
// Notecard attempts a fresh fix every 10 minutes when the trailer is moving.
// Returns true when a valid fix is available; false and 0.0/0.0 otherwise.
// =============================================================================
bool getLocation(float &lat, float &lon) {
    lat = 0.0f;
    lon = 0.0f;
    J *req = notecard.newRequest("card.location");
    if (req == NULL) return false;               // allocation guard (item 7)
    J *rsp = notecard.requestAndResponse(req);
    if (rsp == NULL) return false;
    if (notecard.responseError(rsp)) {
        notecard.deleteResponse(rsp);
        return false;
    }
    lat = (float)JGetNumber(rsp, "lat");
    lon = (float)JGetNumber(rsp, "lon");
    notecard.deleteResponse(rsp);
    return (lat != 0.0f || lon != 0.0f);
}
