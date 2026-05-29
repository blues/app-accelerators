/***************************************************************************
  apiary_hive_monitor_helpers.h — Shared declarations for the Remote Apiary
  Hive Health Monitor.

  Defines the HiveState struct that is serialised into Notecard flash on
  every sleep cycle (via NotePayloadSaveAndSleep) and rehydrated on the
  next wake.  Also declares extern references to the Notecard, HX711, and
  SHT31 objects owned by the .ino, and prototypes for every helper function
  implemented in apiary_hive_monitor_helpers.cpp.

  Hardware context:
    - Notecarrier CX (onboard Cygnet STM32L433 host MCU)
    - Notecard for Skylo (NOTE-NBGLWX) in M.2 slot — cellular + WiFi +
      Skylo satellite (NTN) on one board
    - SparkFun HX711 (SEN-13879) + Zemic H8C single-ended shear-beam load cell
    - Adafruit SHT31-D (#2857) via 0.1″ jumper wires on SDA/SCL
    - Adafruit MAX9814 (#1713) analog microphone on A0
***************************************************************************/

#pragma once

#include <Notecard.h>
#include <HX711.h>
#include <Adafruit_SHT31.h>

// ---------------------------------------------------------------------------
// Debug output — uncomment to enable Serial.print statements during bench
// testing; leave commented for deployed hardware to avoid the ~0.5 mA UART
// idle draw and unnecessary wake-time overhead on a solar-powered device.
// Defined here (shared header) so that both the .ino and helpers.cpp see the
// same flag without requiring a separate build-system -D flag.
// ---------------------------------------------------------------------------
// #define DEBUG_SERIAL

// ---------------------------------------------------------------------------
// Persisted state shared between .ino and helpers
// ---------------------------------------------------------------------------
struct HiveState {
    float    weight_sum_kg;
    float    weight_first_kg;    // first valid weight reading of the window; -1.0f = none yet
    float    weight_last_kg;     // most recent valid weight reading; -1.0f = none yet
    float    temp_sum_c;
    float    humidity_sum_pct;
    float    zcr_sum;
    float    rms_sum;
    float    peak_sum;
    uint16_t sample_count;       // all sensor samples accumulated
    uint16_t weight_sample_count;
    uint16_t temp_valid_count;   // successful SHT31-D reads (avoids biasing avg on failures)
    uint16_t stored_outbound_min; // last hub.set outbound value; reissue hub.set when changed

    uint32_t alert_weight_ts;    // epoch of last weight alert (debounce)
    uint32_t alert_temp_ts;
    uint32_t alert_audio_ts;

    uint32_t last_report_epoch;  // epoch of last daily summary

    bool     first_boot;

    // Audio is captured once per summary window (brief daily snippet).
    // These fields track whether the snapshot has already been taken this window.
    uint16_t audio_sample_count; // 0 before snapshot; 1 after
    bool     audio_sampled;      // true once audio captured this window

    // Edge-trigger state for the reset_state env var.
    // 0 = reset_state was absent or "0" on the last wake that fetched env vars.
    // 1 = reset_state was "1" on the last wake that fetched env vars.
    // A commissioning reset fires only on the 0→1 transition; subsequent wakes
    // that still see "1" (before the next inbound sync delivers the cleared
    // value) are silently skipped, making the reset inherently one-shot.
    uint8_t  last_reset_token;
};

// ---------------------------------------------------------------------------
// Extern references to globals defined in the .ino
// ---------------------------------------------------------------------------
extern Notecard       notecard;
extern HX711          scale;
extern Adafruit_SHT31 sht31;

// ---------------------------------------------------------------------------
// Function declarations
// ---------------------------------------------------------------------------

// Notecard setup (called once on first boot; productUID passed from .ino
// because #define macros in .ino are not visible to compiled .cpp files).
// Returns true on success; false if a critical step fails (retry next wake).
bool notecardConfigure(bool freshBoot, const char *productUID);

// Register compact Note templates compatible with Skylo NTN.
// Returns true only if both templates registered without error.
bool defineTemplates(void);

// Pull operator thresholds from Notehub environment variables.
// resetSeen is set to true if `reset_state` is currently "1" in Notehub,
// false otherwise (absent or "0").  The caller compares resetSeen against
// HiveState::last_reset_token to implement one-shot edge-triggered behaviour:
// the commissioning reset fires only on the 0→1 transition, not on every
// subsequent wake that still reads the same env var value.
void fetchEnvOverrides(uint16_t &sampleMin, uint16_t &reportHr,
                       float &weightDropKg, float &tempLow,
                       float &tempHigh, uint16_t &audioZcr,
                       float &calibration, float &zeroOffsetKg,
                       bool &resetSeen);

// Read weight via HX711 (10-sample average); returns -1.0 on timeout/error
float readWeightKg(float calibration);

// Read SHT31-D temperature and humidity; returns false on sensor error
bool readTempHumidity(float &temp_c, float &humidity_pct);

// Streaming audio feature extraction: ZCR, RMS, peak amplitude.
// Returns true on valid audio; false when the signal is implausible (DC offset
// outside mid-rail band, > 30 % samples at ADC rail, or RMS below the minimum
// threshold — indicating a floating input, disconnected mic, or dead/shorted
// capsule).  Outputs are zeroed and the caller should treat the result as
// missing data (the -9999 sentinel path via safeAvg in sendSummary).
bool readAudioFeatures(float &zcr_mean, float &rms_mean, float &peak_mean);

// Emit an immediate Note with sync:true to bypass outbound timer.
// Returns true if the note.add was acknowledged without error; false on any
// failure (caller should not advance the alert cooldown timestamp on false).
bool sendAlert(const char *alertType, float value1, float value2);

// Emit the daily aggregated summary Note.
// Returns true on success; false on failure (caller should not reset
// last_report_epoch so the next wake retries the send).
bool sendSummary(const HiveState &st);

// Safe average: returns -9999.0 sentinel if count == 0
float safeAvg(float sum, uint16_t count);
