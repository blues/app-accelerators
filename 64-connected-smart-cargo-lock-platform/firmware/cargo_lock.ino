// cargo_lock.ino  ← this file is a stub; the sketch has moved.
//
// Open firmware/cargo_lock/cargo_lock.ino in your Arduino IDE or point
// arduino-cli at the firmware/cargo_lock/ folder.  The sketch is split
// across three files in that folder:
//
//   cargo_lock.ino          — setup(), loop(), user-facing constants
//   cargo_lock_helpers.h    — enums, PersistState struct, function prototypes
//   cargo_lock_helpers.cpp  — Notecard config, env-var handling, state machine
//
// ---- original file header preserved below for reference -----------------------
//
// Connected Smart Cargo Lock Platform
//
// Host:      Blues Notecarrier CX (onboard STM32 Cygnet host — no separate MCU needed)
// Notecard:  Blues Notecard for Skylo (NOTE-NBGLWX)
//            LTE-M / NB-IoT / GPRS primary cellular  +  Skylo NTN satellite failover
//
// Sensors:
//   Shackle:  Normally-closed reed switch on D5  (LOW = shackle magnet present)
//   Bolt:     Honeywell SS461A hall-effect switch on D6 (LOW = bolt magnet detected)
//             VCC supplied from host-gated +3V3; rated 3.0–24 VDC (3.3 V is within spec).
//   Tamper:   Notecard built-in accelerometer via card.motion / card.motion.mode
//
// Lock state machine:
//   LOCKED   — shackle present AND bolt engaged
//   UNLOCKED — shackle absent  OR  bolt withdrawn
//   UNKNOWN  — initial / indeterminate (cleared after first sensor read)
//
// State-transition edge detection uses lock_state (last PHYSICAL state, updated every
// wake) rather than reported_lock_state.  reported_lock_state only advances when
// note.add succeeds and is used solely for the "delivery_gap" retry path in
// runSampleCycle.
//
// Critical event retention: a capacity-1 pending_event record in PersistState captures
// any state-transition note ("opened" or "locked") that fails to queue, and retries it
// on the next wake BEFORE processing new transitions — even if the physical state has
// changed in the interim.  Example: an "opened" note fails, the lock relocks before the
// next wake, and the pending flush re-delivers "opened" before the new "locked" note is
// queued, preventing the highest-priority security event from being silently lost on a
// transient I²C failure.  On successful retry, reported_lock_state is advanced to the
// implied state so the delivery_gap path can identify any subsequent transition that
// also needs delivery.
// The pending slot has capacity 1; see README §9 Limitations for the edge case where a
// second consecutive failure occurs while the slot is occupied.
//
// Event types emitted to lock_event.qo (sync:true, immediate):
//   "locked"       — transition to LOCKED state
//   "opened"       — transition from LOCKED to UNLOCKED
//   "tamper"       — motion above TAMPER_THRESHOLD while LOCKED
//
// Periodic summary emitted to lock_status.qo every REPORT_INTERVAL_MIN.
//
// Wake cadence:
//   Host wakes every SAMPLE_INTERVAL_SEC via NotePayloadSaveAndSleep (card.attn sleep).
//   All runtime state survives the power cut via the Notecard's on-device flash.
//
// Optional BLE key authentication is described in the README but is NOT implemented
// here — it requires an external BLE module and a separate authentication library.
//
// Dependencies:
//   Blues Wireless Notecard  (note-arduino v1.8.5)  — install via Arduino Library Manager
//   stm32duino/Arduino_Core_STM32                   — install via Arduino Boards Manager

#include <Notecard.h>
#include <Wire.h>

#ifndef PRODUCT_UID
#define PRODUCT_UID "" // "com.my-company.my-name:cargo-lock"
#pragma message "PRODUCT_UID is not set. Claim one in Notehub, then define it here."
#endif

// Uncomment the line below to enable USB-Serial debug output on a bench build.
// Leave it commented in production firmware — the USB enumeration wait in
// setup() blocks the host for up to 3 s on every wake, materially increasing
// average power consumption on a 60-second duty cycle.
// #define DEBUG_SERIAL
#define usbSerial Serial   // alias used only inside #if defined(DEBUG_SERIAL) blocks

// -------- Pin assignments (Notecarrier CX dual 16-pin header) --------------------
static const uint8_t PIN_SHACKLE = 5;  // D5: NC reed switch, INPUT_PULLUP
static const uint8_t PIN_BOLT    = 6;  // D6: SS461A hall-effect switch, INPUT_PULLUP

// -------- Default thresholds — all tunable via Notehub environment variables -----
static uint32_t SAMPLE_INTERVAL_SEC  = 60;    // 60 s between wakeups — matches card.motion minutes:1 bucket
static uint32_t REPORT_INTERVAL_MIN  = 360;   // 6 hours between status summaries
static uint8_t  TAMPER_THRESHOLD     = 8;     // motion counts/min → declare tamper
static uint32_t ALERT_COOLDOWN_SEC   = 1800;  // 30 min between repeated same-type alerts

// -------- Lock state enumeration --------------------------------------------------
typedef enum : uint8_t {
    LOCK_STATE_UNKNOWN   = 0,
    LOCK_STATE_LOCKED    = 1,
    LOCK_STATE_UNLOCKED  = 2,
} LockState;

static const char *lockStateStr(LockState s) {
    switch (s) {
        case LOCK_STATE_LOCKED:   return "LOCKED";
        case LOCK_STATE_UNLOCKED: return "UNLOCKED";
        default:                   return "UNKNOWN";
    }
}

// -------- Pending event type for critical delivery retry -------------------------
// Encodes the type of a failed state-transition note so it can be persisted across
// the sleep boundary and re-queued on the next wake regardless of whether the
// physical lock state has changed in the interim.
// Tamper events are excluded: their cooldown is only advanced on successful send,
// so a failed tamper note is naturally retried on the next qualifying wake.
typedef enum : uint8_t {
    PENDING_NONE   = 0,
    PENDING_OPENED = 1,
    PENDING_LOCKED = 2,
} PendingEventType;

static const char *pendingEventTypeStr(PendingEventType t) {
    switch (t) {
        case PENDING_OPENED: return "opened";
        case PENDING_LOCKED: return "locked";
        default:              return "";
    }
}

// -------- State persisted across sleep cycles ------------------------------------
// The Notecard holds this struct in its on-device flash via NotePayloadSaveAndSleep.
// On each wake, NotePayloadRetrieveAfterSleep restores it before setup() runs the
// next sample cycle — so we carry the full lock history across a year of duty cycling.
struct PersistState {
    LockState lock_state;           // last observed PHYSICAL state (updated every wake)
    LockState reported_lock_state;  // last state for which a transition event was successfully
                                    // queued; only advances on note.add success so failed
                                    // transitions remain retryable across sleep boundaries.
                                    // Used ONLY for retry detection — NOT for edge triggering.
    bool      shackle_present;           // last observed shackle state
    bool      bolt_engaged;              // last observed bolt state
    uint32_t  locked_since_epoch;        // epoch when lock entered LOCKED
    uint32_t  tamper_count;              // tamper events in current summary window
    uint32_t  event_count;               // total emitted events (state-change + tamper) in current summary window
    uint32_t  summary_window_start_epoch;
    uint32_t  last_alert_tamper_epoch;   // for per-type cooldown dedup
    uint32_t  last_alert_open_epoch;
    uint32_t  last_applied_outbound_min; // detect hub.set drift when env changes
    bool      config_complete;           // true once note templates are defined; motion.mode is
                                         // re-applied unconditionally on every wake (not gated on
                                         // this flag) so a Notecard reset cannot silently disable
                                         // tamper detection without the host knowing

    // Pending delivery retry (capacity 1).
    // When a state-transition note ("opened" or "locked") fails to queue, the event
    // is saved here so it can be retried on the next wake — even if the physical lock
    // state changes before the retry succeeds.  The slot is cleared only after the
    // note is successfully accepted by the Notecard.
    // If the slot is already occupied when a second failure occurs, the newer failure
    // is not saved; it falls back to the delivery_gap detection path (correct when the
    // physical state has not changed again) or is lost (if it has).  See README §9.
    PendingEventType pending_type;        // PENDING_NONE when no outstanding retry
    bool             pending_shackle;
    bool             pending_bolt;
    int16_t          pending_motion;      // -1 = card.motion fault sentinel
    uint32_t         pending_locked_for_sec;
};
static const char STATE_SEG_ID[] = "CKLK";
static PersistState state;

Notecard notecard;

// -------- Notecard configuration -------------------------------------------------

static void hubConfigure() {
    // Periodic mode: queue summary notes and ship them on the outbound cadence;
    // lock events override with sync:true so they bypass the queue immediately.
    J *req = notecard.newRequest("hub.set");
    if (!req) return;
    if (PRODUCT_UID[0]) JAddStringToObject(req, "product", PRODUCT_UID);
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

static bool defineTemplates() {
    // Templates compress notes from free-form JSON into fixed-length records on the
    // wire — critical for a satellite path where bundled data is 10 KB. Both
    // Notefiles are templated so every byte counts on both the cellular and Skylo paths.
    //
    // String fields use an exemplar whose character count sets the maximum field width.
    // Numeric fields use the Blues type-hint encoding (11 = 1-byte int, 12 = 2-byte int,
    // 14 = 4-byte int, 14.1 = 4-byte float — see dev.blues.io template docs).
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
        JAddNumberToObject(body, "motion",     12);       // 2-byte int: accel motion count; -1 = sensor fault
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

// -------- Notecard helpers -------------------------------------------------------

static uint32_t notecardEpoch() {
    uint32_t epoch = 0;
    J *rsp = notecard.requestAndResponse(notecard.newRequest("card.time"));
    if (rsp) {
        epoch = (uint32_t)JGetNumber(rsp, "time");
        notecard.deleteResponse(rsp);
    }
    return epoch;
}

static void fetchEnvOverrides() {
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
static bool readShackle() {
    return (digitalRead(PIN_SHACKLE) == LOW);
}

// Honeywell SS461A unipolar hall-effect switch (3.0–24 VDC, SIP-3) wired with INPUT_PULLUP.
// A south-pole field pulls the open-collector output LOW; removing the field releases it.
//   LOW  = bolt magnet field detected = bolt engaged
//   HIGH = no field                   = bolt withdrawn
static bool readBolt() {
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
static int readMotionCount() {
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
    usbSerial.println("[cargo_lock] WARN: card.motion failed after 2 attempts; skipping tamper check this wake");
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
static bool sendLockEvent(const char *event_type,
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

static void sendStatusSummary(uint32_t now_epoch) {
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

static void runSampleCycle() {
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
    // pending event (UNLOCKED for "opened", LOCKED for "locked").  This ensures
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
                    // state change.  The pending flush at the top of the next wake's
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
                // Persist the failed event.  The pending flush on the next wake
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
    // note was successfully queued.  Only record when epoch is valid; a zero
    // timestamp would produce a meaningless locked_for value on future wakes.
    if (new_state == LOCK_STATE_LOCKED && state.lock_state != LOCK_STATE_LOCKED
        && epoch_valid) {
        state.locked_since_epoch = now_epoch;
    }

    // Persist physical lock state and sensor reads for the next wake cycle.
    // reported_lock_state is managed exclusively in the transition block above;
    // do NOT touch it here.  Leaving it behind new_state on a failed send is
    // precisely what keeps the transition retryable across sleep boundaries.
    state.lock_state      = new_state;
    state.shackle_present = shackle;
    state.bolt_engaged    = bolt;

    // On the very first cycle (cold boot or payload expiry), set reported_lock_state
    // to the observed physical state so that subsequent wakes can detect departures
    // from it.  No transition event is emitted for this baseline read — there is no
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

// -------- Setup / loop -----------------------------------------------------------
// This sketch uses the "host-is-off-when-idle" sleep pattern. After runSampleCycle()
// completes, loop() calls NotePayloadSaveAndSleep() which serializes state into the
// Notecard's flash and cuts host power via the ATTN pin. The Notecard then wakes the
// host SAMPLE_INTERVAL_SEC seconds later; setup() runs again, restores state, runs
// the next cycle, and sleeps again. On the Notecarrier CX, ATTN is routed to the
// Cygnet host power enable internally — no external jumper is required — so loop()
// is essentially never reached in normal deployed operation.

void setup() {
#if defined(DEBUG_SERIAL)
    usbSerial.begin(115200);
    for (uint32_t t0 = millis(); !usbSerial && (millis() - t0) < 3000; ) {}
#endif

#if defined(DEBUG_SERIAL)
    if (!PRODUCT_UID[0]) {
        usbSerial.println("[cargo_lock] ERROR: PRODUCT_UID is not set — device will not connect"
                         " to any Notehub project. Define PRODUCT_UID in cargo_lock.ino and reflash.");
#ifdef DEBUG
        // In debug builds, halt here so a developer does not waste time chasing
        // connectivity symptoms when the root cause is a missing ProductUID.
        usbSerial.println("[cargo_lock] Halting in DEBUG build until PRODUCT_UID is configured.");
        while (true) { delay(5000); }
#endif
    }
#endif

    pinMode(PIN_SHACKLE, INPUT_PULLUP);
    pinMode(PIN_BOLT,    INPUT_PULLUP);

    Wire.begin();
    notecard.begin();
#if defined(DEBUG_SERIAL)
    notecard.setDebugOutputStream(usbSerial);
#endif

    // Attempt to restore runtime state from the Notecard's payload store.
    // If this is a cold boot (first power, or payload expired), restored == false
    // and we fall through to first-boot initialization.
    NotePayloadDesc payload;
    bool restored = NotePayloadRetrieveAfterSleep(&payload);
    if (restored) {
        restored &= NotePayloadGetSegment(&payload, STATE_SEG_ID, &state, sizeof(state));
        NotePayloadFree(&payload);
    }

    if (!restored) {
        memset(&state, 0, sizeof(state));
    }

    // Retry first-boot configuration on every wake until it fully succeeds.
    // A transient I²C failure on cold boot would otherwise leave note templates
    // undefined indefinitely, because the restored-state path skips this block
    // once state is saved — even if config was never completed.
    if (!state.config_complete) {
        hubConfigure();
        bool tmpl_ok = defineTemplates();
        if (tmpl_ok) {
            state.config_complete = true;
        }
    }

    // Re-apply card.motion.mode on every wake — not just on first boot.
    // The Notecard retains its configuration in Notecard-side storage, but a full
    // Notecard power cycle or firmware update can reset that state. Because the host
    // restores config_complete from its own payload store it has no way to detect a
    // Notecard reset; issuing this unconditionally ensures tamper detection is always
    // armed and can never be silently disabled by a Notecard-side reset.
    //
    // card.motion.mode with start:true is idempotent when already running — calling
    // it on a wake where motion detection is already active is harmless.
    //
    // sensitivity:2 sets the accelerometer detection sensitivity. Consult the Blues
    // API docs for the mapping of sensitivity values to sample rate and detection
    // range. The Notecard accumulates motion events while the host sleeps; the host
    // reads aggregate counts via card.motion on each wake — no interrupt wiring needed.
    {
        J *mot_req = notecard.newRequest("card.motion.mode");
        if (mot_req) {
            JAddBoolToObject  (mot_req, "start",       true);
            JAddNumberToObject(mot_req, "sensitivity",  2);
            if (!notecard.sendRequest(mot_req)) {
#if defined(DEBUG_SERIAL)
                usbSerial.println("[cargo_lock] WARN: card.motion.mode failed; tamper detection may be inactive this wake");
#endif
            }
        }
    }

    // Re-read env vars on every wake. An operator can adjust thresholds in Notehub
    // and they'll take effect on the next wake without re-flashing the device.
    fetchEnvOverrides();

    runSampleCycle();
}

void loop() {
    // Serialize runtime state into the Notecard's on-device flash, then cut host
    // power for SAMPLE_INTERVAL_SEC seconds via card.attn sleep mode.
    // On the Notecarrier CX, ATTN is routed to the Cygnet host power enable
    // internally — no external jumper needed. The Notecard drives ATTN to cut
    // the Cygnet's supply; the host cold-boots into setup() on the next wake.
    NotePayloadDesc payload = {0, 0, 0};
    NotePayloadAddSegment(&payload, STATE_SEG_ID, &state, sizeof(state));
    NotePayloadSaveAndSleep(&payload, SAMPLE_INTERVAL_SEC, NULL);

    // Only reached if NotePayloadSaveAndSleep could not cut host power via ATTN
    // (e.g., a non-CX carrier board or unusual bench configuration). Fall back to
    // a software delay so the loop cadence is preserved.
    delay(SAMPLE_INTERVAL_SEC * 1000UL);
}
