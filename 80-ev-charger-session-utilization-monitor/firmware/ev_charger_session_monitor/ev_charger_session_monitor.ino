/*
 * ev_charger_session_monitor.ino
 *
 * Level 2 EV Charger Session & Utilization Monitor
 * Blues Notecarrier CX + Notecard Cell+WiFi (MBGLW)
 *
 * SCOPE: Session and utilization monitoring for Level 2 EVSE via a DIN-rail
 * single-phase energy meter (EASTRON SDM120-Modbus or compatible) wired on
 * the charger's AC feed.  The meter is polled over Modbus RTU (RS-485) every
 * 30 seconds.  Per-session kWh is derived from the meter's cumulative import-
 * energy register; charger availability is tracked via the meter's V_rms
 * register — when mains voltage falls below the configurable threshold, the
 * charger circuit is classified as unavailable.
 *
 * Reported per session (charger_session.qo, sync:true):
 *   session_kwh    — metered energy from import-kWh delta
 *   duration_min   — elapsed session time
 *   peak_w         — highest active-power reading during the session
 *   start_epoch    — UTC session start (0 if no time sync at open)
 *   timing_valid   — false when start_epoch is unreliable
 *
 * Reported hourly (charger_summary.qo, templated):
 *   sessions, total_kwh, avg_session_kwh, peak_w
 *   charging_min, idle_min, utilization_pct
 *   available_min, availability_pct   ← charger uptime (wall-clock denominator)
 *   sample_coverage_pct               ← fraction of window with valid meter data
 *
 * Alert (charger_alert.qo, sync:true):
 *   alert: "mains_absent"  — mains voltage absent for > alert_offline_min
 *
 * Hardware:
 *   - Blues Notecarrier CX (onboard Cygnet STM32L4 host MCU)
 *   - Blues Notecard Cell+WiFi (MBGLW) in M.2 slot
 *   - EASTRON SDM120-Modbus DIN-rail energy meter on EVSE feed
 *   - SparkFun BOB-10124 (SP3485) RS-485 transceiver: Serial1 D0/D1, RTS→D2
 *   - Blues Mojo (bench validation only; not required for production)
 *
 * See README.md §3–§4 for full BOM and wiring, §5 for Notehub setup.
 *
 * Helper functions (Modbus polling, session state machine, Note emission,
 * env-var handling, state persistence) live in the companion source files:
 *   ev_charger_session_monitor_helpers.h / .cpp
 */

#include <Notecard.h>
#include "ev_charger_session_monitor_helpers.h"

// ── Product identifier ────────────────────────────────────────────────────────
// ▶ Set PRODUCT_UID to your Notehub ProductUID before flashing
//   (e.g. -DPRODUCT_UID='"com.your-company.your-name:ev-charger-monitor"').
//   Leaving it unset produces a hard compile error so a misconfigured binary
//   cannot be accidentally deployed. For local development without a Notehub
//   project yet, add -DALLOW_EMPTY_PRODUCT_UID to the build flags as an
//   explicit override — that flag must not appear in a shipping build.
#ifndef PRODUCT_UID
#  ifndef ALLOW_EMPTY_PRODUCT_UID
#    error "PRODUCT_UID is not set. Define it as your Notehub ProductUID before flashing (e.g. -DPRODUCT_UID='\"com.your-company.your-name:ev-charger-monitor\"'). For local development without a project, add -DALLOW_EMPTY_PRODUCT_UID to suppress this error — that flag must not appear in a shipping build."
#  else
#    define PRODUCT_UID ""
#    pragma message "PRODUCT_UID empty (ALLOW_EMPTY_PRODUCT_UID override active) — device will not associate with any Notehub project"
#  endif
#endif

// ── Global instances ──────────────────────────────────────────────────────────
Notecard notecard;
State    state;

// ═════════════════════════════════════════════════════════════════════════════
void setup() {
    Serial.begin(115200);
    delay(250); // allow USB serial to enumerate before debug output begins

    notecard.begin();
    notecard.setDebugOutputStream(Serial);

    // ── Restore or initialise persistent state ───────────────────────────────
    // Zero-initialise first so that a partial or failed restore always leaves
    // state in a known clean condition rather than containing undefined garbage.
    memset(&state, 0, sizeof(state));
    NotePayloadDesc payload;
    bool restored  = NotePayloadRetrieveAfterSleep(&payload);
    bool cold_boot = true;

    if (restored) {
        bool seg_ok = NotePayloadGetSegment(&payload, STATE_SEG_ID, &state, sizeof(state));
        NotePayloadFree(&payload);
        if (seg_ok && state.magic == STATE_MAGIC) {
            cold_boot = false;
            Serial.println("[app] state restored after sleep");
        } else {
            memset(&state, 0, sizeof(state));
            Serial.println("[app] payload invalid — forcing cold boot");
        }
    }

    if (cold_boot) {
        state.magic               = STATE_MAGIC;
        state.notecard_configured = false;
        state.template_defined    = false;
        state.sample_interval_sec = DEFAULT_SAMPLE_SEC;
        state.report_interval_min = DEFAULT_REPORT_MIN;
        state.session_threshold_w = DEFAULT_SESSION_W;
        state.session_end_count   = DEFAULT_SESSION_END_COUNT;
        state.voltage_present_v   = DEFAULT_VOLTAGE_PRESENT_V;
        state.alert_offline_min   = DEFAULT_ALERT_OFFLINE_MIN;
        state.modbus_slave_id     = MODBUS_DEFAULT_ID;
        state.modbus_baud         = MODBUS_DEFAULT_BAUD;

        Serial.println("[app] cold boot — will configure Notecard");
    }

    // ── Modbus initialisation (always, every wake) ───────────────────────────
    // The STM32L4 UART peripheral registers are not retained through the
    // Notecarrier CX ATTN power-gate, so Serial1 and the ModbusMaster node
    // must be reinitialised on every wake.  initModbus() is fast (register
    // writes only) and safe to call unconditionally.
    initModbus();

    // ── Notecard configuration (idempotent, retried until confirmed) ─────────
    if (!state.notecard_configured) {
        state.notecard_configured = initNotecard(PRODUCT_UID);
    }
    if (!state.template_defined) {
        state.template_defined = defineTemplates();
    }
    if (state.hub_cadence_dirty) {
        if (applyHubCadence()) {
            state.hub_cadence_dirty = false;
            Serial.println("[app] hub cadence re-sync recovered");
        }
    }

    // ── Check for updated environment variables ──────────────────────────────
    fetchEnvOverrides();

    // ── Poll the energy meter via Modbus ─────────────────────────────────────
    MeterReading meter;
    bool meter_ok = pollMeter(&meter);

    if (meter_ok) {
        Serial.print("[app] V=");   Serial.print(meter.voltage_v, 1);
        Serial.print(" V  P=");     Serial.print(meter.power_w, 0);
        Serial.print(" W  kWh=");   Serial.println(meter.import_kwh, 3);
        // Record the last-valid closing baseline for emitSummaryNote().
        // Doing this here — after every successful poll — ensures that a failed
        // meter read on the wake where the hourly summary fires does not force
        // total_kwh to 0 or reset window_start_kwh to a synthetic fallback value
        // that would inflate the following window's total.
        state.last_valid_import_kwh = meter.import_kwh;
    } else {
        Serial.println("[app] WARN: meter poll failed this wake");
    }

    uint32_t now = getEpoch();

    // ── Track mains presence for offline alert ───────────────────────────────
    // Update last_mains_epoch whenever V_rms is above the present threshold.
    // Only overwrite the stored epoch when `now > 0`; a transient card.time
    // failure that returns 0 must not erase a previously-confirmed epoch and
    // collapse the offline_ref fallback to commissioning time.  The suppress
    // flag is always cleared when mains is present so the alert can re-fire.
    if (meter.valid && meter.voltage_v >= state.voltage_present_v) {
        if (now > 0) state.last_mains_epoch = now;
        state.offline_alert_sent = false;
    }

    // ── Open hourly window on first wake with valid time ─────────────────────
    // Must precede runSessionStateMachine() so window accumulators are only
    // incremented once a valid epoch exists.  The kWh baseline is anchored
    // lazily below — see comment block.
    if (state.window_start_epoch == 0 && now > 0) {
        state.window_start_epoch = now;
        Serial.println("[app] reporting window opened");
    }

    // ── Anchor window kWh baseline on first valid meter read after open ──────
    // Deferring this until meter.valid is true prevents the corner case where
    // the first time-synced wake has a failed Modbus poll: if window_start_kwh
    // were left at 0 (or any stale value from a prior window with no valid
    // reads), the next successful poll would set last_valid_import_kwh to the
    // meter's lifetime kWh and the first summary would report that lifetime
    // total as the window's energy — wildly inflated.
    if (state.window_start_epoch > 0 && !state.window_kwh_baseline_set && meter.valid) {
        state.window_start_kwh         = meter.import_kwh;
        state.window_kwh_baseline_set  = true;
        Serial.println("[app] window kWh baseline anchored");
    }

    // ── Session state machine ────────────────────────────────────────────────
    runSessionStateMachine(meter, now);

    // ── Mains-absent alert ───────────────────────────────────────────────────
    // Fire once if no mains-present reading has been seen for more than
    // alert_offline_min minutes.  Set alert_offline_min to 0 to disable.
    // The reference epoch is the last confirmed mains-present reading, or
    // window_start_epoch (commissioning time) if mains has never been confirmed
    // present — so the alert fires even for a circuit dead from day one.
    uint32_t offline_ref = (state.last_mains_epoch > 0)
                           ? state.last_mains_epoch
                           : state.window_start_epoch;
    if (state.alert_offline_min > 0      &&
        !state.offline_alert_sent        &&
        offline_ref > 0                  &&
        now > offline_ref                &&
        (now - offline_ref) > (state.alert_offline_min * 60UL)) {
        if (emitOfflineAlert(now)) {
            state.offline_alert_sent = true;
        }
    }

    // ── Emit hourly summary if the reporting window has elapsed ──────────────
    if (now > 0 && state.window_start_epoch > 0 &&
        (now - state.window_start_epoch) >= (state.report_interval_min * 60UL)) {
        // emitSummaryNote uses state.last_valid_import_kwh as the closing meter
        // reading so a failed poll on this wake does not force total_kwh to zero
        // or corrupt the next window's kWh baseline with a synthetic fallback.
        emitSummaryNote(now);
    }

    // ── Sleep until next sample interval ─────────────────────────────────────
    sleepHost();
}

void loop() {
    // NotePayloadSaveAndSleep() cuts host power via card.attn; this line is
    // only reached if the Notecarrier CX ATTN power-gating is unavailable
    // (e.g., bare breakout on a bench). Spin to prevent re-entering setup()
    // and double-counting session data.
    delay(15000);
}
