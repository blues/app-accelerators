// cargo_lock_helpers.h
//
// Declarations shared between cargo_lock.ino and cargo_lock_helpers.cpp:
// debug-output toggle, enumerations, the persisted-state struct, extern
// references to the globals defined in cargo_lock.ino, and prototypes for
// all helper functions.

#pragma once

// -------- Debug output (must be visible in both compilation units) ---------------
// Uncomment the line below to enable USB-Serial debug output on a bench build.
// Leave it commented in production firmware — the USB enumeration wait in
// setup() blocks the host for up to 3 s on every wake, materially increasing
// average power consumption on a 60-second duty cycle.
// #define DEBUG_SERIAL
#if defined(DEBUG_SERIAL)
#  define usbSerial Serial
#endif

#include <Arduino.h>
#include <Notecard.h>

// -------- Pin assignments (Notecarrier CX dual 16-pin header) --------------------
constexpr uint8_t PIN_SHACKLE = 5;  // D5: NC reed switch, INPUT_PULLUP
constexpr uint8_t PIN_BOLT    = 6;  // D6: SS461A hall-effect switch, INPUT_PULLUP

// -------- Lock state enumeration --------------------------------------------------
typedef enum : uint8_t {
    LOCK_STATE_UNKNOWN  = 0,
    LOCK_STATE_LOCKED   = 1,
    LOCK_STATE_UNLOCKED = 2,
} LockState;

const char *lockStateStr(LockState s);

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

const char *pendingEventTypeStr(PendingEventType t);

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
    uint32_t  event_count;               // total emitted events (state-change + tamper) in window
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
    // state changes before the retry succeeds. The slot is cleared only after the
    // note is successfully accepted by the Notecard.
    // If the slot is already occupied when a second failure occurs, the newer failure
    // is not saved; it falls back to the delivery_gap detection path (correct when the
    // physical state has not changed again) or is lost (if it has). See README §9.
    PendingEventType pending_type;        // PENDING_NONE when no outstanding retry
    bool             pending_shackle;
    bool             pending_bolt;
    int16_t          pending_motion;      // -1 = card.motion fault sentinel
    uint32_t         pending_locked_for_sec;
};

// -------- Globals defined in cargo_lock.ino; shared read/write with helpers ------
extern Notecard     notecard;
extern PersistState state;

extern uint32_t SAMPLE_INTERVAL_SEC;
extern uint32_t REPORT_INTERVAL_MIN;
extern uint8_t  TAMPER_THRESHOLD;
extern uint32_t ALERT_COOLDOWN_SEC;

// -------- Helper function prototypes ---------------------------------------------

// Store PRODUCT_UID for use by hubConfigure. PRODUCT_UID is a preprocessor macro
// in cargo_lock.ino and cannot be shared across compilation units; setup() calls
// lockSetProductUID(PRODUCT_UID) once so all helpers can retrieve it via
// lockGetProductUID().
void        lockSetProductUID(const char *uid);
const char *lockGetProductUID();

// Notecard configuration
void hubConfigure();
bool defineTemplates();

// Notecard utilities
uint32_t notecardEpoch();
void     fetchEnvOverrides();

// Sensor reads
bool readShackle();
bool readBolt();
int  readMotionCount();

// Event emission
bool sendLockEvent(const char *event_type, bool shackle, bool bolt,
                   int motion, uint32_t locked_for_sec);
void sendStatusSummary(uint32_t now_epoch);

// Main sample cycle (called once per wake from setup())
void runSampleCycle();
