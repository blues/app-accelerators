/*******************************************************************************
 * trailer_fleet_tracker_starnote_helpers.h
 *
 * Shared types, constants, extern declarations, and function prototypes for
 * the Untethered Trailer & Chassis Fleet Tracker firmware — ocean-capable
 * variant using Notecard Cellular + Starnote for Iridium LEO.
 * Included by both trailer_fleet_tracker_starnote.ino and
 * trailer_fleet_tracker_starnote_helpers.cpp.
 ******************************************************************************/
#pragma once

#include <Notecard.h>

// ---------------------------------------------------------------------------
// Product UID — copy from Notehub → Project Settings → ProductUID
// ---------------------------------------------------------------------------
#ifndef PRODUCT_UID
#define PRODUCT_UID ""
#pragma message "PRODUCT_UID is not defined. Set it to your Notehub ProductUID."
#endif

// ---------------------------------------------------------------------------
// Debug serial — opt-in.  Uncomment the line below to enable USB-CDC serial
// logging at 115200 baud during development.  Leave it commented out for
// deployment builds: when usbSerial is undefined the entire serial-init block
// in setup() — including any USB-ready wait — is compiled out, keeping every
// solar-powered wake as short as possible.
// ---------------------------------------------------------------------------
// #define usbSerial Serial

// ---------------------------------------------------------------------------
// Notefiles (all compact-templated for satellite efficiency)
// ---------------------------------------------------------------------------
#define NOTEFILE_EVENT      "trailer_event.qo"
#define NOTEFILE_LOCATION   "trailer_location.qo"
#define NOTEFILE_HEARTBEAT  "trailer_heartbeat.qo"

// Compact template port numbers (required for compact format; unique per project)
#define PORT_EVENT          50
#define PORT_LOCATION       51
#define PORT_HEARTBEAT      52

// ---------------------------------------------------------------------------
// State constants
// ---------------------------------------------------------------------------
#define STATE_PARKED        0
#define STATE_MOVING        1

#define EVENT_DEPARTED      1   // trailer just began moving
#define EVENT_ARRIVED       2   // trailer just stopped

// ---------------------------------------------------------------------------
// Firmware defaults — all overridable via Notehub environment variables
// ---------------------------------------------------------------------------
#define DEFAULT_PARKED_CHECK_SECS   300     //  5 min: motion poll cadence when parked
#define DEFAULT_MOVING_PING_SECS    900     // 15 min: GPS report cadence when moving
#define DEFAULT_HEARTBEAT_SECS      21600   //  6 hr:  alive-ping cadence when parked

// Voltage-variable outbound sync (hub.set voutbound) — reduces cellular
// activity on a low or depleted solar battery.
// high ≈ >3.9V, normal ≈ >3.7V, low ≈ >3.5V, dead ≈ <3.5V
#define VOUTBOUND_PROFILE   "high:60;normal:120;low:360;dead:0"
#define VINBOUND_PROFILE    "high:120;normal:240;low:720;dead:0"

// Env-var poll interval: check Notehub for updated thresholds once per hour
#define ENV_POLL_SECS       3600

// Increment whenever a firmware update changes Notecard configuration or note
// templates.  The persisted config_version field is compared on every wake; a
// mismatch triggers a full re-configure so Notecard-side settings stay in sync
// with the new firmware automatically.
// v1: initial release; FIFO pending-event queue and parked_since_needs_init
//     dwell-backfill included from the start.
// v1 → v2: PendingEvent struct extended with gps_valid, event_epoch,
//           event_lat, and event_lon captured at transition time so retried
//           departure/arrival notes are stamped with the original transition
//           location and timestamp rather than the Notecard's GPS state at
//           retry time.  trailer_event.qo template changed: _lat/_lon/_time
//           auto-populated keywords replaced with explicit lat/lon/evt_time
//           fields written from the stored capture.  AppState layout changed —
//           any restored v1 payload is treated as first-boot.
#define FIRMWARE_CONFIG_VERSION   2

// ---------------------------------------------------------------------------
// Pending-event FIFO — persisted inside AppState so in-flight transition
// events survive sleep and are retried in FIFO order until the Notecard
// confirms receipt.  Using a queue rather than a single slot prevents a new
// transition from overwriting an undelivered prior event (e.g. a failed
// DEPARTED immediately followed by an ARRIVED before the first retry lands).
// ---------------------------------------------------------------------------
#define PENDING_QUEUE_DEPTH  4

// One slot in the pending-event ring buffer.
// sizeof(PendingEvent) == 20 bytes.
// gps_valid, event_epoch, event_lat, and event_lon are captured once at
// transition time (not at retry time) so that every delivery attempt for a
// given transition event carries the correct original location and timestamp,
// regardless of how much GPS state has changed between the transition and the
// eventual successful delivery.
typedef struct {
    uint8_t  type;          // 0=none, EVENT_DEPARTED, or EVENT_ARRIVED
    uint8_t  gps_valid;     // 1=valid fix captured at transition time, 0=no fix
    uint8_t  _pad[2];       // explicit alignment padding; always 0
    float    dwell_h;       // hours parked before departure (0.0 for arrivals)
    uint32_t event_epoch;   // Unix epoch captured at transition time (0=unknown)
    float    event_lat;     // GPS latitude captured at transition time
    float    event_lon;     // GPS longitude captured at transition time
} PendingEvent;

// ---------------------------------------------------------------------------
// Persisted application state (saved to Notecard flash across sleep cycles)
// ---------------------------------------------------------------------------
typedef struct {
    uint8_t      config_version;          // re-configure when != FIRMWARE_CONFIG_VERSION
    uint8_t      current_state;           // STATE_PARKED or STATE_MOVING
    uint8_t      parked_since_needs_init; // 1: parked_since not yet set with a valid epoch
    uint8_t      pending_head;            // ring-buffer read index (0–PENDING_QUEUE_DEPTH-1)
    uint8_t      pending_count;           // number of events currently in the FIFO
    uint8_t      _reserved[3];            // alignment padding to reach 8-byte boundary; always 0
    uint32_t     parked_since;            // Unix epoch when trailer last parked (dwell calc)
    uint32_t     last_location_at;        // Unix epoch of last trailer_location.qo
    uint32_t     last_heartbeat_at;       // Unix epoch of last trailer_heartbeat.qo
    uint32_t     last_env_poll_at;        // Unix epoch of last env.get call
    uint32_t     parked_check_secs;       // from env var parked_check_mins
    uint32_t     moving_ping_secs;        // from env var moving_ping_mins
    uint32_t     heartbeat_secs;          // from env var heartbeat_hours
    // Pending transition-event FIFO.  Physical state is committed at the
    // moment of a PARKED↔MOVING transition regardless of whether the
    // corresponding note.add succeeds.  Events are queued here and drained
    // in FIFO order on every wake until the Notecard confirms acceptance.
    // A FIFO prevents a new transition from overwriting an undelivered prior
    // event — e.g. a failed DEPARTED immediately followed by an ARRIVED.
    PendingEvent pending_events[PENDING_QUEUE_DEPTH]; // ring buffer; head at pending_head
} AppState;

// ---------------------------------------------------------------------------
// Hardware objects and persisted-state segment ID
// ---------------------------------------------------------------------------
extern Notecard     notecard;
extern const char   kStateSegId[];

// ---------------------------------------------------------------------------
// Function prototypes
// ---------------------------------------------------------------------------
bool     sendAndCheck(J *req, const char *tag);
bool     notecardConfigure();
bool     defineTemplates();
bool     fetchEnvOverrides(AppState &s);
bool     isMoving(bool &out_moving);
bool     getEpoch(uint32_t &out_time);
bool     getBatteryVoltage(float &out_volt);
bool     hasValidGnssFix();
void     captureGnssState(float &out_lat, float &out_lon, uint8_t &out_gps_valid);
bool     sendTransitionEvent(uint8_t type, float dwell_hours,
                              uint8_t gps_valid, float lat, float lon,
                              uint32_t evt_epoch);
bool     sendLocationNote();
bool     sendHeartbeatNote(float volt);
bool     enqueuePendingEvent(AppState &s, uint8_t type, float dwell_h,
                              uint32_t event_epoch, float event_lat,
                              float event_lon, uint8_t gps_valid);
void     drainPendingQueue(AppState &s);
