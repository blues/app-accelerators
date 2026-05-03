// tote_pool_tracker_helpers.cpp
// Helper implementations for the Tote Pool Tracker firmware.
//
// See tote_pool_tracker_helpers.h for documentation of each function.
// All functions access the globals (g_state, notecard, g_*) that are defined
// in tote_pool_tracker.ino and declared extern in the header.

#include "tote_pool_tracker_helpers.h"

// ===========================================================================
// Internal send helpers — not part of the public API
//
// doSendMotionNote / doSendHeartbeatNote carry out the actual note.add I/O.
// They read their payload exclusively from g_state.pending_* so that both
// fresh sends (via sendMotionEvent / sendHeartbeat) and retries (via
// resendPendingNote) use the same code path and the same original payload.
// On confirmed success the helper clears g_state.pending_note_type; on
// final failure it leaves the pending record intact for the next wake.
// ===========================================================================

// ---------------------------------------------------------------------------
// doSendMotionNote
// Attempt up to MAX_TRIES note.add calls for g_state.pending_event.
// sync:true causes the Notecard to open a cellular session immediately so
// the departure / arrival event arrives at Notehub within seconds.
// ---------------------------------------------------------------------------
static bool doSendMotionNote() {
    const uint8_t MAX_TRIES = 3;
    for (uint8_t attempt = 0; attempt < MAX_TRIES; attempt++) {
        if (attempt > 0) delay(500); // brief back-off before retry

        J *req = notecard.newRequest("note.add");
        JAddStringToObject(req, "file", FILE_EVENTS);
        JAddBoolToObject(req,   "sync", true); // immediate delivery

        J *body = JAddObjectToObject(req, "body");
        JAddStringToObject(body, "event",      g_state.pending_event);
        JAddBoolToObject(body,   "moving",     g_state.pending_moving);
        JAddNumberToObject(body, "battery_mv", g_state.pending_battery_mv);
        JAddNumberToObject(body, "cycle",      (int)g_state.pending_cycle);

        J *rsp = notecard.requestAndResponse(req);
        if (!rsp) continue;
        bool ok = !notecard.responseError(rsp);
        notecard.deleteResponse(rsp);
        if (ok) {
            g_state.pending_note_type = PENDING_NONE;
            return true;
        }
    }
#ifdef DEBUG
    Serial.print(F("[WARN] note.add failed after 3 attempts ("));
    Serial.print(FILE_EVENTS);
    Serial.println(F(") — will retry on short wake"));
#endif
    return false;
}

// ---------------------------------------------------------------------------
// doSendHeartbeatNote
// Attempt up to MAX_TRIES note.add calls for g_state.pending heartbeat.
// sync:true causes the Notecard to open a cellular session immediately, so
// every heartbeat arrives in Notehub within a session-establishment window of
// the on-device timer firing — not deferred to the next periodic outbound sync.
// This guarantees daily cloud-visible delivery regardless of any intervening
// motion-triggered sessions.
// ---------------------------------------------------------------------------
static bool doSendHeartbeatNote() {
    const uint8_t MAX_TRIES = 3;
    for (uint8_t attempt = 0; attempt < MAX_TRIES; attempt++) {
        if (attempt > 0) delay(500);

        J *req = notecard.newRequest("note.add");
        JAddStringToObject(req, "file", FILE_HEARTBEAT);
        JAddBoolToObject(req,   "sync", true); // immediate delivery

        J *body = JAddObjectToObject(req, "body");
        JAddNumberToObject(body, "battery_mv",  g_state.pending_battery_mv);
        JAddBoolToObject(body,   "moving",      g_state.pending_moving);
        JAddNumberToObject(body, "cycle_count", (int)g_state.pending_cycle);
        JAddNumberToObject(body, "reason",      g_state.pending_reason);

        J *rsp = notecard.requestAndResponse(req);
        if (!rsp) continue;
        bool ok = !notecard.responseError(rsp);
        notecard.deleteResponse(rsp);
        if (ok) {
            g_state.pending_note_type = PENDING_NONE;
            return true;
        }
    }
#ifdef DEBUG
    Serial.print(F("[WARN] note.add failed after 3 attempts ("));
    Serial.print(FILE_HEARTBEAT);
    Serial.println(F(") — will retry on short wake"));
#endif
    return false;
}

// ===========================================================================
// notecardConfigure
// One-time Notecard setup called on cold boot. product_uid is passed in from
// the .ino so this translation unit never needs to resolve the PRODUCT_UID
// macro directly. Each setting updates the corresponding g_state.*_confirmed /
// g_state.last_applied_* flag only after a verified success, so a transient
// cold-boot I²C race leaves flags false and the reapply block in setup()
// retries them on every subsequent wake until confirmed.
// ===========================================================================
void notecardConfigure(const char *product_uid) {
    // Seed the desired-env cache with compile-time defaults. fetchEnvOverrides()
    // will overwrite these on the first successful env.get; if env.get ever
    // fails on a later wake, the persisted desired_* values (not the
    // compile-time literals) are restored to the g_* globals so fleet tuning
    // is preserved across the outage.
    g_state.desired_heartbeat_hours   = DEFAULT_HEARTBEAT_HOURS;
    g_state.desired_low_battery_mv    = (float)DEFAULT_LOW_BATTERY_MV;
    g_state.desired_motion_threshold  = DEFAULT_MOTION_THRESHOLD;
    g_state.desired_motion_bucket_sec = DEFAULT_MOTION_BUCKET_SEC;

    // hub.set — periodic mode; outbound/inbound cadence matches the default
    // heartbeat interval. Motion events bypass this via sync:true.
    // sendRequestWithRetry papers over the cold-boot I²C race where the host
    // MCU comes up before the Notecard is fully ready.
    J *req = notecard.newRequest("hub.set");
    if (product_uid && product_uid[0]) JAddStringToObject(req, "product", product_uid);
    JAddStringToObject(req, "mode",     "periodic");
    JAddNumberToObject(req, "outbound", (int)(DEFAULT_HEARTBEAT_HOURS * 60));
    JAddNumberToObject(req, "inbound",  (int)(DEFAULT_HEARTBEAT_HOURS * 60));
    if (notecard.sendRequestWithRetry(req, 10)) {
        g_state.last_applied_heartbeat_hours = DEFAULT_HEARTBEAT_HOURS;
    }

    // card.voltage — set LiPo thresholds so the Notecard's voltage state
    // machine (usb/high/normal/low/dead) maps correctly to a 3.7 V LiPo
    // discharge curve. voltage_mode_confirmed is set only on a verified
    // non-error response; the retry block in setup() re-applies on each
    // subsequent wake until confirmed.
    req = notecard.newRequest("card.voltage");
    JAddStringToObject(req, "mode", "lipo");
    J *rsp = notecard.requestAndResponse(req);
    if (rsp) {
        if (!notecard.responseError(rsp)) g_state.voltage_mode_confirmed = true;
        notecard.deleteResponse(rsp);
    }

    // card.triangulate — enable cell-tower + WiFi AP triangulation. Notehub
    // resolves both sources to lat/lon and appends where_lat/where_lon to
    // every event automatically. WiFi AP data improves accuracy in AP-dense
    // environments (warehouses, distribution centers) from km-scale to tens
    // of meters; falls back to cell-only where no APs are visible.
    // triangulate_confirmed is set only on verified success; the retry block
    // in setup() re-applies on each wake until confirmed.
    req = notecard.newRequest("card.triangulate");
    JAddStringToObject(req, "mode", "wifi,cell");
    JAddBoolToObject(req, "on",  true);
    JAddBoolToObject(req, "set", true);
    rsp = notecard.requestAndResponse(req);
    if (rsp) {
        if (!notecard.responseError(rsp)) g_state.triangulate_confirmed = true;
        notecard.deleteResponse(rsp);
    }

    // card.motion.mode — start the Notecard's built-in accelerometer.
    // motion:5 means 5 accelerometer events must occur within a 30-second
    // bucket before the motion status transitions to "moving". Tune via the
    // motion_threshold / motion_bucket_sec environment variables.
    // start:true is always included so the motion engine is guaranteed to be
    // running, not merely parameterised.
    req = notecard.newRequest("card.motion.mode");
    JAddBoolToObject(req, "start", true);
    JAddNumberToObject(req, "motion",  (int)DEFAULT_MOTION_THRESHOLD);
    JAddNumberToObject(req, "seconds", (int)DEFAULT_MOTION_BUCKET_SEC);
    if (notecard.sendRequestWithRetry(req, 5)) {
        g_state.last_applied_motion_threshold  = DEFAULT_MOTION_THRESHOLD;
        g_state.last_applied_motion_bucket_sec = DEFAULT_MOTION_BUCKET_SEC;
    }
}

// ===========================================================================
// defineTemplates
// Register the heartbeat Notefile template. All fields are fixed-width, so
// every record encodes as a compact fixed-length binary entry rather than a
// variable-length JSON blob. Called once at cold boot, retried on subsequent
// wakes while template_confirmed is false.
//
// Field widths on the wire:
//   battery_mv  : TFLOAT32 = 4 bytes (IEEE-754 float)
//   moving      : TBOOL    = 1 byte  (boolean)
//   cycle_count : TUINT32  = 4 bytes (unsigned 32-bit int)
//   reason      : 11       = 1 byte  (1-byte int; 0=boot,1=heartbeat,2=low_battery)
//
// tote_event.qo is intentionally untemplated — low volume and benefits from
// a flexible schema that may evolve as the design matures.
// ===========================================================================
void defineTemplates() {
    J *req = notecard.newRequest("note.template");
    JAddStringToObject(req, "file", FILE_HEARTBEAT);
    JAddNumberToObject(req, "port", 10);

    J *body = JAddObjectToObject(req, "body");
    JAddNumberToObject(body, "battery_mv",  TFLOAT32); // 4-byte float
    JAddBoolToObject(body,   "moving",      TBOOL);    // 1-byte boolean
    JAddNumberToObject(body, "cycle_count", TUINT32);  // 4-byte unsigned int
    JAddNumberToObject(body, "reason",      11);       // 1-byte int: see REASON_*

    J *rsp = notecard.requestAndResponse(req);
    if (rsp) {
        if (!notecard.responseError(rsp)) g_state.template_confirmed = true;
        notecard.deleteResponse(rsp);
    }
}

// ===========================================================================
// fetchEnvOverrides
// Pull environment variables from Notehub and apply clamped overrides.
// Called every wake.
//
// On failure (NULL response or Notecard error) the function returns silently;
// g_* and g_state.desired_* are not modified, so the values already restored
// from the previous wake's persisted state remain in effect. A connectivity
// outage or Notecard-busy transient therefore cannot revert fleet tuning back
// to compile-time defaults.
//
// On each successful env.get all four tunable values are seeded from
// compile-time defaults first, then keys actually present in the response
// body overwrite the defaults. This means removing a key in Notehub — or
// moving the device to a fleet that omits it — correctly reverts the device
// to the firmware default on the next successful env.get, rather than keeping
// the old override indefinitely.
//
// All resolved values are committed to both the corresponding g_* global AND
// g_state.desired_*, which is saved across sleep via NotePayloadSaveAndSleep.
//
// Whether hub.set or card.motion.mode need to be reissued is determined in
// setup() by comparing the freshly committed desired values against
// g_state.last_applied_*. Seeding from defaults on every successful env.get
// ensures that an env-var removal produces a desired value that differs from
// the persisted last_applied, triggering a Notecard reapply on the same wake.
// ===========================================================================
void fetchEnvOverrides() {
    J *rsp = notecard.requestAndResponse(notecard.newRequest("env.get"));
    if (!rsp) return;

    if (notecard.responseError(rsp)) {
        notecard.deleteResponse(rsp);
        return;
    }

    // env.get succeeded — seed all tunable values from compile-time defaults
    // so that a removed or absent key reverts to the firmware default rather
    // than keeping whatever override was previously persisted. Keys present in
    // the body overwrite these candidates; absent keys keep the default.
    uint32_t h  = DEFAULT_HEARTBEAT_HOURS;
    float    mv = (float)DEFAULT_LOW_BATTERY_MV;
    uint32_t t  = DEFAULT_MOTION_THRESHOLD;
    uint32_t s  = DEFAULT_MOTION_BUCKET_SEC;

    J *body = JGetObject(rsp, "body");
    if (body) {
        const char *v;

        // heartbeat_hours: 1–168 h
        v = JGetString(body, "heartbeat_hours");
        if (v && v[0]) {
            uint32_t hv = (uint32_t)atoi(v);
            if (hv >= 1 && hv <= 168) h = hv;
        }

        // low_battery_mv: 2500–4300 mV
        v = JGetString(body, "low_battery_mv");
        if (v && v[0]) {
            float mvv = (float)atof(v);
            if (mvv >= 2500.0f && mvv <= 4300.0f) mv = mvv;
        }

        // motion_threshold: 1–20 events per bucket
        v = JGetString(body, "motion_threshold");
        if (v && v[0]) {
            uint32_t tv = (uint32_t)atoi(v);
            if (tv >= 1 && tv <= 20) t = tv;
        }

        // motion_bucket_sec: 5–300 seconds
        v = JGetString(body, "motion_bucket_sec");
        if (v && v[0]) {
            uint32_t sv = (uint32_t)atoi(v);
            if (sv >= 5 && sv <= 300) s = sv;
        }
    }

    // Commit resolved values — firmware default where the key is absent,
    // clamped operator override where it was present and in range.
    g_heartbeat_hours                 = h;
    g_low_battery_mv                  = mv;
    g_motion_threshold                = t;
    g_motion_bucket_sec               = s;
    g_state.desired_heartbeat_hours   = h;
    g_state.desired_low_battery_mv    = mv;
    g_state.desired_motion_threshold  = t;
    g_state.desired_motion_bucket_sec = s;

    notecard.deleteResponse(rsp);
}

// ===========================================================================
// notecardEpoch
// Return the current Unix epoch from the Notecard's time service.
// Returns 0 if the Notecard does not yet have valid time (e.g. before the
// first cellular sync on a cold boot). Callers treat 0 as "unknown" and fall
// back to the configured default interval rather than computing a meaningless
// absolute deadline.
// ===========================================================================
uint32_t notecardEpoch() {
    uint32_t epoch = 0;
    J *rsp = notecard.requestAndResponse(notecard.newRequest("card.time"));
    if (rsp) {
        if (!notecard.responseError(rsp)) {
            epoch = (uint32_t)JGetNumber(rsp, "time");
        }
        notecard.deleteResponse(rsp);
    }
    return epoch;
}

// ===========================================================================
// readMotionMoving
// Query the Notecard accelerometer for current motion status.
// Returns true if status is "moving" (threshold exceeded), false if "stopped".
//
// Up to MAX_TRIES attempts are made with a short back-off between them so a
// single transient I²C glitch or a momentary Notecard-busy response does not
// suppress a genuine motion-state change. On each attempt:
//   - NULL response (I²C not acknowledged)  → retry
//   - Notecard error response               → retry
//   - Unrecognised "mode" value             → retry
//   - "moving" or "stopped"                 → return result immediately
//
// If all attempts fail, the persisted g_state.motion_read_err_count counter
// is incremented (capped at 0xFF) and g_state.was_moving is returned so a
// comms fault never creates a spurious motion-state transition (e.g. a bogus
// "arrived" event). The counter is cleared on the first subsequent success
// and is visible in state dumps for field diagnostics without requiring a
// dedicated fault note.
// ===========================================================================
bool readMotionMoving() {
    const uint8_t MAX_TRIES = 3;
    for (uint8_t attempt = 0; attempt < MAX_TRIES; attempt++) {
        if (attempt > 0) delay(250); // brief back-off before retry

        J *rsp = notecard.requestAndResponse(notecard.newRequest("card.motion"));
        if (!rsp) continue;                         // NULL — retry

        if (notecard.responseError(rsp)) {
            notecard.deleteResponse(rsp);
            continue;                               // error response — retry
        }

        const char *mode = JGetString(rsp, "mode");
        bool valid = (mode != NULL &&
                      (strcmp(mode, "moving")  == 0 ||
                       strcmp(mode, "stopped") == 0));
        if (!valid) {
            notecard.deleteResponse(rsp);
            continue;                               // unrecognised mode — retry
        }

        bool moving = (strcmp(mode, "moving") == 0);
        notecard.deleteResponse(rsp);

        // Successful read — clear any accumulated fault counter.
        g_state.motion_read_err_count = 0;
        return moving;
    }

    // All attempts failed — increment diagnostic counter and return the
    // previous state so no spurious motion event is generated.
#ifdef DEBUG
    Serial.println(F("[WARN] card.motion failed after 3 attempts — keeping previous state"));
#endif
    if (g_state.motion_read_err_count < 0xFF) {
        g_state.motion_read_err_count++;
    }
    return g_state.was_moving;
}

// ===========================================================================
// readBatteryMv
// Read the supply voltage from the Notecard's rail monitor and return mV.
// Returns 0.0f on every failure path (NULL response, Notecard error, or
// missing "value" field). Callers treat 0.0f as "unread" — a genuine
// zero-volt reading is physically impossible on a live battery.
// ===========================================================================
float readBatteryMv() {
    J *rsp = notecard.requestAndResponse(notecard.newRequest("card.voltage"));
    if (!rsp) return 0.0f;
    if (notecard.responseError(rsp)) {
        notecard.deleteResponse(rsp);
        return 0.0f;
    }
    float mv = 0.0f;
    if (JGetObjectItem(rsp, "value") != NULL) {
        mv = (float)(JGetNumber(rsp, "value") * 1000.0);
    }
    notecard.deleteResponse(rsp);
    return mv;
}

// ===========================================================================
// sendMotionEvent
// Populate the pending-note record then attempt to deliver a motion event to
// tote_event.qo. Writing the record first means the payload survives a failed
// send and the next wake can replay it via resendPendingNote().
//
// Returns true and clears pending_note_type on success.
// Returns false and leaves the pending record set when all retries fail.
// ===========================================================================
bool sendMotionEvent(const char *event_type, bool moving, float battery_mv) {
    // Write the pending record before any attempt so a subsequent power cut
    // (between retries or before enterSleep) cannot lose the payload.
    g_state.pending_note_type  = PENDING_MOTION;
    strncpy(g_state.pending_event, event_type, sizeof(g_state.pending_event) - 1);
    g_state.pending_event[sizeof(g_state.pending_event) - 1] = '\0';
    g_state.pending_moving     = moving;
    g_state.pending_battery_mv = battery_mv;
    g_state.pending_cycle      = g_state.cycle_count;

    return doSendMotionNote();
}

// ===========================================================================
// sendHeartbeat
// Populate the pending-note record then attempt to deliver a heartbeat note
// to tote_heartbeat.qo.
//
// Returns true and clears pending_note_type on success.
// Returns false and leaves the pending record set when all retries fail.
// ===========================================================================
bool sendHeartbeat(uint8_t reason_code, bool moving, float battery_mv) {
    g_state.pending_note_type  = PENDING_HEARTBEAT;
    g_state.pending_reason     = reason_code;
    g_state.pending_moving     = moving;
    g_state.pending_battery_mv = battery_mv;
    g_state.pending_cycle      = g_state.cycle_count;

    return doSendHeartbeatNote();
}

// ===========================================================================
// resendPendingNote
// Retransmit the note described by g_state.pending_* without modifying the
// original payload fields. Called on retry wakes when pending_note_type is
// non-NONE, so the exact same departed/arrived event or heartbeat that failed
// on the previous wake is replayed with its original battery_mv, motion
// state, and cycle count.
//
// Returns true and clears pending_note_type on success.
// Returns false and leaves the record set on failure.
// Returns true immediately when pending_note_type is PENDING_NONE (no-op).
// ===========================================================================
bool resendPendingNote() {
    switch (g_state.pending_note_type) {
        case PENDING_MOTION:    return doSendMotionNote();
        case PENDING_HEARTBEAT: return doSendHeartbeatNote();
        default:                return true; // PENDING_NONE — nothing to do
    }
}

// ===========================================================================
// enterSleep
// Serialize device state into the Notecard and cut host power via ATTN.
//
// NotePayloadSaveAndSleep issues card.attn with mode "sleep,arm,motionchange":
//   "sleep"        → ATTN pin goes low, Cygnet power is cut by the
//                    Notecarrier CX; payload is held in Notecard memory.
//   "arm"          → required to enable early-wake sources like motionchange
//                    while ATTN is held low (per the Blues asset-tracking
//                    guide). Without arm, motionchange is listed in the mode
//                    string but is not actually armed and will not fire ATTN.
//   "motionchange" → ATTN fires early if the accelerometer-based motion
//                    status transitions (moving↔stopped) before the timer.
//
// sleep_sec is derived from the absolute next_heartbeat_epoch so that motion
// wakes re-arm only the *remaining* time to the deadline, not a full new
// interval. This guarantees the daily heartbeat fires even on frequently
// moving containers. Falls back to the full configured interval when the
// epoch is unavailable or the deadline has not been anchored yet.
//
// When failed_send_count is non-zero, sleep_sec is capped at RETRY_WAKE_SEC
// (15 minutes) so a pending note is retried promptly.
// ===========================================================================
void enterSleep(uint32_t now_epoch) {
    NotePayloadDesc payload = {0, 0, 0};
    NotePayloadAddSegment(&payload, STATE_SEG_ID, &g_state, sizeof(g_state));

    // Derive sleep duration from the remaining time to the absolute deadline.
    uint32_t sleep_sec = g_heartbeat_hours * 3600UL;
    if (now_epoch > 0 && g_state.next_heartbeat_epoch > now_epoch) {
        sleep_sec = g_state.next_heartbeat_epoch - now_epoch;
    }

    // Cap at the retry interval when a note.add failed this wake so the
    // dropped note is retried promptly rather than waiting a full day.
    if (g_state.failed_send_count > 0 && RETRY_WAKE_SEC < sleep_sec) {
        sleep_sec = RETRY_WAKE_SEC;
    }

    // "arm,motionchange" is passed as the additional-modes argument; note-c
    // prepends "sleep," producing the final mode "sleep,arm,motionchange".
    // The "arm" keyword is required for motionchange to actually fire ATTN
    // during the sleep window — see the Blues asset-tracking guide.
    NotePayloadSaveAndSleep(&payload, sleep_sec, "arm,motionchange");

    // Execution should not reach here under normal operation: the Notecard
    // holds the ATTN pin low, cutting Cygnet power. This delay provides a
    // safe fallback for bench testing where ATTN power-gating is not wired.
    delay(30000);
}
