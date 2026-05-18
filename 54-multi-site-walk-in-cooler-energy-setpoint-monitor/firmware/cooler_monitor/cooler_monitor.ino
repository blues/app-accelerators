// cooler_monitor.ino — Multi-site walk-in cooler energy & setpoint monitor
//
// Hardware:
//   Blues Notecarrier CX (Cygnet STM32 host, onboard)
//   Blues Notecard Cell+WiFi (MBGLW) in M.2 slot
//   Blues Mojo (coulomb counter, bench validation only)
//   Adafruit waterproof DS18B20 probe — box air temperature (D5, OneWire)
//   SCT-013-030 split-core CT — compressor current (A0, ADC via bias circuit)
//   Magnetic reed switch — door open/close (D6, INPUT_PULLUP)
//
// Notefiles:
//   cooler_summary.qo  — window-based template-backed summary; queued for outbound sync
//   cooler_alert.qo    — immediate alert; sync:true bypasses outbound cadence
//
// Environment variables (set in Notehub; firmware defaults shown):
//   sample_interval_sec   60      Seconds between samples
//   summary_interval_min  60      Minutes between summary notes
//   temp_setpoint_f       35.0    Target box temperature (°F)
//   temp_alert_f          40.0    Box temp above which temp_high alert fires (°F)
//   door_open_alert_sec   300     Continuous door-open seconds before alert fires;
//                                 clamped up to sample_interval_sec at runtime
//   compressor_on_amps    2.0     Amps threshold for "compressor running"
//   volts_nominal         120.0   Nominal line voltage for apparent-power kWh estimate
//
// Power strategy:
//   The Cygnet host sleeps between samples via NotePayloadSaveAndSleep / card.attn.
//   The Notecard idles at ~8–18 µA between outbound sync sessions.
//   Summary notes queue locally and flush in one cellular session per hour.
//   Alert notes carry sync:true and flush within one session-establishment window.
//
// Helper functions (hubConfigure, defineTemplates, fetchEnvOverrides,
// applyHubSetIfChanged, sensor reads, sendAlert, sendSummary) live in
// cooler_monitor_helpers.cpp; shared types and constants are in
// cooler_monitor_helpers.h.

#include "cooler_monitor_helpers.h"

// ── Runtime objects ────────────────────────────────────────────────────────

Notecard notecard;
OneWire  oneWire(PIN_DS18B20);
DallasTemperature probe(&oneWire);

// Persisted state — global so loop() can reach it for NotePayloadSaveAndSleep.
static AppState state;

// Live config (re-populated from Notehub env vars on every wake)
uint32_t cfgSampleSec        = DEFAULT_SAMPLE_INTERVAL_SEC;
uint32_t cfgSummaryMin       = DEFAULT_SUMMARY_INTERVAL_MIN;
float    cfgTempSetpointF    = DEFAULT_TEMP_SETPOINT_F;
float    cfgTempAlertF       = DEFAULT_TEMP_ALERT_F;
uint32_t cfgDoorAlertSec     = DEFAULT_DOOR_ALERT_SEC;
float    cfgCompressorOnAmps = DEFAULT_COMPRESSOR_ON_AMPS;
float    cfgVoltsNominal     = DEFAULT_VOLTS_NOMINAL;

// ── Forward declarations ───────────────────────────────────────────────────

static void runSampleCycle(AppState &s);

// ── setup() — runs on every host power-on, including wake from card.attn ──

void setup() {
    DBG_BEGIN(115200);

    pinMode(PIN_DOOR, INPUT_PULLUP);

    // Set ADC resolution explicitly to 12 bits.  The Cygnet STM32L433 supports
    // 12-bit ADC natively, but the Arduino core default is not guaranteed to
    // match.  ADC_BITS and the CT scaling math in readCompressorAmps() both
    // depend on this being 12 bits; omitting the call would silently mis-scale
    // the ampere readings.
    analogReadResolution(12);

    // Initialize DS18B20 (12-bit resolution; non-blocking poll mode — conversion
    // is polled in the same wake via isConversionComplete(), not pipelined across sleeps)
    probe.begin();
    probe.setResolution(12);
    probe.setWaitForConversion(false);

    notecard.begin();
    DBG_SET_STREAM();

    // Attempt to restore persisted state from Notecard flash.
    // NotePayloadGetSegment returns false when the stored segment ID does not
    // match SEG_STATE (e.g. after a firmware upgrade that bumps the ID), so
    // restored is set false and the cold-boot branch below runs, ensuring
    // hubConfigure() and defineTemplates() are called with the new build.
    NotePayloadDesc payload;
    bool restored = NotePayloadRetrieveAfterSleep(&payload);
    memset(&state, 0, sizeof(state));
    if (restored) {
        restored = NotePayloadGetSegment(&payload, SEG_STATE, &state, sizeof(state));
        NotePayloadFree(&payload);
    }
    if (!restored) {
        // First boot (or segment ID mismatch after firmware upgrade): attempt
        // hub.set and record whether it succeeded.  If the call fails here
        // (transient I²C race between STM32 start-up and Notecard readiness),
        // the else-if branch below retries on every subsequent warm wake until
        // the Notecard acknowledges, ensuring the device cannot remain
        // permanently unassociated.
        if (hubConfigure()) {
            state.hubSetConfirmed = 1u;
        }
        // Seed prevDoorOpen from the current physical state so the very first
        // runSampleCycle() call does not register a spurious doorOpenCount
        // increment when the door is already open at boot.
        state.prevDoorOpen = readDoorOpen() ? 1u : 0u;
    } else if (!state.hubSetConfirmed) {
        // Warm wake, but hub.set was never confirmed (transient first-boot
        // failure).  Retry unconditionally on every wake, independent of
        // env.get success, until the Notecard acknowledges.  Without this
        // path the device would silently queue Notes forever while remaining
        // unassociated with the Notehub project.
        if (hubConfigure()) {
            state.hubSetConfirmed = 1u;
        }
    }

    // Load last-known-good config from persisted state before attempting env.get.
    // fetchEnvOverrides() will overwrite these when env.get succeeds; on a
    // transient failure the device runs with the last operator-tuned values
    // rather than silently reverting to compile-time defaults.
    if (restored && state.configPersisted) {
        cfgSampleSec        = state.persistedSampleSec;
        cfgSummaryMin       = state.persistedSummaryMin;
        cfgTempSetpointF    = state.persistedTempSetpointF;
        cfgTempAlertF       = state.persistedTempAlertF;
        cfgDoorAlertSec     = state.persistedDoorAlertSec;
        cfgCompressorOnAmps = state.persistedCompressorOnAmps;
        cfgVoltsNominal     = state.persistedVoltsNominal;
    }

    // Retry template registration on every wake until both templates succeed.
    // note.template is idempotent, so re-sending after a transient I²C failure
    // is harmless and guarantees the device never runs long-term with
    // untemplated Notes.
    if (!state.templatesRegistered) {
        if (defineTemplates()) {
            state.templatesRegistered = 1u;
        }
    }

    // Refresh config from Notehub env vars.  Re-apply hub.set only when hub
    // is already confirmed and the cadence has changed — a transient env.get
    // failure must not revert the Notecard's outbound timer, and hub.set
    // cadence updates are meaningless until the initial association succeeds.
    if (fetchEnvOverrides(state)) {
        if (state.hubSetConfirmed) {
            applyHubSetIfChanged(state);
        }
    }

    runSampleCycle(state);
}

// loop() persists state and puts the host to sleep until the next sample
// interval via card.attn (Notecarrier CX).  On a Notecarrier CX the ATTN
// line cuts host power, so execution beyond NotePayloadSaveAndSleep is not
// expected in normal operation.
void loop() {
    NotePayloadDesc outPayload = {0, 0, 0};
    NotePayloadAddSegment(&outPayload, SEG_STATE, &state, sizeof(state));
    NotePayloadSaveAndSleep(&outPayload, cfgSampleSec, NULL);

    // Reached only if ATTN-based host power cut is unavailable (bench / bring-
    // up mode, not expected on a Notecarrier CX in deployment).  Delay one
    // sample interval, then mirror the wake-time housekeeping setup() does:
    // retry template registration, retry hub.set if not yet confirmed, refresh
    // env-var overrides, and re-apply hub.set when the cadence has changed.
    // Without this path, env-var edits made during a bench run (e.g. lowering
    // temp_alert_f to fire a temp_high alert) would not take effect until the
    // host was power-cycled, breaking the threshold tests documented in
    // Section 8 of the README.
    delay(cfgSampleSec * 1000UL);

    if (!state.templatesRegistered) {
        if (defineTemplates()) {
            state.templatesRegistered = 1u;
        }
    }
    if (!state.hubSetConfirmed) {
        if (hubConfigure()) {
            state.hubSetConfirmed = 1u;
        }
    }
    if (fetchEnvOverrides(state)) {
        if (state.hubSetConfirmed) {
            applyHubSetIfChanged(state);
        }
    }
    runSampleCycle(state);
}

// ── Main sample cycle ──────────────────────────────────────────────────────

static void runSampleCycle(AppState &s) {
    const float tempF    = readBoxTempF();
    const float amps     = readCompressorAmps();
    const bool  doorOpen = readDoorOpen();
    const bool  compressorOn = (amps >= cfgCompressorOnAmps);

    // elapsed is the scheduled (requested) sleep duration of the interval that
    // just ended — the value passed to NotePayloadSaveAndSleep on the previous
    // wake.  Awake time spent sampling is not included, so accumulated
    // elapsedSecSinceSummary tracks scheduled sleep time rather than true
    // wall-clock elapsed time.  Using s.prevSampleSec rather than the
    // freshly-fetched cfgSampleSec means an operator change to
    // sample_interval_sec applies only to the upcoming sleep, not retroactively
    // to the interval that already completed.  On first boot prevSampleSec is 0
    // (no prior sleep), so accumulators correctly advance by zero for that
    // first wake.
    const uint32_t elapsed = s.prevSampleSec;

    // ── Per-window averages ────────────────────────────────────────────────
    if (!isnan(tempF)) { s.tempFSum += tempF; s.tempFCount++; }
    s.ampsSum += amps;
    s.ampsCount++;

    // ── Energy & compressor runtime ───────────────────────────────────────
    if (compressorOn) {
        s.compressorRunSec += elapsed;
        // Apparent power estimate: P ≈ V × I (no power-factor correction; see
        // README Limitations section for implications on kWh accuracy).
        const float wh = cfgVoltsNominal * amps * ((float)elapsed / 3600.0f);
        s.kwhAccum += wh / 1000.0f;
    }

    // ── Door tracking (edge detection + accumulation) ─────────────────────
    if (doorOpen) {
        s.doorOpenSec           += elapsed;  // total open time this window
        s.doorContinuousOpenSec += elapsed;  // unbroken open span (alert only)
        if (!s.prevDoorOpen) s.doorOpenCount++;  // rising edge: new open event
    } else if (s.prevDoorOpen) {
        // Falling edge: reset the continuous-open span so door_open_long fires
        // only on a sustained uninterrupted open, not on accumulated time
        // across multiple open/close cycles.
        s.doorContinuousOpenSec = 0u;
    }
    s.prevDoorOpen = doorOpen ? 1u : 0u;

    // ── Alert cooldown timers ─────────────────────────────────────────────
    s.doorAlertCooldownSec = (s.doorAlertCooldownSec > elapsed)
                             ? s.doorAlertCooldownSec - elapsed : 0u;
    s.tempAlertCooldownSec = (s.tempAlertCooldownSec > elapsed)
                             ? s.tempAlertCooldownSec - elapsed : 0u;

    // ── Alert: door left open too long ────────────────────────────────────
    // Uses doorContinuousOpenSec (resets on close) so the alert reflects a
    // true unbroken open duration, not cumulative time across multiple events.
    // Cooldown is armed only after note.add is confirmed queued; a transient
    // I²C failure leaves the cooldown at zero so the alert retries on the
    // next wake rather than being silently suppressed.
    // Guarded on s.templatesRegistered: if template registration has not yet
    // succeeded, skip note.add entirely so no untemplated Notes are ever sent.
    if (s.templatesRegistered &&
        doorOpen &&
        s.doorContinuousOpenSec >= cfgDoorAlertSec &&
        s.doorAlertCooldownSec  == 0) {
        if (sendAlert("door_open_long",
                      isnan(tempF) ? -9999.0f : tempF,
                      amps,
                      s.doorContinuousOpenSec)) {
            s.doorAlertCooldownSec = ALERT_COOLDOWN_SEC;
        }
    }

    // ── Alert: box temperature above temp_alert_f ─────────────────────────
    // Same templatesRegistered guard as the door alert above.
    if (s.templatesRegistered &&
        !isnan(tempF) &&
        tempF > cfgTempAlertF &&
        s.tempAlertCooldownSec == 0) {
        if (sendAlert("temp_high", tempF, amps, 0u)) {
            s.tempAlertCooldownSec = ALERT_COOLDOWN_SEC;
        }
    }

    // ── Periodic summary ──────────────────────────────────────────────────
    // Accumulate the scheduled sample interval into the window timer.
    // elapsedSecSinceSummary tracks scheduled sleep time (awake time excluded);
    // it is emitted as window_sec for use as the rate-calculation denominator.
    //
    // When the window fires, the accumulated sum is captured as windowSec and
    // emitted in the Note so downstream analytics have the correct denominator
    // for rate calculations (e.g. average watts =
    // kwh_window / window_sec × 3,600,000).  All accumulators and the timer
    // reset to zero after a successful summary.  When sample_interval_sec does
    // not evenly divide summary_interval_min×60 the window overshoots by at
    // most one sample period; window_sec carries the measured sum so downstream
    // accuracy is not affected.
    // Guarded on s.templatesRegistered: the window timer continues to
    // accumulate even while templates are pending so the first summary fires
    // at the expected cadence once registration succeeds.
    s.elapsedSecSinceSummary += elapsed;
    const uint32_t targetSec = cfgSummaryMin * 60u;
    if (s.templatesRegistered && s.elapsedSecSinceSummary >= targetSec) {
        const uint32_t windowSec = s.elapsedSecSinceSummary;
        if (sendSummary(s, windowSec)) {
            s.kwhAccum               = 0.0f;
            s.compressorRunSec       = 0u;
            s.doorOpenSec            = 0u;
            s.doorOpenCount          = 0u;
            s.elapsedSecSinceSummary = 0u;
            s.tempFSum   = 0.0f;  s.tempFCount = 0u;
            s.ampsSum    = 0.0f;  s.ampsCount  = 0u;
        }
    }

    // Record the interval governing the upcoming sleep so the next wake can
    // accurately account for the time that actually elapses.
    s.prevSampleSec = cfgSampleSec;
}
