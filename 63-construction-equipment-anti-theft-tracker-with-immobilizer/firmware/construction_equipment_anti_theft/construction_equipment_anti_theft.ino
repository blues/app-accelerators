// ============================================================================
// construction_equipment_anti_theft.ino
//
// Blues Notecarrier CX + Notecard for Skylo (NOTE-NBGLWX)
// Construction equipment anti-theft tracker with remote immobilizer.
//
// Features:
//   - Periodic GPS heartbeat (cadence adapts to motion and time-of-day)
//   - Geofence breach alert: equipment outside configured job-site boundary
//   - After-hours motion alert: vibration/movement at night, engine confirmed off
//   - Inbound immobilize command delivered via Notehub (immobilize.qi)
//   - Relay assertion on next key-on when immobilize command is staged
//   - Compact note templates compatible with Notecard for Skylo satellite uplink
//   - Automatic WiFi → cellular → Skylo-satellite (NTN) failover, enabled at
//     boot via card.transport "wifi-cell-ntn" so equipment stays reachable
//     beyond terrestrial coverage (NTN is off by default and must be turned on)
//   - Host sleeps in STM32 STOP2 between wake cycles, woken by the Notecard's
//     ATTN pin (card.attn "sleep"; see cx_sleep.h)
//
// Hardware:
//   Blues Notecarrier CX with onboard STM32L433 host MCU
//   Blues Notecard for Skylo (NOTE-NBGLWX), seats in CX M.2 slot
//   Jumper: Notecarrier CX ATTN -> D5 (host wake from STOP2); leave EN unconnected
//   Blues Mojo, inline on +VBAT for bench power validation
//   3.7V LiPo battery (JST-PH 2.0mm) + 6V solar panel + solar LiPo charger
//   BSS138 logic-level N-channel MOSFET (relay coil driver; R_DS(on) ≤ 3.5 Ω at V_GS = 2.5 V)
//   10kΩ gate-to-source pulldown (keeps MOSFET off when GPIO is high-impedance)
//   Bosch-style SPDT automotive relay, 12V/30A (ignition circuit intercept)
//   Inline automotive fuse (1–2 A) near the 12V relay coil feed tap
//   1N4007 flyback diode across relay coil
//   33kΩ (high-side) + 10kΩ (low-side) voltage divider (ignition sense: 12V → ~2.79V at A2)
//   NOTE: bare voltage divider is POC-only; add TVS + RC filter for production
//
// Execution model:
//   setup()    — runs once at power-up: hardware initialization, g_state
//                defaults, fence restore from fence.db, ATTN wake arming.
//   runCycle() — one full sensing / transmit cycle, then sleep: the host asks
//                the Notecard to hold ATTN low for the adaptive interval and
//                enters STOP2 (~1-2 µA, RAM retained).  The ATTN rising edge on
//                D5 wakes it and execution resumes in place, so g_state (staged
//                immobilize, alert cooldowns, fence) simply lives in RAM.  If
//                the sleep request fails or ATTN never goes low (jumper
//                missing), runCycle() waits out the same interval awake.
//   loop()     — calls runCycle() on every pass (one wake cycle each).
//
// Wiring for sleep: jumper the Notecarrier CX ATTN pin to D5 (both on the same
// 16-pin header).  Leave EN unconnected — on the CX it enables the shared 3.3 V
// VIO rail, so driving it from ATTN would brown out the board.
// Build: Tools > USB support (if available) > None (usb=none).  With the USB
// CDC stack enabled and no USB host attached, the host cannot stay in STOP2.
// Debug output (DEBUG_SERIAL) goes to the ST-LINK virtual COM port on the CX
// debug jack.
//
// All Notecard interactions are encapsulated in construction_equipment_anti_theft_helpers.
//
// THIS FILE IS A STARTING POINT. Test and adapt to your specific equipment
// wiring and ignition circuit before deployment.
// ============================================================================

// ─── Debug serial ─────────────────────────────────────────────────────────────
// Uncomment the line below (or pass -DDEBUG_SERIAL=1 via build flags) to enable
// debug output and Notecard debug streaming on the ST-LINK virtual COM port
// (LPUART on the CX debug jack).  Leave commented for production: the debug
// UART is never started, eliminating its current draw.
// #define DEBUG_SERIAL 1

#include <Notecard.h>
#include "construction_equipment_anti_theft_helpers.h"
#include "cx_sleep.h"

// ─── Product UID ──────────────────────────────────────────────────────────────
#ifndef PRODUCT_UID
#define PRODUCT_UID ""  // "com.your-company.your-name:equipment_tracker"
#pragma message "PRODUCT_UID is not defined. Set this before flashing."
#endif

// ─── Globals ─────────────────────────────────────────────────────────────────
Notecard notecard;
AppState g_state;
#if DEBUG_SERIAL
// LPUART1 on the CX debug jack, exposed by an ST-LINK V3 as a virtual COM port.
// (USB CDC must be disabled for STOP2 to work; see cx_sleep.h.)
Uart     debugSerial(PIN_VCP_RX, PIN_VCP_TX);
#endif

// Forward declaration
static void runCycle();

// ─── setup() ─────────────────────────────────────────────────────────────────
// Runs once at power-up.  The host resumes in place after each STOP2 sleep, so
// one-time hardware init and g_state initialization live here and every
// per-wake step lives in runCycle().
void setup()
{
#if DEBUG_SERIAL
    debugSerial.begin(115200);
    // Attach the Notecard debug stream only in debug builds.
    notecard.setDebugOutputStream(debugSerial);
#endif

    // Flag an unset PRODUCT_UID loudly: the firmware cannot associate to a
    // Notehub project and would appear alive on serial while never syncing data.
    // (The #pragma above also flags it at build time.)
    if (strlen(PRODUCT_UID) == 0) {
        LOGLN("[APP] ERROR: PRODUCT_UID is empty — set your Notehub "
              "project UID in the sketch and reflash before deploying.");
    }

    // Configure GPIO before I2C so the relay stays de-energized during boot.
    // De-energized = ignition circuit intact (NC contact closed).
    pinMode(PIN_RELAY_DRIVER,   OUTPUT);
    pinMode(PIN_IGNITION_SENSE, INPUT);
    digitalWrite(PIN_RELAY_DRIVER, LOW);

    // Set ADC resolution to 12 bits so getIgnitionState() threshold constants
    // (kOnThreshold=3103, kOffThreshold=2482) match the actual count range.
    // Many Arduino-compatible cores default to 10-bit, which would keep the
    // ignition reading below the ON threshold permanently.
    analogReadResolution(12);

    notecard.begin();  // I2C at default address

    // ── Initialize application state ─────────────────────────────────────────
    // g_state lives in RAM and survives every STOP2 sleep, so this runs only
    // at power-up.  Zero-initialize then apply compile-time defaults.
    // All cfg_* and last_* flags start false (memset) — correct for power-up:
    //   cfg_* false  → ensureConfigured() will run all steps on the first wake.
    //   last_moving/last_afterhrs false → stationary/daytime is a safe default.
    {
        memset(&g_state, 0, sizeof(g_state));
        g_state.fence_radius_m          = DEFAULT_FENCE_RADIUS_M;
        g_state.after_hours_start       = AFTER_HOURS_START_H;
        g_state.after_hours_end         = AFTER_HOURS_END_H;
        g_state.heartbeat_stopped_s     = HEARTBEAT_STOPPED_MIN    * 60;
        g_state.heartbeat_moving_s      = HEARTBEAT_MOVING_MIN     * 60;
        g_state.heartbeat_afterhours_s  = HEARTBEAT_AFTERHOURS_MIN * 60;
        g_state.inbound_s               = INBOUND_MIN              * 60;
        g_state.outbound_s              = OUTBOUND_MIN             * 60;
        g_state.alert_cooldown_s        = ALERT_COOLDOWN_MIN * 60;
        // Attempt to restore a previously-commissioned fence from Notecard flash
        // (fence.db) before allowing GPS auto-anchor.  This prevents a power-loss/
        // reconnect during a theft event from silently re-homing the fence to the
        // thief's current location.
        // fence_io_error distinguishes "confirmed no record" from "transport fault".
        // Auto-anchor (fence_confirmed_absent) is only permitted in the former case;
        // a read error leaves fence_confirmed_absent false so subsequent wakes
        // also block auto-anchor until a power cycle can cleanly re-read fence.db.
        bool fence_io_error = false;
        if (!loadFenceFromFlash(notecard, g_state, fence_io_error)) {
            if (fence_io_error) {
                LOGLN("[APP] ERROR: fence.db read failed after retries — "
                      "auto-anchor suppressed to prevent geofence re-homing.");
                g_state.fence_confirmed_absent = false;
            } else {
                LOGLN("[APP] Power-up — no persisted fence; "
                      "will auto-anchor at first GPS fix.");
                g_state.fence_confirmed_absent = true;
            }
        } else {
            LOGLN("[APP] Power-up — fence restored from flash.");
            g_state.fence_confirmed_absent = false;
        }
    }

    // Arm the ATTN wake: the Notecard raises ATTN (D5) to end each sleep.
    cxSleepBegin();
}

// ─── runCycle() ──────────────────────────────────────────────────────────────
// Full sensing / transmit / sleep cycle.  Called from loop() on every iteration.
// Ends by putting the host into STOP2 until the Notecard raises ATTN; execution
// then resumes here and returns to loop(), which calls runCycle() again with
// g_state intact — all staged state (immobilize, fence, alert cooldowns) simply
// stays in RAM.  If the sleep request fails the function waits out the same
// interval awake instead, so the cadence is preserved either way.
static void runCycle()
{
    // ── Use previous-wake context as proxy for current wake-state ─────────────
    // ctx_moving / ctx_afterhrs reflect the state from the previous wake cycle.
    // Used as the best available estimate of the current context before fresh
    // sensor reads (getIsMoving, getEpochTime) are performed below.
    // On the first wake these default false (stationary/daytime) — a safe
    // conservative starting point.
    bool ctx_moving   = g_state.last_moving;
    bool ctx_afterhrs = g_state.last_afterhrs;

    // ── Ensure Notecard is fully configured on every wake ─────────────────────
    // ensureConfigured() is idempotent: it skips any step already confirmed and
    // retries only those whose cfg_* flag is still false.  This makes setup
    // self-healing: a hub.set or template failure at power-up (or after an
    // independent Notecard reset) does not leave the device permanently
    // misconfigured — it recovers on subsequent wakes without a power cycle.
    if (!ensureConfigured(notecard, PRODUCT_UID, g_state)) {
        LOGLN("[APP] WARN: Notecard config incomplete — "
              "retrying failed steps next wake.");
    }

    // ── Pull updated env vars on every wake (delta-only via env.get `time`) ──
    // When Notehub has new values, also reissue hub.set so the Notecard's
    // internal outbound/inbound session windows reflect the updated cadence.
    // Persist any operator-updated fence coordinates to fence.db immediately so
    // they survive the next power cycle.
    if (fetchEnvOverrides(notecard, g_state)) {
        // Use the previous-wake context proxy (last_moving/last_afterhrs) as the best
        // available context before fresh sensor reads complete.  applyHubCadence()
        // will be called again at the state-transition block below (after sensor
        // reads) if the context has actually changed since the previous wake.
        applyHubCadence(notecard, g_state, ctx_moving, ctx_afterhrs);
        if (g_state.fence_set) {
            if (!saveFenceToFlash(notecard, g_state)) {
                LOGLN("[APP] ERROR: fence persistence failed after env update — "
                      "coordinates are in RAM only until next successful write.");
            }
        }
        LOGLN("[APP] Env vars updated — hub cadence reapplied.");
    }

    // ── Check for inbound immobilize / release command BEFORE reasserting relay ─
    // Draining the command queue first prevents a brief relay pulse on the exact
    // wake that delivers a release command. Track pending/immobilized state before
    // the call so we can detect a newly-received command and send an acknowledgment
    // Note exactly once per command, not on every subsequent wake.
    // cmd_retrieve_ok is false when note.get failed entirely or returned an
    // unexpected error after retries; a diagnostic alert is emitted later (after
    // sensor reads) so it carries location context for operator visibility in Notehub.
    bool was_pending     = g_state.immobilize_pending;
    bool was_immobilized = g_state.immobilized;
    bool cmd_retrieve_ok = checkAndHandleCommand(notecard, g_state);
    bool just_armed    = (!was_pending     && g_state.immobilize_pending);
    // just_released: a release command was processed that cleared a pending or
    // active immobilize — emit release_confirmed alert so the operator can confirm
    // the command was received without relying on side-channel inference.
    bool just_released = ((was_pending || was_immobilized) &&
                          !g_state.immobilize_pending && !g_state.immobilized);

    // Re-assert the relay only after commands are drained so a just-received
    // release command prevents the unnecessary immobilize pulse entirely.
    // STOP2 retains GPIO state, so the coil stays energized through the sleep
    // window; re-asserting here is a belt-and-braces guard. See Limitations in
    // the README for the latching-relay production upgrade path.
    if (g_state.immobilized) {
        assertRelay();
    }

    // ── Core sensing cycle ────────────────────────────────────────────────────
    // Pass last_ignition_on as the hysteresis seed for the analog ignition read.
    bool     ignition_on = getIgnitionState(g_state.last_ignition_on);
    bool     moving      = getIsMoving(notecard);
    // Fetch epoch before location so fix_age_s can be computed as (now - fix_time).
    uint32_t now         = getEpochTime(notecard);
    double   cur_lat = 0.0, cur_lon = 0.0;
    uint32_t fix_time    = 0;
    bool     got_fix     = getLocation(notecard, cur_lat, cur_lon, fix_time);
    // fix_age_s: elapsed seconds since the cached GNSS fix was acquired, computed
    // at the moment of this wake cycle.  Included in every outbound note so
    // operators can assess whether a geofence or alert decision used fresh or
    // stale position data.
    //
    // Sentinel: -1 is emitted when card.time is unavailable (now == 0), for
    // example on first boot before the Notecard has completed a Notehub session.
    // In that case the true age cannot be determined and emitting 0 would falsely
    // indicate a fresh fix.  Downstream consumers must treat -1 as "age unknown"
    // and must not use it as a freshness indicator.
    // A value >= 0 is a real elapsed-seconds count.
    int32_t fix_age_s = (now > 0 && fix_time > 0 && now >= fix_time)
                            ? (int32_t)(now - fix_time)
                            : -1;

    // Use the GPS fix timestamp as a fallback time base so alerts and after-hours
    // detection continue to function when cell network time sync is temporarily
    // unavailable (e.g. first boot before first Notehub session).
    uint32_t eff_time = (now != 0) ? now : fix_time;

    bool afterhrs = isAfterHours(eff_time, g_state.after_hours_start,
                                            g_state.after_hours_end);

    // Reapply hub cadence on context transitions.  The context-aware inbound
    // strategy selects the short interval (inbound_s) when after-hours or moving,
    // and the long interval (heartbeat_stopped_s) when parked and daytime.
    // Calling applyHubCadence() here updates the Notecard's hub.set inbound window
    // immediately on the transition wake rather than waiting for the next env-var
    // change — so the switch from active to parked (or back) takes effect within
    // one wake cycle.
    if (moving != ctx_moving || afterhrs != ctx_afterhrs) {
        LOG("[APP] Wake-state changed (moving=");
        LOG(moving ? "Y" : "N");
        LOG(", afterhrs=");
        LOG(afterhrs ? "Y" : "N");
        LOGLN(") — reapplying hub cadence for new context.");
        applyHubCadence(notecard, g_state, moving, afterhrs);
    }

    // ── Emit command-path diagnostic if retrieval failed ─────────────────────
    // Sent here (after sensor reads) so lat/lon/fix context are available,
    // letting operators distinguish 'no command queued' from 'path unhealthy'
    // in Notehub without side-channel inference.
    // Rate-limited to alert_cooldown_s so a persistent I2C or Notecard fault
    // does not generate a sync:true alert on every wake and flood the operator
    // inbox (or exhaust the satellite data budget) with repeated diagnostics.
    if (!cmd_retrieve_ok) {
        if (alertDue(eff_time, g_state.last_cmd_failed_alert_s,
                     (uint32_t)g_state.alert_cooldown_s)) {
            if (sendAlert(notecard, g_state,
                          "cmd_retrieve_failed", cur_lat, cur_lon, got_fix,
                          ignition_on, g_state.immobilized, fix_age_s)) {
                g_state.last_cmd_failed_alert_s = (eff_time > 0) ? eff_time : 1;
            } else {
                LOGLN("[APP] ERROR: cmd_retrieve_failed diagnostic also failed.");
            }
        }
    }

    // Stage → fire: assert relay only on an OFF→ON ignition edge so the relay is
    // never immediately asserted against a key that was already ON when the command
    // arrived.  g_state.last_ignition_on carries the state from the previous wake
    // so the transition is detectable across sleep cycles.
    bool ignition_edge = (!g_state.last_ignition_on && ignition_on);
    if (g_state.immobilize_pending && ignition_edge && !g_state.immobilized) {
        assertRelay();
        g_state.immobilized        = true;
        g_state.immobilize_pending = false;
        LOGLN("[APP] Relay asserted on key-on edge — ignition blocked.");
        if (!sendAlert(notecard, g_state,
                       "ignition_on_immobilized", cur_lat, cur_lon, got_fix,
                       true, true, fix_age_s)) {
            LOGLN("[APP] ERROR: immobilize alert failed after all retries.");
        }
    }

    // Emit "immobilize_armed" only when the command is still in the staged/pending
    // state and has NOT already fired as a full immobilize on this same wake.
    // Without this guard, a command that arrives on the same wake as a key-on edge
    // would send both "immobilize_armed" and "ignition_on_immobilized", even though
    // the relay was never merely staged.
    if (just_armed && g_state.immobilize_pending && !g_state.immobilized) {
        LOGLN("[APP] Immobilize staged — relay armed for next key-on.");
        if (!sendAlert(notecard, g_state,
                       "immobilize_armed", cur_lat, cur_lon, got_fix,
                       ignition_on, false, fix_age_s)) {
            LOGLN("[APP] ERROR: armed alert failed after all retries.");
        }
    }

    // Send release confirmation exactly once when a release command clears an
    // active or staged immobilize. This gives operators an explicit acknowledgment
    // rather than having to infer processing from the absence of future armed alerts.
    if (just_released) {
        LOGLN("[APP] Release confirmed — relay disarmed.");
        if (!sendAlert(notecard, g_state,
                       "release_confirmed", cur_lat, cur_lon, got_fix,
                       ignition_on, false, fix_age_s)) {
            LOGLN("[APP] ERROR: release alert failed after all retries.");
        }
    }

    // Auto-anchor geofence at first valid GPS fix, but only when the power-up
    // fence-load positively confirmed no persisted record exists.
    // fence_confirmed_absent stays false when loadFenceFromFlash hit a transport
    // error, blocking auto-anchor across all subsequent wakes until a clean
    // power-up re-read succeeds — this prevents a transient I2C failure from
    // silently re-homing the fence to a thief's current location.
    if (!g_state.fence_set && g_state.fence_confirmed_absent && got_fix) {
        g_state.fence_lat = cur_lat;
        g_state.fence_lon = cur_lon;
        g_state.fence_set = true;
        if (saveFenceToFlash(notecard, g_state)) {
            LOGLN("[APP] Home geofence anchored at first GPS fix — saved to flash.");
        } else {
            LOGLN("[APP] ERROR: auto-anchor fence persistence failed after retries — "
                  "fence active in RAM only; power cycle may re-anchor at wrong location.");
        }
    }

    // Geofence evaluation.
    // fence_ok: -1 = unknown (no valid GPS fix or fence not yet anchored),
    //            0 = geofence breach, 1 = in-fence.
    // Defaulting to -1 prevents a GNSS outage (antenna fault, indoor storage)
    // from being silently reported as "in-fence" in tracker.qo and masking theft.
    int8_t fence_ok = -1;
    if (got_fix && g_state.fence_set) {
        float dist = haversineDistanceM(g_state.fence_lat, g_state.fence_lon,
                                        cur_lat, cur_lon);
        fence_ok = (dist <= g_state.fence_radius_m) ? 1 : 0;
        // alertDue() allows the first-ever breach alert even before a valid epoch
        // is available (last_geofence_alert_s == 0). Subsequent alerts require
        // cooldown_s to elapse using eff_time (epoch with fix_time as fallback).
        if (fence_ok == 0 &&
            alertDue(eff_time, g_state.last_geofence_alert_s,
                     (uint32_t)g_state.alert_cooldown_s)) {
            if (sendAlert(notecard, g_state,
                          "geofence_breach", cur_lat, cur_lon, got_fix,
                          ignition_on, g_state.immobilized, fix_age_s)) {
                // Advance the cooldown timestamp only after a confirmed queue.
                // Leaving it unchanged on failure lets the next wake retry
                // immediately instead of silently sitting out the full cooldown.
                // If eff_time is 0 (no time base at all), store 1 as a sentinel
                // so the first-alert guard (last_ts == 0) does not re-fire on
                // the very next wake.
                g_state.last_geofence_alert_s = (eff_time > 0) ? eff_time : 1;
                LOGLN("[APP] Geofence breach alert sent.");
            } else {
                LOGLN("[APP] ERROR: geofence alert failed after all retries — "
                      "will retry next wake.");
            }
        }
    }

    // After-hours motion: movement detected after dark, ignition confirmed OFF.
    // Ignition-off + motion is the characteristic signature of towing or loading.
    // Uses a separate cooldown timestamp from the geofence check so that one
    // alert type cannot suppress the other during the cooldown window.
    // isAfterHours() uses eff_time so after-hours detection works even when cell
    // time sync is temporarily unavailable.
    if (afterhrs && moving && !ignition_on &&
        alertDue(eff_time, g_state.last_motion_alert_s,
                 (uint32_t)g_state.alert_cooldown_s)) {
        if (sendAlert(notecard, g_state,
                      "motion_after_hours", cur_lat, cur_lon, got_fix,
                      false, g_state.immobilized, fix_age_s)) {
            // Advance timestamp only on confirmed queue — same rationale as
            // the geofence breach block above.
            g_state.last_motion_alert_s = (eff_time > 0) ? eff_time : 1;
            LOGLN("[APP] After-hours motion alert sent.");
        } else {
            LOGLN("[APP] ERROR: motion alert failed after all retries — "
                  "will retry next wake.");
        }
    }

    // Periodic heartbeat note (queued; not immediately transmitted).
    // After-hours the wake interval is short (heartbeat_afterhours_s, default 2 min)
    // to detect alerts quickly, but heartbeats queue at the moving/stopped rate —
    // keeping after-hours note volume consistent with daytime operation.
    uint32_t heartbeat_interval = moving
        ? (uint32_t)g_state.heartbeat_moving_s
        : (uint32_t)g_state.heartbeat_stopped_s;

    // Gate on eff_time (epoch with GPS fix_time as fallback) so heartbeats keep
    // flowing even when cell-network time sync is temporarily unavailable but a
    // valid GNSS fix exists.  When eff_time is also 0 (no time base at all),
    // fire exactly once on the first wake (last_heartbeat_s == 0 path) and then
    // store the sentinel value 1 to prevent a per-wake storm while waiting for
    // a time base — the elapsed() guard returns false for eff_time == 0.
    // Advance last_heartbeat_s only after a confirmed queue so a transient
    // note.add failure cannot create a long telemetry gap.
    if (g_state.last_heartbeat_s == 0 ||
        elapsed(eff_time, g_state.last_heartbeat_s, heartbeat_interval)) {
        if (sendHeartbeat(notecard, g_state, cur_lat, cur_lon, got_fix,
                          moving, fence_ok, ignition_on, fix_age_s)) {
            // Use eff_time when available; sentinel 1 when time base is unknown
            // so last_heartbeat_s != 0 blocks per-wake re-fires.
            g_state.last_heartbeat_s = (eff_time > 0) ? eff_time : 1;
        } else {
            LOGLN("[APP] WARN: heartbeat send failed — will retry next wake.");
        }
    }

    g_state.last_ignition_on = ignition_on;  // keep for OFF→ON edge detection next wake
    g_state.last_moving      = moving;        // wake-state proxy for next wake
    g_state.last_afterhrs    = afterhrs;      // wake-state proxy for next wake

    // ── Adaptive sleep: shorter during high-risk after-hours window ───────────
    uint32_t sleep_sec = afterhrs
        ? (uint32_t)g_state.heartbeat_afterhours_s
        : moving ? (uint32_t)g_state.heartbeat_moving_s
                 : (uint32_t)g_state.heartbeat_stopped_s;

    LOG("[APP] Sleeping for ");
    LOG(sleep_sec);
    LOGLN(" s.");

    // Sleep in STOP2 until the Notecard raises ATTN sleep_sec seconds from now.
    // cxSleepUntilAttn() sends card.attn "sleep" (checked, since the host stays
    // alive to see the response), waits for ATTN to go low, and enters STOP2.
    // Execution resumes on the next line; g_state stays in RAM.
#if DEBUG_SERIAL
    Stream *log = &debugSerial;
#else
    Stream *log = NULL;
#endif
    if (!cxSleepUntilAttn(notecard, sleep_sec, NULL, log)) {
        // The Notecard didn't take the sleep request, or ATTN never went low
        // (check the ATTN -> D5 jumper).  Wait out the same adaptive interval
        // awake so the sensing cadence — and the after-hours scan rate — is
        // preserved, then return to loop(), which calls runCycle() again with
        // g_state intact (staged immobilize, alert cooldowns, heartbeat timing,
        // and fence state are all still in RAM).
        LOG("[APP] WARN: ATTN sleep failed — waiting awake for ");
        LOG(sleep_sec);
        LOGLN(" s.");
        delay(sleep_sec * 1000UL);
    }
}

// ─── loop() ──────────────────────────────────────────────────────────────────
// Each pass is one wake cycle: runCycle() senses, transmits, and sleeps the
// host in STOP2 until the Notecard raises ATTN, then returns here with g_state
// intact so the next pass continues where the last one left off.
void loop()
{
    runCycle();
}
