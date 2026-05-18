// solar_battery_dispatcher.ino
//
// Demand-Response Solar + Battery Dispatcher
// Target hardware: Arduino OPTA RS485 + Blues Wireless for OPTA
// Notecard:        Cell+WiFi (NOTE-WBNAW), I²C via the expansion's AUX connector
//
// Reads a solar inverter and battery BMS over Modbus RTU. Polls for inbound
// dispatch commands (peak-discharge, overnight-charge, DR curtailment) from
// Notehub and controls four OPTA relay outputs accordingly. Sends periodic
// telemetry and immediate mode-change events to Notehub.
//
// Dependencies:
//   - Blues Wireless Notecard (note-arduino) -- install via Library Manager
//   - ArduinoModbus, ArduinoRS485              -- install via Library Manager
//   - Arduino Mbed OS Opta Boards (core)       -- install via Boards Manager
//
// Implementation is split across four files in this sketch folder:
//   solar_battery_dispatcher.ino  -- setup, loop, global definitions (this file)
//   dispatcher.h                  -- shared types, externs, function prototypes
//   notecard_helpers.cpp          -- Notecard config, env-var fetch, dispatch polling
//   modbus_helpers.cpp            -- Modbus bus init and device polling
//   mode_helpers.cpp              -- mode resolution, relay control, note emission

#include "dispatcher.h"

// -------- User configuration ------------------------------------------------
// Replace with the ProductUID from your Notehub project. See:
//   https://dev.blues.io/notehub/notehub-walkthrough/#finding-a-productuid
#ifndef PRODUCT_UID
#define PRODUCT_UID "com.your-company.your-name:solar_battery_dispatcher"
#pragma message "PRODUCT_UID not set. Claim one in Notehub, then define it here."
#endif

#define DEBUG_BAUD 115200

// -------- Notecard instance -------------------------------------------------
Notecard notecard;

// -------- Tunable configuration (overridable via Notehub env vars) ----------
// Defaults are safe for commissioning. Enable TOU windows only after wiring,
// Modbus addressing, and SOC threshold verification are complete (see README).
uint32_t g_sample_minutes    = 1;      // Modbus poll cadence (min); DR-safe max is 5
uint32_t g_report_minutes    = 15;     // telemetry note cadence (min)
float    g_soc_min_pct       = 20.0f;  // discharge blocked below this SOC
float    g_soc_max_pct       = 95.0f;  // charge blocked above this SOC
float    g_soc_hyst_pct      = 5.0f;   // hysteresis above soc_min_pct; discharge
                                        //   resumes only after SOC recovers to
                                        //   soc_min_pct + soc_hyst_pct
float    g_soc_max_hyst_pct  = 3.0f;   // hysteresis below soc_max_pct; charge
                                        //   re-enables only after SOC drops to
                                        //   soc_max_pct − soc_max_hyst_pct
uint8_t  g_peak_start_utc    = 0;      // TOU windows DISABLED by default
uint8_t  g_peak_end_utc      = 0;      //   (start == end → off). Enable by setting
uint8_t  g_charge_start_utc  = 0;      //   non-equal values via env vars after
uint8_t  g_charge_end_utc    = 0;      //   wiring and SOC verification are complete.
uint8_t  g_modbus_slave_inv  = 1;      // Modbus slave ID: inverter
uint8_t  g_modbus_slave_bms  = 2;      // Modbus slave ID: battery BMS
uint32_t g_modbus_baud       = 9600;
uint8_t  g_modbus_parity     = 0;      // 0=none(N), 1=even(E), 2=odd(O)
uint8_t  g_modbus_stop_bits  = 1;      // 1 or 2
uint16_t g_reg_inv_base      = 100;    // inverter holding-register block start
uint16_t g_reg_bms_base      = 200;    // BMS holding-register block start

// -------- Runtime state -----------------------------------------------------
// g_commanded_mode is set by checkDispatch() and consumed by resolveMode().
// g_dr_expires_epoch == 0 means the command persists until superseded.
// g_active_mode is the resolved mode applied on the last sample cycle.
DispatchMode   g_commanded_mode   = MODE_NORMAL;
DispatchMode   g_active_mode      = MODE_NORMAL;
uint32_t       g_dr_expires_epoch = 0;
InverterSample g_inv              = {};
BmsSample      g_bms              = {};

// -------- File-private state ------------------------------------------------
static DispatchMode s_prev_mode     = MODE_NORMAL;
static uint32_t     last_sample_ms  = 0;
static uint32_t     last_report_ms  = 0;

// -------- Arduino entry points ----------------------------------------------
void setup() {
    usbSerial.begin(DEBUG_BAUD);
    const uint32_t serial_wait_ms = 3000;
    for (uint32_t t0 = millis(); !usbSerial && millis() - t0 < serial_wait_ms; ) {}

    // Relay outputs default to de-energised — safe state while the Notecard
    // boots and the first Modbus poll has not yet occurred.
    pinMode(RELAY_GRID_EXPORT,    OUTPUT); digitalWrite(RELAY_GRID_EXPORT,    LOW);
    pinMode(RELAY_BATT_DISCHARGE, OUTPUT); digitalWrite(RELAY_BATT_DISCHARGE, LOW);
    pinMode(RELAY_BATT_CHARGE,    OUTPUT); digitalWrite(RELAY_BATT_CHARGE,    LOW);
    pinMode(RELAY_DR_INDICATOR,   OUTPUT); digitalWrite(RELAY_DR_INDICATOR,   LOW);

    // Set the debug stream before begin() so any output during I²C initialization
    // is captured — matches the Wireless for OPTA Quickstart pattern.
    notecard.setDebugOutputStream(usbSerial);
    notecard.begin();

    // hub.set is the very first Notecard request; templates are defined before
    // any note.add; env vars are fetched and hub.set re-issued immediately if
    // report_minutes was overridden, so the Notecard's cadence matches the
    // runtime config from the very first sync session.
    notecardConfigure(PRODUCT_UID);
    defineTemplates();
    fetchEnvOverrides();
    applyHubSetIfChanged(PRODUCT_UID);   // applies any report_minutes override at boot
    applyModbusIfChanged();

    last_sample_ms = millis();
    last_report_ms = millis();
}

void loop() {
    const uint32_t now = millis();

    if (now - last_sample_ms >= (uint32_t)g_sample_minutes * 60UL * 1000UL) {
        applyModbusIfChanged();   // retry bus init on every sample cycle if begin failed
        pollInverter(g_inv);
        pollBms(g_bms);
        checkDispatch();

        // When the BMS is unreachable, pass 0.0f as the sentinel SOC so
        // resolveMode() treats the state as low-SOC and disables discharge
        // (fail-safe). applyRelays() receives bms_valid separately so it can
        // also inhibit charging — both guards fail safe symmetrically on BMS
        // comm loss (see mode_helpers.cpp: applyRelays()).
        const uint32_t epoch         = currentUtcEpoch();
        const float    soc_for_guard = g_bms.valid ? g_bms.soc_pct : 0.0f;
        DispatchMode   new_mode      = resolveMode(epoch, g_bms.valid, soc_for_guard);
        applyRelays(new_mode, soc_for_guard, g_bms.valid);

        // Gate on the wire-visible name, not the raw enum. MODE_NORMAL and
        // MODE_FORCED_NORMAL both serialize to "normal" via modeName(), so a
        // cloud mode:"normal" command arriving while already in normal state
        // (or expiring back to normal) must not emit a dr_event.qo with
        // prev_mode:"normal" / new_mode:"normal". s_prev_mode is updated
        // unconditionally so the next comparison always reflects the current
        // resolved state.
        if (strcmp(modeName(new_mode), modeName(s_prev_mode)) != 0) {
            sendModeEvent(new_mode, s_prev_mode);
        }
        s_prev_mode = new_mode;
        g_active_mode  = new_mode;
        last_sample_ms = now;
    }

    if (now - last_report_ms >= (uint32_t)g_report_minutes * 60UL * 1000UL) {
        fetchEnvOverrides();
        applyModbusIfChanged();
        applyHubSetIfChanged(PRODUCT_UID);   // re-issues hub.set if report_minutes changed
        sendTelemetry();
        last_report_ms = now;
    }

    delay(25);   // yield CPU between boundary checks; not sleep-critical on line power
}
