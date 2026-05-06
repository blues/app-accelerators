/***************************************************************************
  solar_string_monitor_helpers.h — Sensor, Modbus, and Notecard helpers

  Encapsulates all data-path functions for the solar string monitor.
  This is specific to this project and NOT a general-purpose library.

  THIS FILE SHOULD BE EDITED AFTER GENERATION.
  IT IS PROVIDED AS A STARTING POINT FOR THE USER TO EDIT AND EXTEND.
***************************************************************************/

#pragma once

#include <Notecard.h>
#include <ModbusMaster.h>
#include <DallasTemperature.h>

// ---------------------------------------------------------------------------
// Shared configuration struct — extern'd here, defined in .ino
// ---------------------------------------------------------------------------
struct StringAccum {
    float    v_sum;
    float    a_sum;
    float    w_sum;
    float    exp_w_sum;
    uint16_t n;
};

#define MAX_STRINGS 4

struct AppState {
    uint32_t    sample_count;
    uint32_t    last_alert_sample[MAX_STRINGS];
    bool        alert_active[MAX_STRINGS];
    StringAccum accum[MAX_STRINGS];
    // Window-level environment accumulators.  irr_sum/n_env accumulate on every
    // sample cycle (independent of Modbus or probe state) so the irradiance mean
    // is never biased by bus or sensor failures.  mod_temp_sum/n_temp_valid
    // accumulate only when the DS18B20 returns a valid reading; sendSummary()
    // emits -9999 for mod_temp_c when n_temp_valid == 0 for the window.
    float       irr_sum;
    float       mod_temp_sum;
    uint16_t    n_env;               // irradiance sample cycles in current window
    uint16_t    n_temp_valid;        // valid-temperature sample cycles in current window
    // Persistent rate-limiting and sync-cadence tracking (survive sleep cycles)
    uint32_t    last_err_sample;         // sample_count when last modbus_fail was emitted
    uint32_t    last_temp_fault_sample;  // sample_count when last temp_probe_fault was emitted
    uint32_t    last_hub_outbound;       // outbound interval used in the most recent hub.set
    bool        templates_ok;            // true once both note.template calls are confirmed
};

// Runtime config — defined in .ino, used by helpers
extern uint32_t g_sample_interval_sec;
extern uint32_t g_report_interval_min;
extern uint8_t  g_modbus_slave_id;
extern uint32_t g_modbus_baud;
extern char     g_modbus_parity[8];   // "none" | "even" | "odd"
extern uint8_t  g_modbus_stop_bits;   // 1 or 2
extern uint8_t  g_n_strings;
extern uint16_t g_reg_base;
extern float    g_string_v_scale;
extern float    g_string_a_scale;
extern float    g_string_stc_w;
extern float    g_perf_threshold;
extern float    g_irradiance_min;
extern float    g_temp_coeff;
extern uint32_t g_alert_cooldown_sec;
extern float    g_pyranometer_sensitivity;

extern AppState    g_state;
extern Notecard    notecard;
extern ModbusMaster modbus;
extern DallasTemperature tempSensor;

// ---------------------------------------------------------------------------
// Function declarations
// ---------------------------------------------------------------------------
bool     defineTemplates(void);
void     fetchEnvVars(void);
uint32_t serialConfigFromEnv(void); // translates parity/stop-bits → SERIAL_8xx
float    readIrradiance(void);
float    readModuleTemp(float irr_wm2);
bool     readStrings(float v_out[], float a_out[], uint8_t count, float irr, float mod_temp);
float    computeExpected(float irr_wm2, float mod_temp_c);
void     accumulateWindow(float v[], float a[], float irr, float mod_temp);
void     evaluateAndAlert(float v[], float a[], float irr, float mod_temp);
bool     sendSummary(void);
bool     sendAlert(uint8_t str_id, const char *reason,
                   float pr, float v, float a, float irr, float mod_temp);
uint32_t clampU32(uint32_t v, uint32_t lo, uint32_t hi);
float    clampF(float v, float lo, float hi);
