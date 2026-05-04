/*
 * Off-Grid Solar + Battery Bank Monitor
 *
 * Reads two Victron VE.Direct devices:
 *   - SmartShunt 500A  (Serial1)              → SoC, voltage, current, temperature, time-to-go
 *   - SmartSolar MPPT  (D9 / SoftwareSerial)  → panel V/W, daily yield, charge state
 *
 * Accumulates readings across wake cycles, transmits periodic summaries to
 * Notehub, and sends immediate alerts for low SoC, high battery temperature,
 * and excessive load draw.
 *
 * Board:    Blues Notecarrier CX (onboard Cygnet STM32L433 host)
 * Notecard: NOTE-MBGLW (cellular) or NOTE-NBGLWX (Skylo NTN satellite) in M.2 slot
 * Library:  Blues Wireless Notecard (note-arduino) v1.8.5
 *
 * Dependencies (install via Arduino Library Manager):
 *   - "Blues Wireless Notecard"  (note-arduino v1.8.5)
 *   - STMicroelectronics STM32 core (stm32duino) with Cygnet board support
 *
 * Source is split across three modules:
 *   solar_battery_controller.ino            — orchestration (this file)
 *   solar_battery_controller_helpers.*      — VE.Direct UART frame parser
 *   solar_battery_controller_notecard_helpers.* — Notecard I/O, PersistState,
 *                                                  all configuration constants
 *
 * VE.Direct TX is 5V logic; use a 10kΩ/20kΩ resistor divider on each RX
 * line to bring it down to 3.3V for the STM32L433.  See README §4.
 *
 * Sync profile (runtime-configurable via Notehub environment variables):
 *   Default: outbound cadence equals report_interval_min (240 min = 4 h);
 *   inbound is 2× outbound (480 min = 8 h).  Both are re-derived on every
 *   boot so changing report_interval_min via env var also adjusts the
 *   Notecard's sync cadence automatically.  For Skylo NTN satellite
 *   deployments set report_interval_min=1440 in the Fleet Environment to
 *   limit satellite sessions to once per day.  Use sync_outbound_min and
 *   sync_inbound_min env vars to override the derived cadence independently
 *   when needed.  The same binary runs on both SKUs.
 *
 * Alert strategy:
 *   Each alert fires on the rising edge and then repeats every
 *   ALERT_COOLDOWN_SAMPLES wakes (~30 min at the default 15-min interval)
 *   while the fault persists.  The cooldown prevents a persistent condition
 *   from generating a sync:true session on every wake — important on Skylo
 *   NTN deployments where each session consumes satellite data budget.
 *   Active flags and cooldown counters are persisted in PersistState across
 *   sleep cycles.
 *
 * Debug:
 *   Add  -DBLUES_DEBUG  to build flags to enable the 500 ms serial-attach
 *   delay on every wake.
 */

#include <Notecard.h>
#include <SoftwareSerial.h>
#include "solar_battery_controller_helpers.h"
#include "solar_battery_controller_notecard_helpers.h"

// ---------------------------------------------------------------------------
// Configuration — set PRODUCT_UID to your Notehub project before flashing
// ---------------------------------------------------------------------------
#ifndef PRODUCT_UID
#define PRODUCT_UID ""  // "com.your-company.your-name:solar-battery-site"
#pragma message "PRODUCT_UID not defined — set it before deploying."
#endif

// VE.Direct UART pins
// Serial1 (RX = CX header RX pin, TX unused) → SmartShunt
// D9 / SoftwareSerial → SmartSolar MPPT
//
// NOTE: SoftwareSerial is used here because the Cygnet's second hardware UART
// is not exposed at a convenient position on the standard CX header for the
// MPPT path.  For production deployments with heavy interrupt activity (e.g.
// if additional interrupt-driven sensors are added), prefer a hardware UART
// to avoid SoftwareSerial's interrupt-latency sensitivity.  See README §4
// for wiring details and the trade-off discussion.
#define MPPT_RX_PIN     D9    // SoftwareSerial RX for SmartSolar MPPT
#define MPPT_TX_PIN     D10   // SoftwareSerial TX (not driven — leave NC)
#define VED_BAUD        19200

// ---------------------------------------------------------------------------
// Globals
// ---------------------------------------------------------------------------

// Notecard I2C (no explicit address needed for default I2C)
Notecard notecard;

// Second VE.Direct UART (SmartSolar MPPT)
SoftwareSerial mpptSerial(MPPT_RX_PIN, MPPT_TX_PIN);

// PersistState serialised to Notecard flash — layout defined in
// solar_battery_controller_notecard_helpers.h
PersistState state;

// Desired Notecard sync cadence — re-derived from report_interval_min (or
// explicit sync_outbound/inbound_min env vars) on every boot by
// fetchEnvOverrides().  Initialised to 0; fetchEnvOverrides() always writes
// valid values before applyHubSetIfChanged() reads them.  Declared without
// static so the notecard helpers translation unit can access them via extern.
uint32_t desired_outbound_min = 0;
uint32_t desired_inbound_min  = 0;

// ---------------------------------------------------------------------------
// Forward declarations (orchestration helpers defined below)
// ---------------------------------------------------------------------------
static uint16_t samplesPerWindow(uint32_t report_min, uint32_t sample_sec);
static void  accumulate(const VEDirectData &shunt, const VEDirectData &mppt);
static void  checkAndSendAlerts(const VEDirectData &shunt, const VEDirectData &mppt);
static void  checkHarvestDeficit();
static void  resetAccumulators();

// ---------------------------------------------------------------------------
// setup() — entry point after every wake (cold boot or ATTN-triggered)
// ---------------------------------------------------------------------------
void setup() {
    Serial.begin(115200);
#ifdef BLUES_DEBUG
    delay(500);   // Allow serial monitor to attach on bench
#endif

    // notecard.begin() must come before NotePayloadRetrieveAfterSleep so the
    // I2C bus is ready for any Notecard communication during restore.
    notecard.begin();

    // Attempt to restore persisted state from Notecard flash.
    NotePayloadDesc payload;
    bool restored = NotePayloadRetrieveAfterSleep(&payload);
    if (restored) {
        restored &= NotePayloadGetSegment(&payload, STATE_SEG_ID,
                                          &state, sizeof(state));
        NotePayloadFree(&payload);
    }

    // Validate the layout guard.  A magic or version mismatch means the struct
    // changed since the last flash — discard stale content and start clean
    // rather than deserialising garbage into the new layout.
    if (restored &&
        (state.magic != STATE_MAGIC || state.version != STATE_VERSION)) {
        Serial.println(F("[info] PersistState layout changed; starting fresh."));
        restored = false;
    }

    if (!restored) {
        memset(&state, 0, sizeof(state));
        state.magic   = STATE_MAGIC;
        state.version = STATE_VERSION;
        state.soc_alert_pct        = DEFAULT_SOC_ALERT_PCT;
        state.bat_temp_max_c       = DEFAULT_BAT_TEMP_MAX_C;
        state.load_alert_w         = DEFAULT_LOAD_ALERT_W;
        state.sample_interval_sec  = DEFAULT_SAMPLE_INTERVAL_SEC;
        state.report_interval_min  = DEFAULT_REPORT_INTERVAL_MIN;
        state.harvest_deficit_days = 0.0f;
        state.ttg_min              = -1;
        // last_outbound_min / last_inbound_min default to 0, so the
        // applyHubSetIfChanged() call later in this block and the call after
        // fetchEnvOverrides() will both issue hub.set on first boot.
        // templates_confirmed defaults to false via memset; defineTemplates()
        // is called unconditionally below so the first-boot and restored paths
        // share the same retry logic.
        state.samples_until_summary =
            samplesPerWindow(state.report_interval_min, state.sample_interval_sec);

        notecardFirstBoot();

        // Associate with Notehub immediately on first boot, using default
        // cadence, so fetchEnvOverrides() below can reach Notehub and pull
        // any pre-configured Fleet environment variables (e.g. Skylo daily
        // cadence set before commissioning) on this same wake cycle rather
        // than deferring them to the second boot.  On subsequent boots the
        // Notecard is already associated; the applyHubSetIfChanged() call
        // after fetchEnvOverrides() handles any env-var-driven cadence drift.
        desired_outbound_min = state.report_interval_min;
        desired_inbound_min  = state.report_interval_min * 2UL;
        applyHubSetIfChanged(PRODUCT_UID);
    }

    // Register the solar_summary.qo template on every boot until confirmed.
    // note.template is idempotent on the Notecard, so re-registering an already-
    // active template is harmless.  Retrying on every boot until success ensures
    // that a transient I2C failure at first boot is recovered automatically, and
    // that a future firmware revision that changes the template shape (which bumps
    // STATE_VERSION, clears templates_confirmed via memset, and triggers a clean
    // init above) re-registers on the very next wake without operator intervention.
    if (!state.templates_confirmed) {
        state.templates_confirmed = defineTemplates();
    }

    // Refresh thresholds and desired sync cadence from Notehub env vars.
    // If sample or report interval changed, flush the current accumulation
    // window before the new cadence takes effect so no summary ever mixes
    // samples from two different window lengths.
    if (fetchEnvOverrides()) {
        // Cadence changed — best-effort flush of the partial window.  If the
        // send fails the partial data is lost, but window contamination
        // (averages spanning two different window lengths) is the worse outcome.
        bool has_samples = (state.bat_v_cnt > 0.0f || state.pv_w_cnt > 0.0f);
        if (has_samples) {
            checkHarvestDeficit();
            sendSummary();   // best-effort; ignore return value
        }
        resetAccumulators();
        state.samples_until_summary =
            samplesPerWindow(state.report_interval_min, state.sample_interval_sec);
    }

    // Apply hub.set on every boot — ensures outbound/inbound cadence is
    // authoritative for the current firmware and env-var settings, even after
    // a new binary is flashed or stale persisted config is recovered.  On
    // first boot this is the second call: the one inside the !restored block
    // above seeded default cadence so Notehub was reachable for fetchEnvOverrides;
    // this call applies any env-var-driven cadence override that just arrived.
    applyHubSetIfChanged(PRODUCT_UID);

    // -------------------------------------------------------------------------
    // Retry a pending summary before taking new samples.
    // If a prior wake's note.add failed, samples_until_summary was left at 0
    // (not reset) so the closed window's accumulated data is preserved intact.
    // Retry the send here, before reading new VE.Direct data, so no fresh
    // readings can mix into the prior window.  On success, open a new window.
    // On continued failure, skip accumulation this wake and try again next time.
    // -------------------------------------------------------------------------
    bool window_retry = (state.samples_until_summary == 0);
    if (window_retry) {
        if (sendSummary()) {
            resetAccumulators();
            state.samples_until_summary =
                samplesPerWindow(state.report_interval_min, state.sample_interval_sec);
            window_retry = false;
        }
        // Still failed — window_retry stays true; accumulation is skipped below.
    }

    // -------------------------------------------------------------------------
    // Read VE.Direct data from both devices
    // -------------------------------------------------------------------------
    Serial1.begin(VED_BAUD);
    mpptSerial.begin(VED_BAUD);

    VEDirectData shunt, mppt;
    // Allow up to 3 s per device — they broadcast every ~1 s
    bool shunt_ok = readVEDirectFrame(Serial1,    shunt, 3000);
    bool mppt_ok  = readVEDirectFrame(mpptSerial, mppt,  3000);

    if (!shunt_ok) Serial.println(F("[warn] No VE.Direct frame from SmartShunt"));
    if (!mppt_ok)  Serial.println(F("[warn] No VE.Direct frame from SmartSolar MPPT"));

    // -------------------------------------------------------------------------
    // Accumulate and evaluate alerts.
    // Accumulation is skipped when a prior window is pending transmission so
    // new readings cannot contaminate the data waiting to be sent.
    // checkAndSendAlerts always runs so active flags update correctly regardless.
    // -------------------------------------------------------------------------
    if (!window_retry && (shunt_ok || mppt_ok)) {
        accumulate(shunt, mppt);
    }
    checkAndSendAlerts(shunt, mppt);

    // -------------------------------------------------------------------------
    // Advance the summary countdown and close the window when it reaches zero.
    // On a successful send the accumulators reset and a new window opens.
    // On failure, samples_until_summary stays at 0 — the retry block above will
    // attempt the resend on the next wake before any new data is accumulated.
    // -------------------------------------------------------------------------
    if (!window_retry) {
        if (state.samples_until_summary > 0) state.samples_until_summary--;
        if (state.samples_until_summary == 0) {
            checkHarvestDeficit();
            if (sendSummary()) {
                resetAccumulators();
                state.samples_until_summary =
                    samplesPerWindow(state.report_interval_min, state.sample_interval_sec);
            }
            // On failure: leave samples_until_summary at 0.  Next wake retries
            // via the block above before accumulating fresh readings.
        }
    }

    state.boot_count++;

    // -------------------------------------------------------------------------
    // Serialise state to Notecard flash and sleep until next sample interval
    // -------------------------------------------------------------------------
    NotePayloadDesc desc = {0, 0, 0};
    NotePayloadAddSegment(&desc, STATE_SEG_ID, &state, sizeof(state));
    NotePayloadSaveAndSleep(&desc, state.sample_interval_sec, NULL);

    // NotePayloadSaveAndSleep() returned — ATTN host power-cycling is not
    // working (e.g. ATTN pin not connected on this carrier).  Wait out the
    // sample interval and then force a software reset so the next cycle runs
    // from a clean setup() rather than falling into an empty loop().
    Serial.println(F("[fatal] NotePayloadSaveAndSleep returned; "
                     "ATTN power-cycling unavailable. Resetting after delay."));
    Serial.flush();
    delay(state.sample_interval_sec * 1000UL);
    NVIC_SystemReset();
}

void loop() {
    // All logic runs in setup(); the Notecard restarts the host each cycle.
}

// ---------------------------------------------------------------------------
// samplesPerWindow — ceiling division so that non-divisible report/sample
// interval pairs never produce zero or truncate the configured window.
// Returns at least 1 to prevent underflow on samples_until_summary--.
// ---------------------------------------------------------------------------
static uint16_t samplesPerWindow(uint32_t report_min, uint32_t sample_sec) {
    uint32_t report_sec = report_min * 60UL;
    return (uint16_t)max(1UL, (report_sec + sample_sec - 1) / sample_sec);
}

// ---------------------------------------------------------------------------
// accumulate — add one set of readings to the current summary window
// ---------------------------------------------------------------------------
static void accumulate(const VEDirectData &shunt, const VEDirectData &mppt) {
    if (shunt.valid) {
        state.bat_v_sum  += shunt.bat_v;    state.bat_v_cnt++;
        state.bat_a_sum  += shunt.bat_a;    state.bat_a_cnt++;
        state.bat_w_sum  += shunt.bat_w;    state.bat_w_cnt++;
        // Skip SoC accumulation on an unsynchronised SmartShunt (SOC="---" →
        // soc_pct = -1.0f sentinel) so the window's average is not skewed
        // by a bogus zero before the shunt completes its first 100 % sync.
        if (shunt.soc_pct >= 0.0f) {
            state.soc_sum += shunt.soc_pct; state.soc_cnt++;
        }
        if (shunt.bat_temp_c > -50.0f) {    // temp sensor present
            state.temp_sum += shunt.bat_temp_c; state.temp_cnt++;
        }
        state.ttg_min = shunt.ttg_min;      // keep latest
    }
    if (mppt.valid) {
        state.pv_v_sum   += mppt.pv_v;      state.pv_v_cnt++;
        state.pv_w_sum   += mppt.pv_w;      state.pv_w_cnt++;
        state.yield_kwh   = mppt.yield_kwh; // keep latest (resets at midnight)
        state.cs          = mppt.cs;
        // Track whether the battery achieved a full charge state this window.
        // Float and Absorption are the standard end-of-charge stages.
        // Equalize (7) and Auto Equalize/Recondition (247) are performed
        // after the battery has already reached Float, so they also confirm
        // that a full charge was completed.  Including them prevents false
        // harvest_deficit alerts on healthy sites that happen to be sampled
        // during an equalize cycle.
        if (mppt.cs == VED_CS_FLOAT || mppt.cs == VED_CS_ABSORPTION ||
            mppt.cs == VED_CS_EQUALIZE || mppt.cs == VED_CS_AUTO_EQUALIZE) {
            state.saw_full_charge = true;
        }
    }
    // Derived: load_w = pv_w − bat_w  (positive bat_w = charging = less load)
    if (shunt.valid && mppt.valid) {
        float load = mppt.pv_w - shunt.bat_w;
        if (load >= 0.0f) {
            state.load_w_sum += load; state.load_w_cnt++;
        }
    }
}

// ---------------------------------------------------------------------------
// checkHarvestDeficit — called once per summary window to detect persistent
// recharge failure.  Fires a harvest_deficit alert when the MPPT has not
// reached a full-charge state (Float, Absorption, Equalize, or Auto Equalize)
// for harvest_deficit_days consecutive days, indicating the array is not
// keeping up with load.
//
// Cooldown is only armed after the alert note is confirmed queued so a
// transient Notecard failure cannot suppress the next re-fire.
//
// Alert payload: v1=avg SoC %, v2=consecutive windows without full charge,
//                v3=today's accumulated solar yield kWh.
// ---------------------------------------------------------------------------
static void checkHarvestDeficit() {
    if (state.harvest_deficit_days <= 0.0f) return;  // feature disabled
    if (state.pv_w_cnt == 0.0f)             return;  // MPPT not present this window

    if (state.saw_full_charge) {
        // Battery completed a full charge cycle — fully re-arm the alert.
        state.no_charge_window_count = 0;
        state.harvest_cd = 0;
        return;
    }

    state.no_charge_window_count++;
    if (state.harvest_cd > 0) state.harvest_cd--;

    // Threshold in summary windows: harvest_deficit_days × (1440 / report_interval_min)
    uint16_t threshold = (uint16_t)(
        state.harvest_deficit_days * 1440.0f / (float)state.report_interval_min);
    if (threshold == 0) threshold = 1;

    if (state.no_charge_window_count >= threshold && state.harvest_cd == 0) {
        float avg_soc = (state.soc_cnt > 0.0f)
            ? state.soc_sum / state.soc_cnt : 0.0f;
        if (sendAlert("harvest_deficit", avg_soc,
                      (float)state.no_charge_window_count, state.yield_kwh)) {
            // Only arm the cooldown after the alert is confirmed queued.
            // If sendAlert() failed, harvest_cd stays at 0 and the next
            // summary window will retry immediately.
            state.harvest_cd = (uint16_t)(1440.0f / (float)state.report_interval_min);
            if (state.harvest_cd == 0) state.harvest_cd = 1;
        }
    }
}

// ---------------------------------------------------------------------------
// checkAndSendAlerts — evaluate threshold rules and emit immediate Notes.
//
// Alert strategy: rising-edge fire with cooldown repeat.
//   • First trip  — alert fires immediately (active flag false → true).
//   • Persisting  — alert re-fires every ALERT_COOLDOWN_SAMPLES wakes while
//                   the condition holds; ~30 min at the default 15-min interval.
//   • Clears      — active flag and cooldown reset when the condition resolves,
//                   so the next trip sends a fresh alert immediately again.
//
// Missing telemetry: when the source device for a given alert is absent on a
// wake, stale active/cooldown state from before the dropout is explicitly
// cleared.  Without this, a fault that was active when the device disappeared
// would suppress the first valid alert after telemetry returns, because the
// active flag would still be set and the cooldown counter would be positive.
// Clearing on dropout ensures the next fully valid wake fires immediately if
// the condition holds, regardless of what state the prior excursion left behind.
//
// Active flags and cooldown counters are only updated after note.add is
// confirmed queued so a transient Notecard failure cannot arm suppression or
// silently drop the cooldown.
// ---------------------------------------------------------------------------
static void checkAndSendAlerts(const VEDirectData &shunt, const VEDirectData &mppt) {
    // soc_low — battery approaching dark-site threshold.
    // Gate on shunt.soc_pct >= 0.0f so an unsynchronised SmartShunt
    // (SOC="---" on the wire → -1.0f sentinel) cannot fire a false soc_low
    // before the bank has been calibrated.  The same path also clears stale
    // active/cooldown state on a telemetry dropout or unsynchronised shunt
    // so the first valid wake fires immediately if the condition genuinely holds.
    if (shunt.valid && shunt.soc_pct >= 0.0f) {
        if (shunt.soc_pct < state.soc_alert_pct) {
            // Decrement cooldown before evaluating so the alert re-fires after
            // exactly ALERT_COOLDOWN_SAMPLES wakes (~30 min at the default interval).
            if (state.soc_alert_cd > 0) state.soc_alert_cd--;
            if (!state.soc_alert_active || state.soc_alert_cd == 0) {
                if (sendAlert("soc_low", shunt.soc_pct, shunt.bat_v, shunt.bat_w)) {
                    state.soc_alert_active = true;
                    state.soc_alert_cd     = ALERT_COOLDOWN_SAMPLES;
                }
            }
        } else {
            state.soc_alert_active = false;   // condition cleared — re-arm
            state.soc_alert_cd     = 0;
        }
    } else {
        // SmartShunt frame absent or SoC unsynchronised — clear stale state so
        // the next valid reading fires immediately rather than being suppressed
        // by a pre-dropout active flag or a residual cooldown counter.
        state.soc_alert_active = false;
        state.soc_alert_cd     = 0;
    }

    // temp_high — battery overtemperature (only when temp sensor is present)
    if (shunt.valid && shunt.bat_temp_c > -50.0f) {
        if (shunt.bat_temp_c > state.bat_temp_max_c) {
            if (state.temp_alert_cd > 0) state.temp_alert_cd--;
            if (!state.temp_alert_active || state.temp_alert_cd == 0) {
                if (sendAlert("temp_high", shunt.bat_temp_c, shunt.bat_v, shunt.soc_pct)) {
                    state.temp_alert_active = true;
                    state.temp_alert_cd     = ALERT_COOLDOWN_SAMPLES;
                }
            }
        } else {
            state.temp_alert_active = false;
            state.temp_alert_cd     = 0;
        }
    } else if (!shunt.valid) {
        // SmartShunt frame absent — clear stale state.
        // Note: if shunt.valid is true but bat_temp_c is the absent-sensor
        // sentinel (< −50 °C), leave state intact: a missing physical sensor
        // is a permanent hardware condition, not a telemetry dropout.
        state.temp_alert_active = false;
        state.temp_alert_cd     = 0;
    }

    // load_high — load draw exceeds configured ceiling
    if (shunt.valid && mppt.valid) {
        float load = mppt.pv_w - shunt.bat_w;
        if (load > state.load_alert_w) {
            if (state.load_alert_cd > 0) state.load_alert_cd--;
            if (!state.load_alert_active || state.load_alert_cd == 0) {
                if (sendAlert("load_high", load, shunt.soc_pct, mppt.pv_w)) {
                    state.load_alert_active = true;
                    state.load_alert_cd     = ALERT_COOLDOWN_SAMPLES;
                }
            }
        } else {
            state.load_alert_active = false;
            state.load_alert_cd     = 0;
        }
    } else {
        // One or both sources absent — clear stale state so the next fully
        // valid wake fires immediately if the load condition holds.
        state.load_alert_active = false;
        state.load_alert_cd     = 0;
    }
}

// ---------------------------------------------------------------------------
// resetAccumulators — clear window sums and counts after a confirmed send
// ---------------------------------------------------------------------------
static void resetAccumulators() {
    state.bat_v_sum  = state.bat_v_cnt  = 0.0f;
    state.bat_a_sum  = state.bat_a_cnt  = 0.0f;
    state.bat_w_sum  = state.bat_w_cnt  = 0.0f;
    state.soc_sum    = state.soc_cnt    = 0.0f;
    state.temp_sum   = state.temp_cnt   = 0.0f;
    state.pv_v_sum   = state.pv_v_cnt   = 0.0f;
    state.pv_w_sum   = state.pv_w_cnt   = 0.0f;
    state.load_w_sum = state.load_w_cnt = 0.0f;
    // Reset to sentinel so a stale ttg_min from a prior window cannot leak
    // into a window where no SmartShunt data was received.
    state.ttg_min    = -1;
    // Clear the full-charge flag so each window is evaluated fresh.
    state.saw_full_charge = false;
}
