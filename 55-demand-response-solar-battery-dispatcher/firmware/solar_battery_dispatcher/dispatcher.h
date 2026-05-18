// dispatcher.h
//
// Shared types, relay-output pin assignments, extern declarations for
// module-global variables, and function prototypes used across all
// compilation units in the solar_battery_dispatcher sketch.
//
// Implementation is split across four files in this sketch folder:
//   solar_battery_dispatcher.ino  -- setup, loop, global definitions
//   dispatcher.h                  -- this file
//   notecard_helpers.cpp          -- Notecard config, env-var fetch, dispatch polling
//   modbus_helpers.cpp            -- Modbus bus init and device polling
//   mode_helpers.cpp              -- mode resolution, relay control, note emission

#pragma once
#include <Arduino.h>
#include <Notecard.h>
#include <ArduinoRS485.h>
#include <ArduinoModbus.h>
#include <string.h>    // strstr
#include <strings.h>   // strcasecmp
#include <math.h>      // isnan, isinf

#define usbSerial Serial

// -------- Operating modes ---------------------------------------------------
//  NORMAL          PV charges battery + covers load; surplus exports.
//  PEAK_DISCHARGE  Battery discharges to cover load during the TOU peak window.
//  OVERNIGHT_CHARGE Off-peak: grid charges battery to soc_max_pct.
//  DR_CURTAIL      Utility DR event: curtail grid export; DR indicator latches.
//  LOW_SOC_PROTECT Discharge blocked (SOC below soc_min_pct or BMS unreachable).
//  FORCED_NORMAL   Internal-only: cloud-commanded normal. Suppresses TOU schedule
//                  evaluation for any expires_epoch window. On the wire: "normal".
enum DispatchMode {
    MODE_NORMAL           = 0,
    MODE_PEAK_DISCHARGE   = 1,
    MODE_OVERNIGHT_CHARGE = 2,
    MODE_DR_CURTAIL       = 3,
    MODE_LOW_SOC_PROTECT  = 4,
    MODE_FORCED_NORMAL    = 5
};

// Defined in mode_helpers.cpp; used by all modules for logging and note payloads.
const char *modeName(DispatchMode m);

// -------- Sensor sample structs ---------------------------------------------
struct InverterSample {
    bool     valid;
    int32_t  pv_w;       // PV generation (W)
    int32_t  ac_out_w;   // AC output to load (W)
    int32_t  grid_w;     // Grid exchange (W, negative = importing)
    // inv_state: placeholder for future vendor-specific fault logic.
    // Polled from Modbus but not transmitted in this reference design.
    // Add to note.template / note.add body in sendTelemetry() to transmit.
    uint16_t inv_state;  // 0=idle, 1=mppt, 2=fault
};

struct BmsSample {
    bool     valid;
    float    soc_pct;    // State-of-charge (%)
    float    batt_v;     // Battery voltage (V)
    float    batt_a;     // Battery current (A, positive=charging, negative=discharging)
    // bms_state: placeholder for future vendor-specific fault logic. See inv_state.
    uint16_t bms_state;  // 0=idle, 1=charging, 2=discharging, 3=fault
};

// -------- Relay output assignments ------------------------------------------
// D0–D3 are the relay coil output pin names from the Arduino Mbed OS Opta core.
// Relay-status indicator LEDs are slaved to the coil circuits in hardware —
// no separate LED writes are needed.
#define RELAY_GRID_EXPORT    D0   // HIGH = export to grid enabled
#define RELAY_BATT_DISCHARGE D1   // HIGH = battery discharge permitted
#define RELAY_BATT_CHARGE    D2   // HIGH = battery charge permitted
#define RELAY_DR_INDICATOR   D3   // HIGH = DR event active (visual / SCADA indicator)

// -------- Notecard instance (defined in .ino) --------------------------------
extern Notecard notecard;

// -------- Tunable configuration (env-var overridable; defined in .ino) ------
extern uint32_t g_sample_minutes;
extern uint32_t g_report_minutes;
extern float    g_soc_min_pct;
extern float    g_soc_max_pct;
extern float    g_soc_hyst_pct;
extern float    g_soc_max_hyst_pct;
extern uint8_t  g_peak_start_utc;
extern uint8_t  g_peak_end_utc;
extern uint8_t  g_charge_start_utc;
extern uint8_t  g_charge_end_utc;
extern uint8_t  g_modbus_slave_inv;
extern uint8_t  g_modbus_slave_bms;
extern uint32_t g_modbus_baud;
extern uint8_t  g_modbus_parity;
extern uint8_t  g_modbus_stop_bits;
extern uint16_t g_reg_inv_base;
extern uint16_t g_reg_bms_base;

// -------- Runtime state (defined in .ino) ------------------------------------
extern DispatchMode   g_commanded_mode;   // set by checkDispatch, consumed by resolveMode
extern DispatchMode   g_active_mode;      // last mode applied; read by sendTelemetry
extern uint32_t       g_dr_expires_epoch; // 0 = command persists until superseded
extern InverterSample g_inv;              // most recent inverter sample
extern BmsSample      g_bms;             // most recent BMS sample

// -------- Function prototypes -----------------------------------------------
// notecard_helpers.cpp
void    notecardConfigure(const char *product_uid);
void    defineTemplates();
void    fetchEnvOverrides();
void    applyHubSetIfChanged(const char *product_uid);
bool    isQueueEmpty(const char *err);
void    checkDispatch();

// modbus_helpers.cpp
void    applyModbusIfChanged();
bool    pollInverter(InverterSample &out);
bool    pollBms(BmsSample &out);

// mode_helpers.cpp
uint32_t     currentUtcEpoch();
DispatchMode resolveMode(uint32_t utc_epoch, bool bms_valid, float soc_pct);
void         applyRelays(DispatchMode mode, float soc_pct, bool bms_valid);
void         sendTelemetry();
void         sendModeEvent(DispatchMode new_mode, DispatchMode old_mode);
