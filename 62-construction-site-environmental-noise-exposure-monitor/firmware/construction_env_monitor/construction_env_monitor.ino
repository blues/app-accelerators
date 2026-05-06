/*******************************************************************************
  construction_env_monitor.ino — Construction Site Environmental & Noise
  Exposure Monitor

  Hardware:
    - Blues Notecarrier CX (Cygnet STM32 host MCU)
    - Blues Notecard Cell+WiFi MBGLW (cellular uplink + built-in GNSS)
    - Adafruit PMSA003I Air Quality Breakout (#4632) on I2C/Qwiic — PM2.5/PM10
    - DFRobot Gravity Analog Sound Level Meter (SEN0232) on A0 — dB(A)
    - SparkFun Sunny Buddy MPPT charger + 6V solar panel + 1200 mAh LiPo

  Power strategy:
    - Host sleeps between sample cycles via NotePayloadSaveAndSleep / card.attn
    - Default sample interval: 5 minutes.  Default report interval: 30 minutes.
    - The Notecard runs in periodic mode; alerts bypass the queue with sync:true.
    - PM_WARMUP_MS stabilisation delay is applied on every sample cycle after a
      successful begin_I2C().  Whether the Notecarrier CX ATTN sleep path cuts
      the Qwiic/3V3 rail between wakes is carrier-implementation-specific and
      not guaranteed.  Always applying the full warm-up delay is simpler and
      keeps readings valid regardless of the carrier's power topology.  For
      definitive PM sensor power-gating (and acoustic isolation during the sound
      window), add a GPIO-controlled load switch to the PMSA003I power line —
      see README §9.  Verify actual system draw with a Mojo current trace (see
      README §8).
    - The SEN0232 is powered from V+ (raw LiPo voltage), which is NOT gated by
      the ATTN sleep path.  The sensor's op-amp draws a small quiescent current
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

  THIS FILE IS A STARTING POINT.  Review constants, PRODUCT_UID, and sensor
  calibration before deploying to a production site.
*******************************************************************************/

#include <Wire.h>
#include "construction_env_monitor_helpers.h"

// ── Global definitions ────────────────────────────────────────────────────────
// Extern declarations for all of these live in construction_env_monitor_helpers.h
// so that the helper functions in helpers.cpp can access them directly.
AppState          state;
Notecard          notecard;
Adafruit_PM25AQI  aqiSensor = Adafruit_PM25AQI();

// Runtime config — refreshed from Notehub env vars on every wake.
uint32_t cfgSampleSec   = DEFAULT_SAMPLE_INTERVAL_SEC;
uint32_t cfgReportMin   = DEFAULT_REPORT_INTERVAL_MIN;
float    cfgPm25Alert   = DEFAULT_PM25_ALERT_UG_M3;
float    cfgPm10Alert   = DEFAULT_PM10_ALERT_UG_M3;
float    cfgDbAlert     = DEFAULT_DB_A_ALERT;
uint32_t cfgGpsSec      = DEFAULT_GPS_INTERVAL_SEC;
float    cfgDbCalOffset = 0.0f;

// Active-time tracking (not persisted; re-initialised each boot).
// Stores how many seconds the host was awake during the most recent sample
// cycle.  saveStateAndSleep() subtracts this from cfgSampleSec so that the
// total cycle time (active + sleep) equals cfgSampleSec and sample starts
// track the configured cadence rather than drifting by ~55 s per cycle.
static uint32_t g_lastActiveSec = 0;

// ── setup / main application entry point ─────────────────────────────────────
void setup() {

#ifdef DEBUG_SERIAL
    DEBUG_SERIAL.begin(115200);
    const uint32_t dbgStart = millis();
    while (!DEBUG_SERIAL && (millis() - dbgStart) < 3000) {}
#ifndef NOTE_C_LOW_MEM
    notecard.setDebugOutputStream(DEBUG_SERIAL);
#endif
#endif

    Wire.begin();
    analogReadResolution(12);   // Cygnet STM32 supports 12-bit ADC
    notecard.begin();           // I²C interface at default address

    // ── Restore state from previous sleep cycle ───────────────────────────
    // NotePayloadRetrieveAfterSleep returns true when waking from a
    // NotePayloadSaveAndSleep-triggered sleep; false on clean power-on.
    NotePayloadDesc payload;
    bool resumed = NotePayloadRetrieveAfterSleep(&payload);
    if (resumed) {
        resumed = NotePayloadGetSegment(&payload, STATE_SEG_ID,
                                        &state, sizeof(state));
        NotePayloadFree(&payload);
    }

    if (!resumed) {
        // First boot — zero state and configure the Notecard once.
        memset(&state, 0, sizeof(state));
        state.reportCountdown = (uint32_t)DEFAULT_REPORT_INTERVAL_MIN * 60;
        state.gpsCountdown    = 0;   // acquire GPS immediately on first boot

        // lastReportMin = 0 and lastGpsSec = 0 (already zeroed by memset)
        // so applyCardConfig() sends both hub.set and card.location.mode
        // unconditionally on the first wake and retries on any subsequent
        // wake where a previous attempt failed — matching the pattern that
        // ensures cadence config is applied even after a transient I²C error.
        // Both fields are advanced only after a confirmed successful response.
        state.lastReportMin = 0;
        state.lastGpsSec    = 0;

        notecardConfigure();
    }

    // ── Refresh configurable thresholds from Notehub env vars ─────────────
    fetchEnvOverrides();

    // ── Apply note templates on every wake (idempotent) ───────────────────
    // Calling note.template on each wake ensures templates are registered even
    // if a transient I²C failure prevented them from being applied on a
    // previous boot.  The Notecard silently ignores a duplicate template
    // definition for an already-registered notefile.
    defineTemplates();

    // ── Re-apply hub outbound / GPS cadence if env vars changed ──────────
    // applyCardConfig() fires hub.set whenever lastReportMin != cfgReportMin
    // (true on first boot since lastReportMin == 0) and card.location.mode
    // whenever lastGpsSec != cfgGpsSec (also true on first boot).  Both
    // requests include all required fields and advance state only on success.
    applyCardConfig();

    // ── Run one complete sample / accumulate / alert / report cycle ───────
    // runOneSampleCycle() applies PM_WARMUP_MS after every successful
    // begin_I2C() call — the warm-up is required regardless of whether
    // the ATTN sleep path power-cycled the sensor (carrier behaviour is
    // not guaranteed; see README §9 for explicit power-gating options).
    const uint32_t t0 = millis();
    runOneSampleCycle();
    // Record how many seconds the host spent awake this cycle so loop() can
    // trim the sleep duration and keep sample starts on the configured cadence.
    g_lastActiveSec = (millis() - t0 + 500) / 1000UL;

    // Compute trimmed sleep and request host power-off via card.attn.
    const uint32_t sleepSec = (g_lastActiveSec < cfgSampleSec)
                               ? cfgSampleSec - g_lastActiveSec : 1;
    saveStateAndSleep(sleepSec);
}

// loop() is reached only if NotePayloadSaveAndSleep() does not cut host power
// (ATTN not wired, bench testing, or a power-path fault).  Rather than idling
// forever, trim the software-delay sleep by the measured active time (matching
// the ATTN-path behaviour), re-apply any changed config, and run another full
// cycle so non-ATTN deployments still produce periodic data.
void loop() {
    // Compute the remaining sleep time: configured interval minus the seconds
    // already spent awake reading sensors this cycle.  This keeps sample starts
    // on the cfgSampleSec cadence regardless of warm-up and sensor-read time.
    const uint32_t sleepSec = (g_lastActiveSec < cfgSampleSec)
                               ? cfgSampleSec - g_lastActiveSec : 1;

    // ── Fallback: ATTN path absent or power rail not cut ──────────────────
    // NotePayloadSaveAndSleep returned without cutting power; use delay() for
    // the remaining interval before running the next full sample cycle.
    // PM_WARMUP_MS is applied by runOneSampleCycle() on every cycle regardless,
    // so no special handling is needed here for the sensor warm-up.
    delay(sleepSec * 1000UL);

    fetchEnvOverrides();
    applyCardConfig();

    const uint32_t t0 = millis();
    runOneSampleCycle();
    g_lastActiveSec = (millis() - t0 + 500) / 1000UL;

    const uint32_t nextSleep = (g_lastActiveSec < cfgSampleSec)
                                ? cfgSampleSec - g_lastActiveSec : 1;
    saveStateAndSleep(nextSleep);
}
