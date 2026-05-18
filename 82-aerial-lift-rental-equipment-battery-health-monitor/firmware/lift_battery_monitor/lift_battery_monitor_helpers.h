/***************************************************************************
  lift_battery_monitor_helpers.h — shared types, extern globals, and
  function prototypes for the Aerial Lift Battery Health Monitor.

  THIS FILE SHOULD BE EDITED AFTER GENERATION.
  IT IS PROVIDED AS A STARTING POINT FOR THE USER TO EDIT AND EXTEND.
***************************************************************************/
#pragma once

#include <Notecard.h>
#include <Adafruit_INA228.h>
#include <stdint.h>

// ─── Build-configuration toggles ─────────────────────────────────────────────
// ENABLE_CAN_BMS, ENABLE_ACS758, and BENCH_ONLY are defined in
// lift_battery_monitor_config.h — the single authoritative location.
// Edit that file to change any build option; do NOT redefine these macros
// here or in the .ino.  Including the config header from this shared header
// ensures both the .ino and the .cpp translation unit see the same values.
#include "lift_battery_monitor_config.h"

// Guard against contradictory build flags.
// Valid combinations:
//   ENABLE_ACS758 0, BENCH_ONLY 0 — primary field path: INA228 + external shunt
//   ENABLE_ACS758 0, BENCH_ONLY 1 — bench path: INA228 onboard 15 mΩ shunt (≤10 A)
//   ENABLE_ACS758 1, BENCH_ONLY 0 — alternative field path: ACS758 Hall-effect sensor
//   ENABLE_ACS758 1, BENCH_ONLY 1 — invalid: contradictory combination
#if ENABLE_ACS758 && BENCH_ONLY
#error "ENABLE_ACS758 and BENCH_ONLY cannot both be 1. " \
       "Clear BENCH_ONLY for the ACS758 field path, or clear ENABLE_ACS758 and " \
       "set BENCH_ONLY 1 for the onboard-shunt bench path."
#endif

#if !ENABLE_ACS758 && BENCH_ONLY
#warning "BENCH_ONLY build: INA228 onboard shunt current path active (max ~10 A). " \
         "Do NOT flash this image to a field unit."
#endif

#if ENABLE_CAN_BMS
#include <mcp2515.h>
#endif

// ─── Persistent state (serialized to Notecard flash across sleep cycles) ─────
struct PersistState {
    float    soc_pct;               // current state of charge (0–100 %)
    float    soh_pct;               // rolling state of health (0–100 %)
    float    throughput_ah;          // |Ah| throughput (bidirectional) accumulated in current cycle (for reporting)
    float    cycle_discharge_ah;    // discharge-only Ah in current cycle (for SoH math)
    float    measured_cap_ah;       // EWMA of last measured full-cycle capacity
    bool     soh_cycle_started;     // true once SoC has dipped below 30% this cycle
    uint32_t alert_epoch_soc;       // unix epoch of last soc_low alert
    uint32_t alert_epoch_temp_hi;   // unix epoch of last temp_high alert
    uint32_t alert_epoch_temp_lo;   // unix epoch of last temp_low alert
    uint32_t alert_epoch_imb;       // unix epoch of last cell_imbalance alert
    uint32_t alert_epoch_soh;       // unix epoch of last soh_low alert
    uint32_t can_err_epoch;         // unix epoch of last can_error note (persisted so
                                    // the once-per-hour rate limit survives sleep resets)
    float    summ_v_sum;            // running sum of pack voltages (V)
    float    summ_i_sum;            // running sum of pack currents (A)
    float    summ_t_sum;            // running sum of valid temperatures (°C)
    uint16_t summ_count;            // total sample count in current summary window
    uint16_t summ_t_count;          // valid-temperature sample count (non-NaN reads only)
    uint16_t wakes_since_summ;      // wakes since last summary (epoch-less fallback)
    uint32_t last_summ_epoch;       // epoch when last summary was sent
    uint32_t last_applied_report_m; // report_interval_m value last written to hub.set
                                    // (0 on cold boot → forces hub.set on first cycle)
    uint8_t  summ_fail_count;       // consecutive note.add failures for the current
                                    // summary window; reset to 0 on success; window is
                                    // discarded after MAX_SUMM_RETRIES to prevent
                                    // unbounded accumulator growth during long faults
};

// ─── Runtime configuration (populated from Notehub environment variables) ────
struct Config {
    float    soc_alert_pct;     // soc_low fires below this SoC %
    float    temp_high_c;       // temp_high fires above this °C
    float    temp_low_c;        // temp_low fires below this °C
    float    soh_alert_pct;     // soh_low fires below this SoH %
    float    rated_cap_ah;      // nameplate pack capacity (for SoH denominator)
    float    cell_delta_mv;     // max allowed cell-group imbalance in mV
    uint32_t sample_interval_s; // seconds between wakes
    uint32_t report_interval_m; // minutes between hourly summary notes
    bool     is_lithium;        // true = lithium OCV table, false = lead-acid
    // ACS758 calibration (ENABLE_ACS758 1 builds only).
    // Both values are overridable via Notehub environment variables so offset
    // error can be corrected at commissioning without a firmware reflash.
    float    acs758_zero_v;     // zero-current VOUT (V); nominal 2.5 at VCC=5V
    float    acs758_mv_per_a;   // sensitivity (mV/A); 10.0 for ACS758LCB-200B
};

// ─── References to globals defined in the .ino ───────────────────────────────
extern Notecard       notecard;
extern Adafruit_INA228 ina228;
extern Config         cfg;

#if ENABLE_CAN_BMS
extern MCP2515 mcp2515;
extern float   gCellMv[];
extern bool    gCanOk;
#endif

// ─── sendSummary() return values ──────────────────────────────────────────────
// Returned so the caller can decide whether to zero wakes_since_summ.
// Only SUMM_QUEUED and SUMM_DISCARDED warrant opening a fresh window;
// SUMM_RETAINED means the same window must retry on the next wake.
enum SummaryResult {
    SUMM_QUEUED    = 0,  // note handed to Notecard; accumulators cleared
    SUMM_RETAINED  = 1,  // transient failure; window kept for retry next wake
    SUMM_DISCARDED = 2   // retry ceiling hit; window discarded, accumulators cleared
};

// ─── Function prototypes ──────────────────────────────────────────────────────
bool          notecardConfigure(const char *productUID);
bool          defineTemplates(void);
void          fetchEnvOverrides(PersistState &s);

bool  readPackVI(float &packV, float &curA);
float readPackTempC(void);

float voltageToSoC(float voltage, bool isLithium);
void  updateThroughput(PersistState &s, float curA);
void  updateSoH(PersistState &s, float socPct);

void          sendAlert(const char *alert, float packV, float socPct, float tempC,
                        float extraV);
void          checkAlerts(PersistState &s, float packV, float socPct,
                          float tempC, uint32_t now);
SummaryResult sendSummary(PersistState &s, uint32_t now);

#if ENABLE_CAN_BMS
void  pollCanBms(PersistState &s, uint32_t now);
#endif
