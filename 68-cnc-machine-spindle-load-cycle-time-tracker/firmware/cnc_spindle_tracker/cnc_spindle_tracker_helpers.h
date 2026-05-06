// cnc_spindle_tracker_helpers.h
// Shared types, compile-time defaults, extern declarations, and helper-function
// prototypes for the CNC Machine Spindle Load & Cycle Time Tracker.
//
// Include this file in both the .ino and the companion .cpp so all translation
// units share the same struct layouts, constants, and extern declarations for
// the globals defined in cnc_spindle_tracker.ino.
//
// Hardware: Arduino OPTA RS485 + Blues Wireless for OPTA
// Blues docs: https://dev.blues.io

#pragma once

#include <Notecard.h>
#include <ArduinoModbus.h>
#include <Ethernet.h>

// Serial alias — defined once here so both the .ino and the .cpp use the same port.
#define usbSerial Serial

// ---------------------------------------------------------------------------
// Compile-time defaults — all overridable via Notehub environment variables.
// ---------------------------------------------------------------------------
#define DEFAULT_SAMPLE_MINUTES       1
#define DEFAULT_REPORT_MINUTES       60
#define DEFAULT_MODBUS_PORT          502
#define DEFAULT_MODBUS_UNIT_ID       1
// Base holding-register address (0-based wire-level) for the contiguous
// six-register block: load, feed, alarm, state, count, operator_id.
// Override via the `reg_spindle_load` Notehub env var.
#define DEFAULT_REG_SPINDLE_LOAD     256
#define DEFAULT_SPINDLE_OVERLOAD_PCT 90.0f   // spindle load % alert threshold
#define DEFAULT_EXPECTED_CYCLE_SEC   120     // ideal cycle time (OEE dashboard)
// 30-minute de-dup window prevents repeat pages on a sustained spindle overload.
#define SPINDLE_ALERT_COOLDOWN_MS    (30UL * 60UL * 1000UL)
// Depth of the alarm-event ring buffer used to survive transient I²C outages.
// Sized for the worst realistic burst of distinct alarm-code transitions;
// evicts the oldest slot on overflow (logged to Serial).
#define ALARM_FIFO_SIZE              8

// ---------------------------------------------------------------------------
// Shared data structures
// ---------------------------------------------------------------------------
struct Config {
    uint32_t sampleMs;           // milliseconds between Modbus polls
    uint32_t reportMs;           // milliseconds between summary Notes
    uint16_t modbusPort;
    uint8_t  modbusUnitId;
    uint16_t regSpindleLoad;     // base address of contiguous six-register block
    float    spindleOverloadPct;
    uint32_t expectedCycleSec;   // reserved; not transmitted in any Note
};

struct Sample {
    float    spindleLoadPct;
    // Feed-rate override percentage (0–150 % of the programmed feed rate) as
    // exposed by the CNC controller's Modbus holding register.  This is NOT
    // the actual feed rate in engineering units (mm/min or in/min); see the
    // Limitations section of the README for why engineering-unit feed rate is
    // not collected in this reference design.
    float    feedOverridePct;
    uint16_t alarmCode;
    uint8_t  cycleState;         // 0=idle, 1=running, 2=hold, 3=alarm
    uint16_t cycleCount;
    uint16_t operatorId;
    bool     valid;
};

// PendingAlarm: one slot in the alarm-retry FIFO. Stores the alert-type label
// (matching the 32-character string template width for cnc_alarm.qo) alongside
// the raw sample so the full Note payload can be reconstructed on retry.
struct PendingAlarm {
    char   alertType[32];   // matches cnc_alarm.qo 32-character string template field
    Sample sample;
};

struct WindowStats {
    float    spindleSum;         // sum of spindle-load samples while running
    float    spindlePeak;
    float    feedOverrideSum;    // sum of feed-rate override samples while running (0–150 %)
    uint32_t validSamples;       // total successful Modbus reads in this window
    uint32_t runSamples;         // samples taken while cycle state == 1
    uint32_t runMinutes;
    uint32_t idleMinutes;
    uint32_t windowCycleCountDelta; // per-window sum of cycleCount register deltas (primary count)
    uint32_t cyclesCompleted;    // edge-transition count — heuristic used only for avg_cycle_sec
    uint32_t totalCycleMs;       // cumulative in-cycle time (for avg_cycle_sec heuristic)
    uint16_t operatorId;         // most recently observed operator ID (snapshot for hourly summary)
    uint16_t alarmCount;         // CNC alarm-code transitions observed in window
};

// ---------------------------------------------------------------------------
// Globals defined in cnc_spindle_tracker.ino and accessed by the helper .cpp.
// Objects used exclusively inside the .cpp (Modbus client, CNC server IP) are
// kept static there and are not declared here.
// ---------------------------------------------------------------------------
extern Notecard    notecard;
extern Config      cfg;
extern WindowStats g_window;
extern uint8_t     g_lastCycleState;
extern uint32_t    g_lastCycleStartMs;
extern bool        g_modbusConnected;
extern uint32_t    g_envLastModTime;

// ---------------------------------------------------------------------------
// Helper function prototypes.
// productUid is passed explicitly so the helper .cpp does not depend on the
// PRODUCT_UID macro that lives in the user-edited .ino.
// ---------------------------------------------------------------------------
void notecardConfigure(const char *productUid);
void defineTemplates(void);
void fetchEnvOverrides(void);
bool modbusConnect(void);
bool pollCnc(Sample &s);
void sendSummary(void);
bool sendAlarm(const char *alertType, const Sample &s);
void sendOperatorChange(uint16_t prevId, uint16_t newId);
void resetWindow(void);
