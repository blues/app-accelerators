// cargo_lock_helpers.cpp
//
// Notecard configuration, env-var handling, sensor reads, event emission, and
// the main sample-cycle state machine for the Connected Smart Cargo Lock Platform.
//
// State-machine design notes
// ──────────────────────────
// Edge detection uses lock_state (last PHYSICAL state, updated every wake) rather
// than reported_lock_state.  reported_lock_state only advances when note.add
// succeeds and is used solely for the "delivery_gap" retry path in runSampleCycle.
//
// Critical event retention: a capacity-1 pending_event record in PersistState
// captures any state-transition note ("opened" or "locked") that fails to queue,
// and retries it on the next wake BEFORE processing new transitions — even if the
// physical state has changed in the interim.  Example: an "opened" note fails, the
// lock relocks before the next wake, and the pending flush re-delivers "opened"
// before the new "locked" note is queued, preventing the highest-priority security
// event from being silently lost on a transient I²C failure.  On successful retry,
// reported_lock_state is advanced to the implied state so the delivery_gap path can
// identify any subsequent transition that also needs delivery.
// The pending slot has capacity 1; see README §9 Limitations for the edge case
// where a second consecutive failure occurs while the slot is occupied.

#include <Arduino.h>
#include <Notecard.h>
#include "cargo_lock_helpers.h"

// PRODUCT_UID is a preprocessor macro in cargo_lock.ino and cannot be shared
// across compilation units.  setup() calls lockSetProductUID(PRODUCT_UID) once
// so all helpers can reference it through lockGetProductUID().
static const char *s_product_uid = "";

void lockSetProductUID(const char *uid) {
    s_product_uid = uid ? uid : "";
}

const char *lockGetProductUID() {
    return s_product_uid;
}

// -------- Lock state / pending-event string helpers ------------------------------

const char *lockStateStr(LockState s) {
    switch (s) {
        case LOCK_STATE_LOCKED:   return "LOCKED";
        case LOCK_STATE_UNLOCKED: return "UNLOCKED";
        default:                  return "UNKNOWN";
    }
}

const char *pendingEventTypeStr(PendingEventType t) {
    switch (t) {
        case PENDING_OPENED: return "opened";
        case PENDING_LOCKED: return "locked";
        default:             return "";
    }
}

// -------- Notecard configuration -------------------------------------------------

void hubConfigure() {
    // Periodic mode: queue summary notes and ship them on the outbound cadence;
    // lock events override with sync:true so they bypass the queue immediately.
    J *req = notecard.newRequest("hub.set");
    if (!req) return;
    const char *uid = lockGetProductUID();
    if (uid[0]) JAddStringToObject(req, "product", uid);
    JAddStringToObject(req, "mode",     "periodic");
    JAddNumberToObject(req, "outbound", REPORT_INTERVAL_MIN);
    JAddNumberToObject(req, "inbound",  REPORT_INTERVAL_MIN * 2);
    // sendRequestWithRetry on the first transaction dodges the cold-boot I2C race.
    // Only record the applied cadence if the request succeeded; if it failed the
    // Notecard retains its prior outbound setting, so we leave the sentinel unchanged
    // and retry on the next wake via fetchEnvOverrides → hubConfigure.
    if (notecard.sendRequestWithRetry(req, 10)) {
        state.last_applied_outbound_min = REPORT_INTERVAL_MIN;
    }
}

bool defineTemplates() {
    // Templates compress notes from free-form JSON into fixed-length records on the
    // wire — critical for a satellite path where bundled data is 10 KB. Both
    // Notefiles are templated so every byte counts on both the cellular and Skylo paths.
    //
    // String fields use an exemplar whose character count sets the maximum field width.
    // Numeric fields use the Blues type-hint encoding (11 = 1-byte int, 12 = 2-byte int,
    // 14 = 4-byte int — see dev.blues.io template docs).
    bool ok = true;

    // lock_event.qo — immediate alert notes (state transitions + tamper)
    {
        J *req = notecard.newRequest("note.template");
        if (!req) return false;
        JAddStringToObject(req, "file", "lock_event.qo");
        JAddNumberToObject(req, "port", 50);
        J *body = JAddObjectToObject(req, "body");
        // Exemplar "tamper" is 6 chars — the longest of "tamper"/"locked"/"opened".
        JAddStringToObject(body, "event",      "tamper"); // 6-char string exemplar
        JAddNumberToObject(body, "shackle",    11);       // 1-byte int: 1=present, 0=absent
        JAddNumberToObject(body, "bolt",       11);       // 1-byte int: 1=engaged, 0=withdrawn
        JAddNumberToObject(body, "locked",     11);       // 1-byte int: derived lock status
        JAddNumberToObject(body, "motion",     12);       // 2-byte int: accel count; -1=sensor fault
        JAddNumberToObject(body, "locked_for", 14);       // 4-byte int: seconds in locked state
        if (!notecard.sendRequest(req)) {
#if defined(DEBUG_SERIAL)
            usbSerial.println("[cargo_lock] WARN: note.template lock_event.qo failed");
#endif
            ok = false;
        }
    }

    // lock_status.qo — periodic summary notes (batched on outbound cadence)
    {
        J *req = notecard.newRequest("note.template");
        if (!req) return false;
        JAddStringToObject(req, "file", "lock_status.qo");
        JAddNumberToObject(req, "port", 51);
        J *body = JAddObjectToObject(req, "body");
        // Exemplar "UNLOCKED" is 8 chars — longest of "LOCKED"(6)/"UNLOCKED"(8)/"UNKNOWN"(7).
        JAddStringToObject(body, "state",        "UNLOCKED"); // 8-char string exemplar
        JAddNumberToObject(body, "shackle",      11);
        JAddNumberToObject(body, "bolt",         11);
        JAddNumberToObject(body, "locked",       11);
        JAddNumberToObject(body, "tamper_count", 12);   // 2-byte int: tampers in window
        JAddNumberToObject(body, "event_count",  12);   // 2-byte int: total events in window
        JAddNumberToObject(body, "uptime_min",   14);   // 4-byte int: minutes since last summary
        if (!notecard.sendRequest(req)) {
#if defined(DEBUG_SERIAL)
            usbSerial.println("[cargo_lock] WARN: note.template lock_status.qo failed");
#endif
            ok = false;
        }
    }

    return ok;
}

// -------- Notecard utilities -----------------------------------------------------

uint32_t notecardEpoch() {
    uint32_t epoch = 0;
    J *rsp = notecard.requestAndResponse(notecard.newRequest("card.time"));
    if (rsp) {
        epoch = (uint32_t)JGetNumber(rsp, "time");
        notecard.deleteResponse(rsp);
    }
    return epoch;
}

void fetchEnvOverrides() {
    J *req = notecard.newRequest("env.get");
    if (!req) return;
    J *names = JAddArrayToObject(req, "names");
    JAddItemToArray(names, JCreateString("sample_interval_sec"));
    JAddItemToArray(names, JCreateString("report_interval_min"));
    JAddItemToArray(names, JCreateString("tamper_threshold"));
    JAddItemToArray(names, JCreateString("alert_cooldown_sec"));

    J *rsp = notecard.requestAndResponse(req);
    if (!rsp) return;
    if (notecard.responseError(rsp)) { notecard.deleteResponse(rsp); return; }

    J *body = JGetObjectItem(rsp, "body");
    if (body) {
        const char *v;
        uint32_t    val;

        // Each env var is clamped to a sane absolute range before being applied.
        // Out-of-range values are silently ignored; the previous (or default) setting
        // is kept, preventing a bad Notehub value from breaking the low-power design
        // or causing effectively continuous transmissions.
        //
        // A relational clamp applied after the individual values enforces that
        // report_interval_min * 60 >= sample_interval_sec. Summaries are evaluated
        // only on host wakes, so a report interval shorter than one wake period can
        // never be honored — the relational clamp lifts REPORT_INTERVAL_MIN to the
        // smallest achievable value rather than silently delivering a longer interval.

        v = JGetString(body, "sample_interval_sec");
        if (v && v[0]) {
            val = (uint32_t)atol(v);
            // card.motion is always queried with minutes:1 (a 60-second accumulation
            // bucket). A wake interval shorter than 60 s would re-query the same
            // bucket and double-count accumulated motion, distorting tamper scoring.
            // Floor is therefore 60 s, not an arbitrary lower bound.
            if (val >= 60 && val <= 3600) SAMPLE_INTERVAL_SEC = val;
        }

        v = JGetString(body, "report_interval_min");
        if (v && v[0]) {
            val = (uint32_t)atol(v);
            if (val >= 1 && val <= 1440) REPORT_INTERVAL_MIN = val;
        }

        v = JGetString(body, "tamper_threshold");
        if (v && v[0]) {
            val = (uint32_t)atoi(v);
            if (val >= 1 && val <= 255) TAMPER_THRESHOLD = (uint8_t)val;
        }

        v = JGetString(body, "alert_cooldown_sec");
        if (v && v[0]) {
            val = (uint32_t)atol(v);
            if (val >= 60 && val <= 86400) ALERT_COOLDOWN_SEC = val;
        }

        // Relational clamp: a summary fires only when an entire wake-to-wake interval
        // has elapsed, so report_interval_min * 60 must be >= sample_interval_sec.
        // Use ceiling division so the smallest valid integer minute value is chosen.
        uint32_t min_report_min = (SAMPLE_INTERVAL_SEC + 59) / 60;
        if (REPORT_INTERVAL_MIN < min_report_min) {
            REPORT_INTERVAL_MIN = min_report_min;
        }
    }
    notecard.deleteResponse(rsp);

    // Re-apply hub.set if the outbound cadence changed. Without this, an operator
    // changing report_interval_min in Notehub would update the local summary timer
    // but leave the Notecard transmitting on its old schedule.
    if (REPORT_INTERVAL_MIN != state.last_applied_outbound_min) {
        hubConfigure();
    }
}

// -------- Sensor reads -----------------------------------------------------------

// Reed switch wired NC (normally-closed). With INPUT_PULLUP:
//   LOW  = magnet present = shackle seated in the lock body → shackle IS present
//   HIGH = magnet absent  = shackle removed                 → shackle NOT present
bool readShackle() {
    return (digitalRead(PIN_SHACKLE) == LOW);
}

// Honeywell SS461A unipolar hall-effect switch (3.0–24 VDC, SIP-3) wired with INPUT_PULLUP.
// A south-pole field pulls the open-collector output LOW; removing the field releases it.
//   LOW  = bolt magnet field detected = bolt engaged
//   HIGH = no field                   = bolt withdrawn
bool readBolt() {
    return (digitalRead(PIN_BOLT) == LOW);
}

// Query the Notecard's built-in accelerometer for recent motion activity.
// card.motion with minutes:1 returns the aggregate motion count in the last
// ~1-minute bucket. Values consistently above TAMPER_THRESHOLD while the lock
// is LOCKED indicate grinding, prying, or impact — not legitimate transit vibration.
//
// Returns -1 as a fault sentinel if both attempts fail. The caller must treat
// -1 as "motion unknown" and skip tamper evaluation — a sensor failure must not
// be silently indistinguishable from a clean zero-motion reading, as that would
// suppress a tamper alert during an active attack.
int readMotionCount() {
    for (uint8_t attempt = 0; attempt < 2; attempt++) {
        J *req = notecard.newRequest("card.motion");
        if (!req) continue;
        JAddNumberToObject(req, "minutes", 1);
        J *rsp = notecard.requestAndResponse(req);
        if (rsp) {
            if (!notecard.responseError(rsp)) {
                int motion = (int)JGetNumber(rsp, "motion");
                notecard.deleteResponse(rsp);
                return motion;
            }
            notecard.deleteResponse(rsp);
        }
    }
#if defined(DEBUG_SERIAL)
    usbSerial.println("[cargo_lock] WARN: card.motion failed after 2 attempts;"
                      " skipping tamper check this wake");
#endif
    return -1;  // fault sentinel: caller must not treat as a valid zero reading
}

// -------- Event emission ---------------------------------------------------------

// Queues a lock_event.qo Note with sync:true and returns true if the Notecard
// accepted it. Retries once before giving up — a single I²C bus hiccup must not
// cost a security event.
//
// IMPORTANT: callers must not advance cooldown timestamps or per-window counters
// until this returns true. A dropped note that still starts the cooldown window
// would suppress a retry for up to ALERT_COOLDOWN_SEC — unacceptable for a
// theft-detection device.
//
// The motion field carries the raw card.motion count, or -1 if card.motion failed
// both attempts (fault sentinel). Consumers should treat motion == -1 as
// "accelerometer data unavailable for this event" rather than "zero motion."
bool sendLockEvent(const char *event_type,
                   bool shackle, bool bolt,
                   int  motion,  uint32_t locked_for_sec) {
    for (uint8_t attempt = 0; attempt < 2; attempt++) {
        J *req = notecard.newRequest("note.add");
        if (!req) continue;
        JAddStringToObject(req, "file", "lock_event.qo");
        JAddBoolToObject  (req, "sync", true);  // bypass outbound queue — deliver immediately
        J *body = JAddObjectToObject(req, "body");
        JAddStringToObject(body, "event",      event_type);
        JAddNumberToObject(body, "shackle",    shackle ? 1 : 0);
        JAddNumberToObject(body, "bolt",       bolt    ? 1 : 0);
        JAddNumberToObject(body, "locked",     (shackle && bolt) ? 1 : 0);
        JAddNumberToObject(body, "motion",     motion);   // -1 = card.motion fault sentinel
        JAddNumberToObject(body, "locked_for", (int)locked_for_sec);
        if (notecard.sendRequest(req)) {
            state.event_count++;
            return true;
        }
    }
    return false;
}

void sendStatusSummary(uint32_t now_epoch) {
    uint32_t uptime_min = (state.summary_window_start_epoch > 0)
                        ? (now_epoch - state.summary_window_start_epoch) / 60 : 0;

    J *req = notecard.newRequest("note.add");
    if (!req) return;
    // No sync:true — batched to the Notecard's periodic outbound session
    J *body = JAddObjectToObject(req, "body");
    JAddStringToObject(req,  "file",         "lock_status.qo");
    JAddStringToObject(body, "state",        lockStateStr(state.lock_state));
    JAddNumberToObject(body, "shackle",      state.shackle_present ? 1 : 0);
    JAddNumberToObject(body, "bolt",         state.bolt_engaged    ? 1 : 0);
    JAddNumberToObject(body, "locked",       (state.lock_state == LOCK_STATE_LOCKED) ? 1 : 0);
    JAddNumberToObject(body, "tamper_count", (int)state.tamper_count);
    JAddNumberToObject(body, "event_count",  (int)state.event_count);
    JAddNumberToObject(body, "uptime_min",   (int)uptime_min);
    // Reset the per-window accumulators only if the note was successfully queued.
    // On failure the counters stay intact; the next wake will carry this window's
    // events forward into the next summary attempt rather than silently discarding them.
    if (notecard.sendRequest(req)) {
        state.tamper_count               = 0;
        state.event_count                = 0;
        state.summary_window_start_epoch = now_epoch;
    }
}

// -------- Main sample cycle ------------------------------------------------------

void runSampleCycle() {
    uint32_t now_epoch   = notecardEpoch();
    bool     epoch_valid = (now_epoch != 0);

    // Seed locked_since_epoch on the first wake where epoch becomes valid, if the device
    // booted while already LOCKED but epoch was not yet known at that time. Without this,
    // locked_for stays permanently 0 until the next full unlock/relock cycle — because no
    // state transition fires to set the anchor. now_epoch is the best available reference;
    // it does not reconstruct pre-epoch lock duration but prevents the field from being
    // permanently wrong.
    if (epoch_valid && state.lock_state == LOCK_STATE_LOCKED
        && state.locked_since_epoch == 0) {
        state.locked_since_epoch = now_epoch;
    }

    bool shackle = readShackle();
    bool bolt    = readBolt();
    int  motion  = readMotionCount();

    // Derive the current lock state from live sensor readings before any alert
    // logic runs. All tamper and transition checks must gate on new_state (the
    // *current* physical state) rather than the persisted state.lock_state (which
    // reflects the *previous* wake). Using stale state here would allow a tamper
    // event to fire on a wake where the shackle is already absent — contradicting
    // the documented behavior that tamper fires only while the lock is LOCKED.
    LockState new_state = (shackle && bolt) ? LOCK_STATE_LOCKED : LOCK_STATE_UNLOCKED;

    // ---- Pending event retry ----------------------------------------------------
    // Re-queue any state-transition note that failed to send on a previous wake.
    // This flush runs BEFORE tamper evaluation and the state-machine edge detector
    // so that historical events reach Notehub in chronological order.
    //
    // On success, reported_lock_state is advanced to the state implied by the
    // pending event (UNLOCKED for "opened", LOCKED for "locked"). This ensures
    // the delivery_gap check below can identify any subsequent transition that
    // also still needs to be delivered — for example, if the lock relocked while
    // the pending "opened" slot was occupied, advancing reported_lock_state to
    // UNLOCKED lets delivery_gap fire and re-emit "locked" if its own send also
    // failed.
    //
    // On failure, pending_type remains set; the next wake retries automatically.
    if (state.pending_type != PENDING_NONE) {
        const char *evt_str = pendingEventTypeStr(state.pending_type);
        if (sendLockEvent(evt_str,
                          state.pending_shackle,
                          state.pending_bolt,
                          (int)state.pending_motion,
                          state.pending_locked_for_sec)) {
            if (state.pending_type == PENDING_OPENED) {
                state.reported_lock_state = LOCK_STATE_UNLOCKED;
            } else { // PENDING_LOCKED
                state.reported_lock_state = LOCK_STATE_LOCKED;
            }
            state.pending_type = PENDING_NONE;
#if defined(DEBUG_SERIAL)
            usbSerial.print("[cargo_lock] pending retry OK: "); usbSerial.println(evt_str);
#endif
        }
#if defined(DEBUG_SERIAL)
        else {
            usbSerial.print("[cargo_lock] pending retry FAIL (will retry next wake): ");
            usbSerial.println(evt_str);
        }
#endif
    }

    // ---- Tamper check -----------------------------------------------------------
    // Significant accelerometer activity while currently LOCKED is an anomaly —
    // transit vibration is filtered by the threshold; sustained impact or
    // power-tool activity is not.
    //
    // motion == -1 means card.motion failed both attempts; skip the check rather
    // than treating the failure as a clean zero reading — silently swapping a
    // sensor fault for a "no motion" result would suppress a real tamper alert.
    //
    // Cooldown deduplication requires a valid epoch. Before the Notecard has synced
    // time (e.g., first power-up in poor-coverage conditions), the subtraction
    // `now_epoch - last_alert_tamper_epoch` cannot be trusted, so we emit on every
    // qualifying wake rather than silently dropping alerts that may represent a real
    // attack at start-of-journey. `locked_for` is reported as 0 until epoch is valid.
    if (new_state == LOCK_STATE_LOCKED && motion >= 0 && motion > TAMPER_THRESHOLD) {
        bool cooldown_ok = !epoch_valid ||
                           (now_epoch - state.last_alert_tamper_epoch) > ALERT_COOLDOWN_SEC;
        if (cooldown_ok) {
            uint32_t locked_for = (epoch_valid && state.locked_since_epoch > 0)
                                ? (now_epoch - state.locked_since_epoch) : 0;
            // Only advance tamper_count and the cooldown timestamp after the note
            // is successfully queued. A dropped note must not start the cooldown
            // window — that would suppress a retry for up to ALERT_COOLDOWN_SEC.
            if (sendLockEvent("tamper", shackle, bolt, motion, locked_for)) {
                state.tamper_count++;
                // Only record the cooldown timestamp when the epoch is trustworthy;
                // otherwise leave it at zero so the next valid-epoch wake re-evaluates.
                if (epoch_valid) {
                    state.last_alert_tamper_epoch = now_epoch;
                }
            }
        }
    }

    // ---- State machine ----------------------------------------------------------
#if defined(DEBUG_SERIAL)
    usbSerial.print("[cargo_lock] shackle="); usbSerial.print(shackle);
    usbSerial.print(" bolt=");               usbSerial.print(bolt);
    usbSerial.print(" motion=");             usbSerial.print(motion);
    usbSerial.print(" phys=");               usbSerial.print(lockStateStr(state.lock_state));
    usbSerial.print(" rept=");               usbSerial.print(lockStateStr(state.reported_lock_state));
    usbSerial.print(" new=");                usbSerial.println(lockStateStr(new_state));
#endif

    // Edge detection: compare new_state against lock_state (last PHYSICAL state).
    //
    // This prevents a real reopen from being silently missed after a failed "locked"
    // delivery.  Scenario: lock closes → "locked" note fails → reported_lock_state
    // stays UNLOCKED, lock_state advances to LOCKED → lock opens again → new_state
    // is UNLOCKED.  If we compared against reported_lock_state (UNLOCKED) we would
    // see no difference and skip the "opened" event.  Comparing against lock_state
    // (LOCKED) catches the physical edge and queues the alert correctly.
    //
    // reported_lock_state is used ONLY for delivery-retry detection: when the
    // physical state holds steady (new_state == lock_state) but reported hasn't
    // caught up, the delivery-gap branch retries the pending event.
    const bool phys_edge    = (state.lock_state != LOCK_STATE_UNKNOWN)
                           && (new_state != state.lock_state);
    const bool delivery_gap = !phys_edge
                           && (state.reported_lock_state != LOCK_STATE_UNKNOWN)
                           && (new_state != state.reported_lock_state);

    if (phys_edge || delivery_gap) {
        // "From" state for locked_for: on a physical edge use lock_state (actual
        // previous hardware state); on a retry use reported_lock_state (state of
        // the pending undelivered event).
        const LockState from_state = phys_edge ? state.lock_state : state.reported_lock_state;
        uint32_t locked_for = (epoch_valid && from_state == LOCK_STATE_LOCKED
                               && state.locked_since_epoch > 0)
                            ? (now_epoch - state.locked_since_epoch) : 0;

        if (new_state == LOCK_STATE_UNLOCKED) {
            // Transition toward UNLOCKED (physical open, or retry of an "opened" note).
            // Could be a legitimate terminal release or an unauthorized opening.
            // Cooldown deduplication requires a valid epoch; without one, emit
            // immediately so an opening at first power-up is never silently dropped.
            bool cooldown_ok = !epoch_valid ||
                               (now_epoch - state.last_alert_open_epoch) > ALERT_COOLDOWN_SEC;
            if (cooldown_ok) {
                if (sendLockEvent("opened", shackle, bolt, motion, locked_for)) {
                    // Advance reported state and cooldown only on success.
                    state.reported_lock_state = LOCK_STATE_UNLOCKED;
                    if (epoch_valid) {
                        state.last_alert_open_epoch = now_epoch;
                    }
                } else if (state.pending_type == PENDING_NONE) {
                    // Persist the failed event so it survives the next physical
                    // state change. The pending flush at the top of the next wake's
                    // runSampleCycle() re-queues it before processing new transitions,
                    // preventing a critical security event from being silently lost.
                    state.pending_type           = PENDING_OPENED;
                    state.pending_shackle        = shackle;
                    state.pending_bolt           = bolt;
                    state.pending_motion         = (int16_t)motion;
                    state.pending_locked_for_sec = locked_for;
                }
            } else if (phys_edge) {
                // Cooldown is still active but a real physical edge just occurred.
                // Advance reported_lock_state to the new physical truth so any
                // pending "locked" retry is abandoned and the UNLOCKED state is
                // tracked correctly going forward — suppressing the duplicate alert,
                // not the edge itself.
                state.reported_lock_state = LOCK_STATE_UNLOCKED;
            }
        } else { // new_state == LOCK_STATE_LOCKED
            // Transition toward LOCKED (physical relock, or retry of a "locked" note).
            // Reset the opened-alert cooldown on a confirmed physical relock so the
            // next real unlock is always alertable regardless of prior cooldown state.
            if (phys_edge) {
                state.last_alert_open_epoch = 0;
            }
            if (sendLockEvent("locked", shackle, bolt, motion, 0)) {
                // Advance reported state only on success.
                state.reported_lock_state = LOCK_STATE_LOCKED;
            } else if (state.pending_type == PENDING_NONE) {
                // Persist the failed event. The pending flush on the next wake
                // retries it before processing new transitions.
                state.pending_type           = PENDING_LOCKED;
                state.pending_shackle        = shackle;
                state.pending_bolt           = bolt;
                state.pending_motion         = (int16_t)motion;
                state.pending_locked_for_sec = 0;
            }
        }
    }

    // Capture the epoch at which the lock first entered (or re-entered) LOCKED.
    // Gate on physical state (lock_state, not reported_lock_state) so the anchor
    // records the actual hardware transition time regardless of whether the event
    // note was successfully queued. Only record when epoch is valid; a zero
    // timestamp would produce a meaningless locked_for value on future wakes.
    if (new_state == LOCK_STATE_LOCKED && state.lock_state != LOCK_STATE_LOCKED
        && epoch_valid) {
        state.locked_since_epoch = now_epoch;
    }

    // Persist physical lock state and sensor reads for the next wake cycle.
    // reported_lock_state is managed exclusively in the transition block above;
    // do NOT touch it here. Leaving it behind new_state on a failed send is
    // precisely what keeps the transition retryable across sleep boundaries.
    state.lock_state      = new_state;
    state.shackle_present = shackle;
    state.bolt_engaged    = bolt;

    // On the very first cycle (cold boot or payload expiry), set reported_lock_state
    // to the observed physical state so that subsequent wakes can detect departures
    // from it. No transition event is emitted for this baseline read — there is no
    // prior "from" state.
    if (state.reported_lock_state == LOCK_STATE_UNKNOWN) {
        state.reported_lock_state = state.lock_state;
    }

    // ---- Summary window initialization ------------------------------------------
    // Start the summary window on the first wake with a valid epoch.
    // Cold-boot wakes before the Notecard has synced time leave summary_window_start_epoch
    // at 0; the window begins the moment real time is known.
    //
    // On this same wake: reset per-window counters so they only cover activity from this
    // anchor forward (not pre-epoch boot activity that cannot be placed in time); and
    // re-issue hub.set so the Notecard's outbound timer shares the same anchor. Without
    // the re-issue, hub.set was applied at cold-boot before epoch was known, which means
    // the Notecard's first outbound window can fire before the first lock_status.qo is
    // queued — stretching the documented 6-hour heartbeat to nearly 12 hours.
    if (epoch_valid && state.summary_window_start_epoch == 0) {
        state.summary_window_start_epoch = now_epoch;
        state.tamper_count = 0;
        state.event_count  = 0;
        hubConfigure();  // re-anchor Notecard outbound timer to match local window start
    }

    // ---- Periodic summary -------------------------------------------------------
    if (epoch_valid && state.summary_window_start_epoch > 0 &&
        (now_epoch - state.summary_window_start_epoch) >= REPORT_INTERVAL_MIN * 60UL) {
        sendStatusSummary(now_epoch);
    }
}
