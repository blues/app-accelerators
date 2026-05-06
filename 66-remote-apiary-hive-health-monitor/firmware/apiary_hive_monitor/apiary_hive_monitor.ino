/***************************************************************************
  apiary_hive_monitor.ino — Remote Apiary Hive Health Monitor

  Blues Notecarrier CX (Cygnet STM32L433) + Notecard Cell+WiFi + Starnote
  for Skylo. Reads hive weight (HX711 + load cell), brood-box temperature
  and humidity (SHT31-D), and acoustic features (ZCR, RMS, peak) from an
  analog microphone — no raw audio is buffered or transmitted.
  Pushes compact-template daily summaries plus immediate alerts for weight
  loss, temperature excursion, and hive-tone anomaly.

  Hardware:
    - Notecarrier CX (onboard Cygnet STM32L433 host MCU)
    - Notecard Cell+WiFi (MBGLW) in M.2 slot
    - Starnote for Skylo via 6-pin JST NTN cable to Notecard
    - SparkFun HX711 Load Cell Amplifier (SEN-13879)  →  D5 / D6
    - Zemic H8C 100 kg single-ended shear-beam load cell (Wheatstone bridge) → HX711
    - Adafruit SHT31-D Temperature & Humidity Sensor (#2857)  → SDA / SCL
    - Adafruit MAX9814 Electret Microphone Amplifier (#1713)  → A0
    - SparkFun Sunny Buddy MPPT Solar Charger (PRT-12885)
    - Blues Mojo inline on +VBAT (bench power validation)

  Libraries (install via Arduino Library Manager):
    - "Blues Wireless Notecard"  (note-arduino)
    - "HX711 Arduino Library"    by Bogdan Necula & Lukas Bachschwell
    - "Adafruit SHT31 Library"   by Adafruit

  See README.md for full wiring, Notehub setup, and calibration instructions.
  Sensor and Notecard helpers live in apiary_hive_monitor_helpers.h / .cpp.
  DEBUG_SERIAL is controlled in apiary_hive_monitor_helpers.h.
***************************************************************************/

#include <Wire.h>
#include <Notecard.h>
#include <HX711.h>
#include <Adafruit_SHT31.h>
#include "apiary_hive_monitor_helpers.h"

// ---------------------------------------------------------------------------
// Product UID — replace with your Notehub ProductUID before flashing.
// Leaving this empty produces a hard compile error so a misconfigured binary
// cannot be accidentally deployed to a remote site. For local development
// without a Notehub project yet, add -DALLOW_EMPTY_PRODUCT_UID to the build
// flags as an explicit override — that flag must not appear in a shipping
// build.
// ---------------------------------------------------------------------------
#ifndef PRODUCT_UID
#  ifndef ALLOW_EMPTY_PRODUCT_UID
#    error "PRODUCT_UID is not set. Define it as your Notehub ProductUID before flashing (e.g. -DPRODUCT_UID='\"com.your-company.your-project:your-device\"'). For local development without a project, add -DALLOW_EMPTY_PRODUCT_UID to suppress this error — that flag must not appear in a shipping build."
#  else
#    define PRODUCT_UID ""
#    pragma message "PRODUCT_UID empty (ALLOW_EMPTY_PRODUCT_UID override active) — device will not associate with any Notehub project"
#  endif
#endif

// ---------------------------------------------------------------------------
// Pin assignments (Notecarrier CX dual 16-pin headers)
// ---------------------------------------------------------------------------
#define PIN_HX711_DOUT  D5    // HX711 serial data output
#define PIN_HX711_SCK   D6    // HX711 clock / power-down control

// A0 for the MAX9814 microphone is referenced in apiary_hive_monitor_helpers.cpp

// ---------------------------------------------------------------------------
// Sampling and timing defaults (all tunable via Notehub environment variables)
// ---------------------------------------------------------------------------
#define DEFAULT_SAMPLE_INTERVAL_MIN   15      // wake every N minutes
#define DEFAULT_REPORT_INTERVAL_HR    24      // daily summary
#define DEFAULT_WEIGHT_ALERT_KG_DROP  2.0f    // kg loss triggering a weight_drop alert
#define DEFAULT_TEMP_LOW_C            32.0f   // brood cold threshold (°C)
#define DEFAULT_TEMP_HIGH_C           36.0f   // brood heat threshold (°C)
#define DEFAULT_AUDIO_ZCR_ALERT       1200    // zero-crossing rate anomaly threshold
#define DEFAULT_HX711_CALIBRATION     2280.0f // ADC counts per kg — tune with known weight!
#define DEFAULT_HX711_ZERO_OFFSET_KG  0.0f    // kg subtracted after calibration (platform tare)

// One alert per condition per hour maximum
#define ALERT_COOLDOWN_MIN            60

// Notecard state payload segment tag
static const char STATE_SEG_ID[] = "HIVE";

// ---------------------------------------------------------------------------
// Global sensor and Notecard objects (also referenced by helpers.cpp via extern)
// ---------------------------------------------------------------------------
Notecard        notecard;
HX711           scale;
Adafruit_SHT31  sht31;

// ===========================================================================
// setup() — full application runs here each wake cycle; loop() is unreachable
// ===========================================================================
void setup() {
    // Serial and Notecard debug output are gated on DEBUG_SERIAL (defined in
    // apiary_hive_monitor_helpers.h).  Leave the flag undefined for deployed
    // hardware to avoid the wake-time overhead and UART idle draw.
#ifdef DEBUG_SERIAL
    Serial.begin(115200);
    notecard.setDebugOutputStream(Serial);
#endif

    Wire.begin();
    notecard.begin();  // I2C to Notecard; Notecarrier CX has onboard pull-ups

    // ---- Restore persisted state from the previous sleep cycle -----------
    NotePayloadDesc payload;
    HiveState st;
    bool restored = NotePayloadRetrieveAfterSleep(&payload);
    if (restored) {
        restored &= NotePayloadGetSegment(&payload, STATE_SEG_ID, &st, sizeof(st));
        NotePayloadFree(&payload);
    }
    if (!restored) {
        memset(&st, 0, sizeof(st));
        st.first_boot      = true;
        st.weight_first_kg = -1.0f;  // sentinel: no reading yet this window
        st.weight_last_kg  = -1.0f;
    }

    // ---- One-time Notecard configuration on true cold boot ---------------
    // Both steps must succeed before clearing first_boot; a transient I²C
    // or Notecard-side error leaves first_boot = true so setup retries next wake
    // rather than permanently skipping configuration.
    bool configOk = notecardConfigure(st.first_boot, PRODUCT_UID);
    if (st.first_boot) {
        if (configOk && defineTemplates()) {
            st.first_boot = false;
            // Seed stored_outbound_min with the default so the cadence
            // re-alignment block below does not issue a redundant hub.set
            // on this same wake (notecardConfigure already set outbound to
            // DEFAULT_REPORT_INTERVAL_HR * 60 minutes).
            st.stored_outbound_min = (uint16_t)(DEFAULT_REPORT_INTERVAL_HR * 60u);
        }
        // If any step failed, leave first_boot = true; retry next wake.
    }

    // ---- Abort cycle if boot config is incomplete -----------------------
    // Compact templates (hive_summary.qo / hive_alert.qo) must be registered
    // before any note.add so that Starnote compact-payload encoding is
    // guaranteed.  If notecardConfigure() or defineTemplates() failed this
    // wake, persist state and sleep immediately — no env fetch, no sensor
    // reads, no note emission.  first_boot stays true so the next wake
    // retries configuration automatically.
    if (st.first_boot) {
#ifdef DEBUG_SERIAL
        Serial.println("[APP] Boot config incomplete — sleeping to retry");
#endif
        NotePayloadDesc retryPayload = {0, 0, 0};
        NotePayloadAddSegment(&retryPayload, STATE_SEG_ID, &st, sizeof(st));
        NotePayloadSaveAndSleep(&retryPayload,
                                (uint32_t)DEFAULT_SAMPLE_INTERVAL_MIN * 60u, NULL);
        delay((uint32_t)DEFAULT_SAMPLE_INTERVAL_MIN * 60000UL);
        return;
    }

    // ---- Fetch env var overrides from Notehub ----------------------------
    uint16_t sampleMin    = DEFAULT_SAMPLE_INTERVAL_MIN;
    uint16_t reportHr     = DEFAULT_REPORT_INTERVAL_HR;
    float    weightDrop   = DEFAULT_WEIGHT_ALERT_KG_DROP;
    float    tempLow      = DEFAULT_TEMP_LOW_C;
    float    tempHigh     = DEFAULT_TEMP_HIGH_C;
    uint16_t audioZcr     = DEFAULT_AUDIO_ZCR_ALERT;
    float    calibration  = DEFAULT_HX711_CALIBRATION;
    float    zeroOffsetKg = DEFAULT_HX711_ZERO_OFFSET_KG;
    bool     resetSeen    = false;
    fetchEnvOverrides(sampleMin, reportHr, weightDrop, tempLow, tempHigh, audioZcr, calibration, zeroOffsetKg, resetSeen);

    // ---- Commissioning reset — edge-triggered, one-shot per 0→1 transition -
    // The Notecard's env var cache persists the last-fetched values until a new
    // inbound sync delivers an update; with inbound=10080 (one week), the device
    // can see reset_state=1 on every wake for up to seven days after the operator
    // sets it.  To prevent a perpetual reset loop, the reset fires only when
    // resetSeen transitions from false (0/absent) to true ("1") — never when the
    // value is unchanged from the previous wake.
    //
    // last_reset_token persists the last-acted value across sleep cycles:
    //   0 → reset_state was absent or "0" on the last wake
    //   1 → reset_state was "1" on the last wake (reset already fired)
    //
    // Flow for a single operator-requested reset:
    //   Wake N  : resetSeen=true, last_reset_token=0 → forceReset=true  → fires
    //   Wake N+1: resetSeen=true, last_reset_token=1 → forceReset=false → skipped
    //   ...same until operator clears the var and a new inbound delivers it...
    //   Wake M  : resetSeen=false, last_reset_token updated to 0
    //   Wake M+n: if operator sets reset_state=1 again → fires exactly once more
    bool forceReset = (resetSeen && st.last_reset_token == 0);
    st.last_reset_token = resetSeen ? 1u : 0u;

    if (forceReset) {
        st.weight_sum_kg       = 0.0f;
        st.weight_first_kg     = -1.0f;
        st.weight_last_kg      = -1.0f;
        st.weight_sample_count = 0;
        st.temp_sum_c          = 0.0f;
        st.humidity_sum_pct    = 0.0f;
        st.temp_valid_count    = 0;
        st.zcr_sum             = 0.0f;
        st.rms_sum             = 0.0f;
        st.peak_sum            = 0.0f;
        st.audio_sample_count  = 0;
        st.audio_sampled       = false;
        st.sample_count        = 0;
        st.last_report_epoch   = 0;
#ifdef DEBUG_SERIAL
        Serial.println("[APP] reset_state=1 (0→1 transition): summary window cleared; time anchor will re-establish on next card.time");
#endif
    }

    // ---- Re-align Notecard outbound cadence if report_interval_hr changed
    // hub.set is sent on first boot in notecardConfigure(); after that,
    // env var changes to report_interval_hr must be reflected here so the
    // Notecard's outbound sync timer stays aligned with the summary cadence.
    // stored_outbound_min is only updated when the Notecard confirms the request
    // without an err field; a rejected or failed hub.set leaves stored_outbound_min
    // unchanged so the next wake retries automatically.
    uint16_t desiredOutbound = (uint16_t)reportHr * 60u;
    if (st.stored_outbound_min != desiredOutbound) {
        J *syncReq = notecard.newRequest("hub.set");
        JAddStringToObject(syncReq, "mode",     "periodic");
        JAddNumberToObject(syncReq, "outbound", desiredOutbound);
        JAddNumberToObject(syncReq, "inbound",  10080);
        J *syncRsp = notecard.requestAndResponse(syncReq);
        if (syncRsp != NULL) {
            const char *syncErr = JGetString(syncRsp, "err");
            if (!syncErr || !*syncErr) {
                st.stored_outbound_min = desiredOutbound;
            }
            notecard.deleteResponse(syncRsp);
        }
    }

    // ---- Initialise sensors ----------------------------------------------
    scale.begin(PIN_HX711_DOUT, PIN_HX711_SCK);

    if (!sht31.begin(0x44)) {  // 0x44 is SHT31-D default I2C address
#ifdef DEBUG_SERIAL
        Serial.println("[APP] SHT31-D not found — check wiring");
#endif
    }

    // ---- Read all sensors ------------------------------------------------
    float weight_kg = readWeightKg(calibration);

    // Apply platform tare offset (set via hx711_zero_offset_kg env var;
    // see §8 commissioning procedure in README.md for how to determine this value).
    // readWeightKg() returns -1.0f on timeout/error; any value >= 0.0f is a
    // valid measurement.  A true-zero reading (empty or removed hive platform)
    // is valid and must not be discarded.
    bool weightValid = (weight_kg >= 0.0f);
    if (weightValid) {
        weight_kg -= zeroOffsetKg;
        if (weight_kg < 0.0f) weight_kg = 0.0f;
    }
    scale.power_down();    // cut ~1.5 mA HX711 idle draw for sleep period

    float temp_c = -9999.0f, humidity_pct = -9999.0f;
    bool  tempValid = readTempHumidity(temp_c, humidity_pct);

    // Audio variables — populated after window-expiry evaluation below.
    // Placing the capture after the expiry block ensures the rollover wake
    // itself becomes the first wake of the new window for every sensor path.
    float zcr_mean = 0.0f, rms_mean = 0.0f, peak_mean = 0.0f;
    bool  audioAttempted = false;
    bool  audioValid     = false;

    // ---- Get epoch for window-expiry check and alert debounce ------------
    // Alerts are suppressed until now > 0 (valid time sync). This prevents
    // repeated alert storms during cold boot or when operating indoors before
    // the first successful GPS/network time acquisition — both of which would
    // otherwise bypass the cooldown and trigger a cellular/NTN session on
    // every wake cycle.
    uint32_t now = 0;
    {
        J *rsp = notecard.requestAndResponse(notecard.newRequest("card.time"));
        if (rsp != NULL) {
            const char *terr = JGetString(rsp, "err");
            if (!terr || !*terr) {
                now = (uint32_t)JGetInt(rsp, "time");
            }
            notecard.deleteResponse(rsp);
        }
    }

    // ---- Window expiry — must run BEFORE accumulating current readings ----
    // Evaluating expiry first ensures the boundary sample is never counted in
    // both the outgoing summary and the new window simultaneously.
    //
    // On the very first wake with valid time (last_report_epoch == 0) any
    // pre-anchor samples accumulated while card.time was unavailable are
    // discarded; the window is anchored to now and current readings seed it.
    //
    // On a normal rollover: sendSummary() sees the frozen previous-window data
    // (current readings have not been added yet).  On success the accumulators
    // are cleared and last_report_epoch is advanced so the current readings
    // become the first sample of the new window.  On failure the frozen
    // snapshot is preserved intact for retry next wake; the current readings
    // are skipped this cycle to avoid polluting the pending summary.
    bool accumulateThisWake = true;
    if (now > 0) {
        if (st.last_report_epoch == 0) {
            // Anchor: discard pre-anchor data; current readings will seed the
            // new window in the accumulation block below.
            st.weight_sum_kg       = 0.0f;
            st.weight_first_kg     = -1.0f;
            st.weight_last_kg      = -1.0f;
            st.weight_sample_count = 0;
            st.temp_sum_c          = 0.0f;
            st.humidity_sum_pct    = 0.0f;
            st.temp_valid_count    = 0;
            st.zcr_sum             = 0.0f;
            st.rms_sum             = 0.0f;
            st.peak_sum            = 0.0f;
            st.audio_sample_count  = 0;
            st.audio_sampled       = false;
            st.sample_count        = 0;
            st.last_report_epoch   = now;
        } else if (now - st.last_report_epoch >= (uint32_t)(reportHr * 3600u)) {
            // Window expired: send the frozen previous-window snapshot.
            // sample_count == 0 means nothing accumulated this window (e.g.
            // every read errored); skip the send and just advance the epoch so
            // subsequent wakes start a fresh window rather than re-evaluating
            // expiry indefinitely.
            bool sent = (st.sample_count == 0) || sendSummary(st);
            if (sent) {
                st.weight_sum_kg       = 0.0f;
                st.weight_first_kg     = -1.0f;
                st.weight_last_kg      = -1.0f;
                st.weight_sample_count = 0;
                st.temp_sum_c          = 0.0f;
                st.humidity_sum_pct    = 0.0f;
                st.temp_valid_count    = 0;
                st.zcr_sum             = 0.0f;
                st.rms_sum             = 0.0f;
                st.peak_sum            = 0.0f;
                st.audio_sample_count  = 0;
                st.audio_sampled       = false;
                st.sample_count        = 0;
                st.last_report_epoch   = now;
                // accumulateThisWake stays true; current readings seed the new window.
            } else {
                // sendSummary() failed — preserve frozen snapshot for retry.
                // Current readings are not accumulated this cycle so the pending
                // summary remains stable until the next successful send.
                accumulateThisWake = false;
            }
        }
    }

    // ---- Audio sampling — after window-expiry so the rollover wake IS the ----
    // first wake of the new window for every sensor path.  On a successful
    // rollover, st.audio_sampled has just been reset to false, so this block
    // fires on the rollover wake itself.  On a failed rollover (sendSummary()
    // returned false) st.audio_sampled is still true from the previous window
    // so the block is skipped — the frozen summary is preserved for retry.
    //
    // Sampling on every 15-minute wake would add ~30 mA × 0.75 s per cycle
    // with no additional actionable information; colony acoustic state changes
    // on the scale of hours, not minutes.
    //
    // readAudioFeatures() returns false when the signal is implausible (DC
    // offset outside the mid-rail band, > 30 % samples at ADC rail, or RMS
    // below the minimum threshold).  audioAttempted is set regardless of
    // validity so a persistently bad sensor emits -9999 in the summary
    // rather than retrying every 15 minutes for the rest of the window.
    if (!st.audio_sampled) {
        audioAttempted = true;
        audioValid = readAudioFeatures(zcr_mean, rms_mean, peak_mean);
#ifdef DEBUG_SERIAL
        if (!audioValid)
            Serial.println("[APP] Audio invalid — mic disconnected, ADC railed, or near-zero variance");
#endif
    }

    // ---- Accumulate into summary-window aggregates -----------------------
    if (accumulateThisWake) {
        if (weightValid) {
            st.weight_sum_kg += weight_kg;
            st.weight_last_kg = weight_kg;
            if (st.weight_first_kg < 0.0f) st.weight_first_kg = weight_kg;
            st.weight_sample_count++;
        }
        if (tempValid) {
            st.temp_sum_c       += temp_c;
            st.humidity_sum_pct += humidity_pct;
            st.temp_valid_count++;          // count only good reads so avg stays unbiased
        }
        if (audioAttempted) {
            // Mark as attempted regardless of validity — a persistently bad
            // sensor shows -9999 in the summary rather than retrying every
            // wake for the rest of the window.
            st.audio_sampled = true;
        }
        if (audioValid) {
            st.zcr_sum  += zcr_mean;
            st.rms_sum  += rms_mean;
            st.peak_sum += peak_mean;
            st.audio_sample_count++;
        }
        st.sample_count++;
    }

    // ---- Evaluate alert rules --------------------------------------------

    // Rule 1: significant weight loss — swarm, theft, or starvation.
    // weight_first_kg < 0.0f means no prior valid reading exists this window;
    // a reading of exactly 0.0 kg (empty or removed platform) is valid.
    // Cooldown timestamp is only advanced on confirmed delivery so that a
    // transient note.add failure is retried on the next wake.
    if (now > 0 && weightValid && st.weight_first_kg >= 0.0f) {
        float drop = st.weight_first_kg - weight_kg;
        if (drop >= weightDrop &&
            now - st.alert_weight_ts > (uint32_t)(ALERT_COOLDOWN_MIN * 60u)) {
            if (sendAlert("weight_drop", drop, weight_kg)) {
                st.alert_weight_ts = now;
            }
        }
    }

    // Rule 2: brood temperature outside viable range — chilling or overheating
    if (now > 0 && tempValid && (temp_c < tempLow || temp_c > tempHigh) &&
        now - st.alert_temp_ts > (uint32_t)(ALERT_COOLDOWN_MIN * 60u)) {
        if (sendAlert("temp_anomaly", temp_c, humidity_pct)) {
            st.alert_temp_ts = now;
        }
    }

    // Rule 3: acoustic behavioral anomaly — ZCR exceeds baseline; specific cause
    //          requires physical inspection (see §9 Limitations in README.md).
    //          Only evaluated on the one wake cycle per window when audio was sampled.
    if (now > 0 && audioValid && zcr_mean > (float)audioZcr &&
        now - st.alert_audio_ts > (uint32_t)(ALERT_COOLDOWN_MIN * 60u)) {
        if (sendAlert("audio_anomaly", zcr_mean, rms_mean)) {
            st.alert_audio_ts = now;
        }
    }

    // ---- Persist state and sleep until next sample window ----------------
    NotePayloadDesc sleepPayload = {0, 0, 0};
    NotePayloadAddSegment(&sleepPayload, STATE_SEG_ID, &st, sizeof(st));
    NotePayloadSaveAndSleep(&sleepPayload, (uint32_t)sampleMin * 60u, NULL);

    // Fallback only if ATTN pin is not wired (e.g., bare USB bench test)
#ifdef DEBUG_SERIAL
    Serial.println("[APP] card.attn not wired — using delay() fallback");
#endif
    delay((uint32_t)sampleMin * 60000UL);
}

// loop() is unreachable when NotePayloadSaveAndSleep cuts host power
void loop() {}
