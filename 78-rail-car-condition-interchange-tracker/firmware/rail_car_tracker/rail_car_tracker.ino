/***************************************************************************
  rail_car_tracker.ino — Rail Car Condition & Interchange Tracker

  Monitors a leased freight or tank rail car using:
    • Notecard for Skylo (NOTE-NBGLWX) for cellular + satellite connectivity
    • ADXL345 accelerometer for shock/impact scoring
    • Magnetic reed switch for coupler-state sensing
    • Adafruit MPRLS for tank vapor-space pressure (tank cars only)
    • DS18B20 waterproof probe for cargo temperature (tank cars only)
      Enable both TANK_CAR sensors by uncommenting #define TANK_CAR in
      rail_car_tracker_helpers.h

  Runs entirely on the Notecarrier CX's onboard STM32L433 host. Between
  samples the host sleeps in STM32 STOP2 (~1–2 µA, RAM retained) and is woken
  by the Notecard's ATTN pin at the end of a card.attn "sleep" (see
  cx_sleep.h). Execution resumes in place, so application state simply lives
  in RAM — nothing is persisted to the Notecard.

  Wiring for sleep: jumper the Notecarrier CX ATTN pin to D9 (both on the
  same 16-pin header). D5 is taken by the coupler reed switch and D6 by the
  DS18B20, so this sketch sets CX_ATTN_PIN to D9 below. Leave EN
  unconnected — on the CX it enables the shared 3.3 V VIO rail, so ATTN→EN
  browns out the whole board instead of sleeping the host.

  Build: Tools > USB support (if available) > None (usb=none). With the USB
  CDC stack enabled and no USB host attached, the USB wakeup interrupt exits
  STOP2 immediately. Debug output goes to the LPUART on the CX debug jack,
  which an ST-LINK V3 exposes as a virtual COM port (debugSerial below).

  All Notecard interactions, sensor reads, and note emission live in
  rail_car_tracker_helpers.cpp. This file contains only setup/loop
  orchestration and the global object definitions.

  THIS FILE SHOULD BE EDITED AFTER GENERATION.
  IT IS PROVIDED AS A STARTING POINT FOR THE USER TO EDIT AND EXTEND.
***************************************************************************/

#include <Arduino.h>
#include <Wire.h>
#include "rail_car_tracker_helpers.h"

// ATTN is jumpered to D9 on this build (D5 = reed switch, D6 = DS18B20).
#define CX_ATTN_PIN D9
#include "cx_sleep.h"

// ── Global object definitions ─────────────────────────────────────────────────
// Declared extern in rail_car_tracker_helpers.h; defined here once.
Notecard        notecard;
Uart            debugSerial(PIN_VCP_RX, PIN_VCP_TX);
#ifdef TANK_CAR
Adafruit_MPRLS    mprls;
// OneWire and DallasTemperature must be declared in dependency order:
// DallasTemperature requires a pointer to the OneWire bus object at construction.
OneWire           oneWireBus(PIN_TANK_TEMP);
DallasTemperature tankTempSensor(&oneWireBus);
#endif
AppState        state;

// sampleMin published by the env-var fetch so goToSleep() uses the same
// interval for the card.attn sleep.
static uint32_t g_sampleMin = SAMPLE_INTERVAL_MIN_DEFAULT;

// True for the first pass through loop() after power-up. Edge-triggered
// alerts (coupler, pressure drop, motion change) have no previous reading to
// compare against on that pass, and the commissioning summary is sent then.
static bool     g_firstWake = true;

// One-time configuration / sensor-init results. Anything that failed at
// power-up is retried at the top of each wake.
static bool     g_hubSetOk = false;
static bool     g_adxlOk   = false;
#ifdef TANK_CAR
static bool     g_mprlsOk  = false;
static bool     g_ds18Ok   = false;
#endif

// ─────────────────────────────────────────────────────────────────────────────
// Sleep in STOP2 until the Notecard raises ATTN g_sampleMin minutes from now.
// ─────────────────────────────────────────────────────────────────────────────
static void goToSleep() {
    uint32_t sleepSec = g_sampleMin * 60U;
    if (!cxSleepUntilAttn(notecard, sleepSec, NULL, &debugSerial)) {
        // The Notecard didn't take the sleep request, or ATTN never went low
        // (check the ATTN -> D9 jumper). Keep the sample cadence and retry.
        debugSerial.println("[warn] ATTN sleep failed — waiting out the interval awake");
        delay(sleepSec * 1000UL);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Sensor bring-up. Each call is idempotent; loop() retries any that failed.
// ─────────────────────────────────────────────────────────────────────────────
static void initSensors() {
    if (!g_adxlOk) {
        g_adxlOk = adxl345Begin();
        if (!g_adxlOk) debugSerial.println("[warn] ADXL345 not found");
    }
#ifdef TANK_CAR
    if (!g_mprlsOk) {
        g_mprlsOk = mprls.begin();
        if (!g_mprlsOk) debugSerial.println("[warn] MPRLS not found");
    }
    if (!g_ds18Ok) {
        // DS18B20 initialization: getDeviceCount() scans the OneWire bus for
        // responsive devices. setResolution(12) gives 0.0625 °C resolution at the
        // cost of ~750 ms per conversion; setWaitForConversion(true) makes
        // requestTemperatures() block until the conversion is complete so the
        // caller does not need separate timing logic.
        tankTempSensor.begin();
        g_ds18Ok = (tankTempSensor.getDeviceCount() > 0);
        if (g_ds18Ok) {
            tankTempSensor.setResolution(12);
            tankTempSensor.setWaitForConversion(true);
        } else {
            debugSerial.println("[warn] DS18B20 not found — check OneWire probe on D6");
        }
    }
#endif
}

// ─────────────────────────────────────────────────────────────────────────────
// setup() — runs once at power-up. The host resumes in place after each STOP2
// sleep, so one-time Notecard configuration lives here and every per-wake step
// lives in loop().
// ─────────────────────────────────────────────────────────────────────────────
void setup() {
    debugSerial.begin(115200);
    Wire.begin();

    notecard.begin();
#ifndef NOTE_C_LOW_MEM
    notecard.setDebugOutputStream(debugSerial);
#endif

    // Known-zero state: latches, accumulators, configVersion all start fresh.
    // STOP2 retains RAM, so from here on `state` carries across every wake.
    memset(&state, 0, sizeof(state));

    // ── Runtime guard: catch empty PRODUCT_UID before wasting a sync session ──
    // loop() checks this on every wake and sleeps without talking to the
    // Notecard until the sketch is reflashed with a ProductUID.
    if (PRODUCT_UID[0] == '\0') {
        debugSerial.println("[error] PRODUCT_UID is empty — set it in the sketch before flashing.");
    }

    // hub.set is idempotent; applying it at power-up ensures a PRODUCT_UID
    // change in the firmware sketch takes effect immediately without
    // requiring CONFIG_VERSION to be bumped. Failure is non-fatal — the
    // Notecard retains its previous hub.set configuration so notes
    // continue to queue and sync — and it is retried on the next wake.
    g_hubSetOk = configureNotecard();
    if (!g_hubSetOk) {
        debugSerial.println("[warn] hub.set failed — Notecard retains previous configuration");
    }

    initSensors();

    // Arm the ATTN wake: the Notecard raises ATTN (D9) to end each sleep.
    cxSleepBegin();
}

// ─────────────────────────────────────────────────────────────────────────────
// loop() — one sample cycle, then sleep until the Notecard raises ATTN.
// ─────────────────────────────────────────────────────────────────────────────
void loop() {
    bool firstWake = g_firstWake;
    g_firstWake = false;

    if (PRODUCT_UID[0] == '\0') {
        debugSerial.println("[error] PRODUCT_UID is empty — sleeping; set it in the sketch before flashing.");
        goToSleep();
        return;
    }

    // ── Retry one-time configuration that did not complete at power-up ────────
    if (!g_hubSetOk) {
        g_hubSetOk = configureNotecard();
        if (!g_hubSetOk) {
            debugSerial.println("[warn] hub.set failed — Notecard retains previous configuration");
        }
    }

    // ── Templates and GPS/motion: applied once per CONFIG_VERSION ─────────────
    // note.template and card.motion.mode / card.location.mode only change when
    // a firmware update modifies their parameters; bump CONFIG_VERSION in that
    // case so deployed devices reapply automatically. Notes MUST NOT be queued
    // before compact templates are registered (they would be rejected with a
    // format error), so the entire cycle is aborted on failure; configVersion
    // stays unchanged so the next wake retries automatically.
    if (state.configVersion != CONFIG_VERSION) {
        bool configOk = defineTemplates() && configureMotionAndGPS();
        if (configOk) {
            state.configVersion    = CONFIG_VERSION;
            state.locationAcquired = false; // force initial-fix path after any reconfig
            debugSerial.print("[init] templates and GPS/motion configured (v");
            debugSerial.print(CONFIG_VERSION);
            debugSerial.println(")");
        } else {
            debugSerial.println("[init] config incomplete — will retry next wake");
            goToSleep(); // skip all note emission this wake
            return;
        }
    }

    // ── Initial GPS fix acquisition ───────────────────────────────────────────
    // configureMotionAndGPS() deliberately omits the GPS motion threshold so
    // the Notecard can acquire a position in a stationary yard immediately after
    // installation, before the car has moved. Once card.location reports a valid
    // fix (non-zero "time" field), applyGPSMotionGate() re-issues
    // card.location.mode with threshold:1 to gate the GNSS radio on motion,
    // protecting the solar budget during long yard dwell periods. The
    // locationAcquired latch persists across sleep cycles so this block runs
    // one extra card.location query per wake only until the first fix is secured.
    if (!state.locationAcquired) {
        J *rsp = notecard.requestAndResponse(notecard.newRequest("card.location"));
        if (rsp != NULL) {
            bool hasFix = (JGetNumber(rsp, "time") > 0);
            notecard.deleteResponse(rsp);
            if (hasFix) {
                if (applyGPSMotionGate()) {
                    state.locationAcquired = true;
                    debugSerial.println("[init] initial GPS fix confirmed — motion gate enabled");
                }
            } else {
                debugSerial.println("[init] no GPS fix yet — running without motion gate for initial acquisition");
            }
        }
    }

    // ── Pull env-var overrides every wake (no reflash for threshold changes) ──
    uint32_t sampleMin           = SAMPLE_INTERVAL_MIN_DEFAULT;
    uint32_t reportMin           = REPORT_INTERVAL_MIN_DEFAULT;
    uint32_t locationIntervalMin = LOCATION_INTERVAL_MIN_DEFAULT;
    float    shockThreshG        = SHOCK_THRESHOLD_G_DEFAULT;
    uint32_t shockCoolMin        = SHOCK_COOLDOWN_MIN_DEFAULT;
    float    pressMaxPsi         = PRESSURE_MAX_PSI_DEFAULT;
    float    pressDropPsi        = PRESSURE_DROP_PSI_DEFAULT;
    float    tankTempMinC        = TANK_TEMP_MIN_C_DEFAULT;
    float    tankTempMaxC        = TANK_TEMP_MAX_C_DEFAULT;
    fetchEnvOverrides(sampleMin, reportMin, shockThreshG, shockCoolMin,
                      locationIntervalMin,
                      pressMaxPsi, pressDropPsi, tankTempMinC, tankTempMaxC);
    g_sampleMin = sampleMin;

    // ── Retry any sensor that was not found at power-up ───────────────────────
    initSensors();

    // ── Read sensors ──────────────────────────────────────────────────────────
    bool  coupled   = readCouplerState();
    // readPeakShockG() returns NAN when every I²C read in the burst fails.
    float peakG     = g_adxlOk ? readPeakShockG() : NAN;

#ifdef TANK_CAR
    // Adafruit MPRLS readPressure() returns hPa (absolute); divide by 68.948
    // to convert to PSI absolute to match the MPRLS 0–25 PSI sensor range.
    float pressurePsi = g_mprlsOk ? (mprls.readPressure() / 68.948f) : NAN;

    // DS18B20: requestTemperatures() blocks ~750 ms in 12-bit mode.
    // getTempCByIndex(0) returns DEVICE_DISCONNECTED_C (-127.0 °C) on fault;
    // treat any value below -100 °C as an error and return NAN.
    float tankTempC = NAN;
    if (g_ds18Ok) {
        tankTempSensor.requestTemperatures();
        float t = tankTempSensor.getTempCByIndex(0);
        tankTempC = (t > -100.0f) ? t : NAN;
    }
#else
    float pressurePsi = NAN; // unused in non-tank builds; suppresses uninitialised-variable warning
    float tankTempC   = NAN; // unused in non-tank builds
#endif

    debugSerial.print("[sample] coupled="); debugSerial.print(coupled);
    debugSerial.print("  peakG=");          debugSerial.print(peakG);
#ifdef TANK_CAR
    debugSerial.print("  pres=");            debugSerial.print(pressurePsi);
    debugSerial.print("PSI  tank_temp=");    debugSerial.print(tankTempC);
    debugSerial.print("C");
#endif
    debugSerial.println();

    // ── Get motion state (moving vs. stopped) ─────────────────────────────────
    bool moving = false;
    {
        J *rsp = notecard.requestAndResponse(notecard.newRequest("card.motion"));
        if (rsp != NULL) {
            const char *err = JGetString(rsp, "err");
            if (err == NULL || *err == '\0') {
                const char *motionMode = JGetString(rsp, "mode");
                moving = (motionMode != NULL && strcmp(motionMode, "moving") == 0);
            }
            notecard.deleteResponse(rsp);
        }
    }

    // ── Accumulate into application state ─────────────────────────────────────
    // peakShockG: track highest valid G in the window (ignore NAN from failed bursts).
    if (!isnan(peakG) && peakG > state.peakShockG) state.peakShockG = peakG;
    // shockWindowCount: counts sample *windows* whose peak exceeded the threshold,
    // not individual impacts. Each sample cycle with a valid burst is one window.
    if (!isnan(peakG) && peakG >= shockThreshG)    state.shockWindowCount++;
    // elapsedMin accumulates actual minutes so a runtime change to sampleMin
    // does not retroactively shift the summary window boundary.
    state.elapsedMin += sampleMin;
    // shockCooldownRemMin: monotonic countdown (minutes) that gates shock alerts
    // without requiring absolute time from card.time or GPS sync. Cap at
    // shockCoolMin before decrementing so a runtime reduction of the cooldown
    // env var takes effect within one wake cycle, then decrement normally.
    if (state.shockCooldownRemMin > shockCoolMin) state.shockCooldownRemMin = shockCoolMin;
    state.shockCooldownRemMin = (state.shockCooldownRemMin > sampleMin)
                                ? state.shockCooldownRemMin - sampleMin : 0;

    // ── Alert: high-G shock impact ────────────────────────────────────────────
    // Fires when the monotonic countdown reaches zero (shockCooldownRemMin == 0).
    // Because the cooldown is elapsed-time-based rather than epoch-based, shock
    // alerts queue normally before the first GPS or network time sync and
    // throughout extended no-coverage periods. shockCooldownRemMin is reset to
    // shockCoolMin only when the note is accepted, allowing automatic retry if
    // the Notecard rejects the note this wake.
    bool syncNeeded = false;
    if (!isnan(peakG) && peakG >= shockThreshG && state.shockCooldownRemMin == 0) {
        if (sendAlert("impact", peakG)) {
            syncNeeded = true;
            state.shockCooldownRemMin = shockCoolMin; // reset countdown on success
            debugSerial.print("[alert] impact @ "); debugSerial.println(peakG);
        }
    }

    // ── Alert: coupler state change (coupled / decoupled) ─────────────────────
    // Only fire on edges; the 15-minute sample interval provides natural debounce
    // against rail-yard bump transients shorter than one sample window.
    // lastCouplerState is advanced only after a successful alert to allow retry.
    if (!firstWake && (coupled != state.lastCouplerState)) {
        if (sendAlert(coupled ? "coupled" : "decoupled", coupled ? 1.0f : 0.0f)) {
            syncNeeded = true;
            state.lastCouplerState = coupled;
            debugSerial.print("[alert] coupler → ");
            debugSerial.println(coupled ? "coupled" : "decoupled");
        }
        // On failure: lastCouplerState stays at the old value so the edge is
        // re-detected and the alert is retried on the next wake.
    } else {
        // No state change (or first wake): refresh the latch to the current reading.
        state.lastCouplerState = coupled;
    }

#ifdef TANK_CAR
    // ── Alert: tank pressure anomaly ─────────────────────────────────────────
    // pressure_high is edge-detected; lastPressHigh is advanced only on success.
    // pressure_drop requires both the current AND previous readings to be valid;
    // lastPressureValid is cleared whenever a reading fails so stale readings
    // cannot manufacture a false drop alert after one or more bad cycles.
    if (!isnan(pressurePsi)) {
        bool isPressHigh = (pressurePsi > pressMaxPsi);
        if (isPressHigh && !state.lastPressHigh) {
            if (sendAlert("pressure_high", pressurePsi)) {
                syncNeeded = true;
                state.lastPressHigh = true;
            }
        } else if (!isPressHigh) {
            state.lastPressHigh = false; // condition cleared: re-arm
        }

        if (!firstWake && state.lastPressureValid &&
            (state.lastPressurePsi - pressurePsi) > pressDropPsi) {
            if (sendAlert("pressure_drop", state.lastPressurePsi - pressurePsi)) {
                syncNeeded = true;
            }
        }
        state.lastPressurePsi   = pressurePsi;
        state.lastPressureValid = true;
    } else {
        // Current reading invalid: clear the valid flag so the next good sample
        // is not compared against a stale previous reading.
        state.lastPressureValid = false;
    }
#endif // TANK_CAR (pressure alerts)

#ifdef TANK_CAR
    // ── Alert: tank cargo temperature out of range (edge-detected) ────────────
    // Fires once when the condition is first observed; the latch is cleared
    // when temperature returns inside the window, re-arming for the next
    // excursion. lastTankTempLow/High are only advanced on a successful
    // sendAlert() call, so a transient Notecard error leaves the latch un-set
    // and the alert fires again next wake.
    if (!isnan(tankTempC)) {
        bool isTankTempLow  = (tankTempC < tankTempMinC);
        bool isTankTempHigh = (tankTempC > tankTempMaxC);

        if (isTankTempLow && !state.lastTankTempLow) {
            if (sendAlert("tank_temp_low", tankTempC)) {
                syncNeeded = true;
                state.lastTankTempLow = true; // latch only on successful queue
            }
        } else if (!isTankTempLow) {
            state.lastTankTempLow = false; // condition cleared: re-arm for next excursion
        }

        if (isTankTempHigh && !state.lastTankTempHigh) {
            if (sendAlert("tank_temp_high", tankTempC)) {
                syncNeeded = true;
                state.lastTankTempHigh = true;
            }
        } else if (!isTankTempHigh) {
            state.lastTankTempHigh = false;
        }
    }
#endif // TANK_CAR (tank temp alerts)

    // ── Location note: motion-state change and in-motion cadence ─────────────
    // Provides the continuous position stream needed for downstream interchange
    // detection and geofencing. Fires on two independent triggers:
    //
    //   1. Motion-state edge (stopped ↔ moving): captures yard arrival and
    //      departure events. The edge is detected on the host's wake cadence —
    //      a transition that occurs between wakes is reported on the next wake,
    //      up to sample_interval_min minutes later. Once detected, a hub.sync
    //      is requested immediately within the same wake so the note reaches
    //      Notehub without waiting for the next scheduled outbound window.
    //   2. While moving, every locationIntervalMin minutes: fills the gaps
    //      between periodic status summaries with a dense-enough position
    //      record for interchange-boundary determination.
    //
    // lastMovingState and locationElapsedMin are only updated on a successful
    // note send, so transient Notecard failures are automatically retried on
    // the next wake without losing the triggering event.
    state.locationElapsedMin += sampleMin;
    bool motionEdge  = !firstWake && (moving != state.lastMovingState);
    bool locationDue = moving && (state.locationElapsedMin >= locationIntervalMin);

    if (motionEdge || locationDue) {
        if (sendLocationNote(coupled, moving)) {
            state.locationElapsedMin = 0;
            if (motionEdge) {
                state.lastMovingState = moving;
                syncNeeded = true; // hub.sync requested this wake on yard arrival / departure
            }
            debugSerial.print("[location] sent (");
            debugSerial.print(motionEdge ? "motion change" : "cadence");
            debugSerial.println(")");
        }
        // On failure: locationElapsedMin preserved; motionEdge re-detected next wake
        // because lastMovingState is not advanced on failure.
    } else {
        // No edge, no cadence fire: keep the latch current so steady-state
        // operation does not accumulate a stale motionEdge on future wakes.
        state.lastMovingState = moving;
    }

    // ── Periodic status summary ───────────────────────────────────────────────
    // The accumulation window (peakShockG, shockWindowCount, elapsedMin) is
    // only reset when sendSummary() returns true (note accepted by Notecard).
    // On failure the window is preserved intact and the summary is retried
    // on the next wake.
    bool timeForSummary = (state.elapsedMin >= reportMin);
    if (timeForSummary || firstWake) {
        if (sendSummary(pressurePsi, tankTempC, coupled, moving)) {
            state.peakShockG       = 0.0f;
            state.shockWindowCount = 0;
            state.elapsedMin       = 0;
            if (firstWake) {
                // Commissioning summary: request an immediate outbound sync so
                // the first railcar_status.qo note is visible in Notehub during
                // bench setup, without waiting for the next scheduled outbound window.
                syncNeeded = true;
            }
            debugSerial.println("[summary] sent");
        } else {
            debugSerial.println("[warn] summary send failed — window preserved for retry");
        }
    }

    // ── Single sync for alerts, location events, or commissioning summary ─────
    // Coalesce all alerts behind one hub.sync instead of triggering a separate
    // sync session per note.add, which wastes battery and satellite overhead.
    // syncNeeded is also set on the first-wake summary so the commissioning note
    // is uploaded immediately rather than waiting for the next scheduled outbound
    // window, matching the documented quickstart bench behavior.
    if (syncNeeded) {
        J *rsp = notecard.requestAndResponse(notecard.newRequest("hub.sync"));
        if (rsp == NULL) {
            debugSerial.println("[warn] hub.sync: no response — notes will send on next scheduled session");
        } else {
            const char *err = JGetString(rsp, "err");
            if (err && *err) {
                debugSerial.print("[warn] hub.sync: ");
                debugSerial.println(err);
            }
            notecard.deleteResponse(rsp);
        }
    }

    // ── Sleep until the next sample ───────────────────────────────────────────
    // The host enters STOP2 with `state` intact in RAM; the Notecard raises
    // ATTN sampleMin minutes from now and execution resumes at the top of loop().
    goToSleep();
}
