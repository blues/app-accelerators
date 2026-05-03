/***************************************************************************
  rail_car_tracker_helpers.h — Declarations for Rail Car Condition &
  Interchange Tracker

  Contains all compile-time constants, the PersistState struct definition,
  extern declarations for globals shared between the .ino and the .cpp, and
  prototypes for every helper function implemented in
  rail_car_tracker_helpers.cpp.

  THIS FILE SHOULD BE EDITED AFTER GENERATION.
  IT IS PROVIDED AS A STARTING POINT FOR THE USER TO EDIT AND EXTEND.
***************************************************************************/

#pragma once

#include <Arduino.h>
#include <Wire.h>
#include <Notecard.h>

// ── Hardware profile: tank car vs. intermodal / flat car ─────────────────────
// Uncomment the following line when deploying on a tank car fitted with an
// Adafruit MPRLS pressure sensor and DS18B20 cargo-temperature probe. Leave it
// commented out for intermodal flats, covered hoppers, gondolas, and other
// non-tank variants — the sketch will then skip all MPRLS and DS18B20
// initialisation, omit the pressure_psi and tank_temp_c fields from every
// emitted note, and suppress the pressure and tank-temperature alert logic.
// This prevents spurious "sensor not found" warnings and −9999 sentinel values
// from appearing every wake cycle on non-tank assets.
//
// #define TANK_CAR

#ifdef TANK_CAR
#include <Adafruit_MPRLS.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#endif

// ── Product UID ───────────────────────────────────────────────────────────────
#ifndef PRODUCT_UID
#define PRODUCT_UID ""
#pragma message "PRODUCT_UID is not defined. Set it to your Notehub ProductUID."
#endif

// ── Debug serial ─────────────────────────────────────────────────────────────
#define debugSerial Serial

// ── Pin assignments (Notecarrier CX dual 16-pin header) ──────────────────────
#define PIN_COUPLER    D5    // Reed switch (NO contacts): LOW = coupled, HIGH = open
#define PIN_TANK_TEMP  D6    // DS18B20 one-wire data line (TANK_CAR builds only)

// ── I2C device addresses ──────────────────────────────────────────────────────
#define ADXL345_ADDR   0x53  // ADXL345: SDO pin low (default on most breakouts)
// MPRLS fixed address: 0x18 (no user-configurable pins)

// ── ADXL345 register addresses (direct Wire access, no library required) ──────
#define ADXL_REG_POWER     0x2D  // Power Control
#define ADXL_REG_FORMAT    0x31  // Data Format
#define ADXL_REG_DATAX0    0x32  // First data register (X low byte)
#define ADXL_SCALE_16G     0.0039f  // ±16G full-resolution: 3.9 mg/LSB

// ── Shock sample parameters ───────────────────────────────────────────────────
#define SHOCK_SAMPLES      64   // samples per shock-scoring burst (~0.64 s @ 100 Hz)

// ── Defaults (all overridable via Notehub environment variables) ──────────────
#define SAMPLE_INTERVAL_MIN_DEFAULT    15
#define REPORT_INTERVAL_MIN_DEFAULT   240    // 4 hours between status summaries
#define LOCATION_INTERVAL_MIN_DEFAULT  30    // max gap (min) between location notes while moving
#define SHOCK_THRESHOLD_G_DEFAULT      2.5f  // G above which impact windows are counted
#define SHOCK_COOLDOWN_MIN_DEFAULT     5     // min between consecutive shock alerts
#define PRESSURE_MAX_PSI_DEFAULT       20.0f // tank overpressure alert threshold (PSI abs)
#define PRESSURE_DROP_PSI_DEFAULT      10.0f // tank sudden pressure-drop alert threshold (PSI)
#define TANK_TEMP_MIN_C_DEFAULT        -10.0f // cargo low-temp alert threshold (°C; TANK_CAR builds)
#define TANK_TEMP_MAX_C_DEFAULT         50.0f // cargo high-temp alert threshold (°C; TANK_CAR builds)

// ── Notefiles ─────────────────────────────────────────────────────────────────
#define FILE_STATUS    "railcar_status.qo"    // periodic condition summary
#define FILE_ALERT     "railcar_alert.qo"     // edge-triggered alert notes
#define FILE_LOCATION  "railcar_location.qo"  // dedicated position stream

// ── NotePayload segment ID ────────────────────────────────────────────────────
#define STATE_SEG_ID  1

// ── Configuration schema version ─────────────────────────────────────────────
// Stored in PersistState.configVersion. CONFIG_VERSION encodes both the schema
// revision and the build profile so that:
//
//   • A schema change (new template fields or GPS/motion parameters) requires
//     incrementing CONFIG_VERSION_BASE; deployed devices will automatically
//     reapply note.template and card.location/motion.mode on the next wake.
//
//   • Toggling the TANK_CAR flag automatically invalidates the stored version
//     because the two profiles produce different CONFIG_VERSION values (4 for
//     standard, 104 for TANK_CAR). Without this coupling, switching profiles
//     can leave a stale railcar_status.qo template on the Notecard that is
//     missing or spuriously includes the pressure_psi / tank_temp_c fields,
//     causing note.add to reject payloads whose schema does not match the
//     registered template.
//
// hub.set (PRODUCT_UID, sync policy) is applied unconditionally every boot and
// does NOT require a CONFIG_VERSION bump to take effect.
//
//   Standard (non-TANK_CAR) builds : CONFIG_VERSION = 4
//   TANK_CAR builds                : CONFIG_VERSION = 104
//
#define CONFIG_VERSION_BASE  4
#ifdef TANK_CAR
#define CONFIG_VERSION  104
#else
#define CONFIG_VERSION  CONFIG_VERSION_BASE
#endif

// ── State struct persisted across sleep cycles ────────────────────────────────
// Stored in Notecard flash by NotePayloadSaveAndSleep; restored on each wake.
//
// Alert-delivery latches (lastCouplerState, lastEncTempLow, lastEncTempHigh,
// lastPressHigh) are only advanced when the corresponding sendAlert() call
// succeeds. A transient Notecard failure therefore leaves the latch in its
// "not yet sent" state so the alert is retried next wake. The shock cooldown
// (shockCooldownRemMin) is reset only on a successful alert send.
//
// configVersion is compared to CONFIG_VERSION on each boot; a mismatch causes
// note.template and GPS/motion config to be reapplied. hub.set runs
// unconditionally every boot (see CONFIG_VERSION comment above).
//
// New fields must be appended at the end of the struct. NotePayloadGetSegment
// fills the struct with the stored bytes and leaves any bytes beyond the stored
// size untouched. setup() zeroes the struct before the restore call so extra
// fields added in a newer build safely default to 0/false.
typedef struct {
    float    peakShockG;          // highest G reading since the last summary
    uint16_t shockWindowCount;    // count of sample windows where peak G >= threshold
                                  //   (NOT a count of individual impacts — see §6.3)
    bool     lastCouplerState;    // coupler state on the last wake that successfully
                                  //   sent a coupler alert (first-boot: initial state)
    float    lastPressurePsi;     // pressure reading on the previous wake (drop detection)
    bool     lastPressureValid;   // true only when lastPressurePsi holds a valid reading
    uint32_t elapsedMin;          // minutes elapsed since last summary; stable across
                                  //   env-var changes because sampleMin is added each wake
    uint32_t shockCooldownRemMin; // minutes remaining before the next shock alert may
                                  //   fire; decremented by sampleMin each wake, reset
                                  //   to shockCoolMin on a successful alert send;
                                  //   does not require absolute time (GPS/network sync)
    uint8_t  configVersion;       // schema version — rerun configuration when ≠ CONFIG_VERSION
    bool     lastPressHigh;       // true: tank pressure exceeded threshold AND the alert was
                                  //   sent; cleared when pressure drops below max to re-arm
    bool     locationAcquired;    // true once the Notecard has confirmed a valid GPS fix;
                                  //   GPS motion gate is bypassed until this is set so a
                                  //   newly installed tracker can acquire in a stationary yard
    // ── Fields added in CONFIG_VERSION 2 ─────────────────────────────────────
    bool     lastMovingState;     // motion state on the last wake that successfully sent a
                                  //   railcar_location.qo note (edge-change latch for retry)
    uint32_t locationElapsedMin;  // minutes since the last railcar_location.qo note was
                                  //   emitted; reset to 0 on a successful send; continues
                                  //   accumulating when a send fails so the next wake retries
    // ── Fields added in CONFIG_VERSION 4 ─────────────────────────────────────
    bool     lastTankTempLow;     // true: cargo temp below tank_temp_min_c AND alert sent;
                                  //   cleared when temp returns above threshold to re-arm.
                                  //   Unused (always false) in non-TANK_CAR builds.
    bool     lastTankTempHigh;    // true: cargo temp above tank_temp_max_c AND alert sent;
                                  //   cleared when temp drops below threshold to re-arm.
                                  //   Unused (always false) in non-TANK_CAR builds.
} PersistState;

// ── Shared globals (defined in rail_car_tracker.ino) ─────────────────────────
extern Notecard        notecard;
#ifdef TANK_CAR
extern Adafruit_MPRLS    mprls;
extern OneWire           oneWireBus;
extern DallasTemperature tankTempSensor;
#endif
extern PersistState    state;

// ── Function prototypes ───────────────────────────────────────────────────────
bool  notecardReady();
bool  configureNotecard();
bool  defineTemplates();
bool  configureMotionAndGPS();
bool  applyGPSMotionGate();
void  fetchEnvOverrides(uint32_t &sampleMin, uint32_t &reportMin,
                        float &shockThreshG, uint32_t &shockCoolMin,
                        uint32_t &locationIntervalMin,
                        float &pressMaxPsi, float &pressDropPsi,
                        float &tankTempMinC, float &tankTempMaxC);
bool  readCouplerState();
float readPeakShockG();
bool  sendAlert(const char *alertType, float value);
bool  sendSummary(float pressurePsi, float tankTempC, bool coupled, bool moving);
bool  sendLocationNote(bool coupled, bool moving);
bool  adxl345Begin();
bool  adxl345ReadG(float &gx, float &gy, float &gz);
