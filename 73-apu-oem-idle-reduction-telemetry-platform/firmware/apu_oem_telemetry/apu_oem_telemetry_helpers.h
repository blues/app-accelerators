/***************************************************************************
  apu_oem_telemetry_helpers.h - Helper declarations for APU OEM Telemetry Platform

  Pin assignments, Notefile names, state machine constants, Modbus register
  offsets, compiled-in defaults, EnvConfig / AppState struct definitions,
  and function declarations for the APU OEM Idle-Reduction Telemetry Platform.

  See README.md for full wiring, Notehub setup, and validation instructions.
***************************************************************************/
#pragma once

#include <Arduino.h>
#include <Notecard.h>
#include <DallasTemperature.h>

// ---------------------------------------------------------------------------
// Pin assignments (Notecarrier CX header — adjust to match your wiring)
// ---------------------------------------------------------------------------
// RS-485 UART on Cygnet USART2 (PA2=TX, PA3=RX). The STM32duino Cygnet variant
// does not pre-define `Serial2`; instantiate it explicitly in the .cpp.
extern HardwareSerial RS485_SERIAL;
#define RS485_DE_PIN        2         // Direction control → BOB-10124 RTS/DE
#define ONE_WIRE_PIN        3         // OneWire data bus for both DS18B20 probes
#define IGNITION_PIN        A0        // Voltage-divided ignition sense (12/24 V)

// Ignition sense — 12-bit ADC resolution (0–4095), set via analogReadResolution(12)
// in setup().  Voltage divider R_top=220 kΩ, R_bot=33 kΩ → V_adc = V_ign×33/253.
//
//   12 V truck (nominal 12–14.4 V while charging):
//     At 12.0 V: V_adc ≈ 1.565 V → ~1941 counts.
//     At 14.4 V: V_adc ≈ 1.878 V → ~2333 counts.
//
//   24 V truck (nominal 24–28.8 V while charging):
//     At 24.0 V: V_adc ≈ 3.132 V → ~3888 counts — within the 3.3 V ADC rail.
//     At 28.8 V: V_adc would be ≈ 3.758 V without clamping — the 3.3 V Zener
//     clamps the junction to ≤ 3.3 V.  The Zener is mandatory on 24 V trucks;
//     it is a belt-and-suspenders addition on 12 V trucks.
//
//   The same R_top=220 kΩ / R_bot=33 kΩ divider works for both 12 V and 24 V
//   truck types with the thresholds below.  On both truck types, ignition-ON
//   produces an ADC reading well above the ON threshold; ignition-OFF produces
//   a reading near 0.  No resistor change is needed for 24 V trucks.
//
//   Thresholds are set conservatively relative to the 12 V divided level:
//     ON  = 1700 ≈ 10.5 V at ignition line (≈ 1.38 V at ADC pin)
//     OFF = 1400 ≈  8.7 V at ignition line (≈ 1.14 V at ADC pin)
//   On a 24 V truck the ignition-ON ADC count is ≫ 1700 (≥ 3888), so the same
//   threshold reliably detects ignition state on both truck types.
//   The 300-count hysteresis band suppresses chatter during cranking transients.
//   Bench-verify before deployment: measure raw ADC counts at key-on and key-off
//   on your specific truck, then set ON = key_on_counts × 0.85, OFF = ON − 300.
#define IGNITION_THRESHOLD_ON_ADC   1700  // averaged ADC ≥ this → ignition on
#define IGNITION_THRESHOLD_OFF_ADC  1400  // averaged ADC <  this → ignition off

// ---------------------------------------------------------------------------
// Notefiles
// ---------------------------------------------------------------------------
#define NOTEFILE_TELEMETRY  "apu_telemetry.qo"
#define NOTEFILE_EVENT      "apu_event.qo"
#define NOTEFILE_DAILY      "apu_daily.qo"
#define STATE_SEG_ID        "APUS"
#define ENV_SEG_ID          "APUE"   // persisted EnvConfig payload segment

// ---------------------------------------------------------------------------
// State machine
// ---------------------------------------------------------------------------
#define STATE_IDLE_OFF    0
#define STATE_IDLE_ON     1
#define STATE_APU_ACTIVE  2

// ---------------------------------------------------------------------------
// Modbus register offsets from configurable base address
// ---------------------------------------------------------------------------
#define REG_OFFSET_STATUS       0   // bit 0 = APU on, bit 1 = fault active
#define REG_OFFSET_FAULT_CODE   1   // 0 = no fault
#define REG_OFFSET_RUNTIME_X10  2   // cumulative runtime ×10 hours — 16-bit, wraps at 6 553.5 h;
                                    // firmware extends via runtimeWrapCount (see AppState). [4]
#define REG_OFFSET_DC_VOLTS_X10 3   // DC bus voltage ×10 (e.g. 136 = 13.6 V)
#define REG_OFFSET_WATTS        4   // output watts

// ---------------------------------------------------------------------------
// Defaults (overridable via Notehub environment variables)
// ---------------------------------------------------------------------------
#define DEFAULT_SAMPLE_INTERVAL_SEC   60
#define DEFAULT_SUMMARY_INTERVAL_MIN  60
#define DEFAULT_ALERT_COOLDOWN_SEC    1800
#define DEFAULT_MODBUS_SLAVE_ID       1
#define DEFAULT_MODBUS_BAUD           19200
#define DEFAULT_MODBUS_REG_BASE       1
#define MODBUS_READ_RETRIES           3
// Response timeout is computed at runtime from baud rate — see modbusReadHolding().

// ---------------------------------------------------------------------------
// Structs
// ---------------------------------------------------------------------------
struct EnvConfig {
    uint32_t sampleIntervalSec;
    uint32_t summaryIntervalMin;
    uint32_t alertCooldownSec;
    uint8_t  modbusSlaveId;
    uint32_t modbusBaud;
    uint16_t modbusRegBase;
    float    apuFuelRateGph;   // APU rated fuel consumption (gal/hr)
    float    idleFuelRateGph;  // Engine idle fuel consumption (gal/hr)
    float    cabTempHighF;
    float    cabTempLowF;
    // Operator-provisioned DS18B20 ROM IDs — parsed from `amb_rom_id` and
    // `cab_rom_id` Notehub environment variables (16 lowercase hex characters
    // each, e.g. "2800000000000001"). When romIdsFromEnv is true, the firmware
    // uses these directly for probe-role assignment instead of discovery-index
    // commissioning, which is not a safe proxy for physical location.
    // Set via Notehub fleet env vars after reading the IDs from debug output.
    uint8_t  ambRomIdFromEnv[8];
    uint8_t  cabRomIdFromEnv[8];
    bool     romIdsFromEnv;   // true when both ROM IDs parsed successfully
};

struct AppState {
    uint8_t  state;
    uint32_t apuRuntimeSec;
    uint32_t idleTimeSec;
    float    fuelSavedGal;
    float    fuelUsedGal;
    float    cabTempAccF;
    float    ambTempAccF;
    uint16_t ambTempSamples;   // ambient-probe valid sample count this window
    uint16_t cabTempSamples;   // cab-probe valid sample count this window
    uint16_t faultCount;       // distinct fault occurrences this window (edge-detected)
    uint16_t lastFaultCode;
    uint32_t dcVoltsX10Acc;   // uint32 avoids overflow on long summary windows
    uint32_t wattsAcc;
    uint16_t powerSamples;
    uint32_t samplesInWindow;
    // Monotonic sample counter — incremented at the start of each runSampleCycle
    // and persisted across sleep cycles. Alert cooldown is expressed in samples
    // (alertCooldownSec / sampleIntervalSec) so dedup remains correct regardless
    // of card.time availability.
    uint32_t sampleCount;
    // Per-alert-type last-sent sample index. 0 means never sent (eligible immediately).
    // Cooldown check: (sampleCount - lastAlertXxxSample) >= samplesForCooldown.
    uint32_t lastAlertFaultSample;
    uint32_t lastAlertCabSample;
    uint32_t lastAlertSensorFailSample;
    // Fault code in the most recently sent apu_fault alert. Cooldown is bypassed
    // when the active code differs so a new fault type is never hidden by a prior
    // alert that happened to share the same cooldown bucket. [3]
    uint16_t lastAlertedFaultCode;
    // Edge-detection state for fault counting. faultWasActive is true while the
    // last sample had an active fault. activeFaultCode holds the fault code at
    // the last edge for persistent comparison across summary-window boundaries —
    // it is NOT reset by sendSummary(). lastFaultCode is for per-window reporting
    // and IS reset in sendSummary(). faultCount only increments on 0→nonzero or
    // a fault-code change, not on every sample while a fault remains latched. [4]
    bool     faultWasActive;
    uint16_t activeFaultCode;  // persistent edge-tracking — never reset by sendSummary
    uint16_t badSensorCycles;  // uint16_t avoids overflow during long cooldown windows [3]
    // Wrap-aware lifetime runtime.  The controller register (REG_OFFSET_RUNTIME_X10)
    // is a 16-bit unsigned counter in tenths of hours, rolling over at 6 553.5 h
    // (~273 days) — too short for warranty/lifetime odometry.  lastRawRuntimeX10
    // caches the last valid reading so a decrease on the next Modbus read is
    // detected as a rollover; runtimeWrapCount accumulates those events.  The
    // extended total (runtimeWrapCount × 65 536 + lastRawRuntimeX10) / 10.0 is
    // stored in lastControllerRuntimeHr for use in summary notes.  All three
    // fields are persisted across sleep cycles so a wrap that spans a host-power-
    // off cycle is still detected on the next successful read. [4]
    uint16_t lastRawRuntimeX10;    // last valid raw register value (hours ×10)
    uint16_t runtimeWrapCount;     // number of 16-bit rollovers detected
    float    lastControllerRuntimeHr;  // wrap-corrected lifetime hours (for notes)
    // Applied Notecard outbound cadence (minutes). Compared against
    // g_env.summaryIntervalMin on every wake; hub.set is re-issued immediately
    // when they differ so fleet env-var changes take effect without waiting for
    // the next summary window to fire. [7]
    uint32_t appliedSummaryIntervalMin;
    // Previous ignition determination — persisted across sleep cycles for
    // hysteresis. Holds state in the dead-band between ON and OFF thresholds. [9]
    bool     lastIgnitionOn;
    // Mandatory-configuration flag — set true once card.transport and both
    // note.template requests succeed.  If false on a resumed wake, config is
    // re-attempted before entering the normal sample cycle.  Sampling is skipped
    // until this flag is true so un-templated notes cannot reach Notehub and
    // satellite fallback is not used without the Skylo transport mode set. [3]
    bool     configuredOk;

    // DS18B20 ROM commissioning — set on the first wake where both probes are
    // present; persisted across sleep cycles so subsequent wakes can match each
    // device by its 64-bit ROM ID rather than discovery index.  Matching by ID
    // keeps the cab probe valid even when the ambient probe is temporarily
    // missing, so cab_overheat / cab_freeze alerts remain active regardless of
    // ambient probe state.
    bool    probesCommissioned;
    uint8_t ambRomId[8];   // 64-bit ROM address of the ambient probe
    uint8_t cabRomId[8];   // 64-bit ROM address of the cab probe

    // Daily fuel-saved rollup — accumulated continuously alongside the
    // per-summary-window totals.  Emitted once per calendar day in
    // apu_daily.qo and reset after confirmed delivery.
    float    dailyFuelSavedGal;
    float    dailyFuelUsedGal;
    uint32_t dailyApuRuntimeSec;
    uint32_t dailyIdleTimeSec;
    // card.time epoch/86400 value at the last daily emission.  0 means no
    // daily emission has occurred yet (or card.time has not been synced).
    uint32_t lastDailyEpochDay;
    // Fallback sample counter for daily boundary detection when card.time is
    // unavailable (e.g. no GPS fix or cellular sync on a fresh unit).
    uint32_t samplesSinceDaily;
};

// ---------------------------------------------------------------------------
// Function declarations
// ---------------------------------------------------------------------------
// ncSend — checked send helper. Wraps requestAndResponse, inspects "err",
// logs failures, returns true only on confirmed success. Use instead of
// sendRequest() wherever a silent failure would corrupt state or drop data.
bool ncSend(Notecard &notecard, J *req);
// notecardConfigure — transport, GPS mode, accelerometer off.
// Returns true only if both card.transport and card.location.mode (both
// mandatory for Skylo) succeeded. card.motion.mode is best-effort.
bool notecardConfigure(Notecard &notecard);
// defineTemplates — registers compact Notefile templates for all three notefiles.
// Returns true only if all three note.template requests succeeded.
bool defineTemplates(Notecard &notecard);
// initEnvDefaults — seeds compiled-in defaults into env. Call on cold boot
// before fetchEnvOverrides so the device has a valid baseline configuration.
void initEnvDefaults(EnvConfig &env);
// fetchEnvOverrides — fetches env.get and updates env on confirmed success.
// On any failure (NULL response, error field, missing body) the function
// returns without modifying env, preserving last-known-good configuration.
void fetchEnvOverrides(Notecard &notecard, EnvConfig &env);
// modbusReadHolding — read holding registers; response timeout derived from baud.
bool modbusReadHolding(uint8_t slaveId, uint16_t startAddr,
                       uint8_t count, uint16_t *results, uint32_t baud);
// readTemperatures — per-probe independent read.  ambFound / cabFound indicate
// whether the respective ROM address was seen on the OneWire bus this wake.
// An absent ambient probe does NOT suppress the cab validity flag.
void readTemperatures(DallasTemperature &ds, bool ambFound, bool cabFound,
                      DeviceAddress ambAddr, DeviceAddress cabAddr,
                      float *ambF, float *cabF, bool *ambOk, bool *cabOk);
// sendAlert — queues an apu_event.qo with sync:true; returns true on confirmed
// note.add success.  Callers must gate cooldown-timestamp and failure-counter
// updates on the return value so a transient Notecard error does not suppress
// retry attempts during the full cooldown window. [2]
bool sendAlert(Notecard &notecard, const AppState &state,
               const char *event, uint16_t faultCode,
               float ambF, float cabF, float dcVolts, uint16_t watts);
void sendSummary(Notecard &notecard, AppState &state, const EnvConfig &env);
// sendDailySummary — emit an apu_daily.qo note with the day's accumulated fuel
// and runtime totals.  Returns true on confirmed success and resets daily
// accumulators.  On failure, accumulators are preserved for retry on the next
// calendar-day boundary check.
bool sendDailySummary(Notecard &notecard, AppState &state);
