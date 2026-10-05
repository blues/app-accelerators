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
 * Host sleep: between polls the Notecarrier CX's STM32L433 host sleeps in
 * STOP2 (~1–2 µA, RAM retained) and is woken by the Notecard's ATTN pin at
 * the end of a card.attn "sleep" (see cx_sleep.h).  Execution resumes in
 * place, so all session and window state simply lives in the State struct in
 * RAM — nothing is persisted to the Notecard.
 *
 * Wiring for sleep: jumper the Notecarrier CX ATTN pin to D5 (both on the
 * same 16-pin header).  Leave EN unconnected — on the CX it enables the shared
 * 3.3 V VIO rail, so ATTN→EN browns out the board instead of sleeping the host.
 *
 * Build: Tools > USB support (if available) > None (usb=none).  With the USB
 * CDC stack enabled and no USB host attached, the USB wakeup interrupt exits
 * STOP2 immediately.  Debug output goes to the LPUART on the CX debug jack,
 * which an ST-LINK V3 exposes as a virtual COM port (debugSerial below);
 * Serial1 on D0/D1 remains dedicated to the RS-485 link.
 *
 * Hardware:
 *   - Blues Notecarrier CX (onboard STM32L433 host MCU)
 *   - Blues Notecard Cell+WiFi (MBGLW) in M.2 slot
 *   - EASTRON SDM120-Modbus DIN-rail energy meter on EVSE feed
 *   - SparkFun BOB-10124 (SP3485) RS-485 transceiver: Serial1 D0/D1, RTS→D2
 *   - ATTN → D5 jumper on the Notecarrier CX header (host sleep/wake)
 *   - Blues Mojo (bench validation only; not required for production)
 *
 * See README.md §3–§4 for full BOM and wiring, §5 for Notehub setup.
 *
 * Helper functions (Modbus polling, session state machine, Note emission,
 * env-var handling, host sleep) live in the companion source files:
 *   ev_charger_session_monitor_helpers.h / .cpp
 *   cx_sleep.h — STOP2 entry + ATTN wake for the Notecarrier CX
 */

#include <Notecard.h>
#include "ev_charger_session_monitor_helpers.h"
#include "cx_sleep.h"

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
// Debug output: LPUART1 on the CX debug jack (ST-LINK virtual COM port).
// USB CDC is disabled for STOP2, so Serial is not available.
Uart     debugSerial(PIN_VCP_RX, PIN_VCP_TX);

// ═════════════════════════════════════════════════════════════════════════════
// setup() — runs once at power-up.  The host resumes in place after each
// STOP2 sleep, so one-time initialisation lives here and every per-wake step
// lives in loop().
// ═════════════════════════════════════════════════════════════════════════════
void setup() {
    debugSerial.begin(115200);

    notecard.begin();
    notecard.setDebugOutputStream(debugSerial);

    // ── Initialise state ─────────────────────────────────────────────────────
    // Zero everything, then seed the runtime config with firmware defaults.
    // STOP2 retains RAM, so from here on the session, window, and config state
    // carries across every wake until the next power cycle.
    memset(&state, 0, sizeof(state));
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
    debugSerial.println("[app] power-up — will configure Notecard");

    // ── Modbus initialisation ────────────────────────────────────────────────
    // STOP2 retains the UART peripheral registers, so Serial1 and the
    // ModbusMaster node only need to be set up once here.  fetchEnvOverrides()
    // calls initModbus() again if the baud rate or slave ID changes.
    initModbus();

    // ── Notecard configuration (retried in loop() until confirmed) ───────────
    state.notecard_configured = initNotecard(PRODUCT_UID);
    state.template_defined    = defineTemplates();

    // Arm the ATTN wake: the Notecard raises ATTN (D5) to end each sleep.
    cxSleepBegin();
}

// ═════════════════════════════════════════════════════════════════════════════
// loop() — one poll/wake cycle, then sleep until the Notecard raises ATTN.
// ═════════════════════════════════════════════════════════════════════════════
void loop() {
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
            debugSerial.println("[app] hub cadence re-sync recovered");
        }
    }

    // ── Check for updated environment variables ──────────────────────────────
    fetchEnvOverrides();

    // ── Poll the energy meter via Modbus ─────────────────────────────────────
    MeterReading meter;
    bool meter_ok = pollMeter(&meter);

    if (meter_ok) {
        debugSerial.print("[app] V=");   debugSerial.print(meter.voltage_v, 1);
        debugSerial.print(" V  P=");     debugSerial.print(meter.power_w, 0);
        debugSerial.print(" W  kWh=");   debugSerial.println(meter.import_kwh, 3);
        // Record the last-valid closing baseline for emitSummaryNote().
        // Doing this here — after every successful poll — ensures that a failed
        // meter read on the wake where the hourly summary fires does not force
        // total_kwh to 0 or reset window_start_kwh to a synthetic fallback value
        // that would inflate the following window's total.
        state.last_valid_import_kwh = meter.import_kwh;
    } else {
        debugSerial.println("[app] WARN: meter poll failed this wake");
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
        debugSerial.println("[app] reporting window opened");
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
        debugSerial.println("[app] window kWh baseline anchored");
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
    // The host enters STOP2 with `state` intact in RAM; the Notecard raises
    // ATTN sample_interval_sec from now and execution resumes at the top of
    // loop().
    sleepHost();
}
