/*
 * app_state.h — Shared types, constants, and user configuration for
 *               the Ambient Home Well-Being & Activity Hub.
 *
 * Keep this header free of SDK-specific types so it can be included
 * by every translation unit without pulling in Notecard.h or sensor
 * driver headers.
 */
#pragma once

#include <stdint.h>

// ---------------------------------------------------------------------------
// User configuration — set your Notehub ProductUID here
// ---------------------------------------------------------------------------
#ifndef PRODUCT_UID
#define PRODUCT_UID ""  // "com.your-company.your-name:activity-hub"
#pragma message "PRODUCT_UID is not defined. Set it before flashing."
#endif

// Uncomment to enable USB Serial and Notecard debug output.
// Leave commented in production builds — debug serial adds per-wake latency
// and Notecard debug streaming produces verbose output that is only useful
// during bring-up.
// #define DEBUG_SERIAL

// Uncomment to print the raw bed peak-to-peak ADC amplitude to Serial.
// CALIBRATION_MODE has no effect unless DEBUG_SERIAL is also uncommented —
// both defines must be active to see any serial output.
// #define CALIBRATION_MODE

// ---------------------------------------------------------------------------
// Pin assignments
// ---------------------------------------------------------------------------
#define PIN_PIR   D6   // Adafruit 4871 — digital HIGH on motion
#define PIN_DOOR  D9   // Adafruit 375  — reed switch, HIGH = door open (INPUT_PULLUP)
#define PIN_BED   A0   // SparkFun SEN-09197 piezo via 1 MΩ/10 kΩ divider + clamp

// ---------------------------------------------------------------------------
// Timing defaults (overridable via Notehub environment variables)
// ---------------------------------------------------------------------------
#define DEFAULT_SAMPLE_INTERVAL_SEC       300   // 5 min between wakes
#define DEFAULT_SUMMARY_INTERVAL_MIN       60   // hourly summaries
#define ALERT_COOLDOWN_SEC               1800   // 30 min before re-alerting same type

// Activity-pattern defaults
#define DEFAULT_MORNING_START_HOUR          6   // local hour — start of morning window
#define DEFAULT_MORNING_END_HOUR            9   // local hour — alert if nothing detected by here
#define DEFAULT_SLEEP_START_HOUR           22   // local hour — sleep window start (10 pm)
#define DEFAULT_SLEEP_END_HOUR              6   // local hour — sleep window end (6 am)
#define DEFAULT_NIGHT_BATHROOM_LIMIT        3   // alert when nighttime door trips exceed this
#define DEFAULT_BED_THRESHOLD              50   // ADC peak-to-peak counts (12-bit, 0–4095)
#define DEFAULT_HUMIDITY_BASELINE_ALPHA  0.02f  // EWMA weight for humidity baseline update
#define DEFAULT_UTC_OFFSET_HOURS            0   // hours ahead of UTC for local time-of-day
#define DEFAULT_QUIET_MINUTES_FOR_ALERT    20   // min of consecutive bed-quiet before alert

// Alert index constants matching AppState::last_alert_time[]
#define ALERT_IDX_NO_MORNING  0
#define ALERT_IDX_NIGHT_BATH  1
#define ALERT_IDX_BED_EMPTY   2

// ---------------------------------------------------------------------------
// Application state — retained in SRAM across Stop mode wakes; reset on
// power-on. A static instance of this struct lives in activity_hub.ino.
//
// Changing any field (type, order, or count) alters the struct layout.
// Because state lives in SRAM, a layout change takes effect automatically
// on the next power-on reset with no migration needed.
// ---------------------------------------------------------------------------
struct AppState {
    uint32_t summary_window_start;    // UNIX epoch when this summary window began
    uint16_t pir_count;               // PIR events accumulated this window
    uint16_t door_count;              // door-open transitions this window
    uint16_t bed_samples;             // total bed reads this window
    uint16_t bed_motion_samples;      // reads where bed vibration was detected
    // Consecutive quiet samples recorded ONLY while inside the sleep window.
    uint8_t  sleep_quiet_samples;
    float    humidity_baseline;       // slow EWMA of ambient bathroom humidity
    float    humidity_last;           // most recent valid humidity reading (%), or -9999.0
    float    temp_last;               // most recent valid temperature reading (°C), or -9999.0
    uint8_t  night_bathroom_count;    // door trips during sleep window

    // Two separate day-boundary guards (both stored as (local_epoch/86400) % 256).
    // Keeping them separate lets sleep_end_hour != morning_start_hour work
    // correctly: the morning reset fires on the first wake at or after
    // morning_start_hour each day, and the sleep-window reset fires on the
    // first wake at or after sleep_end_hour each day.
    uint8_t  last_reset_day;          // last local day the morning-state reset fired
    uint8_t  last_night_reset_day;    // last local day the sleep-end-state reset fired

    bool     morning_activity;        // any activity in today's morning window
    bool     morning_alerted;         // once-per-day latch: no_morning_activity already fired
    bool     last_door_state;         // last sampled door state (true = open)
    bool     time_initialized;        // true once Notecard has delivered valid UTC time; guards first-sync false-alert
    uint32_t last_alert_time[3];      // per-alert cooldown timestamps

    // Idempotent-config flags. Set true only after Notecard confirms success;
    // retried on every cycle until confirmed. hub_configured is paired with
    // hub_product_hash: if PRODUCT_UID changes between flashes, hub_configured
    // is treated as false and hub.set is reissued with the new ProductUID.
    bool     hub_configured;
    bool     templates_defined;
    bool     motion_stopped;

    // Edge-trigger latches for Rules B and C.
    bool     night_bath_alerted;      // Rule B latch — rearms at sleep-end reset
    bool     bed_empty_alerted;       // Rule C latch — rearms when bed motion detected

    // Pending-alert flags for Rules B and C. Set when the trigger condition is
    // first met but sendAlert() fails; cleared only after a successful send.
    // These survive the sleep-end counter reset so a transient I2C or network
    // failure on the last in-window cycle does not silently discard the alert —
    // it is retried on the next cycle before conditions are lost.
    bool     night_bath_pending;      // Rule B: condition met, send failed — retry next cycle
    bool     bed_empty_pending;       // Rule C: condition met, send failed — retry next cycle

    // Outbound cadence last confirmed applied via hub.set. Persisted in SRAM
    // across Stop mode wakes; resets to DEFAULT_SUMMARY_INTERVAL_MIN on power-on
    // (the first runCycle() then re-applies it to the Notecard if needed).
    uint16_t applied_summary_interval_min;

    // djb2 hash of the PRODUCT_UID compiled into this firmware image.
    // Compared against productUidHash() on every wake; a mismatch forces
    // hub.set to re-run even if hub_configured is true. This ensures that
    // a reflash with a corrected or newly-set ProductUID is always applied.
    uint32_t hub_product_hash;
};
