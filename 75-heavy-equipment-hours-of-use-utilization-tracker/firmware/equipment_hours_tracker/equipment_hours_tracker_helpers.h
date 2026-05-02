/*******************************************************************************
 * equipment_hours_tracker_helpers.h
 *
 * Shared types, constants, extern declarations, and function prototypes for
 * the Heavy Equipment Hours-of-Use & Utilization Tracker firmware.
 * Included by both equipment_hours_tracker.ino and
 * equipment_hours_tracker_helpers.cpp.
 ******************************************************************************/
#pragma once

#include <Notecard.h>
#include <Adafruit_LSM6DSOX.h>
#include <Wire.h>
#include <math.h>
#include <string.h>   // strncpy, memset
#include <stdio.h>    // snprintf

#ifndef PRODUCT_UID
#define PRODUCT_UID ""  // replace with your Notehub ProductUID
#pragma message "PRODUCT_UID is not defined — set to your Notehub ProductUID"
#endif

// ── Timing ────────────────────────────────────────────────────────────────────
#define SAMPLE_INTERVAL_SEC    30    // host wakes every 30 s via card.attn
#define SUMMARY_INTERVAL_MIN   1440  // default: once daily (24 h)

// ── Vibration classifier defaults (env-var tunable) ───────────────────────────
#define VIB_RUN_MG_DEFAULT     15.0f // RMS residual (milli-g) above this = activity
#define VIB_CV_MAX_DEFAULT     0.40f // CV below this = engine (steady); above = transport (bursty)
#define VIB_SAMPLE_COUNT       208   // 104 Hz × 2 s; ~26 diesel combustion cycles

// ── GPS ───────────────────────────────────────────────────────────────────────
#define GPS_PERIOD_SECONDS     900   // periodic GPS interval (15 min)
#define GPS_HEARTBEAT_HOURS    4     // _track.qo heartbeat cadence

// ── Geofence radius limits ────────────────────────────────────────────────────
// Below ~50 m GPS jitter will false-trigger an exit constantly; above 50 km the
// fence encompasses more than any plausible job site.  Non-zero values outside
// this range are clamped with a log message.  Zero is the sentinel meaning
// "no fence" and is never clamped.
#define GEOFENCE_RADIUS_MIN_M  50u
#define GEOFENCE_RADIUS_MAX_M  50000u

// ── Pending-event queue depth ─────────────────────────────────────────────────
// A ring buffer of this depth tolerates PENDING_QUEUE_DEPTH consecutive wakes
// with no Notecard acknowledgement (~2 min at 30 s per wake for depth 4) before
// an overflow drop is logged.  Even depth 2 eliminates the single-slot overwrite
// window; depth 4 is chosen as an inexpensive safety margin.
#define PENDING_QUEUE_DEPTH    4

// ── Compile-time option: disable the Notecard's internal motion subsystem ─────
// The external LSM6DSOX handles all vibration classification, so the Notecard
// motion subsystem is not needed for that role.  However, the interaction between
// card.motion.mode {"stop":true} and periodic GPS / geofencing on the Notecard
// for Skylo (NOTE-NBGLWX) has not been bench-validated: if the Notecard's
// periodic-mode timer relies on any internal motion-subsystem wakeup path,
// stopping it could silently break GPS heartbeats and geofence events — which
// are core features of this application.  Leave this macro undefined (the
// default) until you have confirmed on your target Notecard firmware that all
// periodic/geofence behaviour remains intact with the motion subsystem stopped.
// #define DISABLE_NOTECARD_MOTION

// ── Equipment state ───────────────────────────────────────────────────────────
typedef enum : uint8_t {
    ST_IDLE      = 0,
    ST_RUNNING   = 1,   // steady high-frequency vibration (engine idle / under load)
    ST_TRANSPORT = 2    // bursty low-frequency vibration (road transport)
} EquipState;

// ── Pending event (one slot in the delivery ring buffer) ──────────────────────
struct PendingEvent {
    char     tag[18];          // longest tag "transport_start" = 15 chars + null
    uint32_t epoch;            // Unix timestamp of the state transition; transmitted in equip_event.qo body for server-side dedup
    float    session_min;      // session duration captured at transition time
    float    run_h_total;      // run_h_total snapshot captured at transition time
};

// ── Persisted state (Notecard flash, survives power-gate sleep) ───────────────
struct PersistState {
    bool       configured;
    EquipState prev_state;
    float      run_h_today;
    float      run_h_total;
    float      transport_h_today;
    uint32_t   run_session_start;     // epoch when current run session began
    uint32_t   last_summary_epoch;
    uint32_t   last_sample_epoch;     // epoch at end of previous wake (elapsed-time calc)

    // Geofence — mirrors the last values successfully applied to the Notecard;
    // used by applyGeofenceIfChanged() to detect whether a Notecard call is needed.
    float      fence_lat;
    float      fence_lon;
    uint32_t   fence_radius_m;
    bool       fence_was_active;

    // Last-good env reads — seeded into the runtime globals on every wake so a
    // transient env.get miss leaves previously-applied tuning and fence parameters
    // intact rather than reverting to compile-time defaults.  Updated only when
    // env.get returns a valid, in-range value; zero-initialised on cold boot.
    float      applied_vib_run_mg;           // 0 = never set, use compile-time default
    float      applied_vib_cv_max;           // 0 = never set, use compile-time default
    uint32_t   applied_summary_interval_min; // 0 = never set, use compile-time default
    float      applied_env_fence_lat;        // last good lat read from env (seed for next wake)
    float      applied_env_fence_lon;        // last good lon read from env
    uint32_t   applied_env_fence_radius_m;   // last good radius read from env (0 = no fence)

    // Pending-event ring buffer — replaces single-slot design so that a second
    // state transition before the first note.add is acknowledged does not silently
    // overwrite and drop the older record.  All four event types are preserved in
    // FIFO order; delivery is attempted oldest-first on each wake.
    uint8_t      evq_head;              // index of the oldest queued event
    uint8_t      evq_count;             // number of events currently in the queue
    PendingEvent evq[PENDING_QUEUE_DEPTH];
    uint32_t     evq_overflow_count;    // incremented when a transition record is dropped due to a full queue; reported in next summary
};

// ── Hardware objects ──────────────────────────────────────────────────────────
extern Notecard          notecard;
extern Adafruit_LSM6DSOX sox;

// ── Persisted state and payload segment ID ────────────────────────────────────
extern PersistState g_s;
extern const char   SEG_ID[];

// ── Runtime env overrides — re-seeded from applied_* fields on every wake ─────
extern float    g_vib_run_mg;
extern float    g_vib_cv_max;
extern uint32_t g_summary_interval_min;
extern float    g_fence_lat;
extern float    g_fence_lon;
extern uint32_t g_fence_radius_m;

// ── Function prototypes ───────────────────────────────────────────────────────
bool       checkedRequest(J *req);
float      clampF(double v, float minv, float maxv, float fallback);
uint32_t   clampU32(long v, uint32_t minv, uint32_t maxv, uint32_t fallback);
bool       notecardConfigure(void);
bool       defineTemplates(void);
void       fetchEnvOverrides(void);
void       applyGeofenceIfChanged(void);
EquipState classifyVibration(void);
void       updateHourAccumulator(uint32_t now);
bool       enqueueEvent(const char *tag, uint32_t epoch,
                        float session_min, float run_h_total);
bool       sendNextPendingEvent(void);
bool       sendSummary(void);
uint32_t   getEpoch(void);
float      getBatteryVoltage(void);
void       goToSleep(void);
