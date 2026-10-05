/***************************************************************************
  transformer_load_monitor — Utility Distribution Transformer Load Monitor

  Runs on the STM32L433 host embedded in the Blues Notecarrier CX.
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
    6. Sleeps the host in STM32 STOP2 until the Notecard's ATTN pin wakes it
       at the next sample interval (cx_sleep.h).  Execution resumes in place
       with RAM intact, so all runtime state simply lives in the AppState
       struct — nothing is persisted to the Notecard.

  All thresholds (rated_amps, overload_pct, imbalance_pct_thresh,
  temp_alert_c, sample_interval_sec, etc.) are overridable at runtime via
  Notehub environment variables — no re-flash required for field tuning.

  Assumed hardware:
    - Blues Notecarrier CX  (onboard STM32L433 host)
    - Blues Notecard Cell+WiFi (MBGLW) in the M.2 slot
    - 3× YHDC SCT-013-000  (100 A / 50 mA) split-core CTs on A0, A1, A2
      (firmware defaults to phase_count=2, reading A0/A1 only; set
      phase_count=3 via env var for three-phase installations)
    - Per-channel bias circuit: 22 Ω burden, 2× 10 kΩ divider, 10 µF cap
    - Adafruit MCP9808 I²C temperature sensor (default address 0x18)
    - Jumper from the Notecarrier CX ATTN pin to D5 (same 16-pin header).
      Leave EN unconnected: on the CX it enables the shared 3.3 V VIO rail,
      so ATTN→EN browns out the board instead of sleeping the host.

  Build: Tools > USB support (if available) > None (usb=none).  With the USB
  CDC stack enabled and no USB host attached, the USB wakeup interrupt exits
  STOP2 immediately.  Debug output goes to the LPUART on the CX debug jack,
  which an ST-LINK V3 exposes as a virtual COM port (debugSerial below).

  Dependencies:
    - Blues Wireless Notecard library (note-arduino)
      https://github.com/blues/note-arduino
    - Adafruit MCP9808 library (install via Arduino Library Manager)
      https://github.com/adafruit/Adafruit_MCP9808_Library
    - STM32duino Low Power (+ its dependency STM32duino RTC)
      https://github.com/stm32duino/STM32LowPower
    - STM32duino Arduino core (board: Blues boards → Cygnet, pnum=CYGNET)
      https://github.com/stm32duino/Arduino_Core_STM32

  Helper functions, struct definitions, and all constants are in
  transformer_load_monitor_helpers.h / .cpp.
***************************************************************************/

#include "transformer_load_monitor_helpers.h"
#include "cx_sleep.h"

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
AppState         state;
EnvConfig        cfg;
// Debug output: LPUART1 on the CX debug jack (ST-LINK virtual COM port).
// USB CDC is disabled for STOP2, so Serial is not available.
Uart             debugSerial(PIN_VCP_RX, PIN_VCP_TX);

// ---------------------------------------------------------------------------
// setup() — runs once at power-up.  The host resumes in place after each
// STOP2 sleep, so one-time Notecard configuration lives here and every
// per-wake step lives in loop().
// ---------------------------------------------------------------------------
void setup() {
    // Initialize cfg to safe defaults so loop() never runs with a zero sleep
    // interval before the first successful env fetch.  The real values are
    // loaded by fetchEnvOverrides() on every wake.
    cfg.sample_interval_sec  = DEFAULT_SAMPLE_INTERVAL_SEC;
    cfg.summary_interval_min = DEFAULT_SUMMARY_INTERVAL_MIN;
    cfg.rated_amps           = DEFAULT_RATED_AMPS;
    cfg.overload_pct         = DEFAULT_OVERLOAD_PCT;
    cfg.imbalance_pct_thresh = DEFAULT_IMBALANCE_PCT_THRESH;
    cfg.temp_alert_c         = DEFAULT_TEMP_ALERT_C;
    cfg.alert_cooldown_sec   = DEFAULT_ALERT_COOLDOWN_SEC;
    cfg.phase_count          = DEFAULT_PHASE_COUNT;

    debugSerial.begin(115200);

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
        debugSerial.println("[init] ERROR: PRODUCT_UID is not set — Notecard cannot "
                            "associate with a Notehub project. Edit PRODUCT_UID in "
                            "the sketch and reflash before deploying.");
    }

    // Known-zero state at power-up.  STOP2 retains RAM, so from here on the
    // accumulators, cooldowns, and pending alerts carry across every wake.
    memset(&state, 0, sizeof(state));

    // Confirm the Notecard is ready before the one-time configuration.  The
    // host can come up before the Notecard's I²C stack after a cold power-up;
    // the configuration flags below are retried on every wake until confirmed,
    // so a failed ping here only defers them.
    {
        J *ping = notecard.newRequest("card.version");
        if (!notecard.sendRequestWithRetry(ping, 5)) {
            debugSerial.println("[init] Notecard not responding after 5-second retry "
                                "window — configuration deferred to the next wake");
        }
    }

    // One-time Notecard configuration.  Each flag is set only when the
    // Notecard confirms the request; loop() retries anything still false.
    state.hub_configured      = hubConfigure(PRODUCT_UID);
    state.template_configured = defineTemplates();

    // Silence the accelerometer to keep the Mojo trace clean on the bench
    // (non-fatal).
    {
        J *req = notecard.newRequest("card.motion.mode");
        JAddBoolToObject(req, "stop", true);
        if (!notecard.sendRequest(req)) {
            debugSerial.println("[init] card.motion.mode stop failed (non-fatal)");
        }
    }

    // Arm the ATTN wake: the Notecard raises ATTN (D5) to end each sleep.
    cxSleepBegin();
}

// ---------------------------------------------------------------------------
// loop() — one sample cycle, then sleep in STOP2 until the Notecard raises
// ATTN sample_interval_sec from now.
// ---------------------------------------------------------------------------
void loop() {
    // Retry one-time Notecard configuration on any wake where a previous
    // attempt was not confirmed (transient cold-boot failure).  This prevents
    // a single hiccup from becoming a permanent misconfiguration for the rest
    // of the deployment.
    if (!state.hub_configured) {
        state.hub_configured = hubConfigure(PRODUCT_UID);
    }
    if (!state.template_configured) {
        state.template_configured = defineTemplates();
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
            debugSerial.print("[alert] retrying pending alert from previous wake: ");
            debugSerial.println(pa.type);
            sendAlert(s, pa.type, pa.i_a, pa.i_b, pa.i_c, pa.temp_c, pa.extra);
        }
    }

    // ---- Sensor readings -----------------------------------------------
    float i_a    = readCtRms(PIN_CT_A);
    float i_b    = (cfg.phase_count >= 2) ? readCtRms(PIN_CT_B) : 0.0f;
    float i_c    = (cfg.phase_count >= 3) ? readCtRms(PIN_CT_C) : 0.0f;
    float temp_c = readTemperatureC();

    debugSerial.print("[sample] i_a="); debugSerial.print(i_a);
    debugSerial.print(" i_b=");         debugSerial.print(i_b);
    debugSerial.print(" i_c=");         debugSerial.print(i_c);
    debugSerial.print(" temp_c=");      debugSerial.println(temp_c);

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
    // Ask the Notecard to hold ATTN low for sample_interval_sec, then enter
    // STOP2.  When ATTN rises, execution resumes here and loop() runs again
    // with `state` intact in RAM.
    uint32_t sleep_sec = (cfg.sample_interval_sec > 0)
                         ? cfg.sample_interval_sec
                         : DEFAULT_SAMPLE_INTERVAL_SEC;
    if (!cxSleepUntilAttn(notecard, sleep_sec, NULL, &debugSerial)) {
        // The Notecard didn't take the sleep request, or ATTN never went low
        // (check the ATTN -> D5 jumper).  Keep the sample cadence and retry.
        debugSerial.println("[sleep] ATTN sleep failed — waiting out the interval awake");
        delay(sleep_sec * 1000UL);
    }
}
