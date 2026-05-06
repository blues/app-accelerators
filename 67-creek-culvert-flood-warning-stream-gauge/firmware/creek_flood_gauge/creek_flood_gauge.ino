/*
 * Creek / Culvert Flood-Warning Stream Gauge
 *
 * Reads a MaxBotix MB7389 HRXL-MaxSonar-WR ultrasonic sensor and a tipping-
 * bucket rain gauge to monitor water level and rainfall at bridges, culverts,
 * and low-water crossings.  Rising-water rate detection means alerts fire on
 * TREND before the culvert is already full, not only when a threshold is
 * breached at a single instant.
 *
 * Hardware:
 *   Blues Notecarrier CX (Cygnet STM32 host)
 *   Blues Notecard for Skylo (NOTE-NBGLWX) — cellular + satellite + WiFi
 *   MaxBotix MB7389 HRXL-MaxSonar-WR  — water level, UART 9600 on Serial1
 *   SparkFun Rain Gauge SEN-28867      — tipping bucket, reed switch on D5
 *
 * THIS FILE SHOULD BE EDITED AFTER GENERATION.
 * IT IS PROVIDED AS A STARTING POINT FOR THE USER TO EDIT AND EXTEND.
 *
 * Sensor drivers, Notecard helpers, and scheduler utilities live in
 * creek_flood_gauge_helpers.h / .cpp to keep this file focused on
 * setup/loop orchestration.  The DEBUG_SERIAL toggle is also in helpers.h.
 */

// ---------------------------------------------------------------------------
// Dependencies
// ---------------------------------------------------------------------------
#include <Notecard.h>
#include "creek_flood_gauge_helpers.h"

// ---------------------------------------------------------------------------
// Project identity — uncomment the line below and replace the placeholder
// with your Notehub ProductUID before building.  The build intentionally
// fails when PRODUCT_UID is left unset so that a reused Notecard cannot
// silently keep routing data to a previous project. For local development
// without a Notehub project yet, add -DALLOW_EMPTY_PRODUCT_UID to the build
// flags as an explicit override — that flag must not appear in a shipping
// build.
// ---------------------------------------------------------------------------
// #define PRODUCT_UID "com.your-company.your-name:creek_gauge"
#ifndef PRODUCT_UID
#  ifndef ALLOW_EMPTY_PRODUCT_UID
#    error "PRODUCT_UID is not set. Define it as your Notehub ProductUID before flashing (e.g. -DPRODUCT_UID='\"com.your-company.your-name:creek_gauge\"'). For local development without a project, add -DALLOW_EMPTY_PRODUCT_UID to suppress this error — that flag must not appear in a shipping build."
#  else
#    define PRODUCT_UID ""
#    pragma message "PRODUCT_UID empty (ALLOW_EMPTY_PRODUCT_UID override active) — device will not associate with any Notehub project"
#  endif
#endif

// ---------------------------------------------------------------------------
// Default sampling parameters (all overridable via Notehub env vars)
// ---------------------------------------------------------------------------
#define DEFAULT_SAMPLE_INTERVAL_SEC     300   // 5 minutes between samples
#define DEFAULT_SUMMARY_INTERVAL_MIN     60   // 1 hour between summary notes
#define DEFAULT_INBOUND_INTERVAL_MIN   1440   // 24 h inbound poll — conserves NTN budget during satellite fallback
#define DEFAULT_LEVEL_WARNING_MM        400   // sensor distance (mm) <= this → warning
#define DEFAULT_LEVEL_CRITICAL_MM       200   // sensor distance (mm) <= this → critical
#define DEFAULT_RATE_WARNING_MM_PER_MIN  20   // rising rate (mm/min) >= this → alert
#define DEFAULT_RAIN_INTENSE_TIPS         2   // tips counted in RAIN_POLL_MS window >= this → alert
                                              // (coarse burst indicator; most tips fall in the 297-second sleep gap)
#define DEFAULT_SENSOR_HEIGHT_MM       1500   // sensor mounting height above dry bed (mm)
#define DEFAULT_ALERT_COOLDOWN_SEC      900   // 15 min global cooldown shared across all alert kinds

// ---------------------------------------------------------------------------
// Runtime configuration — all overridable via Notehub env vars each wake.
// Defined here (non-static) so creek_flood_gauge_helpers.cpp can reference
// them via the extern declarations in creek_flood_gauge_helpers.h.
// ---------------------------------------------------------------------------
uint32_t g_sampleIntervalSec   = DEFAULT_SAMPLE_INTERVAL_SEC;
uint32_t g_summaryIntervalMin  = DEFAULT_SUMMARY_INTERVAL_MIN;
uint32_t g_inboundIntervalMin  = DEFAULT_INBOUND_INTERVAL_MIN;
float    g_levelWarningMm      = DEFAULT_LEVEL_WARNING_MM;
float    g_levelCriticalMm     = DEFAULT_LEVEL_CRITICAL_MM;
float    g_rateWarningMmPerMin = DEFAULT_RATE_WARNING_MM_PER_MIN;
uint32_t g_rainIntenseTips     = DEFAULT_RAIN_INTENSE_TIPS;
float    g_sensorHeightMm      = DEFAULT_SENSOR_HEIGHT_MM;
uint32_t g_alertCooldownSec    = DEFAULT_ALERT_COOLDOWN_SEC;

AppState g_state;
Notecard notecard;

// ===========================================================================
// setup() — called on every wake (fresh boot and post-sleep re-entry)
// ===========================================================================
void setup() {
    DBG_BEGIN(115200);

    // MB7389 on the Notecarrier CX UART header (Cygnet USART1 → RX/TX pins)
    Serial1.begin(MB7389_BAUD);

    // Rain gauge reed switch: normally open, active LOW
    pinMode(PIN_RAIN_GAUGE, INPUT_PULLUP);

    // I2C to Notecard
    notecard.begin();
    DBG_NOTECARD();

    // -----------------------------------------------------------------------
    // Attempt to recover state from Notecard flash (written before last sleep)
    // -----------------------------------------------------------------------
    NotePayloadDesc payload;
    bool recovered = NotePayloadRetrieveAfterSleep(&payload);
    if (recovered) {
        recovered = NotePayloadGetSegment(&payload, STATE_SEG_ID,
                                          &g_state, sizeof(g_state));
        NotePayloadFree(&payload);
    }
    if (!recovered) {
        memset(&g_state, 0, sizeof(g_state));
    }

    // Run first-boot setup when state has not been recovered OR when a prior
    // boot's setup did not fully complete (e.g., Notecard I2C enumeration race).
    // hubConfigure() and defineTemplates() both return bool; we only mark
    // setupDone after both succeed so the next wake retries any failed step.
    if (!recovered || !g_state.setupDone) {
        bool ok = hubConfigure(PRODUCT_UID)
               && defineTemplates()
               && defineEnvTemplate()
               && configureSkyloTransport();
        if (ok) {
#ifdef BENCH_TEST
            // Bench-test only: stop the accelerometer to reduce idle-current
            // noise on scope traces during power-consumption validation.
            // Never define BENCH_TEST in field firmware builds.
            J *req = notecard.newRequest("card.motion.mode");
            JAddBoolToObject(req, "stop", true);
            notecard.sendRequest(req);
#endif
            g_state.setupDone = true;
        }
        // If ok is false, setupDone stays false; loop() will skip sensing and
        // note emission for this wake, then sleep so setup is retried next wake.
    }

    // Verify GNSS location is available for Skylo NTN satellite fallback.
    // The Notecard retains the fix in flash: once locationOk is true it
    // persists across sleep cycles without an extra I2C round-trip each wake.
    // Re-checked while false so a power-cycle before the first outdoor fix
    // does not permanently suppress the diagnostic; the warning in
    // checkLocationAcquired() tells the installer what to do.
    if (g_state.setupDone && !g_state.locationOk) {
        g_state.locationOk = checkLocationAcquired();
    }

    // Pre-load last-good effective config from persisted state so that a
    // transient env.get failure in this wake cycle does not revert thresholds
    // to compile-time defaults.  On the very first boot cfgValid is false
    // (memset above) so globals keep their compile-time initializers.
    if (g_state.cfgValid) {
        g_sampleIntervalSec   = g_state.cfgSampleIntervalSec;
        g_summaryIntervalMin  = g_state.cfgSummaryIntervalMin;
        g_inboundIntervalMin  = g_state.cfgInboundIntervalMin;
        g_levelWarningMm      = g_state.cfgLevelWarningMm;
        g_levelCriticalMm     = g_state.cfgLevelCriticalMm;
        g_rateWarningMmPerMin = g_state.cfgRateWarningMmPerMin;
        g_rainIntenseTips     = g_state.cfgRainIntenseTips;
        g_sensorHeightMm      = g_state.cfgSensorHeightMm;
        g_alertCooldownSec    = g_state.cfgAlertCooldownSec;
    }
}

// ===========================================================================
// loop() — single pass per wake; ends with a sleep call (never returns)
// ===========================================================================
void loop() {
    // 0. Advance wake counter before any time-dependent decisions so the
    //    first wake is counter value 1 (never 0, which is the "never happened"
    //    sentinel for lastAlertWake / lastSummaryWake).
    g_state.wakeCount++;

    // Setup guard: if hubConfigure() or defineTemplates() failed in setup(),
    // skip sensing and note emission for this wake to avoid queuing untemplated
    // notes or scheduling syncs before hub.set has been applied.  State is
    // persisted so the sleep interval is honoured; setup is retried on the
    // next wake.
    if (!g_state.setupDone) {
        sleepUntilNextSample();
        return;
    }

    // 1. Pull any updated env vars from the Notecard; re-applies hub.set if
    //    summary_interval_min changed to keep the radio schedule aligned.
    fetchEnvOverrides();

    // 2. Read water level (average of 3 valid readings)
    float levelSum   = 0.0f;
    int   levelCount = 0;
    for (int i = 0; i < 3; i++) {
        float r = readWaterLevelMm();
        if (r > 0.0f && r < 6000.0f) { levelSum += r; levelCount++; }
    }
    float levelMm      = (levelCount > 0) ? levelSum / levelCount : -1.0f;
    float waterDepthMm = (levelMm > 0.0f) ? g_sensorHeightMm - levelMm : -1.0f;

    // 3. Count rain gauge tips during a fixed observation window
    uint32_t windowTips = countRainTips(RAIN_POLL_MS);
    g_state.totalTips  += windowTips;

    // 4. Fetch Notecard epoch — used for both scheduling and history timestamps
    uint32_t nowSec = notecardTimeSec();

    // 5. Update history and compute rising rate — only when sensor read is valid.
    //    When levelMm < 0 (all three MB7389 reads failed) the rate and trend
    //    fields are set to the -9999 sentinel so downstream analytics can
    //    distinguish sensor failures from real near-zero readings.
    float rateMmPerMin = -9999.0f;  // sentinel: sensor offline or insufficient history
    if (levelMm > 0.0f) {
        updateHistory(levelMm, nowSec);
        if (g_state.historyCount >= 2) {
            rateMmPerMin = calcRisingRateMmPerMin();
        }
        // If historyCount < 2, rateMmPerMin remains -9999.0f so that trend
        // is reported as "unknown" rather than a misleading "stable".
    }

    // 6. Evaluate alert thresholds
    //
    // Cooldown logic uses a two-track approach:
    //   • When the Notecard has a valid epoch (nowSec > 0) and we have a prior
    //     alert epoch (lastAlertSec > 0), compare epoch timestamps directly.
    //   • Otherwise fall back to wake-count arithmetic: wakes × sample interval
    //     provides a conservative elapsed-time estimate without relying on epoch.
    //
    // "neverAlerted" (both epoch and wake markers are 0) always passes the
    // cooldown so the very first alert can fire immediately.
    //
    // Level- and rate-based alerts require a valid sensor reading (levelMm > 0).
    // rain_intense is evaluated independently so it can fire even when the
    // ultrasonic sensor is offline or times out, subject to the same cooldown.
    bool neverAlerted = (g_state.lastAlertSec == 0 && g_state.lastAlertWake == 0);
    bool cooldownOk;
    if (neverAlerted) {
        cooldownOk = true;
    } else if (nowSec > 0 && g_state.lastAlertSec > 0) {
        cooldownOk = (nowSec - g_state.lastAlertSec) >= g_alertCooldownSec;
    } else {
        // Epoch unavailable or was unavailable when the last alert fired:
        // use wake count × sample interval as a conservative time estimate.
        uint32_t wakesElapsed = g_state.wakeCount - g_state.lastAlertWake;
        cooldownOk = (wakesElapsed * g_sampleIntervalSec >= g_alertCooldownSec);
    }

    if (cooldownOk) {
        bool alerted = false;

        // Level- and rate-based alerts: only valid when the ultrasonic sensor
        // returned a good reading.
        if (levelMm > 0.0f) {
            if      (levelMm <= g_levelCriticalMm) {
                alerted = sendAlert("level_critical", levelMm, rateMmPerMin, windowTips, waterDepthMm);
            } else if (levelMm <= g_levelWarningMm) {
                alerted = sendAlert("level_warning",  levelMm, rateMmPerMin, windowTips, waterDepthMm);
            } else if (rateMmPerMin >= g_rateWarningMmPerMin) {
                alerted = sendAlert("rate_rising",    levelMm, rateMmPerMin, windowTips, waterDepthMm);
            }
        }

        // rain_intense fires regardless of water-level sensor state.  It is
        // checked last in priority so that when levelMm is valid, a level or
        // rate alert takes precedence and the cooldown is consumed only once.
        if (!alerted && windowTips >= g_rainIntenseTips) {
            alerted = sendAlert("rain_intense", levelMm, rateMmPerMin, windowTips, waterDepthMm);
        }

        // Advance cooldown markers only after a note was successfully queued.
        if (alerted) {
            g_state.lastAlertSec  = nowSec;   // 0 if no epoch; wake path used next time
            g_state.lastAlertWake = g_state.wakeCount;
        }
    }

    // 7. Periodic summary
    //
    // Same two-track scheduling as alert cooldown.  Guarding the epoch path
    // with lastSummarySec > 0 prevents an underflow if the Notecard acquires
    // time mid-deployment when lastSummarySec is still 0 from before the
    // first successful sync (an epoch difference of ~1.7 billion would falsely
    // indicate a summary was immediately due).
    bool neverSummarized = (g_state.lastSummarySec == 0 && g_state.lastSummaryWake == 0);
    bool summaryDue;
    if (neverSummarized) {
        // Count wakes from boot rather than comparing against a zero anchor,
        // so the first summary fires after one full interval, not immediately.
        summaryDue = (g_state.wakeCount * g_sampleIntervalSec >= g_summaryIntervalMin * 60u);
    } else if (nowSec > 0 && g_state.lastSummarySec > 0) {
        summaryDue = (nowSec - g_state.lastSummarySec) >= (g_summaryIntervalMin * 60u);
    } else {
        uint32_t wakesElapsed = g_state.wakeCount - g_state.lastSummaryWake;
        summaryDue = (wakesElapsed * g_sampleIntervalSec >= g_summaryIntervalMin * 60u);
    }
    if (summaryDue) {
        bool sent = sendSummary(levelMm, rateMmPerMin, windowTips, waterDepthMm);
        if (sent) {
            g_state.lastSummarySec  = nowSec;
            g_state.lastSummaryWake = g_state.wakeCount;
        }
    }

    // 8. Persist state and sleep until next sample
    sleepUntilNextSample();
}
