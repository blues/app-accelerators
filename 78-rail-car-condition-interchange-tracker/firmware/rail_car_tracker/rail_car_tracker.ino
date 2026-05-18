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

  Runs entirely on the Notecarrier CX's onboard Cygnet STM32L433 MCU.
  Sleeps between samples using card.attn host-power gating.

  All Notecard interactions, sensor reads, and note emission live in
  rail_car_tracker_helpers.cpp. This file contains only setup/loop
  orchestration and the global object definitions.

  THIS FILE SHOULD BE EDITED AFTER GENERATION.
  IT IS PROVIDED AS A STARTING POINT FOR THE USER TO EDIT AND EXTEND.
***************************************************************************/

#include <Arduino.h>
#include <Wire.h>
#include "rail_car_tracker_helpers.h"

// ── Global object definitions ─────────────────────────────────────────────────
// Declared extern in rail_car_tracker_helpers.h; defined here once.
Notecard        notecard;
#ifdef TANK_CAR
Adafruit_MPRLS    mprls;
// OneWire and DallasTemperature must be declared in dependency order:
// DallasTemperature requires a pointer to the OneWire bus object at construction.
OneWire           oneWireBus(PIN_TANK_TEMP);
DallasTemperature tankTempSensor(&oneWireBus);
#endif
PersistState    state;

// sampleMin published by setup() so loop() uses the same interval for sleep.
static uint32_t g_sampleMin = SAMPLE_INTERVAL_MIN_DEFAULT;

// True only after setup() has successfully run NotePayloadRetrieveAfterSleep so
// that `state` reflects either the previously persisted payload or a known
// fresh-boot zero. While false, loop() must NOT call NotePayloadSaveAndSleep:
// `state` is in its BSS-zero post-reset representation, and writing those zeros
// to Notecard flash would clobber the real persisted state (latches,
// accumulators, configVersion) that survived the prior sleep cycle.
static bool     g_persistStateValid = false;

// ─────────────────────────────────────────────────────────────────────────────
void setup() {
    debugSerial.begin(115200);
    Wire.begin();

    // ── Runtime guard: catch empty PRODUCT_UID before wasting a sync session ──
    if (PRODUCT_UID[0] == '\0') {
        debugSerial.println("[fatal] PRODUCT_UID is empty — set it in the sketch before flashing.");
        while (true) { delay(60000); }
    }

    notecard.begin();
#ifndef NOTE_C_LOW_MEM
    notecard.setDebugOutputStream(debugSerial);
#endif

    // ── Per-boot Notecard readiness ping ─────────────────────────────────────
    // Retries card.version for up to 10 s to let the Notecard I²C stack
    // settle before any real request. Abort the entire cycle on failure;
    // loop() will see g_persistStateValid == false, skip the save (so the
    // existing persisted PersistState in Notecard flash is preserved intact),
    // and fall through to the card.attn / WFI sleep fallback so the host does
    // not spin indefinitely if the Notecard is unresponsive this wake.
    if (!notecardReady()) {
        debugSerial.println("[error] Notecard not ready — aborting cycle; will retry next wake");
        return;
    }

    // ── Restore state persisted from the previous sleep cycle ─────────────────
    // Zero the struct before the restore call so any fields added in a newer
    // firmware build that are absent from an older stored payload default to
    // 0/false rather than reading uninitialized bytes. NotePayloadGetSegment
    // then overlays the saved values up to the stored segment size.
    // A dedicated restore descriptor is used; the save descriptor in loop() is
    // freshly initialised to avoid carrying forward stale segment state.
    memset(&state, 0, sizeof(state));
    NotePayloadDesc restorePayload;
    bool wakeFromSleep = NotePayloadRetrieveAfterSleep(&restorePayload);
    if (wakeFromSleep) {
        wakeFromSleep &= NotePayloadGetSegment(&restorePayload, STATE_SEG_ID,
                                               &state, sizeof(state));
        NotePayloadFree(&restorePayload);
    }
    // From this point on `state` is in a known-consistent representation
    // (either the restored payload or a clean zero-init for the first boot),
    // so it is safe for loop() to write it back to Notecard flash on sleep.
    g_persistStateValid = true;

    // ── hub.set: always applied at each boot ──────────────────────────────────
    // hub.set is idempotent; re-issuing it every boot ensures a PRODUCT_UID
    // change in the firmware sketch takes effect immediately without requiring
    // CONFIG_VERSION to be bumped. Failure is non-fatal — the Notecard retains
    // its previous hub.set configuration so notes continue to queue and sync.
    if (!configureNotecard()) {
        debugSerial.println("[warn] hub.set failed — Notecard retains previous configuration");
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
            return; // skip all note emission; loop() saves state and sleeps
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

    // ── Initialize sensors ────────────────────────────────────────────────────
    bool adxlOk = adxl345Begin();
    if (!adxlOk) debugSerial.println("[warn] ADXL345 not found");
#ifdef TANK_CAR
    bool mprlsOk = mprls.begin();
    if (!mprlsOk) debugSerial.println("[warn] MPRLS not found");

    // DS18B20 initialization: getDeviceCount() scans the OneWire bus for
    // responsive devices. setResolution(12) gives 0.0625 °C resolution at the
    // cost of ~750 ms per conversion; setWaitForConversion(true) makes
    // requestTemperatures() block until the conversion is complete so the
    // caller does not need separate timing logic.
    tankTempSensor.begin();
    bool ds18Ok = (tankTempSensor.getDeviceCount() > 0);
    if (ds18Ok) {
        tankTempSensor.setResolution(12);
        tankTempSensor.setWaitForConversion(true);
    } else {
        debugSerial.println("[warn] DS18B20 not found — check OneWire probe on D6");
    }
#endif

    // ── Read sensors ──────────────────────────────────────────────────────────
    bool  coupled   = readCouplerState();
    // readPeakShockG() returns NAN when every I²C read in the burst fails.
    float peakG     = adxlOk ? readPeakShockG() : NAN;

#ifdef TANK_CAR
    // Adafruit MPRLS readPressure() returns hPa (absolute); divide by 68.948
    // to convert to PSI absolute to match the MPRLS 0–25 PSI sensor range.
    float pressurePsi = mprlsOk ? (mprls.readPressure() / 68.948f) : NAN;

    // DS18B20: requestTemperatures() blocks ~750 ms in 12-bit mode.
    // getTempCByIndex(0) returns DEVICE_DISCONNECTED_C (-127.0 °C) on fault;
    // treat any value below -100 °C as an error and return NAN.
    float tankTempC = NAN;
    if (ds18Ok) {
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

    // ── Accumulate into persistent state ──────────────────────────────────────
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
    // shockCoolMin before decrementing so any legacy epoch value that may be
    // stored in this field from older firmware is clamped to a sane range within
    // one wake cycle, then decrements normally from that point forward.
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
    if (wakeFromSleep && (coupled != state.lastCouplerState)) {
        if (sendAlert(coupled ? "coupled" : "decoupled", coupled ? 1.0f : 0.0f)) {
            syncNeeded = true;
            state.lastCouplerState = coupled;
            debugSerial.print("[alert] coupler → ");
            debugSerial.println(coupled ? "coupled" : "decoupled");
        }
        // On failure: lastCouplerState stays at the old value so the edge is
        // re-detected and the alert is retried on the next wake.
    } else {
        // No state change (or first boot): refresh the latch to the current reading.
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

        if (wakeFromSleep && state.lastPressureValid &&
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
    bool motionEdge  = wakeFromSleep && (moving != state.lastMovingState);
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
    if (timeForSummary || !wakeFromSleep) {
        if (sendSummary(pressurePsi, tankTempC, coupled, moving)) {
            state.peakShockG       = 0.0f;
            state.shockWindowCount = 0;
            state.elapsedMin       = 0;
            if (!wakeFromSleep) {
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
    // syncNeeded is also set on the first-boot summary so the commissioning note
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

    // Return to loop() which saves state and issues NotePayloadSaveAndSleep.
}

// ─────────────────────────────────────────────────────────────────────────────
void loop() {
    // ── Persist state and sleep until next sample ─────────────────────────────
    // A freshly initialised descriptor is used (not reused from setup's restore
    // path) so no stale segment definitions are carried forward.
    //
    // NotePayloadSaveAndSleep is retried up to three times before declaring
    // failure. A transient I²C glitch must not cause state loss: dropping the
    // persisted PersistState after alerts were queued in setup() would reset
    // shockCooldownRemMin to zero, skew summary window counters, and allow
    // duplicate edge-triggered alerts on the next wake.
    //
    // When g_persistStateValid is false, setup() returned before
    // NotePayloadRetrieveAfterSleep completed (e.g. notecardReady() failed)
    // and `state` is still BSS-zero. Saving that to Notecard flash would
    // overwrite the real persisted state from the prior sleep cycle, so the
    // save/segment step is skipped entirely and we fall straight through to
    // the card.attn sleep fallback below — which sleeps the host without
    // touching the saved payload.
    bool segOk   = false;
    bool sleepOk = false;
    NotePayloadDesc savePayload = {0, 0, 0};
    if (g_persistStateValid) {
        segOk = NotePayloadAddSegment(&savePayload, STATE_SEG_ID,
                                      &state, sizeof(state));
        if (segOk) {
            for (uint8_t attempt = 0; attempt < 3 && !sleepOk; attempt++) {
                if (attempt > 0) {
                    debugSerial.print("[warn] NotePayload save/sleep retry ");
                    debugSerial.println(attempt);
                    delay(500);
                }
                sleepOk = NotePayloadSaveAndSleep(&savePayload, g_sampleMin * 60U, NULL);
            }
        }
    } else {
        debugSerial.println("[warn] state not restored this cycle — skipping persist save to protect Notecard flash");
    }

    if (!sleepOk) {
        // Either NotePayloadSaveAndSleep failed for this cycle, or the save
        // was deliberately skipped because state had not been restored. In
        // the failed-save case, state is not persisted for this cycle: shock
        // cooldown resets to zero on the next wake, accumulated summary
        // counters may skew, and edge-triggered alerts already queued this
        // wake could be re-queued on the next wake. Fall back to card.attn
        // sleep to avoid leaving the host fully awake for the entire sample
        // interval (tens of mA on a solar device).
        debugSerial.println("[info] issuing card.attn sleep fallback (no persist save this cycle)");
        bool attnOk = false;
        for (uint8_t attempt = 0; attempt < 3 && !attnOk; attempt++) {
            if (attempt > 0) delay(500);
            J *req = notecard.newRequest("card.attn");
            if (req != NULL) {
                JAddStringToObject(req, "mode",    "sleep");
                JAddNumberToObject(req, "seconds", (double)(g_sampleMin * 60U));
                J *rsp = notecard.requestAndResponse(req);
                if (rsp != NULL) {
                    const char *err = JGetString(rsp, "err");
                    attnOk = (err == NULL || *err == '\0');
                    notecard.deleteResponse(rsp);
                }
            }
        }
        if (!attnOk) {
            // Notecard unresponsive: the ATTN line cannot cut host power.
            // Park the CPU in ARM WFI (Wait-For-Interrupt) between SysTick
            // interrupts, drawing ~1–2 mA instead of ~10 mA in active run mode.
            // (__WFI is a CMSIS intrinsic available on all Cortex-M targets.)
            debugSerial.println("[error] card.attn failed — entering WFI fail-safe sleep");
            uint32_t deadlineMs = millis() + (g_sampleMin * 60UL * 1000UL);
            while ((int32_t)(deadlineMs - millis()) > 0) {
                __WFI();
            }
            return; // skip bench-testing delay; loop() re-runs to retry next cycle
        }
    }

    // card.attn cuts host power; this line is reached only when the
    // Notecarrier CX is not gating host power via ATTN (e.g. bench testing).
    delay(g_sampleMin * 60UL * 1000UL);
}
