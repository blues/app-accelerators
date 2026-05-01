/***************************************************************************
  diesel_gen_monitor_helpers.h

  Helper declarations for the diesel_gen_monitor sketch.
  Modbus polling, environment-variable overrides, statistics accumulation,
  and rule evaluation live here to keep the main .ino file readable.

  THIS FILE IS SPECIFIC TO THIS PROJECT AND IS NOT A GENERAL-PURPOSE LIBRARY.
***************************************************************************/

#pragma once

#include <Arduino.h>
#include <climits>         // INT16_MIN — correct sentinel for signed coolant-peak tracking
#include <Notecard.h>
#include <ArduinoRS485.h>
#include <ArduinoModbus.h>

// -------- Shared data types -------------------------------------------------------------

struct GenSample {
    bool     valid;
    uint16_t engine_rpm;
    float    fuel_pct;
    float    load_pct;
    float    oil_kpa;
    int16_t  coolant_c;
    uint16_t run_hours;    // 16-bit demo register; see §9 for 32-bit two-register production path
    uint16_t alarm_word;
};

// Rolling stats accumulated between summary emissions. Run-state and stopped-state
// samples are tracked separately: load and oil pressure are only meaningful while
// the engine is spinning; mixing idle readings into their means distorts the signal.
struct RollingStats {
    uint32_t sample_minutes_active;  // polling cadence that produced the samples in this window;
                                     // stored here so a mid-boundary sample_minutes change does
                                     // not corrupt run_min / stop_min in the outgoing summary
    uint32_t run_samples;
    uint32_t stop_samples;
    uint16_t samples_failed;         // Modbus polls that returned no valid data this window;
                                     // paired with run_samples+stop_samples for coverage reporting
    float    fuel_sum;
    float    load_sum;     float load_peak;
    float    oil_sum;      float oil_peak;
    int32_t  coolant_sum;  int16_t coolant_peak;
    uint16_t run_hours_last;
    uint8_t  engine_starts;     // rising-edge start-event count this window
    uint16_t last_alarm_word;   // latest alarm-word polled this window; updated on every valid sample so a sustained fault stays non-zero across report boundaries
    void reset() {
        memset(this, 0, sizeof(*this));
        // memset zeros coolant_peak, but 0°C is a valid (even typical) temperature.
        // INT16_MIN as sentinel ensures the first real sample, however negative,
        // always wins the peak comparison in accumulate().
        coolant_peak = INT16_MIN;
    }
};

// -------- Shared globals (defined in .ino, used by helpers) ----------------------------
extern Notecard notecard;
extern RollingStats stats;

extern uint32_t g_sample_minutes;           // active cadence; applied at window boundaries (see fix [2])
extern uint32_t g_pending_sample_minutes;   // value from Notehub; promoted to g_sample_minutes at next window close
extern uint32_t g_report_minutes;
extern const char *g_product_uid;          // PRODUCT_UID as a runtime string for helpers.cpp
extern uint8_t  g_modbus_slave_id;
extern uint32_t g_modbus_baud;
extern char     g_modbus_parity[8];
extern uint8_t  g_modbus_stop_bits;
extern uint16_t g_reg_engine_rpm;
extern uint16_t g_reg_fuel_pct;
extern uint16_t g_reg_load_pct;
extern uint16_t g_reg_oil_kpa;
extern uint16_t g_reg_coolant_c;
extern uint16_t g_reg_run_hours;
extern uint16_t g_reg_alarm_word;
extern float    g_fuel_low_pct;
extern float    g_coolant_alarm_c;
extern float    g_oil_low_kpa;
extern uint16_t g_alarm_mask_fts;
extern uint16_t g_rpm_running;

extern uint16_t g_current_alarm_word;
extern bool     g_engine_was_running;
extern bool     g_active_fuel_low;
extern bool     g_active_coolant_overtemp;
extern bool     g_active_oil_low;
extern bool     g_active_controller_alarm;
extern bool     g_active_fts;
extern GenSample g_last_known_sample;
extern uint32_t g_last_modbus_baud;
extern uint16_t g_last_serial_cfg;
extern uint32_t g_last_hubset_outbound;
extern bool     g_hub_provisioned;     // true once a full hub.set (product+mode) has been confirmed
extern bool     g_modbus_ready;        // true after ModbusRTUClient.begin() succeeds; used to
                                       // retry bring-up on the sample cadence without waiting
                                       // for a serial-config change to retrigger applyModbusSerialIfChanged()

// -------- Helper function declarations -------------------------------------------------

// Environment-variable overrides
void fetchEnvOverrides();

// Modbus bus management
void applyModbusSerialIfChanged();
void applyHubSetIfChanged();

// Modbus polling
bool pollGenerator(GenSample &out);

// Statistics and rules
void accumulate(const GenSample &s);
void evaluateRules();

// Note emission (defined in .ino, called from helpers)
//
// sendEvent — emits gen_event.qo with sync:true and retries up to 3×.
//   alert            — event type name (e.g. "fuel_low", "controller_alarm").
//   s                — sample to embed as machine-state context; nullptr → zero fields.
//   trigger_val      — for report-window alerts: the window aggregate or peak that
//                      fired the rule (e.g. mean fuel %, peak coolant °C, mean oil
//                      kPa). Pass the default -1.0f for per-poll alerts where the
//                      sample fields already explain the trigger.
//   trigger_threshold — matching configured limit (e.g. g_fuel_low_pct). -1.0f for
//                      per-poll alerts.
bool sendEvent(const char *alert, const GenSample *s,
               float trigger_val = -1.0f, float trigger_threshold = -1.0f);

// Returns true when the note.add was confirmed by the Notecard (stats reset,
// cadence promoted). Returns false on allocation failure or a failed note.add,
// leaving stats intact so the window data is not silently dropped.
bool sendSummary();
