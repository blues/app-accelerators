/*******************************************************************************
  construction_env_monitor_helpers.h — Shared constants, types, extern
  declarations, and helper-function prototypes for the Construction Site
  Environmental & Noise Exposure Monitor.

  Included by both construction_env_monitor.ino (which provides the global
  definitions) and construction_env_monitor_helpers.cpp (which provides the
  function implementations).
*******************************************************************************/

#pragma once

#include <Notecard.h>
#include <Adafruit_PM25AQI.h>
#include <Wire.h>
#include <math.h>

// ── Product UID ───────────────────────────────────────────────────────────────
// Replace with your Notehub project ProductUID before deploying.
// See: https://dev.blues.io/notehub/notehub-walkthrough/#finding-a-productuid
#ifndef PRODUCT_UID
#define PRODUCT_UID ""
#pragma message "Set PRODUCT_UID to your Notehub project UID before deploying."
#endif

// ── Debug serial ──────────────────────────────────────────────────────────────
// Comment out the line below to disable USB-serial debug output in the field.
#define DEBUG_SERIAL Serial

// ── Notefiles ─────────────────────────────────────────────────────────────────
#define NOTEFILE_SUMMARY  "env_summary.qo"
#define NOTEFILE_ALERT    "env_alert.qo"

// ── Pin assignments ───────────────────────────────────────────────────────────
#define PIN_SOUND_LEVEL   A0    // DFRobot SEN0232 analog output

// ── ADC / sound-level constants ───────────────────────────────────────────────
// SEN0232 operating range: 3.3–5 V.  Powered from V+ (~3.7–4.2 V LiPo) the
// supply is within spec.  The published Vout mapping (0.6 V → 30 dB, 2.6 V →
// 130 dB) is calibrated at 5 V; apply 'db_cal_offset' to zero-trim against a
// reference meter at commissioning time.
#define ADC_REF_VOLTAGE   3.3f
#define ADC_RESOLUTION    4096.0f   // 12-bit on Cygnet STM32
#define SEN0232_V_LOW     0.6f      // volts corresponding to SEN0232_DB_LOW
#define SEN0232_V_HIGH    2.6f      // volts corresponding to SEN0232_DB_HIGH
#define SEN0232_DB_LOW    30.0f     // dB(A) at minimum output voltage
#define SEN0232_DB_HIGH   130.0f    // dB(A) at maximum output voltage

// ── Sampling parameters ───────────────────────────────────────────────────────
#define PM_WARMUP_MS              30000   // ms for PMSA003I fan/laser to settle
#define PM_SAMPLE_COUNT               10  // PM readings to average per cycle
#define PM_SAMPLE_INTERVAL_MS       1000  // 1 Hz PM sampling
#define SOUND_SAMPLE_MS            15000  // ms of audio sampling per cycle
#define SOUND_SAMPLE_INTERVAL_MS     250  // 4 Hz audio sampling (60 samples total)

// ── Alert cooldown ────────────────────────────────────────────────────────────
#define ALERT_COOLDOWN_SEC  1800    // 30 min minimum between same-type alerts

// ── Firmware defaults (all overridable via Notehub environment variables) ─────
#define DEFAULT_SAMPLE_INTERVAL_SEC   300     // 5 min
#define DEFAULT_REPORT_INTERVAL_MIN    30     // 30 min
#define DEFAULT_PM25_ALERT_UG_M3      35.0f  // heuristic dust-alert starting point; not a silica-specific or regulatory threshold
#define DEFAULT_PM10_ALERT_UG_M3     150.0f  // heuristic starting point; not a regulatory threshold
#define DEFAULT_DB_A_ALERT            85.0f  // heuristic starting point; not a regulatory determination
#define DEFAULT_GPS_INTERVAL_SEC    14400    // 4 hours (static site rarely moves)

// ── Persistent state segment ID ───────────────────────────────────────────────
// 'static' gives each TU its own copy; the string is 5 bytes so the duplication
// is inconsequential and simpler than an extern/definition pair.
static const char STATE_SEG_ID[] = "SITE";

// ── Persistent state struct (serialised to Notecard flash between sleeps) ─────
struct AppState {
    // Sample-window accumulators; reset after each successfully queued summary.
    uint32_t sampleCount;      // total wake cycles in window (denominator for dB avg)
    uint32_t pmSampleCount;    // valid PM-sensor reads in window (denominator for PM avg)
    float    pm25Sum;
    float    pm10Sum;
    // dB(A) samples are accumulated as acoustic energy (10^(dB/10)) so that
    // the window mean is computed in the linear domain.  sendSummary() converts
    // back to dB via 10·log10(dbEnergySum / sampleCount).
    float    dbEnergySum;
    float    pm25Peak;
    float    pm10Peak;
    float    dbPeak;

    // Report-cycle countdown (seconds until next env_summary.qo note).
    uint32_t reportCountdown;

    // Per-alert cooldown counters (seconds remaining before next alert of each type).
    uint32_t pm25AlertCooldown;
    uint32_t pm10AlertCooldown;
    uint32_t dbAlertCooldown;

    // Last-known GPS fix from card.location; included in every note payload.
    float    siteLat;
    float    siteLon;
    bool     gpsValid;

    // GPS freshness tracking across sleep/wake cycles and after site moves.
    //
    // When a device is redeployed from one construction site to another, the
    // Notecard's GNSS hardware may retain a cached fix from the previous site
    // and return it immediately on the first card.location call after a fresh
    // power-on.  gpsBootSeenTime records the fix timestamp (epoch seconds)
    // from the first successful card.location response after boot so that
    // subsequent calls can detect when the GNSS has acquired a genuinely new
    // position at the current site (indicated by a newer timestamp).
    //
    // gpsBootConfirmed is set true the first time card.location returns a
    // fix whose timestamp is newer than gpsBootSeenTime, confirming the GNSS
    // has acquired a fresh fix at this site.  Both fields are zeroed by
    // memset() on a fresh power-on, so the confirmation resets on every
    // power cycle; it is preserved across ATTN sleep/wake cycles so the
    // device does not re-confirm on every sample wake once settled at a site.
    //
    // Every outbound note body includes a 'location_valid' boolean mirroring
    // gpsBootConfirmed so downstream consumers can distinguish a confirmed
    // fix from a potentially stale cached position.
    uint32_t gpsBootSeenTime;   // epoch of first GPS fix seen after boot
    bool     gpsBootConfirmed;  // true once a newer fix has been confirmed

    // GPS re-acquire countdown (seconds until next card.location call).
    // Held at zero until a valid fix is confirmed so the host retries on
    // every wake rather than waiting a full interval with absent coordinates.
    uint32_t gpsCountdown;

    // Last confirmed Notecard cadence configuration.  hub.set and
    // card.location.mode are re-issued whenever these differ from the
    // current env-var-derived values, so Notehub operators can tune
    // outbound sync and GPS acquisition cadence without re-flashing.
    //
    // Both lastReportMin and lastGpsSec are initialised to 0 on first boot
    // so applyCardConfig() sends hub.set and card.location.mode
    // unconditionally on the first wake and retries on any subsequent wake
    // where a previous attempt failed — ensuring cadence config is always
    // applied even after a transient I²C error at boot.  Both fields are
    // advanced only after a confirmed successful requestAndResponse.
    uint32_t lastReportMin;
    uint32_t lastGpsSec;
};

// ── Extern declarations — global objects/variables defined in .ino ────────────
extern AppState          state;
extern Notecard          notecard;
extern Adafruit_PM25AQI  aqiSensor;
extern uint32_t cfgSampleSec;
extern uint32_t cfgReportMin;
extern float    cfgPm25Alert;
extern float    cfgPm10Alert;
extern float    cfgDbAlert;
extern uint32_t cfgGpsSec;
extern float    cfgDbCalOffset;

// ── Helper function declarations ──────────────────────────────────────────────
void     notecardConfigure(void);
void     defineTemplates(void);
void     applyCardConfig(void);
void     fetchEnvOverrides(void);
bool     updateGPS(void);
bool     readPmSensor(float &pm25Out, float &pm10Out);
float    readSoundLevelDb(void);
bool     sendSummary(void);
bool     sendAlert(const char *type, float value, float threshold);
void     runOneSampleCycle(void);
void     saveStateAndSleep(uint32_t sleepSec);
float    clampF(float v, float lo, float hi);
uint32_t clampU(uint32_t v, uint32_t lo, uint32_t hi);
