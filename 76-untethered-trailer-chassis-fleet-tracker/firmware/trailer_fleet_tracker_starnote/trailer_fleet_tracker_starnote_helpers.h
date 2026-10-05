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
// Debug serial — opt-in.  Uncomment the line below to enable serial logging
// at 115200 baud during development.  Leave it commented out for deployment
// builds: when usbSerial is undefined every log call is compiled out, keeping
// every solar-powered wake as short as possible.
//
// The macro name is historical: USB CDC must be disabled (USB support: None)
// for the Swan to stay in STOP2, so logging goes to debugSerial — the UART on
// the Swan's STLINK debug connector, which an ST-LINK exposes as a virtual COM
// port — not to the Swan's USB-C port.  debugSerial is defined in the .ino.
// ---------------------------------------------------------------------------
// #define usbSerial debugSerial

#ifdef usbSerial
extern Uart debugSerial;   // defined in trailer_fleet_tracker_starnote.ino
#endif

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
// activity on a low or depleted solar battery.  notecardConfigure() issues
// `card.voltage {"mode":"lipo"}` which sets the bucket thresholds to:
// usb >= 4.6V, high >= 4.0V, normal >= 3.5V, low >= 3.2V, dead < 3.2V.
#define VOUTBOUND_PROFILE   "high:60;normal:120;low:360;dead:0"
#define VINBOUND_PROFILE    "high:120;normal:240;low:720;dead:0"

// Env-var poll interval: check Notehub for updated thresholds once per hour
#define ENV_POLL_SECS       3600

// Increment whenever a firmware update changes Notecard configuration or note
// templates.  config_version is set only after notecardConfigure() and
// defineTemplates() both succeed, so it doubles as the "Notecard configured"
// flag: while it differs from this value, every wake retries configuration.
// AppState lives in RAM and is zeroed by every reset, so a reflash always
// starts from an unconfigured state and reapplies Notecard-side settings.
// v1: initial release; FIFO pending-event queue and parked_since_needs_init
//     dwell-backfill included from the start.
// v1 → v2: PendingEvent struct extended with gps_valid, event_epoch,
//           event_lat, and event_lon captured at transition time so retried
//           departure/arrival notes are stamped with the original transition
//           location and timestamp rather than the Notecard's GPS state at
//           retry time.  trailer_event.qo template changed: _lat/_lon/_time
//           auto-populated keywords replaced with explicit lat/lon/evt_time
//           fields written from the stored capture.  AppState layout changed.
#define FIRMWARE_CONFIG_VERSION   2

// ---------------------------------------------------------------------------
// Pending-event FIFO — held inside AppState so in-flight transition events
// survive sleep and are retried in FIFO order until the Notecard confirms
// receipt.  Using a queue rather than a single slot prevents a new
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
// Application state.  Lives in RAM: the Swan sleeps in STM32 STOP2 between
// wakes, which retains SRAM, so this struct survives every sleep/wake cycle
// and is reset only by a power cycle or reset (which re-runs setup()).
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
// Hardware objects
// ---------------------------------------------------------------------------
extern Notecard     notecard;

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
