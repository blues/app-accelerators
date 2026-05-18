/*
 * ev_charger_session_monitor_helpers.h
 *
 * Shared constants, State struct definition, extern globals, and
 * helper-function declarations for the EV Charger Session &
 * Utilization Monitor sketch.
 *
 * Include this header in both ev_charger_session_monitor.ino and
 * ev_charger_session_monitor_helpers.cpp.
 */

#pragma once

#include <Notecard.h>

// ── RS-485 / Modbus hardware ──────────────────────────────────────────────────
// Serial1 (D0 = RX, D1 = TX on the Notecarrier CX dual 16-pin header) carries
// Modbus RTU frames to/from the SDM120 energy meter over RS-485.
// PIN_RS485_DE drives the transceiver's direction-control input HIGH during the
// transmit window and LOW during receive.  On the SparkFun BOB-10124 (SP3485),
// this is the RTS pad, which the board routes to both DE and RE internally.
#define PIN_RS485_DE            2       // D2 on Notecarrier CX header

// SDM120 Modbus RTU defaults (both overridable via Notehub environment variables)
#define MODBUS_DEFAULT_BAUD     9600
#define MODBUS_DEFAULT_ID       1

// ── SDM120 input-register addresses (Function Code 0x04) ─────────────────────
// Each parameter is an IEEE-754 float stored in two consecutive 16-bit registers
// in big-endian word order (high word first, low word second).
// Pass each address with a count of 2 to ModbusMaster::readInputRegisters().
#define SDM_REG_VOLTAGE         0x0000  // Volts RMS
#define SDM_REG_POWER           0x000C  // Active power, Watts (signed; < 0 = export)
#define SDM_REG_IMPORT_KWH      0x0048  // Cumulative import active energy, kWh

// ── Notefile names ────────────────────────────────────────────────────────────
#define FILE_SESSION            "charger_session.qo"    // per-session event; sync:true
#define FILE_ALERT              "charger_alert.qo"      // mains-absent alert; sync:true
#define FILE_SUMMARY            "charger_summary.qo"    // hourly summary; templated

// ── Firmware defaults (all overridable via Notehub environment variables) ─────
#define DEFAULT_SAMPLE_SEC          30       // seconds between host wakes
#define DEFAULT_REPORT_MIN          60       // minutes between summary Notes
#define DEFAULT_SESSION_W           500.0f   // W threshold to open a charging session
#define DEFAULT_SESSION_END_COUNT   3        // consecutive below-threshold wakes to close session
#define DEFAULT_VOLTAGE_PRESENT_V   85.0f    // V_rms floor; below = mains absent
#define DEFAULT_ALERT_OFFLINE_MIN   240      // minutes of mains absence before alert fires

// ── Payload segment ID and layout magic ──────────────────────────────────────
#define STATE_SEG_ID            "EVCS"
// Bump STATE_MAGIC whenever the State struct layout changes so that existing
// persisted payloads are rejected and a cold boot re-initialises cleanly.
// Revision history:
//   0x45564353 ('E','V','C','S') — original CT-only layout
//   0x45564354 ('E','V','C','T') — added notecard_configured / template_defined flags
//   0x45564355 ('E','V','C','U') — added hub_cadence_dirty; pending-session fields
//   0x45564356 ('E','V','C','V') — energy-meter redesign: replaced CT ADC fields with
//                                   SDM120 Modbus fields; session energy from meter delta;
//                                   added window_available_sec; removed CT accumulators
//   0x45564357 ('E','V','C','W') — added window_elapsed_sec (wall-clock availability
//                                   denominator) and last_valid_import_kwh (safe kWh
//                                   closing baseline); sample_coverage_pct in template
//   0x45564358 ('E','V','C','X') — added window_kwh_baseline_set: defers window_start_kwh
//                                   anchoring until the first valid meter read after window
//                                   open or summary reset; prevents inflated total_kwh when
//                                   the first time-synced wake had an invalid meter poll
#define STATE_MAGIC             0x45564358u

// ── Meter reading returned by pollMeter() ────────────────────────────────────
struct MeterReading {
    float voltage_v;      // V_rms (0.0 if invalid)
    float power_w;        // Active power, Watts (0.0 if invalid or export)
    float import_kwh;     // Cumulative import energy, kWh (0.0 if invalid)
    bool  valid;          // false if any Modbus read failed this wake
};

// ── Persistent state across sleep cycles ─────────────────────────────────────
// Serialised into Notecard flash via NotePayloadSaveAndSleep(); recovered after
// card.attn wakes the host.  All session and window state lives here so it
// survives the host being fully powered down between samples.
struct State {
    uint32_t magic;                        // must equal STATE_MAGIC; mismatch → cold boot

    // ── Notecard initialisation flags ───────────────────────────────────────
    // Each flag starts false on cold boot and is set true only after the
    // corresponding Notecard request returns without error, so a transient
    // I²C failure on a prior wake triggers an automatic retry on the next.
    // hub_cadence_dirty is set before a hub.set attempt and cleared only on
    // confirmed success, preventing permanent cadence drift from a single fault.
    bool     notecard_configured;
    bool     template_defined;
    bool     hub_cadence_dirty;

    // ── Active session ──────────────────────────────────────────────────────
    bool     session_active;
    uint32_t session_start_epoch;          // UTC epoch when session opened
    float    session_start_kwh;            // meter import_kwh at session open;
                                           // session energy = (close_kwh − session_start_kwh)
    float    session_peak_w;               // peak active power seen this session
    uint8_t  below_threshold_count;        // consecutive below-threshold wakes

    // ── Pending completed-session note ──────────────────────────────────────
    // When a session closes and emitSessionNote() fails, the payload is stored
    // here and session_active is cleared immediately — decoupling new-session
    // detection from Note-queue retries.  Retried at the top of
    // runSessionStateMachine() every wake, then falls through to normal state-
    // machine processing so a back-to-back session during a Notecard fault is
    // never silently missed.  Single slot; see README §9 for the ring-buffer
    // extension if extended fault conditions are a concern.
    bool     pending_session_note;
    float    pending_session_kwh;
    float    pending_session_peak_w;
    uint32_t pending_session_start_epoch;
    uint32_t pending_session_end_epoch;

    // ── Hourly window accumulators ──────────────────────────────────────────
    uint8_t  window_sessions;              // completed sessions in this window
    float    window_completed_session_kwh; // kWh from sessions that closed in this window
    float    window_peak_w;                // peak active power seen in window
    uint32_t charging_sec;                 // seconds with power ≥ session_threshold_w
    uint32_t idle_sec;                     // seconds with meter.valid and power < session_threshold_w
                                           // (wakes where all Modbus retries failed are excluded)
    uint32_t window_available_sec;         // seconds with V_rms ≥ voltage_present_v (valid-poll
                                           // wakes only); numerator for availability_pct
    uint32_t window_elapsed_sec;           // wall-clock seconds since window open; advances every
                                           // wake regardless of meter validity — denominator for
                                           // availability_pct so invalid wakes count as unavailable
    uint32_t window_start_epoch;           // epoch when current window opened
    float    window_start_kwh;             // meter import_kwh at window open;
                                           // window total kWh = last_valid_import_kwh − window_start_kwh
    bool     window_kwh_baseline_set;      // true once window_start_kwh has been anchored to a valid
                                           // meter reading.  Set lazily on the first successful poll
                                           // after window open (and after each summary reset) so a
                                           // failed first-poll cannot leave window_start_kwh at 0
                                           // and inflate total_kwh by the meter's lifetime energy.
    float    last_valid_import_kwh;        // most recent successfully-read import_kwh; used as the
                                           // closing meter reading when computing window total_kwh
                                           // so a failed poll at summary time cannot force total_kwh
                                           // to zero or corrupt the next window's kWh baseline

    // ── Mains-absent alert state ────────────────────────────────────────────
    uint32_t last_mains_epoch;             // epoch of last wake with V_rms ≥ voltage_present_v
    bool     offline_alert_sent;           // true while alert is suppressed (mains still absent)

    // ── Environment variable cache ──────────────────────────────────────────
    uint32_t env_last_modified;            // last env.get "time" stamp; avoids
                                           // re-reading unchanged vars every wake

    // ── Runtime config (mirrors env vars; persisted across reboots) ─────────
    uint32_t sample_interval_sec;
    uint32_t report_interval_min;
    float    session_threshold_w;          // W; session opens when active power exceeds this
    uint8_t  session_end_count;
    float    voltage_present_v;            // V_rms floor for mains-present classification
    uint32_t alert_offline_min;
    uint8_t  modbus_slave_id;
    uint32_t modbus_baud;
};

// ── Globals defined in ev_charger_session_monitor.ino ────────────────────────
extern Notecard notecard;
extern State    state;

// ── Helper-function declarations ──────────────────────────────────────────────
void     initModbus();
bool     initNotecard(const char *product_uid);
bool     defineTemplates();
bool     applyHubCadence();
void     fetchEnvOverrides();
bool     pollMeter(MeterReading *out);
uint32_t getEpoch();
void     runSessionStateMachine(const MeterReading &meter, uint32_t now);
bool     emitSessionNote(float kwh, float peak_w,
                         uint32_t start_epoch, uint32_t end_epoch);
bool     emitSummaryNote(uint32_t now);
bool     emitOfflineAlert(uint32_t now);
void     sleepHost();
uint32_t clampU32(uint32_t v, uint32_t lo, uint32_t hi);
float    clampF(float v, float lo, float hi);
