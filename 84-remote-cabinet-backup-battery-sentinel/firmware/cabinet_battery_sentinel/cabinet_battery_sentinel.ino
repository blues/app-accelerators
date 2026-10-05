// ============================================================================
// cabinet_battery_sentinel.ino
//
// Remote Cabinet Backup Battery Sentinel — Blues Application Accelerator #84
// Pack-Level 12 V / 24 V Battery Monitor
//
// Monitors a VRLA (valve-regulated lead-acid) or LFP (lithium iron phosphate)
// backup battery inside a traffic-signal controller cabinet, roadside IoT
// gateway, industrial RTU, or equipment shelter — installations with a
// 12 V or 24 V positive-referenced DC bus.
//
// Reports pack voltage, bidirectional float current, derived power (V×I), net
// charge balance, state-of-charge estimate, and surface temperature over
// cellular — independently of the site equipment it protects.
// Pack-level sensing only: per-cell voltage monitoring and cell-imbalance
// detection are outside the scope of this design; see §9 of the README.
// SoC is tracked via coulomb counting; commission usable_capacity_ah and
// soc_pct_init via Notehub environment variables before relying on soc_pct.
//
// Six threshold rules run on the device:
//   float_voltage_low   — battery can't reach or hold float voltage
//   float_voltage_high  — charger overvoltage fault
//   float_current_high  — elevated float current (sulfation / internal short)
//   temp_high           — elevated pack surface temperature
//   power_outage        — current reversal: site is running on battery
//   soc_low             — state-of-charge below configured threshold
//
// Hardware:
//   Blues Notecarrier CX (onboard STM32L433 host, onboard LiPo charger)
//   Jumper: Notecarrier CX ATTN → D5 (host wake from STOP2; leave EN unconnected)
//   Blues Notecard Cell+WiFi (MBGLW)
//   Adafruit INA228 Power Monitor Breakout (#5832) on Qwiic / I2C
//   10 kΩ NTC thermistor (β=3950, waterproof probe) on A0
//   3.7 V 2000 mAh LiPo battery on JST PH connector
//
// Notefiles:
//   battery_summary.qo — template-backed hourly telemetry (periodic outbound)
//   battery_alert.qo   — immediate alert (sync:true, bypasses outbound timer)
//
// Sleep / wake:
//   setup() runs once.  loop() runs one sample cycle, then asks the Notecard to
//   hold ATTN low for g_sampleSec seconds (card.attn "sleep") and puts the host
//   in STM32 STOP2 (~1-2 µA, RAM retained).  The ATTN rising edge wakes the
//   host and execution resumes in place, so SentinelState simply lives in RAM.
//   Jumper the Notecarrier CX ATTN pin to D5 (same 16-pin header).  Leave EN
//   unconnected — on the CX it enables the shared 3.3 V VIO rail, so driving
//   it from ATTN browns out the whole board instead of sleeping the host.
//   See cx_sleep.h.
//
// Build:
//   Tools > USB support (if available) > None (usb=none).  With the USB CDC
//   stack enabled and no USB host attached, the host cannot stay in STOP2.
//   Debug output goes to the LPUART on the CX debug jack, which an ST-LINK V3
//   exposes as a virtual COM port (dbgSerial below).
//
// All shared constants, types, extern globals, and helper prototypes are in
// cabinet_battery_sentinel_helpers.h; implementations are in
// cabinet_battery_sentinel_helpers.cpp.
// ============================================================================

#include <Wire.h>   // explicit: Wire.begin() is called in setup()
#include "cabinet_battery_sentinel_helpers.h"
#include "cx_sleep.h"

// ─── Globals ─────────────────────────────────────────────────────────────────
Notecard        notecard;
Adafruit_INA228 ina228;

// Debug output: LPUART1 on the CX debug jack (ST-LINK V3 virtual COM port).
// USB CDC is disabled in this build, so Serial is not available; see cx_sleep.h.
Uart dbgSerial(PIN_VCP_RX, PIN_VCP_TX);

// Runtime configuration — re-populated from Notehub env vars on every wake.
uint32_t g_sampleSec        = DEFAULT_SAMPLE_INTERVAL_SEC;
uint32_t g_summaryMin       = DEFAULT_SUMMARY_INTERVAL_MIN;
float    g_voltMinV         = DEFAULT_VOLT_MIN_V;
float    g_voltMaxV         = DEFAULT_VOLT_MAX_V;
float    g_floatCurrHiMa    = DEFAULT_FLOAT_CURR_HI_MA;
float    g_tempAlertC       = DEFAULT_TEMP_ALERT_C;
float    g_dischargeMa      = DEFAULT_DISCHARGE_MA;
float    g_usableCapacityAh = DEFAULT_USABLE_CAPACITY_AH;
float    g_socLowPct        = DEFAULT_SOC_LOW_PCT;
float    g_socPctInit       = DEFAULT_SOC_PCT_INIT;

SentinelState state;

// True once initINA228() has succeeded; retried on each wake until it does.
static bool g_inaOk = false;

// Forward declaration — one sample cycle, defined below.
static void runCycle(void);

// ─── setup() — runs once at power-up ─────────────────────────────────────────
// The host resumes in place after each STOP2 sleep, so one-time initialisation
// lives here and every per-wake step lives in loop() / runCycle().
void setup() {
    dbgSerial.begin(115200);

    // Explicit I2C initialisation before any I2C device (Notecard or INA228).
    // On STM32 targets, leaving this implicit is a bring-up risk.
    Wire.begin();

    notecard.begin();               // I2C
    notecard.setDebugOutputStream(dbgSerial);

    // 12-bit ADC resolution on the STM32L433 host (default is 10-bit).
    analogReadResolution(12);

    // ── PRODUCT_UID runtime guard ─────────────────────────────────────────
    // An empty ProductUID means the device will never associate with a Notehub
    // project — it appears healthy locally while all outbound data is silently
    // discarded.  runCycle() halts normal telemetry until the firmware is
    // reflashed with a real ProductUID so first-light failures are immediately
    // obvious.
    if (PRODUCT_UID[0] == '\0') {
        dbgSerial.println("[sentinel] ERROR: PRODUCT_UID is empty. "
                          "Set PRODUCT_UID in cabinet_battery_sentinel_helpers.h "
                          "and reflash before deploying.");
    } else {
        // Clean state plus one-time Notecard configuration (hub.set, template,
        // accelerometer off).  Anything that fails here is retried from
        // runCycle() on every wake until confirmed.
        doFirstBoot();
    }

    // ── Initialise sensors ────────────────────────────────────────────────
    // The Qwiic rail stays powered while the host sleeps, so the INA228 keeps
    // its calibration; initialise once and retry from runCycle() only on failure.
    g_inaOk = initINA228();

    // Arm the ATTN wake: the Notecard raises ATTN (D5) to end each sleep.
    cxSleepBegin();
}

// ─── loop() — one sample cycle, then STOP2 until the Notecard raises ATTN ────
void loop() {
    runCycle();

    if (!cxSleepUntilAttn(notecard, g_sampleSec, NULL, &dbgSerial)) {
        // The Notecard didn't take the sleep request, or ATTN never went low
        // (check the ATTN -> D5 jumper).  Keep the sample cadence and try again.
        dbgSerial.println("[sentinel] ATTN sleep failed — waiting out the interval awake");
        delay(g_sampleSec * 1000UL);
    }
}

// ─── runCycle() — one wake: env vars, config retry, sample, alerts, summary ──
static void runCycle(void) {
    // Empty ProductUID: keep sleeping on the sample cadence, no telemetry.
    if (PRODUCT_UID[0] == '\0') {
        dbgSerial.println("[sentinel] PRODUCT_UID is empty — telemetry halted.");
        return;
    }

    // ── Fetch environment variable overrides (every wake) ────────────────
    // Thresholds or cadence changed in Notehub will take effect on the next
    // inbound sync without requiring a firmware reflash.
    fetchEnvOverrides();

    // ── Retry hub configuration if the power-up attempt failed ───────────
    // hub.set is retried with the full parameter set (including ProductUID) on
    // every wake until confirmed delivered, so a transient I2C or Notecard-
    // readiness fault at power-up cannot leave the device permanently
    // unassociated.  Once the device is configured, only re-send hub.set when
    // the operator changes summary_interval_min from Notehub.
    if (!state.hubConfigured) {
        J *req = notecard.newRequest("hub.set");
        JAddStringToObject(req, "product",  PRODUCT_UID);
        JAddStringToObject(req, "mode",     "periodic");
        JAddNumberToObject(req, "outbound", (int)g_summaryMin);
        JAddNumberToObject(req, "inbound",  120);
        if (notecard.sendRequestWithRetry(req, 5)) {
            state.hubConfigured  = true;
            state.lastSummaryMin = g_summaryMin;
        } else {
            notecard.logDebug("hub.set retry failed — will retry next wake\n");
        }
    } else if (g_summaryMin != state.lastSummaryMin) {
        // Re-apply the outbound cadence when the operator changes it from
        // Notehub.  Also reset the local summary window so the next
        // battery_summary.qo covers exactly the new interval.
        J *req = notecard.newRequest("hub.set");
        JAddStringToObject(req, "mode",     "periodic");
        JAddNumberToObject(req, "outbound", (int)g_summaryMin);
        if (notecard.sendRequest(req)) {
            state.lastSummaryMin   = g_summaryMin;
            state.voltSum   = state.currSum   = state.tempSum = 0.0f;
            state.voltCount = state.currCount = state.tempCount = 0;
            state.chargeAh  = 0.0f;
            state.voltMin   = VOLT_MIN_INIT;
            state.currMin   = CURR_MIN_INIT;
            state.tempMax   = TEMP_MAX_INIT;
            state.windowElapsedSec = 0;
            notecard.logDebug("summary_interval_min changed — summary window reset\n");
        } else {
            notecard.logDebug("hub.set re-apply failed — will retry next wake\n");
        }
    }

    // ── Retry Note template registration if the power-up attempt failed ──
    if (!state.templateDefined) {
        defineTemplate();
    }

    // ── Reset full summary window if sample cadence changed ──────────────
    // Resetting only windowElapsedSec would leave sums and the charge
    // integrator populated with samples taken under the old cadence.
    if (g_sampleSec != state.lastSampleSec) {
        state.voltSum   = state.currSum   = state.tempSum = 0.0f;
        state.voltCount = state.currCount = state.tempCount = 0;
        state.chargeAh  = 0.0f;
        state.voltMin   = VOLT_MIN_INIT;
        state.currMin   = CURR_MIN_INIT;
        state.tempMax   = TEMP_MAX_INIT;
        state.windowElapsedSec = 0;
        state.lastSampleSec    = g_sampleSec;
        notecard.logDebug("Sample cadence changed — full summary window reset\n");
    }

    // ── Retry INA228 init if it failed at power-up ────────────────────────
    if (!g_inaOk) {
        g_inaOk = initINA228();
    }
    const bool inaOk = g_inaOk;

    // ── Read sensors ─────────────────────────────────────────────────────
    float volt = inaOk ? readBatteryVoltage() : NAN;
    float curr = inaOk ? readBatteryCurrent() : NAN;
    float temp = readPackTempC();

    // ── Bring-up diagnostics (ST-LINK virtual COM port at 115200 baud) ───
    if (inaOk) {
        dbgSerial.print("[sentinel] volt=");
        dbgSerial.print(volt, 3);
        dbgSerial.print("V  curr=");
        dbgSerial.print(curr, 1);
        dbgSerial.println("mA");
    } else {
        dbgSerial.println("[sentinel] INA228 FAIL — check Qwiic wiring.");
    }
    if (!isnan(temp)) {
        dbgSerial.print("[sentinel] temp=");
        dbgSerial.print(temp, 1);
        dbgSerial.println("C");
    } else {
        dbgSerial.println("[sentinel] NTC open/shorted — check probe wiring.");
    }

    // ── Tick down per-alert cooldowns (time-based) ────────────────────────
    // Decrement by the configured sample interval so the suppression duration
    // is independent of sampling cadence.
#define TICK_DOWN(field) \
    state.field = (state.field > g_sampleSec) ? state.field - g_sampleSec : 0u
    TICK_DOWN(coolVoltLowSec);
    TICK_DOWN(coolVoltHighSec);
    TICK_DOWN(coolCurrHighSec);
    TICK_DOWN(coolTempHighSec);
    TICK_DOWN(coolOutageSec);
    TICK_DOWN(coolInaFaultSec);
    TICK_DOWN(coolSocLowSec);
#undef TICK_DOWN

    // ── INA228 persistent-fault remote notification ───────────────────────
    // A serial-only log is invisible in the field.  Rate-limited to
    // ALERT_COOLDOWN_SEC so a sustained hardware fault generates one alert per
    // 30-minute window rather than flooding battery_alert.qo every sample.
    // Cooldown is only armed when note.add is accepted — a transient I2C fault
    // at alert time must not suppress the alert for 30 minutes without queuing.
    if (!inaOk && state.coolInaFaultSec == 0) {
        if (sendAlert("ina228_unreachable", NAN, NAN, temp)) {
            state.coolInaFaultSec = ALERT_COOLDOWN_SEC;
        }
    }

    // ── Per-metric accumulation ───────────────────────────────────────────
    // Each metric's sum and count increment only when that reading is valid.
    // A failed thermistor probe never suppresses voltage/current accumulation
    // or alert evaluation; an INA228 fault never biases temperature averages.
    if (!isnan(volt)) {
        state.voltSum += volt;
        state.voltCount++;
        if (volt < state.voltMin) state.voltMin = volt;
    }
    if (!isnan(curr)) {
        state.currSum += curr;
        state.currCount++;
        if (curr < state.currMin) state.currMin = curr;  // track deepest discharge
        // Single-point charge integration.
        // Positive current = charger supplying float current (battery gaining charge).
        // Negative current = battery discharging (power outage event).
        state.chargeAh += (curr / 1000.0f) * ((float)g_sampleSec / 3600.0f);
    }
    if (!isnan(temp)) {
        state.tempSum += temp;
        state.tempCount++;
        if (temp > state.tempMax) state.tempMax = temp;
    }

    // ── State-of-charge estimation (coulomb counting) ────────────────────
    // If the operator has changed soc_pct_init in Notehub, apply it once as
    // the new SoC baseline.  Detecting the *change* (vs. lastSocInit) means a
    // persistent env var does not re-initialise SoC on every wake — only when
    // the operator explicitly updates the value to recalibrate.
    if (g_socPctInit >= 0.0f && g_socPctInit != state.lastSocInit) {
        state.socPct      = g_socPctInit;
        state.lastSocInit = g_socPctInit;
    }
    // Update SoC by integrating the current sample.  Only runs after the SoC
    // has been commissioned (socPct >= 0) and a valid usable capacity is set.
    // Positive current (float charge) increases SoC; negative (discharge) lowers it.
    // Clamped to [0, 110] — the 10 % headroom above full accommodates the brief
    // overcharge current pulse seen immediately after a deep-discharge recovery.
    if (!isnan(curr) && state.socPct >= 0.0f && g_usableCapacityAh > 0.0f) {
        float deltaAh = (curr / 1000.0f) * ((float)g_sampleSec / 3600.0f);
        state.socPct  += deltaAh / g_usableCapacityAh * 100.0f;
        if (state.socPct > 110.0f) state.socPct = 110.0f;
        if (state.socPct <   0.0f) state.socPct = 0.0f;
    }

    // ── Advance summary window elapsed time ───────────────────────────────
    state.windowElapsedSec += g_sampleSec;

    // ── Alert evaluation ──────────────────────────────────────────────────
    //
    // Voltage and current alerts fire whenever those readings are valid,
    // independently of the thermistor state.  Temperature alert fires
    // independently of INA228 state.
    //
    // Float-state alert gating:
    //   float_voltage_low / float_voltage_high — only meaningful while the
    //     charger is present (curr >= discharge_ma).  Suppress during active
    //     discharge so a legitimate voltage sag while running on battery does
    //     not generate spurious float-fault alerts.
    //   float_current_high — suppress during active discharge AND during the
    //     post-discharge settling window so normal bulk-recharge current is not
    //     misclassified as the elevated float current that indicates VRLA
    //     sulfation or an internal short.

    // Maintain post-discharge settling window.  Reload on every discharge
    // sample so the countdown starts from the LAST discharge sample.
    if (!isnan(curr)) {
        if (curr < g_dischargeMa) {
            state.postDischargeSec = POST_DISCHARGE_SETTLE_SEC;
        } else {
            state.postDischargeSec = (state.postDischargeSec > g_sampleSec)
                                     ? state.postDischargeSec - g_sampleSec : 0u;
        }
    }

    // Voltage-based float alerts — only when charger is confirmed present.
    if (!isnan(volt)) {
        // chargerPresent: current is valid and above the discharge threshold.
        const bool chargerPresent = !isnan(curr) && (curr >= g_dischargeMa);

        // Float voltage too low: charger fault, failed cell, or battery unable
        // to reach float voltage.
        if (chargerPresent && volt < g_voltMinV && state.coolVoltLowSec == 0) {
            if (sendAlert("float_voltage_low", volt, curr, temp)) {
                state.coolVoltLowSec = ALERT_COOLDOWN_SEC;
            }
        }

        // Float voltage too high: charger overvoltage fault.
        if (chargerPresent && volt > g_voltMaxV && state.coolVoltHighSec == 0) {
            if (sendAlert("float_voltage_high", volt, curr, temp)) {
                state.coolVoltHighSec = ALERT_COOLDOWN_SEC;
            }
        }
    }

    // Current-based alerts.
    if (!isnan(curr)) {
        // Elevated float current: battery cannot hold charge — sulfation,
        // internal short, or near-end-of-life condition.  Only evaluated in
        // true float state: charger present AND post-discharge settling expired.
        const bool inFloat = (curr >= g_dischargeMa) && (state.postDischargeSec == 0);
        if (inFloat && curr > g_floatCurrHiMa && state.coolCurrHighSec == 0) {
            if (sendAlert("float_current_high", volt, curr, temp)) {
                state.coolCurrHighSec = ALERT_COOLDOWN_SEC;
            }
        }

        // Power outage: current has reversed — battery actively discharging.
        // The Notecarrier CX's attached LiPo keeps this sentinel running through the outage.
        if (curr < g_dischargeMa && state.coolOutageSec == 0) {
            if (sendAlert("power_outage", volt, curr, temp)) {
                state.coolOutageSec = ALERT_COOLDOWN_SEC;
            }
        }
    }

    // High pack surface temperature: independent of INA228 state.
    if (!isnan(temp) && temp > g_tempAlertC && state.coolTempHighSec == 0) {
        if (sendAlert("temp_high", volt, curr, temp)) {
            state.coolTempHighSec = ALERT_COOLDOWN_SEC;
        }
    }

    // Low state-of-charge.  Only fires once SoC has been commissioned (>= 0)
    // and the estimate has fallen below the configured threshold.  The alert
    // note includes soc_pct automatically via sendAlert() when socPct >= 0.
    if (state.socPct >= 0.0f && state.socPct < g_socLowPct &&
        state.coolSocLowSec == 0) {
        if (sendAlert("soc_low", volt, curr, temp)) {
            state.coolSocLowSec = ALERT_COOLDOWN_SEC;
        }
    }

    // ── Summary window check (elapsed-time based) ─────────────────────────
    // If emission fails after retries, accumulators are preserved and
    // windowElapsedSec stays at or above the target; the next wake retries
    // with one additional sample appended to the existing window data.
    //
    // windowTargetSec is derived from state.lastSummaryMin — the cadence
    // last written to the Notecard via hub.set — not g_summaryMin.  If an
    // operator changes summary_interval_min in Notehub but the hub.set
    // re-apply fails this wake, the local window keeps firing at the old
    // confirmed interval so the summary and outbound cadences stay aligned.
    // The window resets to the new interval on the first wake where hub.set
    // succeeds (handled in the hub.set change-handling block above).
    const uint32_t windowTargetSec = state.lastSummaryMin * 60U;
    if (state.windowElapsedSec >= windowTargetSec) {
        if (sendSummary()) {
            state.windowElapsedSec = 0;
        }
        // else: leave windowElapsedSec >= target so the next wake retries.
    }
}
