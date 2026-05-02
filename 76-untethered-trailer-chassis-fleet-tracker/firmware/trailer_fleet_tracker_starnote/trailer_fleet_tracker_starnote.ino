/***************************************************************************
  trailer_fleet_tracker_starnote.ino — Untethered Trailer & Chassis Fleet Tracker
                                       (Ocean-Capable — Notecard Cellular +
                                        Starnote for Iridium LEO)

  Runs on Notecarrier XI (Swan STM32U5 host in Feather slot) with a cellular
  Notecard (e.g., NOTE-WBEX) in the M.2 slot and a Starnote for Iridium
  module in the Notecarrier XI Starnote connector.  The Notecard provides
  LTE-M/NB-IoT cellular connectivity and the built-in accelerometer; the
  Starnote for Iridium adds globally continuous satellite fallback and
  combined GPS/GNSS via its single Iridium-certified antenna.

  COVERAGE SCOPE — GLOBAL (pole-to-pole).  Iridium LEO provides uninterrupted
  coverage over trans-oceanic routes, polar corridors, and all land routes
  with no geographic exclusions.  This firmware is appropriate for container
  chassis on ocean segments and any asset that roams beyond geostationary
  satellite footprints.  For land-route-only deployments within Skylo's
  defined NTN footprint, a NOTE-NBGLWX on a Notecarrier CX is an alternative
  hardware path — see README §9 for notes on that option.

  On every wake-from-sleep the sketch:
    1. Restores the persisted AppState from Notecard flash.
    2. Queries the Notecard accelerometer for moving / stopped status.
    3. Detects PARKED→MOVING (departed) and MOVING→PARKED (arrived) transitions.
    4. On a transition, enqueues an event note and drains the pending FIFO;
       gps_valid=1 when a valid GNSS fix is available, gps_valid=0 otherwise.
    5. While MOVING, queues a GPS position note every `moving_ping_mins`
       (suppressed when no valid GNSS fix is available).
    6. While PARKED, queues an alive heartbeat every `heartbeat_hours`;
       gps_valid indicates whether the embedded location is a confirmed fix.
    7. Saves state back to Notecard flash and puts the host to sleep.

  Transport: cellular-first (LTE-M / NB-IoT) with automatic Iridium LEO
  satellite fallback via `card.transport "method":"cell-ntn"`.  The Notecard
  auto-detects the Starnote for Iridium and uses it for NTN fallback.

  Power: solar-trickle-charged LiPo; Swan host enters deep sleep between
  checks via NotePayloadSaveAndSleep / card.attn ATTN interrupt.  Debug
  serial logging is opt-in (see trailer_fleet_tracker_starnote_helpers.h) so
  that the USB-ready wait is never compiled into deployment builds.

  Configuration and helper functions are in
  trailer_fleet_tracker_starnote_helpers.h/.cpp.
  Set PRODUCT_UID in trailer_fleet_tracker_starnote_helpers.h before flashing.
***************************************************************************/

#include "trailer_fleet_tracker_starnote_helpers.h"

// Retry interval (seconds) used when Notecard configuration or template
// registration fails on a wake.  Normal event emission is suppressed until
// the Notecard is fully configured; this short interval ensures the next wake
// retries configuration promptly rather than waiting a full parked-check cycle.
#define CONFIG_RETRY_SECS   60U

// ===========================================================================
// setup() — all per-wake work happens here; loop() is never reached normally
// ===========================================================================
void setup()
{
#ifdef usbSerial
    usbSerial.begin(115200);
#endif

    // Fail fast when PRODUCT_UID was not defined before flashing.
    if (!PRODUCT_UID[0]) {
#ifdef usbSerial
        while (true) {
            usbSerial.println("ERROR: PRODUCT_UID is not set. "
                              "Define it in trailer_fleet_tracker_starnote_helpers.h.");
            delay(5000);
        }
#else
        while (true) { delay(60000); }   // halt; conserve battery
#endif
    }

    notecard.begin();   // I2C @ 100 kHz (default)
#ifdef usbSerial
    notecard.setDebugOutputStream(usbSerial);
#endif

    // Cold-boot I²C warm-up: the host may come up before the Notecard
    // completes its power-on sequence.  A retry-protected transaction here
    // ensures the very first real calls do not race the bus.
    {
        J *warmup = notecard.newRequest("card.version");
        if (warmup) notecard.sendRequestWithRetry(warmup, 5);
    }

    // ── Fetch current time ──────────────────────────────────────────────────
    uint32_t now = 0;
    bool time_ok = getEpoch(now);

    // ── Restore persisted state, or initialize on first boot ───────────────
    NotePayloadDesc payload = {0, 0, 0};
    bool restored = NotePayloadRetrieveAfterSleep(&payload);

    AppState state;
    memset(&state, 0, sizeof(state));

    if (restored) {
        if (!NotePayloadGetSegment(&payload, kStateSegId, &state, sizeof(state))) {
            restored = false;
            memset(&state, 0, sizeof(state));
        }
        NotePayloadFree(&payload);
    }

    bool config_complete = (state.config_version == FIRMWARE_CONFIG_VERSION);

    if (!restored) {
        state.current_state     = STATE_PARKED;
        state.parked_check_secs = DEFAULT_PARKED_CHECK_SECS;
        state.moving_ping_secs  = DEFAULT_MOVING_PING_SECS;
        state.heartbeat_secs    = DEFAULT_HEARTBEAT_SECS;
        if (time_ok && now > 0) state.parked_since = now;

        config_complete = false;
        if (notecardConfigure() && defineTemplates()) {
            state.config_version = FIRMWARE_CONFIG_VERSION;
            config_complete = true;
        }
        if (config_complete) {
            if (fetchEnvOverrides(state)) {
                state.last_env_poll_at = (time_ok && now > 0) ? now : 1;
            }
        }

    } else if (state.config_version != FIRMWARE_CONFIG_VERSION) {
        // A firmware update changed Notecard configuration, note templates,
        // or the AppState struct layout.  Reset the entire state to defaults
        // so that stale bytes from the prior layout are never misinterpreted.
        // current_state is preserved when it holds a valid sentinel.
        uint8_t saved_state =
            (state.current_state == STATE_PARKED ||
             state.current_state == STATE_MOVING)
            ? state.current_state : STATE_PARKED;
        memset(&state, 0, sizeof(state));
        state.current_state     = saved_state;
        state.parked_check_secs = DEFAULT_PARKED_CHECK_SECS;
        state.moving_ping_secs  = DEFAULT_MOVING_PING_SECS;
        state.heartbeat_secs    = DEFAULT_HEARTBEAT_SECS;
        if (time_ok && now > 0) state.parked_since = now;

        config_complete = false;
        if (notecardConfigure() && defineTemplates()) {
            state.config_version = FIRMWARE_CONFIG_VERSION;
            config_complete = true;
        }
        if (config_complete) {
            if (fetchEnvOverrides(state)) {
                state.last_env_poll_at = (time_ok && now > 0) ? now : 1;
            }
        }
    }

    // ── Gate all tracking on successful Notecard configuration ──────────────
    uint32_t sleep_secs;

    if (!config_complete) {
        sleep_secs = CONFIG_RETRY_SECS;
#ifdef usbSerial
        usbSerial.print("[config] incomplete; retrying in ");
        usbSerial.print(sleep_secs);
        usbSerial.println("s");
#endif

    } else {

        // ── Periodic env-var refresh (once per ENV_POLL_SECS) ─────────────────
        if (state.last_env_poll_at == 0 ||
            (time_ok && now > 0 && now > state.last_env_poll_at + ENV_POLL_SECS)) {
            if (fetchEnvOverrides(state)) {
                state.last_env_poll_at = (time_ok && now > 0) ? now : 1;
            }
        }

        // ── Backfill dwell baseline when clock was unavailable at arrival ────
        // When the trailer arrived (MOVING→PARKED) on a prior wake where
        // getEpoch() failed or returned 0, parked_since_needs_init was set
        // so that parked_since is never left at a stale pre-arrival value.
        // On the first subsequent wake with a valid epoch, seed both
        // parked_since and last_heartbeat_at from now — the closest available
        // reference — so dwell_h on the next departure is always grounded in
        // real time.
        if (state.parked_since_needs_init && time_ok && now > 0) {
            state.parked_since            = now;
            state.last_heartbeat_at       = now;
            state.parked_since_needs_init = 0;
#ifdef usbSerial
            usbSerial.println("[arrived] parked_since backfilled from first valid epoch");
#endif
        }

        // ── Drain stale pending events from prior wakes ──────────────────────
        // Events queued by a prior transition that failed to deliver survive
        // sleep in the FIFO.  Drain in FIFO order before evaluating new
        // transitions so that event ordering is always preserved.  Stops at
        // the first failed delivery; remaining events are retried next wake.
        drainPendingQueue(state);

        // ── Read current motion status from Notecard accelerometer ────────────
        bool moving    = false;
        bool motion_ok = isMoving(moving);
        uint8_t prev   = state.current_state;

        // ── State transitions ─────────────────────────────────────────────────
        if (motion_ok) {

            if (prev == STATE_PARKED && moving) {
                // ── PARKED → MOVING (Departed) ─────────────────────────────
                float dwell_h =
                    (state.parked_since > 0 && time_ok && now > state.parked_since)
                    ? (float)(now - state.parked_since) / 3600.0f
                    : 0.0f;
#ifdef usbSerial
                usbSerial.print("[departed] dwell_h=");
                usbSerial.println(dwell_h, 1);
#endif
                // Capture departure GNSS state BEFORE enabling GPS.  GPS mode
                // is currently "off" (parked), so card.location returns the
                // Notecard's last cached fix — the best available position at
                // departure time.  That fix may be stale after a long dwell;
                // see §6.3 for the gps_valid flag semantics.
                float   cap_lat = 0.0f, cap_lon = 0.0f;
                uint8_t cap_gps_valid = 0;
                captureGnssState(cap_lat, cap_lon, cap_gps_valid);

                // Enable periodic GPS acquisition for the moving phase.
                // Explicit mode switch enforces GPS-off-while-parked
                // independent of the Notecard's implicit periodic-mode
                // motion-gating, which is not guaranteed for the Starnote
                // for Iridium's combined GPS hardware path.
                {
                    J *loc = notecard.newRequest("card.location.mode");
                    if (loc) {
                        JAddStringToObject(loc, "mode",    "periodic");
                        JAddNumberToObject(loc, "seconds",
                                           (double)state.moving_ping_secs);
                        sendAndCheck(loc, "card.location.mode periodic");
                    }
                }
#ifdef usbSerial
                usbSerial.println("[departed] GPS enabled (periodic)");
#endif

                state.current_state = STATE_MOVING;
                if (time_ok && now > 0) state.last_location_at = now;

                enqueuePendingEvent(state, EVENT_DEPARTED, dwell_h,
                                    (time_ok && now > 0) ? now : 0U,
                                    cap_lat, cap_lon, cap_gps_valid);

            } else if (prev == STATE_MOVING && !moving) {
                // ── MOVING → PARKED (Arrived) ──────────────────────────────
#ifdef usbSerial
                usbSerial.println("[arrived] trailer stopped");
#endif
                // Capture arrival GNSS state WHILE GPS is still in periodic
                // mode — the cached fix is within one moving_ping_secs interval
                // of the actual stop.  Must be done before disabling GPS below.
                float   cap_lat = 0.0f, cap_lon = 0.0f;
                uint8_t cap_gps_valid = 0;
                captureGnssState(cap_lat, cap_lon, cap_gps_valid);

                // Disable GPS for the parked phase.  Re-enabled on the next
                // PARKED→MOVING departure.  Explicit mode switch matches the
                // corresponding enable in the departure branch above.
                {
                    J *loc = notecard.newRequest("card.location.mode");
                    if (loc) {
                        JAddStringToObject(loc, "mode", "off");
                        sendAndCheck(loc, "card.location.mode off");
                    }
                }
#ifdef usbSerial
                usbSerial.println("[arrived] GPS disabled (off)");
#endif

                state.current_state = STATE_PARKED;
                if (time_ok && now > 0) {
                    state.parked_since            = now;
                    state.last_heartbeat_at       = now;
                    state.parked_since_needs_init = 0;
                } else {
                    state.parked_since_needs_init = 1;
                }

                enqueuePendingEvent(state, EVENT_ARRIVED, 0.0f,
                                    (time_ok && now > 0) ? now : 0U,
                                    cap_lat, cap_lon, cap_gps_valid);
            }

        } else {
#ifdef usbSerial
            usbSerial.println("[motion] read failed; transitions suppressed this cycle");
#endif
        }

        // ── Attempt immediate delivery of any newly-queued event ─────────────
        drainPendingQueue(state);

        // ── Steady-state behavior ─────────────────────────────────────────────
        if (state.current_state == STATE_MOVING) {
            if (time_ok && now > 0 &&
                now >= state.last_location_at + state.moving_ping_secs) {
#ifdef usbSerial
                usbSerial.println("[location] queuing position note");
#endif
                if (sendLocationNote()) {
                    state.last_location_at = now;
                }
            }
        } else {
            if (time_ok && now > 0 &&
                now >= state.last_heartbeat_at + state.heartbeat_secs) {
                float volt = 0.0f;
                if (getBatteryVoltage(volt)) {
#ifdef usbSerial
                    usbSerial.print("[heartbeat] volt=");
                    usbSerial.println(volt, 2);
#endif
                    if (sendHeartbeatNote(volt)) {
                        state.last_heartbeat_at = now;
                    }
                } else {
#ifdef usbSerial
                    usbSerial.println("[heartbeat] voltage read failed; skipping");
#endif
                }
            }
        }

        // ── Compute sleep interval ────────────────────────────────────────────
        sleep_secs = (state.current_state == STATE_MOVING)
                     ? state.moving_ping_secs
                     : state.parked_check_secs;

        if (state.current_state == STATE_PARKED &&
            state.last_heartbeat_at > 0 && time_ok && now > 0) {
            uint32_t hb_due = state.last_heartbeat_at + state.heartbeat_secs;
            if (hb_due > now) {
                uint32_t until_hb = hb_due - now;
                if (until_hb < sleep_secs) sleep_secs = until_hb;
            }
        }
        if (sleep_secs < 60) sleep_secs = 60;

    }   // end if (config_complete)

#ifdef usbSerial
    usbSerial.print("[sleep] sleeping ");
    usbSerial.print(sleep_secs);
    usbSerial.println("s");
#endif

    // ── Persist state and put host to sleep ────────────────────────────────
    // NotePayloadSaveAndSleep serializes AppState to Notecard flash, then
    // issues card.attn to wake Swan from deep sleep after sleep_secs seconds.
    // The next hardware wake re-enters setup() from cold; NotePayloadRetrieve-
    // AfterSleep at the top of setup() rehydrates state.
    NotePayloadDesc out = {0, 0, 0};
    NotePayloadAddSegment(&out, kStateSegId, &state, sizeof(state));
    NotePayloadSaveAndSleep(&out, sleep_secs, NULL);

    for (uint8_t retries = 0; retries < 3; ++retries) {
#ifdef usbSerial
        usbSerial.print("[sleep] NotePayloadSaveAndSleep returned unexpectedly; retry ");
        usbSerial.print(retries + 1);
        usbSerial.println("/3");
#endif
        delay(2000);
        NotePayloadSaveAndSleep(&out, sleep_secs, NULL);
    }

#ifdef usbSerial
    usbSerial.println("[sleep] all sleep retries failed; forcing host reset");
    delay(100);
#endif
    NVIC_SystemReset();
}

void loop()
{
    // All application logic lives in setup().  Force a reset if execution
    // ever reaches here so the tracker resumes normal operation.
    NVIC_SystemReset();
}
