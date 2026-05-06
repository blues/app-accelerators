/***************************************************************************
  transformer_load_monitor — Utility Distribution Transformer Load Monitor

  Runs on the Cygnet STM32L4 host embedded in the Blues Notecarrier CX.
  On every wake (default every 5 minutes), the firmware:
    1. Reads three YHDC SCT-013-000 split-core current-output CTs (100 A /
       50 mA) on ADC channels A0, A1, A2, using a two-pass RMS algorithm.
    2. Reads enclosure temperature from an Adafruit MCP9808 over I²C.
    3. Accumulates per-phase RMS current and temperature into a rolling
       summary window (default 60 minutes).
    4. Evaluates three independent threshold rules: overload, phase
       imbalance, and high temperature.  Threshold trips queue an
       xfmr_alert.qo Note with sync:true so the Notecard wakes the radio
       immediately.
    5. At the end of each summary window, queues an xfmr_summary.qo Note
       (template-encoded for efficient cellular transport).
    6. Serializes all runtime state to Notecard flash via
       NotePayloadSaveAndSleep(), then lets card.attn cut host power until
       the next sample interval.

  All thresholds (rated_amps, overload_pct, imbalance_pct_thresh,
  temp_alert_c, sample_interval_sec, etc.) are overridable at runtime via
  Notehub environment variables — no re-flash required for field tuning.

  Assumed hardware:
    - Blues Notecarrier CX  (Cygnet STM32L4 host)
    - Blues Notecard Cell+WiFi (MBGLW) in the M.2 slot
    - 3× YHDC SCT-013-000  (100 A / 50 mA) split-core CTs on A0, A1, A2
      (firmware defaults to phase_count=2, reading A0/A1 only; set
      phase_count=3 via env var for three-phase installations)
    - Per-channel bias circuit: 22 Ω burden, 2× 10 kΩ divider, 10 µF cap
    - Adafruit MCP9808 I²C temperature sensor (default address 0x18)

  Dependencies:
    - Blues Wireless Notecard library (note-arduino)
      https://github.com/blues/note-arduino
    - Adafruit MCP9808 library (install via Arduino Library Manager)
      https://github.com/adafruit/Adafruit_MCP9808_Library
    - STM32duino Arduino core for the Cygnet (STM32L4)
      https://github.com/stm32duino/Arduino_Core_STM32

  Helper functions, struct definitions, and all constants are in
  transformer_load_monitor_helpers.h / .cpp.
***************************************************************************/

#include "transformer_load_monitor_helpers.h"

// ---------------------------------------------------------------------------
// Project configuration — set PRODUCT_UID to your Notehub project ProductUID
// ---------------------------------------------------------------------------
#ifndef PRODUCT_UID
#define PRODUCT_UID ""  // "com.your-company.your-name:xfmr-monitor"
#pragma message "PRODUCT_UID is not defined. Set it before deploying."
#endif

// ---------------------------------------------------------------------------
// Globals
// ---------------------------------------------------------------------------
Notecard         notecard;
Adafruit_MCP9808 mcp9808;
PersistState     state;
EnvConfig        cfg;
bool             coldBoot = true;

// ---------------------------------------------------------------------------
// setup() — runs from cold on every wake because card.attn cuts host power
// ---------------------------------------------------------------------------
void setup() {
    // Initialize cfg to safe defaults before any early return so that loop()
    // never executes with a zero sleep interval on the Notecard-failure path.
    // The real values are loaded by fetchEnvOverrides() further below; these
    // only take effect on the rare early-exit path.
    cfg.sample_interval_sec  = DEFAULT_SAMPLE_INTERVAL_SEC;
    cfg.summary_interval_min = DEFAULT_SUMMARY_INTERVAL_MIN;
    cfg.rated_amps           = DEFAULT_RATED_AMPS;
    cfg.overload_pct         = DEFAULT_OVERLOAD_PCT;
    cfg.imbalance_pct_thresh = DEFAULT_IMBALANCE_PCT_THRESH;
    cfg.temp_alert_c         = DEFAULT_TEMP_ALERT_C;
    cfg.alert_cooldown_sec   = DEFAULT_ALERT_COOLDOWN_SEC;
    cfg.phase_count          = DEFAULT_PHASE_COUNT;

    Serial.begin(115200);

    // I2C must be started before notecard.begin() and before MCP9808 init.
    Wire.begin();

    // Explicitly configure 12-bit ADC resolution before any analogRead call.
    // CT_SCALE and ADC_COUNTS are both derived from this setting; a mismatch
    // shifts every current reading by a large factor (e.g. 4× for 10-bit).
    analogReadResolution(12);

    // note-arduino I2C init.
    notecard.begin();

    // Runtime guard: catch an empty PRODUCT_UID early so the operator sees
    // the root cause on the serial console rather than a silent failure.
    if (PRODUCT_UID[0] == '\0') {
        Serial.println("[init] ERROR: PRODUCT_UID is not set — Notecard cannot "
                       "associate with a Notehub project. Edit PRODUCT_UID in "
                       "the sketch and reflash before deploying.");
    }

    // Confirm the Notecard is ready before touching any sleep-payload state.
    // Skipping the sample cycle on failure preserves the payload intact so
    // the next ATTN wake is not misclassified as a cold boot and the
    // accumulators are not wiped.
    {
        J *ping = notecard.newRequest("card.version");
        if (!notecard.sendRequestWithRetry(ping, 5)) {
            Serial.println("[init] Notecard not responding after 5-second retry "
                           "window — resetting after delay to retry Notecard init");
            delay(DEFAULT_SAMPLE_INTERVAL_SEC * 1000UL);
            NVIC_SystemReset();
            // NVIC_SystemReset() does not return; the line above is the exit point.
        }
    }

    // Attempt to recover persistent state from the Notecard's sleep payload.
    // If this is a genuine cold boot, the call returns false and we start fresh.
    NotePayloadDesc payload;
    coldBoot = !NotePayloadRetrieveAfterSleep(&payload);

    if (!coldBoot) {
        NotePayloadGetSegment(&payload, SEG_ID, &state, sizeof(state));
        NotePayloadFree(&payload);
    } else {
        memset(&state, 0, sizeof(state));
    }

    // Run one-time Notecard configuration on cold boot AND on any subsequent
    // wake where a previous attempt was not confirmed (transient cold-boot
    // failure).  This prevents a single hiccup from becoming a permanent
    // misconfiguration for the rest of the deployment.
    if (!state.hub_configured) {
        state.hub_configured = hubConfigure(PRODUCT_UID);
    }
    if (!state.template_configured) {
        state.template_configured = defineTemplates();
    }

    // Silence the accelerometer to keep the Mojo trace clean on the bench
    // (non-fatal; only attempted on genuine cold boot).
    if (coldBoot) {
        J *req = notecard.newRequest("card.motion.mode");
        JAddBoolToObject(req, "stop", true);
        if (!notecard.sendRequest(req)) {
            Serial.println("[init] card.motion.mode stop failed (non-fatal)");
        }
    }

    // Fetch thresholds from Notehub env vars on every wake (may have changed).
    // fetchEnvOverrides() uses a batch env.get and commits env.modified only
    // on full success, so a transient I²C glitch cannot permanently mask a
    // pending config update.
    fetchEnvOverrides(cfg);

    // Retry any alerts that failed to queue on the previous wake.
    // Each alert type has its own slot so overload and phase_imbalance
    // failures are preserved independently.
    for (uint8_t s = 0; s < ALERT_SLOT_COUNT; s++) {
        if (state.pending_alerts[s].active) {
            PendingAlert &pa = state.pending_alerts[s];
            Serial.print("[alert] retrying pending alert from previous wake: ");
            Serial.println(pa.type);
            sendAlert(s, pa.type, pa.i_a, pa.i_b, pa.i_c, pa.temp_c, pa.extra);
        }
    }

    // ---- Sensor readings -----------------------------------------------
    float i_a    = readCtRms(PIN_CT_A);
    float i_b    = (cfg.phase_count >= 2) ? readCtRms(PIN_CT_B) : 0.0f;
    float i_c    = (cfg.phase_count >= 3) ? readCtRms(PIN_CT_C) : 0.0f;
    float temp_c = readTemperatureC();

    Serial.print("[sample] i_a="); Serial.print(i_a);
    Serial.print(" i_b=");         Serial.print(i_b);
    Serial.print(" i_c=");         Serial.print(i_c);
    Serial.print(" temp_c=");      Serial.println(temp_c);

    // ---- Accumulate into summary window --------------------------------
    bool ct_valid = (i_a > CT_NOISE_FLOOR_A || i_b > CT_NOISE_FLOOR_A ||
                     i_c > CT_NOISE_FLOOR_A);
    if (ct_valid) {
        state.sum_i_a += i_a;
        state.sum_i_b += i_b;
        state.sum_i_c += i_c;
        state.valid_samples++;
    }
    if (temp_c >= -40.0f && temp_c <= 125.0f) {
        state.sum_temp_c += temp_c;
        state.valid_temp_samples++;
    }
    state.total_cycles++;
    state.elapsed_sec += cfg.sample_interval_sec;

    // ---- Tick down alert cooldown counters ------------------------------
    if (state.cd_overload  > 0) state.cd_overload--;
    if (state.cd_imbalance > 0) state.cd_imbalance--;
    if (state.cd_temp      > 0) state.cd_temp--;

    // ---- Alert evaluation ----------------------------------------------
    checkAlerts(i_a, i_b, i_c, temp_c);

    // ---- Summary flush -------------------------------------------------
    // Prefer wall-clock elapsed time from the Notecard's RTC for accurate
    // hourly windows.  Fall back to the accumulated sample-interval estimate
    // (elapsed_sec) when no confirmed time reference is available.
    // Accumulators are only cleared after the note is confirmed queued; on
    // failure the window stays open so data is carried into the next wake.
    uint32_t now_epoch = 0;
    {
        J *req = notecard.newRequest("card.time");
        J *rsp = notecard.requestAndResponse(req);
        if (rsp) {
            now_epoch = (uint32_t)JGetNumber(rsp, "time");
            notecard.deleteResponse(rsp);
        }
    }
    bool time_to_flush = (now_epoch && state.last_summary_epoch)
        ? ((now_epoch - state.last_summary_epoch) >= cfg.summary_interval_min * 60U)
        : (state.elapsed_sec >= cfg.summary_interval_min * 60U);

    if (time_to_flush) {
        if (sendSummary()) {
            // Reset accumulators but keep cooldown counters running.
            state.sum_i_a            = 0.0f;
            state.sum_i_b            = 0.0f;
            state.sum_i_c            = 0.0f;
            state.sum_temp_c         = 0.0f;
            state.valid_samples      = 0;
            state.valid_temp_samples = 0;
            state.total_cycles       = 0;
            state.overload_count     = 0;
            state.elapsed_sec        = 0;
            state.last_summary_epoch = now_epoch;
        }
    }

    // ---- Sleep ---------------------------------------------------------
    // Persist state to Notecard flash and cut host power for
    // sample_interval_sec seconds.  On the next ATTN wake, execution
    // re-enters setup() from the top.
    //
    // NotePayloadSaveAndSleep() frees its descriptor on return regardless of
    // success or failure, so the retry must build a fresh descriptor.
    {
        NotePayloadDesc out = {0, 0, 0};
        NotePayloadAddSegment(&out, SEG_ID, &state, sizeof(state));
        if (!NotePayloadSaveAndSleep(&out, cfg.sample_interval_sec, NULL)) {
            Serial.println("[sleep] NotePayloadSaveAndSleep failed; retrying once");
            NotePayloadDesc out2 = {0, 0, 0};
            NotePayloadAddSegment(&out2, SEG_ID, &state, sizeof(state));
            if (!NotePayloadSaveAndSleep(&out2, cfg.sample_interval_sec, NULL)) {
                Serial.println("[sleep] retry failed — state not persisted; "
                               "next wake is cold boot");
            }
        }
    }

    // card.attn should have cut host power before this point.
    // If it did not (bench debug / wiring fault), reset after the sample
    // interval so setup() re-runs and the sample cycle is not permanently skipped.
    delay(cfg.sample_interval_sec * 1000UL);
    NVIC_SystemReset();
}

// ---------------------------------------------------------------------------
// loop() — intentionally minimal; all real work happens in setup() on each
// wake.  card.attn normally cuts host power before loop() is ever reached.
// This body is a last-resort fallback for bench / wiring-fault scenarios;
// NVIC_SystemReset() forces re-entry into setup() so no sample cycle is
// permanently skipped regardless of whether card.attn controls the power rail.
// ---------------------------------------------------------------------------
void loop() {
    // cfg.sample_interval_sec is initialized to DEFAULT_SAMPLE_INTERVAL_SEC
    // at the top of setup() before any early return, so this guard is belt-
    // and-suspenders — it prevents spinning at 0 s in the unlikely event that
    // the initialization somehow did not execute.
    uint32_t sleep_sec = (cfg.sample_interval_sec > 0)
                         ? cfg.sample_interval_sec
                         : DEFAULT_SAMPLE_INTERVAL_SEC;

    J *req = notecard.newCommand("card.attn");
    JAddStringToObject(req, "mode",    "sleep");
    JAddNumberToObject(req, "seconds", (int)sleep_sec);
    notecard.sendRequest(req);
    delay(sleep_sec * 1000UL);
    // Reset so setup() re-runs the full sample cycle.  Only reached when
    // card.attn did not cut host power (bench debug or wiring fault).
    NVIC_SystemReset();
}
