// tote_pool_tracker_helpers.h
// Shared types, constants, and declarations for the Tote Pool Tracker firmware.
// Included by both tote_pool_tracker.ino and tote_pool_tracker_helpers.cpp.
//
// PRODUCT_UID must be defined by the caller before this header is processed.
// In tote_pool_tracker.ino it is defined near the top of the file; the
// static_assert below that include catches a missing value in release builds.
// tote_pool_tracker_helpers.cpp receives the UID as a parameter to
// notecardConfigure() rather than reading the macro directly, so the .cpp
// translation unit never needs to see the macro.

#pragma once

// ---------------------------------------------------------------------------
// Debug output switch — uncomment to enable Serial logging and Notecard trace
// output (I²C traffic, request/response JSON, etc.) in both the .ino and the
// helper .cpp translation units. Disabled by default because serializing every
// Notecard transaction over UART adds ~1 mA to the wake-time average on the
// Cygnet — a meaningful penalty on a battery-powered build that wakes
// infrequently.
//
// Defined here (in the shared header) rather than in the .ino so that both
// translation units — tote_pool_tracker.ino and tote_pool_tracker_helpers.cpp
// — compile with the same DEBUG setting. The .ino includes this header before
// any #ifdef DEBUG checks.
// ---------------------------------------------------------------------------
// #define DEBUG

#include <Notecard.h>

// ---------------------------------------------------------------------------
// Notefile names
// ---------------------------------------------------------------------------
#define FILE_EVENTS    "tote_event.qo"
#define FILE_HEARTBEAT "tote_heartbeat.qo"

// ---------------------------------------------------------------------------
// Heartbeat reason codes — 1-byte integer in the template record
// ---------------------------------------------------------------------------
#define REASON_BOOT         ((uint8_t)0)   // first power-on
#define REASON_HEARTBEAT    ((uint8_t)1)   // normal timer wake
#define REASON_LOW_BATTERY  ((uint8_t)2)   // battery below threshold

// ---------------------------------------------------------------------------
// Pending-note type codes — stored in ToteState.pending_note_type
// ---------------------------------------------------------------------------
#define PENDING_NONE        ((uint8_t)0)   // no note awaiting retry
#define PENDING_MOTION      ((uint8_t)1)   // motion event (tote_event.qo)
#define PENDING_HEARTBEAT   ((uint8_t)2)   // heartbeat note (tote_heartbeat.qo)

// ---------------------------------------------------------------------------
// Default configuration — overridable via Notehub environment variables
// ---------------------------------------------------------------------------
#define DEFAULT_HEARTBEAT_HOURS    24    // hours between heartbeat notes
#define DEFAULT_LOW_BATTERY_MV   3300   // mV threshold for REASON_LOW_BATTERY
#define DEFAULT_MOTION_THRESHOLD    5   // accel events per bucket → "moving"
#define DEFAULT_MOTION_BUCKET_SEC  30   // bucket duration for motion counting

// Short retry interval used after a failed note.add. Capped at the remaining
// time to the heartbeat deadline so the device never sleeps past it.
#define RETRY_WAKE_SEC  (15UL * 60UL)

// Notecard payload segment identifier
#define STATE_SEG_ID "TOTE"

// ---------------------------------------------------------------------------
// State persisted across sleep cycles via NotePayloadSaveAndSleep
// ---------------------------------------------------------------------------
struct ToteState {
    bool     was_moving;   // motion state on the previous wake cycle
    uint32_t cycle_count;  // incremented before emitting notes each wake

    // Absolute Unix epoch at which the next heartbeat is due. Persisting the
    // absolute deadline (rather than restarting a fresh full interval) keeps
    // the daily heartbeat on schedule even when motion wakes occur frequently.
    // 0 until the Notecard has valid time; enterSleep() falls back to the full
    // default interval while 0.
    uint32_t next_heartbeat_epoch;

    // Consecutive note.add failure count across wakes. Non-zero drives a
    // shorter RETRY_WAKE_SEC sleep. Cleared at the wake level only after
    // every note queued on that wake has confirmed success (Item 4).
    uint8_t  failed_send_count;

    // Cumulative count of consecutive card.motion read failures. Incremented
    // by readMotionMoving() when all retry attempts fail; cleared on the next
    // successful card.motion response. Persisted so repeated failures are
    // visible in state dumps for field diagnostics without a dedicated note.
    uint8_t  motion_read_err_count;

    // Last values successfully applied to the Notecard. Persisting these
    // correctly detects env-var reverts to default: when an operator removes a
    // variable, the desired value returns to the firmware default but
    // last_applied still holds the previous non-default, so the mismatch is
    // detected and the Notecard is updated.
    uint32_t last_applied_heartbeat_hours;
    uint32_t last_applied_motion_threshold;
    uint32_t last_applied_motion_bucket_sec;

    // Last-known desired env values — persisted across sleep cycles so a
    // transient env.get failure on any wake cannot silently revert fleet
    // tuning back to compile-time defaults. Initialised to compile-time
    // defaults by notecardConfigure() on first boot; overwritten only when
    // env.get succeeds. Restored to the g_* globals at the top of setup()
    // before fetchEnvOverrides() is called each wake.
    uint32_t desired_heartbeat_hours;
    float    desired_low_battery_mv;
    uint32_t desired_motion_threshold;
    uint32_t desired_motion_bucket_sec;

    // One-time Notecard configuration confirmation flags. False on first boot;
    // set only after a verified successful response. Any flag still false on a
    // later wake triggers a retry, so a cold-boot I²C race never permanently
    // misconfigures the device.
    bool     triangulate_confirmed;
    bool     voltage_mode_confirmed;
    bool     template_confirmed;

    // -----------------------------------------------------------------------
    // Pending-note record — persisted until a confirmed note.add succeeds.
    //
    // Before each note.add attempt the send helper writes the payload into
    // these fields. If all retries fail, the record survives the sleep so the
    // next wake can replay the exact same payload. On confirmed success the
    // helper clears pending_note_type to PENDING_NONE.
    //
    // Only one note is kept pending at a time. Motion events (tote_event.qo)
    // take priority: if a motion send fails the heartbeat is deferred until
    // the motion note succeeds, and next_heartbeat_epoch is not advanced until
    // the heartbeat itself has been confirmed.
    // -----------------------------------------------------------------------
    uint8_t  pending_note_type;   // PENDING_NONE / PENDING_MOTION / PENDING_HEARTBEAT
    char     pending_event[9];    // "departed" or "arrived" (8 chars + NUL)
    bool     pending_moving;      // motion state captured at original send time
    float    pending_battery_mv;  // battery voltage captured at original send time
    uint32_t pending_cycle;       // cycle_count at original send time
    uint8_t  pending_reason;      // REASON_* code (heartbeat notes only)
};

// ---------------------------------------------------------------------------
// Globals defined in tote_pool_tracker.ino, used by helpers.cpp
// ---------------------------------------------------------------------------
extern ToteState  g_state;
extern Notecard   notecard;
extern uint32_t   g_heartbeat_hours;
extern float      g_low_battery_mv;
extern uint32_t   g_motion_threshold;
extern uint32_t   g_motion_bucket_sec;

// ---------------------------------------------------------------------------
// Function declarations (implemented in tote_pool_tracker_helpers.cpp)
// ---------------------------------------------------------------------------

// notecardConfigure — one-time cold-boot setup. Accepts the ProductUID so the
// .cpp translation unit never needs to resolve the PRODUCT_UID macro directly.
void     notecardConfigure(const char *product_uid);
void     defineTemplates();
void     fetchEnvOverrides();
uint32_t notecardEpoch();
bool     readMotionMoving();
float    readBatteryMv();

// sendMotionEvent / sendHeartbeat write the pending-note record before each
// attempt and clear it only on confirmed success. Return true on success;
// false when all retries fail (pending record remains set for the next wake).
bool     sendMotionEvent(const char *event_type, bool moving, float battery_mv);
bool     sendHeartbeat(uint8_t reason_code, bool moving, float battery_mv);

// resendPendingNote replays the note described by g_state.pending_* without
// overwriting the original payload fields. Returns true and clears
// pending_note_type on success; false if all retries fail.
bool     resendPendingNote();

void     enterSleep(uint32_t now_epoch);
