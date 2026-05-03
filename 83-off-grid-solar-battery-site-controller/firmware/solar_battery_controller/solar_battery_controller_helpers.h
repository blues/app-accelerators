/***************************************************************************
  solar_battery_controller_helpers - VE.Direct Parser for Off-Grid Solar
  Battery Site Controller

  Encapsulates VE.Direct UART frame parsing for the
  solar_battery_controller project. Handles both Victron SmartShunt
  (battery-side metrics) and SmartSolar MPPT (solar-side metrics).
***************************************************************************/

#pragma once
#include <Arduino.h>

// Maximum number of label/value pairs in one VE.Direct frame.
// SmartShunt emits ~20 fields per frame; MPPT emits ~15-18.
#define VED_MAX_FIELDS  28
#define VED_FIELD_LEN   20  // longest label is ~8 chars, longest value ~12

// -------------------------------------------------------------------------
// VEDirectData — the parsed result from one VE.Direct device frame.
//
// SmartShunt fields: bat_v, bat_a, bat_w, soc_pct, bat_temp_c, ttg_min
// SmartSolar MPPT fields: pv_v, pv_w, yield_kwh, cs, err
//
// Fields not present in the connected device will retain their sentinel
// defaults (see readVEDirectFrame for defaults).
// -------------------------------------------------------------------------
struct VEDirectData {
    bool valid;         // true = a complete Checksum-terminated frame received

    // --- SmartShunt / battery-side ---
    float   bat_v;       // Battery voltage, V (e.g. 25.6)
    float   bat_a;       // Battery current, A (positive=charging, negative=discharging)
    float   bat_w;       // Battery power, W (positive=charging, negative=discharging)
    float   soc_pct;     // State of charge, % (0.0 – 100.0)
    float   bat_temp_c;  // Battery temperature, °C. Sentinel -99.0 = no sensor.
    int32_t ttg_min;     // Time to go, minutes. -1 = not discharging / N/A.

    // --- SmartSolar MPPT / solar-side ---
    float   pv_v;        // Panel voltage, V
    float   pv_w;        // Panel power, W (instantaneous)
    float   yield_kwh;   // Daily solar yield, kWh (from H20 register)
    int16_t cs;          // Charge state (see VED_CS_* constants below; int16 to
                         // hold vendor codes 245-252 without truncation)
    int8_t  err;         // Error code (0 = no error)
};

// Charge-state constants for the SmartSolar MPPT CS field.
// Source: Victron VE.Direct Protocol specification §3 (rev 3.34+).
// States 245–252 exceed the range of int8_t — the field is typed int16_t
// to hold them correctly.
//
// Full-charge states — any of these means the battery completed a charge
// cycle this window (used by the harvest_deficit logic):
//   VED_CS_FLOAT, VED_CS_ABSORPTION — standard bulk→absorption→float cycle
//   VED_CS_EQUALIZE     — manual equalize, performed post-float
//   VED_CS_AUTO_EQUALIZE — automatic equalize/recondition, also post-float
//
// All other states (Off, Fault, Bulk, Starting up, External Control) mean
// the battery did NOT reach a full charge during this window.
#define VED_CS_OFF              0    // Not charging / powered off
#define VED_CS_FAULT            2    // Fault (see ERR field)
#define VED_CS_BULK             3    // Bulk charging
#define VED_CS_ABSORPTION       4    // Absorption stage
#define VED_CS_FLOAT            5    // Float (maintenance)
#define VED_CS_EQUALIZE         7    // Equalize (manual) — post-float
#define VED_CS_STARTING_UP    245    // Briefly visible during boot
#define VED_CS_AUTO_EQUALIZE  247    // Auto equalize / Recondition — post-float
#define VED_CS_EXTERNAL       252    // Charger under external (BMS) control

// -------------------------------------------------------------------------
// readVEDirectFrame()
//
// Listens on `serial` for up to `timeout_ms` milliseconds and parses the
// first complete VE.Direct frame received. A "complete frame" is a sequence
// of tab-separated label/value lines ending with a "Checksum" line.
//
// Both SmartShunt and SmartSolar MPPT broadcast one frame per second; a
// 3-second timeout is sufficient under normal conditions.
//
// VE.Direct TX is 5 V logic. The STM32L433 host is 3.3 V tolerant.
// Use a 10 kΩ / 20 kΩ resistor voltage divider on the RX line:
//   Device TX → 10 kΩ → MCU RX pin
//                      ↕
//                    20 kΩ
//                      ↕
//                     GND
//
// Returns true if a valid frame was parsed; false on timeout.
// -------------------------------------------------------------------------
bool readVEDirectFrame(Stream &serial, VEDirectData &out, uint32_t timeout_ms = 3000);
