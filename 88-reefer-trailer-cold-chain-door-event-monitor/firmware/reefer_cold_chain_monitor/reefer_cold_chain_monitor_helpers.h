/*
 * reefer_cold_chain_monitor_helpers.h
 *
 * Shared constants, AppState definition, extern declarations, and helper-
 * function prototypes for reefer_cold_chain_monitor.ino.
 *
 * Include this header from both the .ino and the helpers .cpp; all
 * application-global state is defined in the .ino and accessed here via
 * extern declarations.
 */
#pragma once

#include <Notecard.h>
#include <DallasTemperature.h>

// ── Debug output ──────────────────────────────────────────────────────────────
// DEBUG_MODE is defined by the user in the .ino (or via -DDEBUG_MODE build
// flag) *before* this header is included.  The macros below expand to no-ops
// in production builds, so there is zero overhead on the deployed trailer.
#ifdef DEBUG_MODE
#  define DEBUG_PRINT(x)   Serial.print(x)
#  define DEBUG_PRINTLN(x) Serial.println(x)
#else
#  define DEBUG_PRINT(x)
#  define DEBUG_PRINTLN(x)
#endif

// ── Product UID ───────────────────────────────────────────────────────────────
#ifndef PRODUCT_UID
#define PRODUCT_UID ""
#pragma message "PRODUCT_UID is not defined. Set it to your Notehub ProductUID."
#endif

// ── Pin assignments ───────────────────────────────────────────────────────────
// A0: 1-Wire data bus.  The 4.7 kΩ pull-up resistor ships with Adafruit #381;
//     wire it between A0 and +3V3.
#define ONE_WIRE_PIN  A0

// A1: Door reed switch (Adafruit #375, normally-open).
//     Wire one lead to A1, the other to GND.  INPUT_PULLUP is set in firmware.
//     LOW = door CLOSED (magnet present → switch closed → GND)
//     HIGH = door OPEN  (magnet absent  → switch open  → pulled HIGH)
#define DOOR_PIN      A1

// ── Default thresholds (all overridable via Notehub environment variables) ────
#define DEFAULT_TEMP_MAX_C           7.0f   // °C: excursion warm alert threshold
#define DEFAULT_TEMP_MIN_C         -25.0f   // °C: freeze / cold excursion threshold
#define DEFAULT_DOOR_ALERT_SEC       600UL  // s:  alert if door stays open this long
#define DEFAULT_SAMPLE_INTERVAL_SEC   60UL  // s:  between sensor samples
#define DEFAULT_SUMMARY_INTERVAL_MIN  60UL  // min: between summary notes
#define DEFAULT_ALERT_COOLDOWN_SEC  1800UL  // s:  min gap between repeated temp alerts

// ── Notefile names ────────────────────────────────────────────────────────────
//
// Alert notefile (critical, any transport):
//
//   NOTEFILE_ALERT  ("trailer_alert.qo")
//     NTN-compatible: format:compact, port:TEMPLATE_PORT_ALERT.
//     No delete:true — a queued alert is never purged before a sync.
//     The Notecard delivers it over the first available transport (cellular
//     preferred; Skylo NTN satellite as automatic fallback).  Compact encoding
//     with a port number satisfies NTN requirements and is fully supported on
//     cellular too.  Downstream consumers can distinguish transport via Notehub
//     session metadata attached to each received event.
//
// Per-sample log notefile (noncritical, cellular/WiFi only):
//
//   NOTEFILE_LOG  ("trailer_log_cell.qo")
//     Cellular/WiFi only: no format/port, delete:true.
//     One note per sample cycle (default every 60 s).  Notes queue locally
//     on the Notecard and arrive in Notehub in batches on each outbound
//     session (default 60 min).  Non-NTN-compatible template (no format/port)
//     + delete:true: the Notecard discards queued notes at sync time when NTN
//     is the active transport — per-sample records never consume the 10 KB
//     bundled satellite budget.  See Blues Satellite Best Practices:
//     https://dev.blues.io/starnote/satellite-best-practices/
//     Logs resume accumulating and uploading when terrestrial connectivity
//     returns; NTN-only gaps in the per-sample record are expected.
//
// Hourly summary notefile (noncritical, cellular/WiFi only):
//
//   NOTEFILE_SUMMARY  ("trailer_summary_cell.qo")
//     Cellular/WiFi only: no format/port, delete:true.
//     Hourly aggregates (mean/min/max per probe, door event count).
//     Same non-NTN + delete:true pattern as NOTEFILE_LOG: the Notecard
//     discards queued notes at sync time when NTN is the active transport,
//     rather than routing stale summaries over satellite.
//
// Note: note.template registration is a local Notecard operation that does not
// require an active network session.  Templates are registered at first boot
// and take effect immediately, before the first sync over any transport.
#define NOTEFILE_ALERT   "trailer_alert.qo"
#define NOTEFILE_LOG     "trailer_log_cell.qo"
#define NOTEFILE_SUMMARY "trailer_summary_cell.qo"

// ── Note template port number (NTN-compatible alert notefile only) ────────────
// Port is required for compact/NTN operation; range 1-100 per Blues docs.
// The summary notefile is non-NTN and has no port.
#define TEMPLATE_PORT_ALERT  51

// ── State segment ID for NotePayloadSaveAndSleep ──────────────────────────────
// 4-char tag per note-c NP_SEGTYPE_LEN; identifies the AppState blob in payload.
#define STATE_SEG_ID  "STAT"

// ── Sentinel for a disconnected or failed DS18B20 probe ──────────────────────
// DallasTemperature returns DEVICE_DISCONNECTED_C (-127.0) on failure.
#define TEMP_INVALID  -127.0f

// ── Door pending-alert types (stored in AppState.pending_door_alert) ─────────
// An in-flight door alert is retried until sendAlert() confirms enqueue.
// This prevents a transient note.add failure from permanently losing an event.
#define DOOR_PENDING_NONE   0u
#define DOOR_PENDING_OPEN   1u
#define DOOR_PENDING_CLOSE  2u
#define DOOR_PENDING_LONG   3u

// ── Temperature pending-alert types (stored in AppState.pending_temp_alert) ──
// Mirrors the door pending-alert pattern.  A brief excursion that returns to
// normal before the next wake is not lost: the alert is latched at detection
// and retried until both notefiles are successfully enqueued.
#define TEMP_PENDING_NONE  0u
#define TEMP_PENDING_WARM  1u   // temp_excursion (above g_tempMaxC)
#define TEMP_PENDING_COLD  2u   // temp_cold      (below g_tempMinC)

// ── Application state (persisted to Notecard flash across sleep cycles) ───────
typedef struct {
    bool     configured;            // true after first-boot hub.set / templates
    uint32_t summary_interval_min;  // last-applied hub.set outbound cadence

    // Rolling summary accumulators
    float    t1_sum_c;
    float    t2_sum_c;
    int32_t  t1_count;
    int32_t  t2_count;
    float    t1_min_c;
    float    t1_max_c;
    float    t2_min_c;
    float    t2_max_c;

    // Door tracking
    bool     door_open;             // last known door state
    uint32_t door_opened_epoch;     // epoch when door last opened
    bool     door_long_alert_sent;  // true once door_open_long fires per open event
    int32_t  door_events;           // door-open count in current summary window

    // Pending door-transition alert.
    // Physical door state (door_open, door_opened_epoch) is advanced immediately
    // on detection so the same transition is never re-detected.  The alert is
    // considered delivered only after sendAlert() returns true; until then
    // pending_door_alert holds the event type and pending_door_open_sec holds
    // the duration so the next wake can retry without losing the event.
    // pending_door_sent tracks whether the alert note has already been enqueued
    // so a retry does not duplicate a note if the prior wake successfully
    // enqueued it but failed on hub.sync.
    // pending_door_lat/lon are latched at event-detection time so retries carry
    // the location where the event occurred, not the trailer's current position.
    uint8_t  pending_door_alert;     // DOOR_PENDING_* value
    uint32_t pending_door_open_sec;  // duration for close / long-open retries
    bool     pending_door_sent;      // alert notefile already enqueued
    float    pending_door_lat;       // GNSS latitude at event-detection time
    float    pending_door_lon;       // GNSS longitude at event-detection time

    // Pending temperature-excursion alert (mirrors door pending-alert pattern).
    // Latched at first detection; cleared only after the alert notefile is
    // successfully enqueued.  Preserves a brief excursion that returns to
    // normal before the next wake and whose initial sendAlert() failed.
    // The sampled readings, door state, and location are stored so the retry
    // transmits the exact conditions captured at detection time, not the current
    // (possibly normal) readings or a different trailer position on a later wake.
    uint8_t  pending_temp_alert;     // TEMP_PENDING_* value
    float    pending_temp_t1;        // probe readings at detection time
    float    pending_temp_t2;
    bool     pending_temp_door_open; // door state at detection time
    bool     pending_temp_sent;      // alert notefile already enqueued
    float    pending_temp_lat;       // GNSS latitude at event-detection time
    float    pending_temp_lon;       // GNSS longitude at event-detection time

    // Summary window
    uint32_t window_start_epoch;

    // Alert deduplication (temperature)
    uint32_t last_temp_alert_epoch;

    // Timekeeping
    //
    // rtc_synced_once — set permanently the first time card.time returns a
    //   non-zero epoch.  Prevents reverting to tiny synthetic timestamps after
    //   a transient card.time failure once the device has seen network time.
    //
    // synthetic_epoch — monotonic counter advanced by g_sampleIntervalSec on
    //   each wake before the RTC is ever synced (pre-sync startup path only).
    //   Cleared to 0 permanently on the first real-epoch wake.
    //
    // last_good_epoch — the most recent non-zero epoch returned by card.time.
    //   After rtc_synced_once is true, transient card.time failures advance
    //   from this value (by g_sampleIntervalSec) rather than resetting to tiny
    //   synthetic timestamps that would corrupt cooldown/elapsed-time math.
    bool     rtc_synced_once;
    uint32_t synthetic_epoch;
    uint32_t last_good_epoch;
} AppState;

// ── Clamp helpers ─────────────────────────────────────────────────────────────
// Reject env-var values outside safe operational ranges; keeps the existing
// value as fallback.  Prevents zero-interval tight loops, uint32_t wrapping
// from negative strings, and other pathological behaviour from bad env values.
static inline uint32_t clampU32(long v, uint32_t minv, uint32_t maxv,
                                 uint32_t fallback) {
    if (v < (long)minv || v > (long)maxv) return fallback;
    return (uint32_t)v;
}
static inline float clampF(double v, float minv, float maxv, float fallback) {
    if (v < (double)minv || v > (double)maxv) return fallback;
    return (float)v;
}

// ── Extern declarations for globals defined in the .ino ───────────────────────
extern Notecard          notecard;
extern DallasTemperature probes;
extern AppState          g_state;
extern float             g_tempMaxC;
extern float             g_tempMinC;
extern uint32_t          g_doorAlertSec;
extern uint32_t          g_sampleIntervalSec;
extern uint32_t          g_summaryIntervalMin;
extern uint32_t          g_alertCooldownSec;

// ── Helper function prototypes ────────────────────────────────────────────────
bool     hubConfigure(void);
bool     defineTemplates(void);
void     fetchEnvOverrides(void);
void     applyHubSetIfChanged(AppState &s);
bool     readTemperatures(float &t1, float &t2);
bool     readDoorState(void);
void     checkDoorEvents(AppState &s, float t1, float t2,
                         bool doorOpen, uint32_t now);
void     checkTemperatureExcursion(AppState &s, float t1, float t2,
                                   uint32_t now);
void     accumulateSummary(AppState &s, float t1, float t2);
bool     sendSummary(AppState &s, bool doorOpen);
bool     sendAlert(const char *type, float t1, float t2,
                   bool doorOpen, uint32_t openSec, float lat, float lon,
                   bool &done);
void     sendLog(float t1, float t2, bool doorOpen);
uint32_t getEpochTime(void);
bool     getLocation(float &lat, float &lon);
