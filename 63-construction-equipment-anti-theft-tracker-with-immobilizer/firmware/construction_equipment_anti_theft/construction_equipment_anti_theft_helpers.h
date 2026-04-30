/***************************************************************************
  construction_equipment_anti_theft_helpers.h

  Library for Blues Notecard interaction — Construction Equipment Anti-Theft.

  This library encapsulates all Notecard functionality for the
  construction_equipment_anti_theft project.
  This is specific to your project and is NOT A GENERAL PURPOSE LIBRARY.

  THIS FILE SHOULD BE EDITED AFTER GENERATION.
  IT IS PROVIDED AS A STARTING POINT FOR THE USER TO EDIT AND EXTEND.
***************************************************************************/

#pragma once
#include <Notecard.h>

// ─── Debug serial ────────────────────────────────────────────────────────────
// Define DEBUG_SERIAL as 1 before including this header (or via compiler -D
// flag) to enable serial debug output and Notecard debug streaming.
// Leave undefined (defaults to 0) for production builds: Serial.begin() is
// skipped entirely, eliminating the blocking USB-enumeration wait on every wake
// and removing the associated current draw from the solar/LiPo budget.
#ifndef DEBUG_SERIAL
#define DEBUG_SERIAL 0
#endif
#if DEBUG_SERIAL
#  define LOG(x)    Serial.print(x)
#  define LOGLN(x)  Serial.println(x)
#else
#  define LOG(x)    ((void)0)
#  define LOGLN(x)  ((void)0)
#endif

// ─── Timing bounds (applied when loading env-var overrides) ───────────────────
// Floor and ceiling prevent a bad Notehub fleet variable from creating a sleep
// interval so long it disables monitoring, or so short it pathologically drains
// the battery.
#define MIN_ENV_INTERVAL_MIN           1     //  1 min  — absolute floor for all timers
#define MAX_HEARTBEAT_STOPPED_MIN   1440     // 24 h    — max stopped-equipment heartbeat
#define MAX_HEARTBEAT_MOVING_MIN      60     // 60 min  — max moving-equipment heartbeat
#define MAX_HEARTBEAT_AFTERHOURS_MIN  30     // 30 min  — max after-hours scan interval
#define MAX_ALERT_COOLDOWN_MIN        60     // 60 min  — max alert-suppression window
#define MAX_INBOUND_MIN               60     // 60 min  — max inbound poll interval

// ─── Elapsed-time guard ───────────────────────────────────────────────────────
// Returns true only when `now` is valid (non-zero), no underflow would occur
// (now >= last_ts), and at least `interval` seconds have passed since `last_ts`.
// Centralising this guard prevents uint32_t wrap-around bugs when card.time
// fails and getEpochTime() returns 0 while last_ts is a stored non-zero epoch.
static inline bool elapsed(uint32_t now, uint32_t last_ts, uint32_t interval)
{
    return (now != 0) && (now >= last_ts) && ((now - last_ts) >= interval);
}

// ─── Alert-due guard ─────────────────────────────────────────────────────────
// Returns true if an alert should fire. First alert (last_ts == 0) is always
// allowed so geofence and motion alerts reach the operator even before the
// Notecard has synced a valid epoch from the network. Subsequent alerts require
// enough time to have elapsed since the last one, using eff_time (epoch with
// GPS fix timestamp as fallback) as the time base.
// If eff_time is also 0, suppresses repeat alerts until a time base is available.
static inline bool alertDue(uint32_t eff_time, uint32_t last_ts, uint32_t cooldown_s)
{
    if (last_ts == 0) return true;                       // first alert always fires
    return elapsed(eff_time, last_ts, cooldown_s);
}

// ─── Pin assignments (Notecarrier CX / Cygnet STM32) ─────────────────────────
// Relay driver: Cygnet GPIO → BSS138 MOSFET gate → relay coil → 12 V.
// BSS138 selected for R_DS(on) ≤ 3.5 Ω at V_GS = 2.5 V (Nexperia datasheet).
// 10 kΩ gate-to-source pulldown keeps MOSFET off when GPIO is high-impedance.
#define PIN_RELAY_DRIVER    A1
// Ignition sense: 12V ignition rail → 33kΩ (high-side) → A2 → 10kΩ (low-side) → GND.
// Divider output: 12V × 10/(33+10) ≈ 2.79V at A2 (HIGH = ignition ON).
// NOTE: bare voltage divider — POC front end only; add TVS and RC filtering for production.
#define PIN_IGNITION_SENSE  A2

// ─── Notefiles ───────────────────────────────────────────────────────────────
#define TRACKER_NOTEFILE   "tracker.qo"
#define ALERT_NOTEFILE     "alert.qo"
#define COMMAND_NOTEFILE   "immobilize.qi"
// fence.db persists the commissioned home-fence coordinates in Notecard flash.
// This is independent of the NotePayload sleep state so a power-loss/reconnect
// during a theft event cannot silently re-home the fence to the thief's location.
#define FENCE_NOTEFILE     "fence.db"

// Compact template port numbers — required for Notecard for Skylo satellite path.
#define TRACKER_PORT   50
#define ALERT_PORT     51

// ─── Timing defaults (all overridable via Notehub environment variables) ───────
#define HEARTBEAT_STOPPED_MIN     60   // 1-hour host wake interval when stationary (queues a heartbeat note each wake)
#define OUTBOUND_MIN             240   // 4-hour outbound batch window — decoupled from host wake interval
#define HEARTBEAT_MOVING_MIN       5   // 5-min heartbeat while moving
#define HEARTBEAT_AFTERHOURS_MIN   2   // 2-min after-hours scan
#define AFTER_HOURS_START_H       18   // 6 PM UTC
#define AFTER_HOURS_END_H          6   // 6 AM UTC
#define ALERT_COOLDOWN_MIN         5   // minimum gap between same-type alerts
// INBOUND_MIN: the "short" inbound interval applied when the device is after-hours
// or moving.  During daytime stopped operation the firmware uses a context-aware
// inbound equal to heartbeat_stopped_s instead, eliminating idle inbound sessions
// while the equipment sits static on the job site.  INBOUND_MIN governs the
// active-monitoring cadence only; applyHubCadence() selects between the two
// values based on the current moving/afterhrs context.
// Override via the Notehub `inbound_min` fleet env var (clamped to
// MIN_ENV_INTERVAL_MIN..MAX_INBOUND_MIN = 1..60 min).
#define INBOUND_MIN                4
#define DEFAULT_FENCE_RADIUS_M   200.0f

// ─── Persisted application state (survives sleep via NotePayload) ─────────────
struct AppState {
    bool    immobilize_pending;      // Relay staged; waiting for next key-on
    bool    immobilized;             // Relay currently asserted (ignition cut)
    bool    fence_set;               // Geofence center has been pinned
    bool    last_ignition_on;        // Ignition state on previous wake — OFF→ON edge detection for immobilizer
    // Set true only when loadFenceFromFlash positively confirms no persisted
    // record exists (Notecard returned "does not exist" / "not found").
    // GPS auto-anchor is gated on this flag so a transient I2C read failure on
    // boot cannot silently re-home the geofence to a thief's current location.
    bool    fence_confirmed_absent;
    // Notecard configuration success flags — persisted so ensureConfigured() can
    // retry exactly the steps that have not yet been confirmed on subsequent wakes.
    // All start false (cold boot memset) and are set true when the corresponding
    // Notecard request succeeds.  ensureConfigured() skips already-confirmed steps.
    bool    cfg_hub_ok;       // hub.set (product UID + mode) accepted
    bool    cfg_gps_ok;       // card.location.mode accepted
    bool    cfg_motion_ok;    // card.motion.mode accepted
    bool    cfg_templates_ok; // both compact note.templates registered
    // Wake-context flags from the previous wake cycle.  Used as a proxy for the
    // current state when applying Notecard cadence before fresh sensor reads are
    // available.  Updated at the end of each wake.  Default false (cold boot
    // memset) → stationary/daytime — a safe conservative starting point.
    bool    last_moving;     // Motion state on previous wake
    bool    last_afterhrs;   // After-hours state on previous wake
    double  fence_lat;
    double  fence_lon;
    float   fence_radius_m;
    int8_t  after_hours_start;      // UTC hour (0-23)
    int8_t  after_hours_end;
    int32_t heartbeat_stopped_s;
    int32_t heartbeat_moving_s;
    int32_t heartbeat_afterhours_s;
    // inbound_s: short inbound poll cadence (seconds) used when after-hours or
    // moving.  When daytime stopped, applyHubCadence() uses heartbeat_stopped_s
    // as the inbound interval instead — avoiding idle sessions while parked.
    // Default: INBOUND_MIN * 60 = 240 s (4 min).
    int32_t inbound_s;
    // outbound_s: Notecard outbound batch window (seconds) — how often queued
    // heartbeat notes are transmitted to Notehub.  Decoupled from the host wake
    // interval (heartbeat_stopped_s) so multiple heartbeats can accumulate per
    // outbound session, reducing total cellular sessions without degrading
    // detection latency.  Alert notes (sync:true) bypass this window entirely.
    // Default: OUTBOUND_MIN * 60 = 14400 s (240 min / 4 hours).
    int32_t outbound_s;
    int32_t alert_cooldown_s;
    uint32_t env_modified;              // Epoch of last known env-var change
    uint32_t last_heartbeat_s;
    uint32_t last_geofence_alert_s;     // Epoch of last geofence_breach alert
    uint32_t last_motion_alert_s;       // Epoch of last motion_after_hours alert
    // Separate cooldown for the cmd_retrieve_failed diagnostic alert so a
    // persistent command-path fault does not generate a sync:true storm on
    // every wake.  Uses the same alert_cooldown_s window as theft alerts.
    uint32_t last_cmd_failed_alert_s;   // Epoch of last cmd_retrieve_failed alert
};

extern const char kStateSegID[];

// ─── Function declarations ────────────────────────────────────────────────────
// Notecard configuration and note emission
// ensureConfigured: idempotent Notecard setup — safe to call on every wake.
// Checks cfg_hub_ok, cfg_gps_ok, cfg_motion_ok, and cfg_templates_ok in AppState
// and re-applies only the steps that have not yet been confirmed.  Returns true
// when all four steps are confirmed; false when one or more are still pending
// (they will be retried on the next wake automatically).  This makes the device
// self-healing: a hub.set or template failure on cold boot does not leave it
// permanently misconfigured — it recovers without a full power cycle.
// Inbound cadence is set from s.inbound_s (default INBOUND_MIN * 60, tunable
// via the `inbound_min` Notehub fleet env var) so commands arrive promptly
// even during long daytime sleep periods.
bool ensureConfigured(Notecard &nc, const char *product_uid, AppState &s);
// defineTemplates: returns true only when both compact note templates are
// successfully registered.  Compact templates are required for Notecard for
// Skylo satellite uplinks; each template is retried up to 3 times.
bool defineTemplates(Notecard &nc);
// fetchEnvOverrides returns true when Notehub delivered new env-var values
// (i.e. the env_modified timestamp advanced), false when nothing changed or on
// error.  The caller should reissue applyHubCadence() after a true return so
// that the Notecard's internal outbound/inbound session windows and GNSS
// acquisition cadence stay in sync with any updated heartbeat intervals.
bool fetchEnvOverrides(Notecard &nc, AppState &s);
// applyHubCadence reissues hub.set (outbound/inbound) AND card.location.mode
// (GNSS acquisition seconds) so all cadences stay in sync with the current
// AppState values.  Called when env vars change OR when the moving/afterhrs
// context transitions so the Notecard's inbound window adjusts immediately.
//
// Inbound is context-aware:
//   • After-hours or moving: s.inbound_s (default 4 min) — short poll for
//     prompt command delivery and alert responsiveness.
//   • Daytime stopped: s.heartbeat_stopped_s — matches the host wake interval
//     so the Notecard does one combined session per wake rather than dozens of
//     idle inbound polls while the equipment sits parked on the job site.
//
// Outbound uses s.outbound_s (default OUTBOUND_MIN * 60 = 240 min) — a batch
// window decoupled from the host wake interval.  Multiple heartbeats accumulate
// between sessions; alert notes (sync:true) bypass this window entirely.
void applyHubCadence(Notecard &nc, const AppState &s, bool moving, bool afterhrs);
// checkAndHandleCommand drains the entire immobilize.qi queue in one wake cycle.
// If multiple commands are queued the last one wins — operators must cancel any
// outstanding command before issuing a new one if ordering matters.
// Returns true when the command path is healthy (queue empty or commands processed).
// Returns false when note.get failed after all retries, OR when it returned a
// non-empty-queue error — the caller should emit a diagnostic alert so operators
// can distinguish 'no command queued' from 'command path unhealthy'.
bool checkAndHandleCommand(Notecard &nc, AppState &s);
// sendHeartbeat queues a periodic heartbeat note. Returns false and logs a
// serial warning if the Notecard request fails.
// fence_ok: -1 = unknown (no valid GPS fix or fence not yet anchored),
//            0 = geofence breach, 1 = in-fence.
// loc_valid: true when cur_lat/cur_lon reflect a real GNSS fix; false when
//            coordinates are 0,0 due to no fix (prevents downstream map pins
//            at null island being interpreted as real equipment location).
bool sendHeartbeat(Notecard &nc, const AppState &s,
                   double lat, double lon, bool loc_valid,
                   bool moving, int8_t fence_ok,
                   bool ignition_on, int32_t fix_age_s);
// sendAlert immediately syncs a security event note.  Retries up to 3 times on
// transient I2C / Notecard errors so alerts and command acknowledgments are not
// silently lost.  Returns false only when all retries fail.
// loc_valid: true when lat/lon carry a real GNSS fix (see sendHeartbeat above).
// fix_age_s: elapsed seconds since the cached GNSS fix was acquired, or -1 when
// card.time is unavailable and the age cannot be determined.  Downstream
// consumers must treat -1 as "age unknown" and must not interpret it as fresh.
bool sendAlert(Notecard &nc, const AppState &s,
               const char *alert_type, double lat, double lon, bool loc_valid,
               bool ignition, bool immobilized, int32_t fix_age_s);

// Geofence flash persistence
// saveFenceToFlash writes the current fence center and radius to FENCE_NOTEFILE
// (fence.db) so they survive a full power loss, preventing re-homing to the
// thief's location on reconnect.  Retries up to 3 times on transient failures.
// Returns true on success; false when all retries fail.  The caller must emit a
// diagnostic and must NOT treat the fence as durably commissioned across reboots
// until a subsequent call succeeds.
bool saveFenceToFlash(Notecard &nc, const AppState &s);
// loadFenceFromFlash restores fence_lat, fence_lon, fence_radius_m, and fence_set
// from FENCE_NOTEFILE.  Returns true on success.  Returns false in two distinct
// cases, disambiguated by the io_error out-parameter:
//   io_error == false: Notecard positively confirmed no record exists
//                      (first-ever boot or record was deliberately deleted).
//                      GPS auto-anchor is safe.
//   io_error == true:  Transport or API failure; fence presence is unknown.
//                      Caller must NOT allow GPS auto-anchor — see AppState::fence_confirmed_absent.
// Retries up to 3 times on transport failures before returning with io_error == true.
bool loadFenceFromFlash(Notecard &nc, AppState &s, bool &io_error);

// Sensor reads (all return safe defaults on Notecard error)
// last_state: the ignition state persisted from the previous wake (g_state.last_ignition_on).
// Used as the hysteresis seed: the ADC output must cross both the on-threshold (rising)
// and the off-threshold (falling) before the declared state toggles, filtering
// battery-sag and line-noise transients common in automotive/equipment environments.
bool     getIgnitionState(bool last_state);
bool     getIsMoving(Notecard &nc);
// getLocation populates lat/lon and fix_time (epoch of the cached GNSS fix).
// fix_time == 0 when no valid fix has been recorded.  The caller computes
// fix_age_s = (now - fix_time) when card.time is available (now > 0), or
// emits the sentinel -1 when card.time is unavailable, so downstream consumers
// can distinguish "fix is fresh" from "age is unknown".
bool     getLocation(Notecard &nc, double &lat, double &lon, uint32_t &fix_time);
float    getBatteryVoltage(Notecard &nc);
uint32_t getEpochTime(Notecard &nc);

// Utility
bool  isAfterHours(uint32_t epoch, int8_t start_h, int8_t end_h);
float haversineDistanceM(double lat1, double lon1, double lat2, double lon2);

// Relay
void assertRelay();
void releaseRelay();
