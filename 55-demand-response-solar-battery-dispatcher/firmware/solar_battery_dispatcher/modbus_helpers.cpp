// modbus_helpers.cpp
//
// Modbus RTU bus initialization and device-polling helpers for the
// solar_battery_dispatcher sketch.
//
// The demo register layout uses contiguous 16-bit holding registers (4 per
// device). Real inverters and BMS units have vendor-specific maps; see the
// README Limitations section. Adjust g_reg_inv_base / g_reg_bms_base via
// Notehub environment variables as a first step, then add vendor-specific
// scaling and register-address changes in code.
//
// Inverter block (base g_reg_inv_base): PV W (uint16), AC-out W (int16),
//   grid W signed (negative = importing), inverter state (uint16).
// BMS block (base g_reg_bms_base): SOC×10 (int16), Vbatt×10 (uint16),
//   Ibatt×10 signed (negative = discharging), BMS state (uint16).

#include "dispatcher.h"

// -------- Module-private Modbus config tracking -----------------------------
// Sentinel values (0 / 255) guarantee a bus init on the very first call to
// applyModbusIfChanged() because they cannot match any valid runtime setting.
static uint32_t s_last_modbus_baud      = 0;
static uint8_t  s_last_modbus_parity    = 255;
static uint8_t  s_last_modbus_stop_bits = 255;

// -------- Modbus initialization ---------------------------------------------
// Maps parity and stop-bit settings onto one of the six SERIAL_8xx constants
// the ArduinoRS485 / Mbed core recognises (8-bit data is always assumed).
static uint32_t modbusSerialConfig() {
    switch (g_modbus_parity) {
        case 1:  return (g_modbus_stop_bits == 2) ? SERIAL_8E2 : SERIAL_8E1;
        case 2:  return (g_modbus_stop_bits == 2) ? SERIAL_8O2 : SERIAL_8O1;
        default: return (g_modbus_stop_bits == 2) ? SERIAL_8N2 : SERIAL_8N1;
    }
}

void applyModbusIfChanged() {
    if (g_modbus_baud      == s_last_modbus_baud   &&
        g_modbus_parity    == s_last_modbus_parity  &&
        g_modbus_stop_bits == s_last_modbus_stop_bits) return;
    ModbusRTUClient.end();
    RS485.setDelays(50, 50);
    if (!ModbusRTUClient.begin(g_modbus_baud, modbusSerialConfig())) {
        usbSerial.println("[modbus] begin failed; will retry on next sample cycle");
    } else {
        s_last_modbus_baud      = g_modbus_baud;
        s_last_modbus_parity    = g_modbus_parity;
        s_last_modbus_stop_bits = g_modbus_stop_bits;
        const char *par = (g_modbus_parity == 1) ? "E" : (g_modbus_parity == 2) ? "O" : "N";
        usbSerial.print("[modbus] bus up at ");
        usbSerial.print(g_modbus_baud);
        usbSerial.print(" 8");
        usbSerial.print(par);
        usbSerial.println(g_modbus_stop_bits);
    }
}

// -------- Device polling ----------------------------------------------------
// Each device allows up to three retries per poll cycle; all three failing
// leaves `valid` false. The -9999 sentinel appears in the telemetry note if
// the poll that immediately preceded the report boundary failed.

bool pollInverter(InverterSample &out) {
    out.valid = false;
    for (uint8_t t = 0; t < 3; t++) {
        if (ModbusRTUClient.requestFrom(g_modbus_slave_inv, HOLDING_REGISTERS,
                                        g_reg_inv_base, 4)) {
            out.pv_w      = (int32_t)(uint16_t)ModbusRTUClient.read();
            out.ac_out_w  = (int32_t)(int16_t) ModbusRTUClient.read();
            out.grid_w    = (int32_t)(int16_t) ModbusRTUClient.read();
            out.inv_state = (uint16_t)          ModbusRTUClient.read();
            out.valid = true;
            return true;
        }
        delay(100);
    }
    usbSerial.println("[modbus] inverter unreachable");
    return false;
}

bool pollBms(BmsSample &out) {
    out.valid = false;
    for (uint8_t t = 0; t < 3; t++) {
        if (ModbusRTUClient.requestFrom(g_modbus_slave_bms, HOLDING_REGISTERS,
                                        g_reg_bms_base, 4)) {
            out.soc_pct   = (float)((int16_t) ModbusRTUClient.read()) / 10.0f;
            out.batt_v    = (float)((uint16_t)ModbusRTUClient.read()) / 10.0f;
            out.batt_a    = (float)((int16_t) ModbusRTUClient.read()) / 10.0f;
            out.bms_state = (uint16_t)         ModbusRTUClient.read();
            out.valid = true;
            return true;
        }
        delay(100);
    }
    usbSerial.println("[modbus] BMS unreachable");
    return false;
}
