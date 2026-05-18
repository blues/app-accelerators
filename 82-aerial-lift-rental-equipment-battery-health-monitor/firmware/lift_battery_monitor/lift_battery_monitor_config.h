/***************************************************************************
  lift_battery_monitor_config.h — build-configuration toggles for the
  Aerial Lift Battery Health Monitor.

  *** THIS IS THE ONE FILE TO EDIT when changing build options. ***

  Both lift_battery_monitor.ino and lift_battery_monitor_helpers.cpp include
  this header (the .cpp via lift_battery_monitor_helpers.h), so changes here
  propagate to every translation unit in the build automatically.  Do NOT
  redefine these macros in the .ino or the .cpp — doing so will produce
  conflicting definitions and may silently produce a mismatched build.

  THIS FILE SHOULD BE EDITED AFTER GENERATION.
  IT IS PROVIDED AS A STARTING POINT FOR THE USER TO EDIT AND EXTEND.
***************************************************************************/
#pragma once

// ─── CAN BMS toggle ───────────────────────────────────────────────────────────
// Set to 1 if a compact MCP2515 SPI CAN module is installed (see README §3 and
// §4 for the breakout + TXB0104 level-shifter wiring).
// 0 = CAN feature disabled (default); 1 = CAN feature compiled in.
#define ENABLE_CAN_BMS  0

// ─── Current-sensor toggle ────────────────────────────────────────────────────
// 0 (default) = primary field build: INA228 readCurrent() through an external
//   precision shunt wired to VIN+/VIN− per README §4.  Update
//   DEFAULT_SHUNT_MOHM and DEFAULT_SHUNT_MAX_A in lift_battery_monitor.ino to
//   match the installed shunt before building.  BENCH_ONLY must be 0.
// 1 = alternative field build: ACS758LCB-200B-PFF-T Hall-effect sensor on A1
//   per README §4.  Use when breaking and re-terminating the traction conductor
//   for an inline shunt is impractical and an isolated Hall-effect sensor is
//   preferred instead.  Handles traction currents up to 200 A.  BENCH_ONLY
//   must be 0.
#define ENABLE_ACS758   0

// ─── Bench-mode opt-in ───────────────────────────────────────────────────────
// Set to 1 ONLY when ENABLE_ACS758 is also 0 AND the INA228 onboard 15 mΩ
// shunt is intentionally used for bench / prototype testing at ≤ 10 A.
// Setting BENCH_ONLY 1 suppresses the compile-time ACS758/shunt contradiction
// check and emits a boot-time serial warning.  Do NOT set BENCH_ONLY 1 for
// field builds that use an external precision shunt — only the onboard shunt
// is bench-only; an external field shunt with BENCH_ONLY 0 is a valid and safe
// production configuration.  Setting BENCH_ONLY 1 while ENABLE_ACS758 is also
// 1 triggers a compile-time #error (contradictory flags).
#define BENCH_ONLY  0

// ─── CAN BMS hardware parameters (edit to match your BMS vendor) ─────────────
// These constants are referenced from both lift_battery_monitor.ino (for the
// MCP2515 instance and gCellMv[] array sizing) and lift_battery_monitor_helpers.cpp
// (inside pollCanBms / parseCellGroupFrame).  They live here so both
// translation units see identical values.
#if ENABLE_CAN_BMS
#define PIN_CAN_CS          5            // SPI CS for MCP2515 — Notecarrier CX D5 (see README §4)
#define BMS_CELL_COUNT      8            // array capacity for decoded cell-group voltages;
                                         // a single classic CAN frame (DLC ≤ 8 bytes) holds
                                         // at most four 16-bit values — BMS_CELL_COUNT may
                                         // exceed that if the BMS uses a multi-frame protocol
#define BMS_CELL_GROUP_ID  0x18FF50E5UL  // 29-bit extended CAN ID (placeholder)
#endif
