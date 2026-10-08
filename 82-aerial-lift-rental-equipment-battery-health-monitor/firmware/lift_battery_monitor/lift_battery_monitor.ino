/***************************************************************************
  lift_battery_monitor.ino — Aerial Lift / Rental Equipment Battery Health
  Monitor on Blues Notecarrier CX + Notecard for Skylo (NOTE-NBGLWX).

  Hardware:
    • Notecarrier CX (onboard STM32L433 host MCU)
    • Notecard for Skylo NOTE-NBGLWX (LTE-M / NB-IoT / WiFi / Skylo NTN fallback)
    • Adafruit INA228 high-voltage I²C power monitor (#5832) on Qwiic bus
    • 10 kΩ NTC thermistor on A0 (β = 3950, series 10 kΩ pull-up to 3.3 V)
    • External precision shunt (50–200 A, 50–75 mV FS) on INA228 VIN+/VIN− — primary field current path
      (update DEFAULT_SHUNT_MOHM / DEFAULT_SHUNT_MAX_A below to match the installed shunt)
    • Jumper: Notecarrier CX ATTN → D6 (host wake from STOP2; D5 is reserved
      for the optional CAN module's chip select)
    • Blues Mojo (LTC2959 coulomb counter) optional bench power validator via Qwiic
    • MCP2515 SPI CAN module optional — set ENABLE_CAN_BMS 1 in lift_battery_monitor_config.h to enable

  Sleep / wake pattern:
    setup() runs once: Notecard, sensors, and the ATTN wake are initialised
    there.  loop() runs one sample cycle, then asks the Notecard to hold ATTN
    low for sample_interval_s seconds (card.attn "sleep") and puts the host in
    STM32 STOP2 (~1-2 µA, RAM retained).  The ATTN rising edge wakes the host
    and execution resumes in place, so all state simply lives in RAM.

  Wiring for sleep:
    Jumper the Notecarrier CX ATTN pin to D6 (both on the same 16-pin header).
    Leave EN unconnected — on the CX it enables the shared 3.3 V VIO rail, so
    driving it from ATTN browns out the whole board instead of sleeping the
    host.  See cx_sleep.h.

  Build:
    Tools > USB support (if available) > None (usb=none).  With the USB CDC
    stack enabled and no USB host attached, the host cannot stay in STOP2.
    Debug output goes to the LPUART on the CX debug jack, which an ST-LINK V3
    exposes as a virtual COM port (dbgSerial below).

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

// ATTN wake pin.  D5 is the MCP2515 chip select when ENABLE_CAN_BMS is set,
// so the ATTN jumper lands on D6 instead (must be defined before cx_sleep.h).
#define CX_ATTN_PIN D6
#include "cx_sleep.h"

#if ENABLE_CAN_BMS
#include <SPI.h>
#include <mcp2515.h>
#endif

// ─── Notehub product UID ──────────────────────────────────────────────────────
// Replace with your Notehub project UID before first flash.
// Create a free project at https://notehub.io.
#define PRODUCT_UID  "com.your-company:your-project"

// ─── CAN BMS hardware parameters ──────────────────────────────────────────────
// PIN_CAN_CS, BMS_CELL_COUNT, and BMS_CELL_GROUP_ID are defined in
// lift_battery_monitor_config.h so both this translation unit and
// lift_battery_monitor_helpers.cpp see identical values — edit them there.

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

// Debug output: LPUART1 on the CX debug jack (ST-LINK V3 virtual COM port).
// USB CDC is disabled in this build, so Serial is not available; see cx_sleep.h.
Uart dbgSerial(PIN_VCP_RX, PIN_VCP_TX);

#if ENABLE_CAN_BMS
MCP2515 mcp2515(PIN_CAN_CS);
float   gCellMv[BMS_CELL_COUNT];
bool    gCanOk = false;
#endif

// Application state.  Lives in RAM; STOP2 retains SRAM, so it survives every
// sleep/wake cycle and is reset only by a power cycle (which re-runs setup()).
static AppState s;

// True for the first pass through loop() after power-up; drives the one-time
// Notecard configuration (what the old cold-boot path did).
static bool g_first_wake = true;

// True once ina228.begin() has succeeded; retried on each wake until it does.
static bool g_ina_ok = false;

// ─────────────────────────────────────────────────────────────────────────────
// initIna228 — find the INA228 on the Qwiic bus and calibrate its shunt path.
// ─────────────────────────────────────────────────────────────────────────────
static bool initIna228(void) {
    if (!ina228.begin()) {
        dbgSerial.println("[error] INA228 not found — check Qwiic / I2C wiring");
        return false;
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
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// setup — runs once at power-up.  The host resumes in place after each STOP2
// sleep, so one-time initialisation lives here and every per-wake step lives
// in loop() / runCycle().
// ─────────────────────────────────────────────────────────────────────────────
void setup(void) {
    dbgSerial.begin(115200);

#if BENCH_ONLY && !ENABLE_ACS758
    // Belt-and-suspenders runtime guard: makes a bench image unmistakable even
    // if the compile-time #warning was missed or the binary was shared without
    // its build flags.
    dbgSerial.println("*** BENCH_ONLY BUILD — INA228 current path active (max ~10 A). "
                      "NOT safe for field deployment. ***");
#endif

    Wire.begin();
    analogReadResolution(12);        // 12-bit ADC on the STM32L433 host (ADC_COUNTS = 4096)
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

    // ── Seed the application state ────────────────────────────────────────────
    memset(&s, 0, sizeof(s));
    s.soc_pct         = 100.0f;
    s.soh_pct         = 100.0f;
    s.measured_cap_ah = 0.0f;

    // ── Initialise Adafruit INA228 power monitor ──────────────────────────────
    // On failure runCycle() retries on each wake before sampling.
    g_ina_ok = initIna228();

#if ENABLE_CAN_BMS
    // ── Initialise MCP2515 CAN controller ────────────────────────────────────
    // SPI.begin() must be called before touching the MCP2515; the autowp
    // mcp2515 library does not initialise the SPI peripheral itself.
    SPI.begin();
    mcp2515.reset();
    mcp2515.setBitrate(CAN_250KBPS, MCP_8MHZ);
    mcp2515.setNormalMode();
    dbgSerial.println("[can] MCP2515 ready at 250 kbps");
#endif

    // Arm the ATTN wake: the Notecard raises ATTN (D6) to end each sleep.
    cxSleepBegin();
}

// ─────────────────────────────────────────────────────────────────────────────
// runCycle — one wake cycle: env vars, Notecard configuration retry, sample,
// alerts, summary.  Returns early on anything that should skip this sample;
// loop() sleeps afterwards either way.
// ─────────────────────────────────────────────────────────────────────────────
static void runCycle(void) {
    bool first_wake = g_first_wake;
    g_first_wake = false;

    // Pull any updated environment variables from Notehub.  Uses the cached
    // env body from the last inbound sync if currently offline; silently
    // skips on the first wake after power-up (no cached env body yet).
    fetchEnvOverrides(s);

    if (first_wake || cfg.report_interval_m != s.last_applied_report_m) {
        // Register the hub connection and compact templates on the first wake,
        // and again whenever report_interval_m changes via a Notehub fleet/
        // device env var so the Notecard outbound cadence matches the updated
        // value without waiting for a reboot.  Compare against
        // s.last_applied_report_m (held in RAM), not the firmware default, so
        // this fires only once per actual change.  It also covers the retry
        // path: last_applied_report_m is written to a non-zero value only
        // after BOTH notecardConfigure and defineTemplates succeed, so a
        // failed first wake re-enters this branch on the next wake.
        if (first_wake) {
            dbgSerial.println("[boot] first wake — configuring Notecard");
        } else {
            dbgSerial.print("[cfg] report_interval_m ");
            dbgSerial.print(s.last_applied_report_m);
            dbgSerial.print(" -> ");
            dbgSerial.print(cfg.report_interval_m);
            dbgSerial.println(" min — reapplying hub.set");
        }
        if (notecardConfigure(PRODUCT_UID)) {
            if (defineTemplates()) {
                s.last_applied_report_m = cfg.report_interval_m;
            } else {
                // last_applied_report_m unchanged → retries on next wake.
                dbgSerial.println("[error] template definition failed — "
                                  "will retry on next wake");
            }
        } else {
            // hub.set failed (bad PRODUCT_UID, Notecard not ready, etc.).
            // last_applied_report_m unchanged → retries on next wake.
            dbgSerial.println("[error] Notecard configuration failed — "
                              "sleeping and retrying on next wake");
            return;
        }
    }

    // ── Safety gate: only sample when templates have been confirmed ───────────
    // Proceeding while last_applied_report_m is still 0 would cause note.add
    // calls to fall back to uncompressed JSON on battery_status.qo and
    // battery_alert.qo, breaking the compact/satellite data-budget strategy
    // and the Skylo note-size contract.
    if (s.last_applied_report_m == 0) {
        dbgSerial.println("[error] Notecard templates not yet confirmed — "
                          "skipping sample cycle until templates are defined");
        return;
    }

    // ── Retry INA228 init if it failed at power-up ────────────────────────────
    if (!g_ina_ok) {
        g_ina_ok = initIna228();
        if (!g_ina_ok) return;       // sleep; the next wake will retry init
    }

    // ── Get current epoch from Notecard ──────────────────────────────────────
    // now == 0 means the Notecard clock is not yet set (no cellular fix).
    // Retry up to 3 times so a transient I²C stall does not silently drop the
    // epoch and push the device into the no-epoch path.
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
        dbgSerial.println("[warn] INA228 read out-of-range — skipping sample");
        return;
    }

    // ── Update SoC, cycle Ah throughput, and rolling SoH ─────────────────
    s.soc_pct = voltageToSoC(packV, cfg.is_lithium);
    updateThroughput(s, curA);
    updateSoH(s, s.soc_pct);

    dbgSerial.print("[meas] v=");  dbgSerial.print(packV, 2);
    dbgSerial.print("V  i=");      dbgSerial.print(curA, 2);
    // -9999.0 printed for missing/invalid thermistor to match the outbound sentinel.
    dbgSerial.print("A  t=");      dbgSerial.print(isnan(tempC) ? -9999.0f : tempC, 1);
    dbgSerial.print("C  soc=");    dbgSerial.print(s.soc_pct, 0);
    dbgSerial.print("%  soh=");    dbgSerial.print(s.soh_pct, 0);
    dbgSerial.println("%");

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

// ─────────────────────────────────────────────────────────────────────────────
// loop — one wake cycle, then STOP2 until the Notecard raises ATTN
// sample_interval_s seconds from now.
// ─────────────────────────────────────────────────────────────────────────────
void loop(void) {
    runCycle();

    if (!cxSleepUntilAttn(notecard, cfg.sample_interval_s, NULL, &dbgSerial)) {
        // The Notecard didn't take the sleep request, or ATTN never went low
        // (check the ATTN -> D6 jumper).  Keep the sample cadence and try again.
        dbgSerial.println("[sleep] ATTN sleep failed — waiting out the interval awake");
        delay(cfg.sample_interval_s * 1000UL);
    }
}
