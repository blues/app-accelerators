/***************************************************************************
  lift_battery_monitor.ino — Aerial Lift / Rental Equipment Battery Health
  Monitor on Blues Notecarrier CX + Notecard for Skylo (NOTE-NBGLWX).

  Hardware:
    • Notecarrier CX (embedded Cygnet STM32L433 host MCU)
    • Notecard for Skylo NOTE-NBGLWX (LTE-M / NB-IoT / WiFi / Skylo NTN fallback)
    • Adafruit INA228 high-voltage I²C power monitor (#5832) on Qwiic bus
    • 10 kΩ NTC thermistor on A0 (β = 3950, series 10 kΩ pull-up to 3.3 V)
    • External precision shunt (50–200 A, 50–75 mV FS) on INA228 VIN+/VIN− — primary field current path
      (update DEFAULT_SHUNT_MOHM / DEFAULT_SHUNT_MAX_A below to match the installed shunt)
    • Blues Mojo (LTC2959 coulomb counter) optional bench power validator via Qwiic
    • MCP2515 SPI CAN module optional — set ENABLE_CAN_BMS 1 in lift_battery_monitor_config.h to enable

  Sleep / wake pattern:
    All application logic runs in setup().  At the end of each cycle the
    Notecard drives the ATTN pin to wake the Cygnet after sample_interval_s
    seconds; execution restarts at setup() on every wake.  loop() is empty.

  THIS FILE SHOULD BE EDITED AFTER GENERATION.
  IT IS PROVIDED AS A STARTING POINT FOR THE USER TO EDIT AND EXTEND.
***************************************************************************/

// ─── Build-configuration toggles ─────────────────────────────────────────────
// ENABLE_CAN_BMS, ENABLE_ACS758, and BENCH_ONLY are defined in
// lift_battery_monitor_config.h — edit that file to change any build option.
// The config header is pulled in automatically by lift_battery_monitor_helpers.h
// below, so this translation unit sees the same values as the .cpp file.

#include <Arduino.h>
#include <Wire.h>
#include <Notecard.h>
#include <Adafruit_INA228.h>
#include "lift_battery_monitor_helpers.h"

#if ENABLE_CAN_BMS
#include <SPI.h>
#include <mcp2515.h>
#endif

// ─── Notehub product UID ──────────────────────────────────────────────────────
// Replace with your Notehub project UID before first flash.
// Create a free project at https://notehub.io.
#define PRODUCT_UID  "com.your-company:your-project"

// ─── NotePayload segment identifier ──────────────────────────────────────────
// Short string label for this sketch's PersistState in Notecard flash.
// Change only if another sketch on the same Notecard needs a different label.
#define STATE_SEG_ID  "LIFT"

// ─── CAN BMS hardware parameters (edit to match your BMS vendor) ─────────────
#if ENABLE_CAN_BMS
#define PIN_CAN_CS          5            // SPI CS for MCP2515 — Notecarrier CX D5 (see README §4)
#define BMS_CELL_COUNT      8            // array capacity for decoded cell-group voltages;
                                         // a single classic CAN frame (DLC ≤ 8 bytes) holds
                                         // at most four 16-bit values — BMS_CELL_COUNT may
                                         // exceed that if the BMS uses a multi-frame protocol
#define BMS_CELL_GROUP_ID  0x18FF50E5UL  // 29-bit extended CAN ID (placeholder)
#endif

// ─── Firmware-default runtime configuration ───────────────────────────────────
// All values are overridable at runtime via Notehub environment variables
// without reflashing — see README §5 for the complete variable table.
#define DEFAULT_SOC_ALERT_PCT       20.0f  // fire soc_low alert below 20 % SoC
#define DEFAULT_TEMP_HIGH_C         45.0f  // fire temp_high alert above 45 °C
#define DEFAULT_TEMP_LOW_C           5.0f  // fire temp_low alert below 5 °C (charging risk)
#define DEFAULT_SOH_ALERT_PCT       70.0f  // fire soh_low alert below 70 % SoH
#define DEFAULT_RATED_CAP_AH       100.0f  // 100 Ah nameplate pack capacity
#define DEFAULT_CELL_DELTA_MV      200.0f  // 200 mV max cell-group spread
// External shunt calibration (primary field build, ENABLE_ACS758 0, BENCH_ONLY 0).
// *** Update these two constants to match the installed shunt before building. ***
// Select a manganin or nichrome alloy precision shunt rated for the pack's full
// discharge current at 50–75 mV full-scale (e.g., 0.5 mΩ at 100 A = 50 mV,
// or 0.375 mΩ at 200 A = 75 mV).  Wire shunt IN+ to pack positive and IN− to
// the load/charger positive bus; connect INA228 VIN+ to IN+ and VIN− to IN−.
// setShunt() writes SHUNT_CAL so readCurrent() returns calibrated amperes.
#define DEFAULT_SHUNT_MOHM           0.5f  // shunt resistance in mΩ — MUST match installed shunt
#define DEFAULT_SHUNT_MAX_A        200.0f  // full-scale current (A) — MUST match installed shunt

// ACS758 calibration defaults (alternative field build, ENABLE_ACS758 1).
// Hall-sensor zero-offset can bias the Ah accumulator by multiple amps;
// set acs758_zero_v via Notehub after commissioning with zero current flowing.
#define DEFAULT_ACS758_ZERO_V        2.5f  // nominal zero-current VOUT at VCC=5V per datasheet
#define DEFAULT_ACS758_MV_PER_A     10.0f  // ACS758LCB-200B nominal sensitivity
#define DEFAULT_SAMPLE_INTERVAL_S    300U  // wake every 5 minutes
#define DEFAULT_REPORT_INTERVAL_M     60U  // emit hourly rolling summary

// ─── Global object definitions ────────────────────────────────────────────────
// Non-static so helpers.cpp can reach them via the extern declarations in
// lift_battery_monitor_helpers.h.
Notecard        notecard;
Adafruit_INA228 ina228;
Config          cfg;

#if ENABLE_CAN_BMS
MCP2515 mcp2515(PIN_CAN_CS);
float   gCellMv[BMS_CELL_COUNT];
bool    gCanOk = false;
#endif

// ─────────────────────────────────────────────────────────────────────────────
// setup — runs once per wake from card.attn sleep.  All application logic
// lives here; the MCU powers down completely at the end via NotePayloadSaveAndSleep.
// ─────────────────────────────────────────────────────────────────────────────
void setup(void) {
    Serial.begin(115200);
    delay(100);                      // allow USB-serial to stabilise on cold boot

#if BENCH_ONLY && !ENABLE_ACS758
    // Belt-and-suspenders runtime guard: makes a bench image unmistakable even
    // if the compile-time #warning was missed or the binary was shared without
    // its build flags.
    Serial.println("*** BENCH_ONLY BUILD — INA228 current path active (max ~10 A). "
                   "NOT safe for field deployment. ***");
#endif

    Wire.begin();
    analogReadResolution(12);        // 12-bit ADC on Cygnet (ADC_COUNTS = 4096)
    notecard.begin();                // I²C, default Notecard address, 400 kHz

    // ── Populate cfg with firmware defaults before env.get overrides ──────────
    cfg.soc_alert_pct     = DEFAULT_SOC_ALERT_PCT;
    cfg.temp_high_c       = DEFAULT_TEMP_HIGH_C;
    cfg.temp_low_c        = DEFAULT_TEMP_LOW_C;
    cfg.soh_alert_pct     = DEFAULT_SOH_ALERT_PCT;
    cfg.rated_cap_ah      = DEFAULT_RATED_CAP_AH;
    cfg.cell_delta_mv     = DEFAULT_CELL_DELTA_MV;
    cfg.sample_interval_s = DEFAULT_SAMPLE_INTERVAL_S;
    cfg.report_interval_m = DEFAULT_REPORT_INTERVAL_M;
    cfg.is_lithium        = true;    // assume LiFePO4; override via chemistry env var
    cfg.acs758_zero_v     = DEFAULT_ACS758_ZERO_V;
    cfg.acs758_mv_per_a   = DEFAULT_ACS758_MV_PER_A;

    // ── Restore persistent state from Notecard flash ──────────────────────────
    // NotePayloadRetrieveAfterSleep returns true on a warm boot (saved payload
    // found) and false on first cold boot (struct stays zero-initialised).
    PersistState s = {};
    NotePayloadDesc inPayload = {0};
    bool warmBoot = NotePayloadRetrieveAfterSleep(&inPayload);
    if (warmBoot) {
        NotePayloadGetSegment(&inPayload, STATE_SEG_ID, &s, sizeof(s));
    }
    NotePayloadFree(&inPayload);

    if (!warmBoot) {
        // Cold boot: seed reasonable defaults into the persistent state.
        s.soc_pct         = 100.0f;
        s.soh_pct         = 100.0f;
        s.measured_cap_ah = 0.0f;
    }

    // Pull any updated environment variables from Notehub.  Uses the cached
    // env body from the last inbound sync if currently offline; silently
    // skips on first cold boot (no cached env body yet).
    fetchEnvOverrides(s);

    if (!warmBoot) {
        // Register the hub connection and compact templates once per cold boot.
        // sendRequestWithRetry inside notecardConfigure handles the I²C cold-
        // boot race where the Notecard takes up to 10 s to become ready.
        Serial.println("[boot] cold start — configuring Notecard");
        if (!notecardConfigure(PRODUCT_UID)) {
            // hub.set failed (bad PRODUCT_UID, Notecard not ready, etc.).
            // Leave last_applied_report_m at 0 so the next wake re-enters
            // this path and retries both notecardConfigure and defineTemplates.
            Serial.println("[error] Notecard configuration failed — "
                           "sleeping and retrying on next wake");
            goto persist_and_sleep;
        }
        if (defineTemplates()) {
            s.last_applied_report_m = cfg.report_interval_m;
        } else {
            // last_applied_report_m stays 0 → the != comparison on the next
            // warm wake re-enters this branch and retries both calls.
            Serial.println("[error] template definition failed — "
                           "will retry on next wake");
        }
    } else if (cfg.report_interval_m != s.last_applied_report_m) {
        // report_interval_m changed via a Notehub fleet/device env var since
        // the last hub.set was issued — re-apply so the Notecard outbound
        // cadence matches the updated value without waiting for a reboot.
        // Compare against s.last_applied_report_m (persisted in flash), not
        // the firmware default, so this fires only once per actual change.
        // Also covers the cold-boot-failure retry path: when last_applied_report_m
        // is still 0 from a failed cold boot, the warm wake re-issues hub.set and
        // defineTemplates together.
        Serial.print("[cfg] report_interval_m ");
        Serial.print(s.last_applied_report_m);
        Serial.print(" -> ");
        Serial.print(cfg.report_interval_m);
        Serial.println(" min — reapplying hub.set");
        if (notecardConfigure(PRODUCT_UID)) {
            if (defineTemplates()) {
                s.last_applied_report_m = cfg.report_interval_m;
            } else {
                // last_applied_report_m unchanged → retries on next wake.
                Serial.println("[error] template definition failed — "
                               "will retry on next wake");
            }
        }
        // On notecardConfigure failure, last_applied_report_m is unchanged → retries on next wake.
    }

    // ── Safety gate: only sample when templates have been confirmed ───────────
    // last_applied_report_m is written to a non-zero value only after BOTH
    // notecardConfigure and defineTemplates succeed.  Proceeding while it is
    // still 0 would cause note.add calls to fall back to uncompressed JSON on
    // battery_status.qo and battery_alert.qo, breaking the compact/satellite
    // data-budget strategy and the Skylo note-size contract.
    if (s.last_applied_report_m == 0) {
        Serial.println("[error] Notecard templates not yet confirmed — "
                       "skipping sample cycle until templates are defined");
        goto persist_and_sleep;
    }

    // ── Initialise Adafruit INA228 power monitor ──────────────────────────────
    if (!ina228.begin()) {
        Serial.println("[error] INA228 not found — check Qwiic / I2C wiring");
        // Persist state and sleep; the next wake will retry init.
        goto persist_and_sleep;
    }
    // Calibrate the INA228 shunt path.  setShunt() writes SHUNT_CAL so
    // readCurrent() returns calibrated amperes.
    //   BENCH_ONLY 1: onboard 15 mΩ shunt, ≤10 A (bench/dev only).
    //   BENCH_ONLY 0: external field shunt wired to VIN+/VIN−.
    //     Update DEFAULT_SHUNT_MOHM and DEFAULT_SHUNT_MAX_A above to match
    //     the installed shunt before building for field deployment.
    // When ENABLE_ACS758 1 the INA228 is used for voltage only; these values
    // do not affect current measurement in that path.
#if BENCH_ONLY
    ina228.setShunt(0.015f, 10.0f);                              // onboard 15 mΩ, ≤10 A — bench only
#else
    ina228.setShunt(DEFAULT_SHUNT_MOHM * 0.001f, DEFAULT_SHUNT_MAX_A);  // external field shunt
#endif

#if ENABLE_CAN_BMS
    // ── Initialise MCP2515 CAN controller ────────────────────────────────────
    // SPI.begin() must be called before touching the MCP2515; the autowp
    // mcp2515 library does not initialise the SPI peripheral itself.
    SPI.begin();
    mcp2515.reset();
    mcp2515.setBitrate(CAN_250KBPS, MCP_8MHZ);
    mcp2515.setNormalMode();
    Serial.println("[can] MCP2515 ready at 250 kbps");
#endif

    {
        // ── Get current epoch from Notecard ──────────────────────────────────
        // now == 0 means the Notecard clock is not yet set (no cellular fix).
        // Retry up to 3 times so a transient I²C stall on warm boot does not
        // silently drop the epoch and push the device into the no-epoch path.
        uint32_t now = 0;
        {
            J *rsp = nullptr;
            for (int attempt = 0; attempt < 3 && !rsp; attempt++) {
                if (attempt) delay(1000);
                rsp = notecard.requestAndResponse(
                          notecard.newRequest("card.time"));
            }
            if (rsp) {
                if (!JGetString(rsp, "err")) {
                    now = (uint32_t)JGetInt(rsp, "time");
                }
                notecard.deleteResponse(rsp);
            }
        }

        // ── Read pack voltage, current, and temperature ───────────────────────
        float packV = 0.0f, curA = 0.0f;
        float tempC = readPackTempC();   // NAN if thermistor open or shorted

        if (!readPackVI(packV, curA)) {
            Serial.println("[warn] INA228 read out-of-range — skipping sample");
            goto persist_and_sleep;
        }

        // ── Update SoC, cycle Ah throughput, and rolling SoH ─────────────────
        s.soc_pct = voltageToSoC(packV, cfg.is_lithium);
        updateThroughput(s, curA);
        updateSoH(s, s.soc_pct);

        Serial.print("[meas] v=");  Serial.print(packV, 2);
        Serial.print("V  i=");      Serial.print(curA, 2);
        // -9999.0 printed for missing/invalid thermistor to match the outbound sentinel.
        Serial.print("A  t=");      Serial.print(isnan(tempC) ? -9999.0f : tempC, 1);
        Serial.print("C  soc=");    Serial.print(s.soc_pct, 0);
        Serial.print("%  soh=");    Serial.print(s.soh_pct, 0);
        Serial.println("%");

        // ── Accumulate running sums for the hourly summary window ─────────────
        s.summ_v_sum += packV;
        s.summ_i_sum += curA;
        if (!isnan(tempC)) { s.summ_t_sum += tempC; s.summ_t_count++; }
        s.summ_count++;
        s.wakes_since_summ++;

        // ── Evaluate alert thresholds (independent 30-min cooldowns per type) ─
        checkAlerts(s, packV, s.soc_pct, tempC, now);

#if ENABLE_CAN_BMS
        // ── Poll CAN BMS for cell-group voltages and imbalance check ──────────
        pollCanBms(s, now);
#endif

        // ── Emit summary on report-window expiry or wake-count fallback ───────
        // When epoch is known: seed last_summ_epoch on the first timed wake to
        // defer the first summary until one full report interval has elapsed [7].
        // When epoch is unavailable (now == 0): fall back to a wake-count
        // threshold so summaries are not suppressed indefinitely before the first
        // cellular/NTN time sync [5].
        {
            uint32_t wakes_per_report =
                (cfg.report_interval_m * 60UL) / cfg.sample_interval_s;
            if (wakes_per_report < 1) wakes_per_report = 1;

            if (now > 0) {
                if (s.last_summ_epoch == 0) {
                    // First wake with a valid epoch — start the report window
                    // without emitting an under-populated first summary.
                    s.last_summ_epoch  = now;
                    s.wakes_since_summ = 0;
                } else if (now >= s.last_summ_epoch +
                                  cfg.report_interval_m * 60UL) {
                    // Zero wakes_since_summ only when the window is closed
                    // (QUEUED or DISCARDED).  SUMM_RETAINED leaves it intact so
                    // the wake-count stat stays coherent with the open window.
                    if (sendSummary(s, now) != SUMM_RETAINED) {
                        s.wakes_since_summ = 0;
                    }
                }
            } else if (s.wakes_since_summ >= wakes_per_report) {
                // No epoch yet — use accumulated wake count as a proxy for
                // elapsed time and emit the summary anyway.  Only zero
                // wakes_since_summ when the window was actually closed;
                // SUMM_RETAINED keeps the counter intact so the retry fires on
                // the very next wake rather than after another full window.
                if (sendSummary(s, 0) != SUMM_RETAINED) {
                    s.wakes_since_summ = 0;
                }
            }
        }
    }

persist_and_sleep:
    // ── Persist state to Notecard flash and enter deep sleep ─────────────────
    // Notecard drives ATTN low after sample_interval_s seconds, restarting the
    // Cygnet MCU and re-entering setup().  On MCUs without hardware ATTN wake
    // the while(true) fallback holds until the next power cycle.
    {
        NotePayloadDesc outPayload = {0};
        NotePayloadAddSegment(&outPayload, STATE_SEG_ID, &s, sizeof(s));
        NotePayloadSaveAndSleep(&outPayload, cfg.sample_interval_s, NULL);
        NotePayloadFree(&outPayload);
    }
    while (true) { delay(1000); }
}

// ─────────────────────────────────────────────────────────────────────────────
// loop — intentionally empty.  All logic runs in setup() to match the
// card.attn sleep / wake-to-reset pattern used on the Cygnet MCU.
// ─────────────────────────────────────────────────────────────────────────────
void loop(void) {}
