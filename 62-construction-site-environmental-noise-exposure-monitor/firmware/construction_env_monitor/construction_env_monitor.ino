/*******************************************************************************
  construction_env_monitor.ino — Construction Site Environmental & Noise
  Exposure Monitor

  Hardware:
    - Blues Notecarrier CX (onboard STM32L433 host MCU)
    - Blues Notecard Cell+WiFi MBGLW (cellular uplink + built-in GNSS)
    - Adafruit PMSA003I Air Quality Breakout (#4632) on I2C/Qwiic — PM2.5/PM10
    - DFRobot Gravity Analog Sound Level Meter (SEN0232) on A0 — dB(A)
    - SparkFun Sunny Buddy MPPT charger + 6V solar panel + 1200 mAh LiPo
    - Jumper: Notecarrier CX ATTN -> D5 (host wake from STOP2)

  Power strategy:
    - Host sleeps in STM32 STOP2 (~1-2 µA, RAM retained) between sample cycles
      and is woken by the Notecard's ATTN pin (card.attn "sleep", see
      cx_sleep.h).  Execution resumes in place, so AppState simply lives in RAM.
    - Wiring: jumper the Notecarrier CX ATTN pin to D5 (both on the same 16-pin
      header).  Leave EN unconnected — on the CX it enables the shared 3.3 V VIO
      rail, so driving it from ATTN would brown out the board rather than sleep
      the host.
    - Build: Tools > USB support (if available) > None (usb=none).  With the USB
      CDC stack enabled and no USB host attached, the host cannot stay in STOP2.
      Debug output goes to the ST-LINK virtual COM port on the CX debug jack.
    - Default sample interval: 5 minutes.  Default report interval: 30 minutes.
    - The Notecard runs in periodic mode; alerts bypass the queue with sync:true.
    - PM_WARMUP_MS stabilisation delay is applied on every sample cycle after a
      successful begin_I2C().  Putting the host into STOP2 does not switch the
      Qwiic/3V3 rail, so the PMSA003I stays powered between wakes; always
      applying the full warm-up delay keeps readings valid whether the sensor
      was already running or cold-started.  For definitive PM sensor
      power-gating (and acoustic isolation during the sound window), add a
      GPIO-controlled load switch to the PMSA003I power line — see README §9.
      Verify actual system draw with a Mojo current trace (see README §8).
    - The SEN0232 is powered from V+ (raw LiPo voltage), which stays on while
      the host sleeps.  The sensor's op-amp draws a small quiescent current
      (~3-5 mA estimated) continuously; account for this in system power budgets.
    - Sleep duration is trimmed each cycle by the measured active (awake) time
      so that sample starts track the configured interval rather than drifting by
      the ~55 s of warm-up and sensor-read time per cycle.

  Env vars (set in Notehub, picked up on next inbound sync):
    sample_interval_sec   — seconds between samples             (default 300)
    report_interval_min   — minutes between summary notes       (default 30)
    pm25_alert_ug_m3      — PM2.5 alert threshold µg/m³         (default 35)
    pm10_alert_ug_m3      — PM10  alert threshold µg/m³         (default 150)
    db_a_alert            — sound alert threshold dB(A)         (default 85)
    gps_interval_sec      — GPS re-acquire period in seconds    (default 14400)
    db_cal_offset         — dB(A) calibration bias offset       (default 0)

  Source files:
    construction_env_monitor.ino          — global state, setup(), loop()
    construction_env_monitor_helpers.h    — shared constants, types, externs
    construction_env_monitor_helpers.cpp  — sensor, config, and note helpers
    cx_sleep.h                            — STOP2 host sleep with ATTN wake

  THIS FILE IS A STARTING POINT.  Review constants, PRODUCT_UID, and sensor
  calibration before deploying to a production site.
*******************************************************************************/

#include <Wire.h>
#include "construction_env_monitor_helpers.h"
#include "cx_sleep.h"

// ── Global definitions ────────────────────────────────────────────────────────
// Extern declarations for all of these live in construction_env_monitor_helpers.h
// so that the helper functions in helpers.cpp can access them directly.
AppState          state;
Notecard          notecard;
Adafruit_PM25AQI  aqiSensor = Adafruit_PM25AQI();

// Debug output: LPUART1 on the CX debug jack, which an ST-LINK V3 exposes as
// a virtual COM port.  (USB CDC must be disabled for STOP2 to work; see
// cx_sleep.h.)  The DEBUG_SERIAL macro in the helpers header aliases this.
Uart              dbgSerial(PIN_VCP_RX, PIN_VCP_TX);

// Runtime config — refreshed from Notehub env vars on every wake.
uint32_t cfgSampleSec   = DEFAULT_SAMPLE_INTERVAL_SEC;
uint32_t cfgReportMin   = DEFAULT_REPORT_INTERVAL_MIN;
float    cfgPm25Alert   = DEFAULT_PM25_ALERT_UG_M3;
float    cfgPm10Alert   = DEFAULT_PM10_ALERT_UG_M3;
float    cfgDbAlert     = DEFAULT_DB_A_ALERT;
uint32_t cfgGpsSec      = DEFAULT_GPS_INTERVAL_SEC;
float    cfgDbCalOffset = 0.0f;

// ── setup() — runs once at power-up ───────────────────────────────────────────
// The host resumes in place after each STOP2 sleep, so one-time initialisation
// lives here and every per-wake step lives in loop().
void setup() {

#ifdef DEBUG_SERIAL
    DEBUG_SERIAL.begin(115200);
#ifndef NOTE_C_LOW_MEM
    notecard.setDebugOutputStream(DEBUG_SERIAL);
#endif
#endif

    Wire.begin();
    analogReadResolution(12);   // the STM32L433 host supports 12-bit ADC
    notecard.begin();           // I²C interface at default address

    // ── Zero state and configure the Notecard once ────────────────────────
    memset(&state, 0, sizeof(state));
    state.reportCountdown = (uint32_t)DEFAULT_REPORT_INTERVAL_MIN * 60;
    state.gpsCountdown    = 0;   // acquire GPS immediately at power-up

    // lastReportMin = 0 and lastGpsSec = 0 (already zeroed by memset)
    // so applyCardConfig() sends both hub.set and card.location.mode
    // unconditionally on the first wake and retries on any subsequent
    // wake where a previous attempt failed — matching the pattern that
    // ensures cadence config is applied even after a transient I²C error.
    // Both fields are advanced only after a confirmed successful response.
    state.lastReportMin = 0;
    state.lastGpsSec    = 0;

    notecardConfigure();

    // Arm the ATTN wake: the Notecard raises ATTN (D5) to end each sleep.
    cxSleepBegin();
}

// ── loop() — one sample cycle, then sleep until the Notecard raises ATTN ──────
void loop() {
    // ── Refresh configurable thresholds from Notehub env vars ─────────────
    fetchEnvOverrides();

    // ── Apply note templates on every wake (idempotent) ───────────────────
    // Calling note.template on each wake ensures templates are registered even
    // if a transient I²C failure prevented them from being applied on a
    // previous wake.  The Notecard silently ignores a duplicate template
    // definition for an already-registered notefile.
    defineTemplates();

    // ── Re-apply hub outbound / GPS cadence if env vars changed ──────────
    // applyCardConfig() fires hub.set whenever lastReportMin != cfgReportMin
    // (true on the first wake since lastReportMin == 0) and card.location.mode
    // whenever lastGpsSec != cfgGpsSec (also true on the first wake).  Both
    // requests include all required fields and advance state only on success.
    applyCardConfig();

    // ── Run one complete sample / accumulate / alert / report cycle ───────
    // runOneSampleCycle() applies PM_WARMUP_MS after every successful
    // begin_I2C() call — the Qwiic rail stays on while the host sleeps, so
    // the warm-up guarantees valid readings whether the sensor was already
    // running or cold-started (see README §9 for explicit power-gating).
    const uint32_t t0 = millis();
    runOneSampleCycle();
    // Measure how many seconds the host spent awake this cycle and trim the
    // sleep so that the total cycle time (active + sleep) equals cfgSampleSec
    // and sample starts track the configured cadence rather than drifting by
    // ~55 s per cycle.
    const uint32_t activeSec = (millis() - t0 + 500) / 1000UL;
    const uint32_t sleepSec  = (activeSec < cfgSampleSec)
                                ? cfgSampleSec - activeSec : 1;

    // STOP2 until the Notecard raises ATTN sleepSec seconds from now.
    // Execution resumes here; state and the cfg* globals stay in RAM.  If the
    // sleep request fails (check the ATTN -> D5 jumper) sleepUntilAttn() waits
    // out the interval awake instead, so the cadence is preserved either way.
    sleepUntilAttn(sleepSec);
}
