// tote_pool_tracker.ino
// Returnable Container / Tote Pool Tracker
//
// Tracks reusable totes, kegs, and gas cylinders across the supply chain
// using the Notecard's built-in accelerometer for motion detection and
// cell-tower and WiFi AP triangulation for power-efficient location. The
// Notecarrier CX's STM32 host sleeps in STOP2 between wake cycles and is woken
// by the Notecard's ATTN pin (see cx_sleep.h). This POC build uses a
// rechargeable LiPo battery; realistic service life is 12–24 months (see README §9
// for the Li-SOCl₂ primary-cell path that extends deployments to 3–5+ years).
//
// Wake sources (whichever fires first):
//   1. Notecard accelerometer detects a motion-state change
//      (stopped → moving or moving → stopped)
//   2. Heartbeat timer expires (default: 24 hours)
//
// Events emitted:
//   tote_event.qo    – immediate (sync:true) on motion-state change
//   tote_heartbeat.qo – templated daily check-in (sync:true, immediate delivery)
//
// Notehub appends cell-tower-resolved location (where_lat, where_lon,
// where_location) to every event — no GPS hardware or firmware code needed.
//
// Hardware: Blues Notecarrier CX + Notecard Cell+WiFi (MBGLW) + LiPo battery (POC build).
//           Jumper the Notecarrier CX ATTN pin to D5 (same 16-pin header); leave EN
//           unconnected.
// Build:    Tools > USB support (if available) > None (usb=none). With the USB CDC
//           stack enabled and no USB host attached the host cannot stay in STOP2.

// ---------------------------------------------------------------------------
// Product UID — replace with your Notehub project's ProductUID before flashing.
// Leaving this empty produces a hard compile error so a misconfigured binary
// cannot be accidentally deployed. For local development without a Notehub
// project yet, add -DALLOW_EMPTY_PRODUCT_UID to the build flags as an explicit
// override — that flag must not appear in a shipping build.
// ---------------------------------------------------------------------------
#ifndef PRODUCT_UID
#  ifndef ALLOW_EMPTY_PRODUCT_UID
#    error "PRODUCT_UID is not set. Define it as your Notehub ProductUID before flashing (e.g. -DPRODUCT_UID='\"com.your-company.your-name:tote-tracker\"'). For local development without a project, add -DALLOW_EMPTY_PRODUCT_UID to suppress this error — that flag must not appear in a shipping build."
#  else
#    define PRODUCT_UID ""
#    pragma message "PRODUCT_UID empty (ALLOW_EMPTY_PRODUCT_UID override active) — device will not associate with any Notehub project"
#  endif
#endif

// Debug output is controlled by #define DEBUG in tote_pool_tracker_helpers.h.
// Uncomment it there to enable logging and Notecard I²C trace output in both
// this sketch and the helper .cpp translation unit. Output goes to the LPUART
// on the Notecarrier CX debug jack, which an ST-LINK V3 exposes as a virtual
// COM port (USB CDC must stay disabled for the host to sleep).

#include "tote_pool_tracker_helpers.h"
#include "cx_sleep.h"

// ---------------------------------------------------------------------------
// Globals — definitions; extern declarations live in tote_pool_tracker_helpers.h
// ---------------------------------------------------------------------------
ToteState  g_state;
Notecard   notecard;
#ifdef DEBUG
Uart       debugSerial(PIN_VCP_RX, PIN_VCP_TX);
#endif

// True for the first pass through loop() after power-up; drives the boot
// heartbeat and deadline anchoring (what the old cold-boot path did).
static bool g_first_wake = true;

uint32_t   g_heartbeat_hours   = DEFAULT_HEARTBEAT_HOURS;
float      g_low_battery_mv    = (float)DEFAULT_LOW_BATTERY_MV;
uint32_t   g_motion_threshold  = DEFAULT_MOTION_THRESHOLD;
uint32_t   g_motion_bucket_sec = DEFAULT_MOTION_BUCKET_SEC;

// ===========================================================================
// setup() — runs once at power-up. The host resumes in place after each
// STOP2 sleep, so one-time Notecard configuration lives here and every
// per-wake step lives in loop().
// ===========================================================================
void setup() {
    // Debug output is gated behind DEBUG so field builds don't pay the ~1 mA
    // UART penalty.
#ifdef DEBUG
    debugSerial.begin(115200);
#endif

    // Initialize Notecard over I²C (the Notecarrier CX exposes SDA/SCL
    // through its M.2 interface — no additional wiring required).
    notecard.begin();
#ifdef DEBUG
    notecard.setDebugOutputStream(debugSerial);
#endif

    // Zero state and run one-time Notecard configuration.
    memset(&g_state, 0, sizeof(g_state));
    notecardConfigure(PRODUCT_UID);
    defineTemplates();

    // Arm the ATTN wake: the Notecard raises ATTN (D5) to end each sleep.
    cxSleepBegin();
}

// ===========================================================================
// loop() — one wake cycle, then sleep until the Notecard raises ATTN
// (motion-state change or heartbeat timer, whichever comes first).
// ===========================================================================
void loop() {
    bool first_wake = g_first_wake;
    g_first_wake = false;

    // -----------------------------------------------------------------------
    // Fetch environment variable overrides on every wake. On first boot the
    // inbound sync hasn't happened yet so defaults are used; overrides arrive
    // on the first successful cellular session.
    // -----------------------------------------------------------------------
    fetchEnvOverrides();

    // -----------------------------------------------------------------------
    // Retry any one-time Notecard configuration not confirmed on a previous
    // wake. On a successful first boot each flag is already true; these
    // blocks are no-ops.
    // -----------------------------------------------------------------------
    if (!g_state.voltage_mode_confirmed) {
        J *req = notecard.newRequest("card.voltage");
        JAddStringToObject(req, "mode", "lipo");
        J *rsp = notecard.requestAndResponse(req);
        if (rsp) {
            if (!notecard.responseError(rsp)) g_state.voltage_mode_confirmed = true;
            notecard.deleteResponse(rsp);
        }
    }

    if (!g_state.triangulate_confirmed) {
        J *req = notecard.newRequest("card.triangulate");
        JAddStringToObject(req, "mode", "wifi,cell");
        JAddBoolToObject(req, "on",  true);
        JAddBoolToObject(req, "set", true);
        J *rsp = notecard.requestAndResponse(req);
        if (rsp) {
            if (!notecard.responseError(rsp)) g_state.triangulate_confirmed = true;
            notecard.deleteResponse(rsp);
        }
    }

    if (!g_state.template_confirmed) {
        defineTemplates();
    }

    // -----------------------------------------------------------------------
    // Reapply Notecard configuration whenever desired values differ from what
    // was last successfully applied (see ToteState.last_applied_* for rationale).
    // -----------------------------------------------------------------------
    if (g_motion_threshold     != g_state.last_applied_motion_threshold ||
        g_motion_bucket_sec    != g_state.last_applied_motion_bucket_sec) {
        J *req = notecard.newRequest("card.motion.mode");
        JAddBoolToObject(req, "start", true);
        JAddNumberToObject(req, "motion",  (int)g_motion_threshold);
        JAddNumberToObject(req, "seconds", (int)g_motion_bucket_sec);
        if (notecard.sendRequest(req)) {
            g_state.last_applied_motion_threshold  = g_motion_threshold;
            g_state.last_applied_motion_bucket_sec = g_motion_bucket_sec;
        }
    }

    // -----------------------------------------------------------------------
    // Fetch epoch before the heartbeat-hours check so we can reanchor
    // next_heartbeat_epoch in the same wake cycle that hub.set is reissued
    // (Item 3). Reading it here once avoids a redundant card.time call later.
    // -----------------------------------------------------------------------
    uint32_t now_epoch = notecardEpoch();

    if (g_heartbeat_hours != g_state.last_applied_heartbeat_hours) {
        J *req = notecard.newRequest("hub.set");
        if (PRODUCT_UID[0]) JAddStringToObject(req, "product", PRODUCT_UID);
        JAddStringToObject(req, "mode",     "periodic");
        JAddNumberToObject(req, "outbound", (int)(g_heartbeat_hours * 60));
        JAddNumberToObject(req, "inbound",  (int)(g_heartbeat_hours * 60));
        if (notecard.sendRequest(req)) {
            g_state.last_applied_heartbeat_hours = g_heartbeat_hours;
            // Reanchor the local heartbeat deadline immediately so the device
            // starts sleeping toward the new interval in this same wake cycle
            // rather than finishing the old deadline first (Item 3). When the
            // epoch is unavailable the deadline is left as-is and will be
            // reanchored on the next wake that returns a valid epoch.
            if (now_epoch > 0) {
                g_state.next_heartbeat_epoch = now_epoch + g_heartbeat_hours * 3600UL;
            }
        }
    }

    // -----------------------------------------------------------------------
    // Increment the wake counter before emitting any note so the cycle field
    // reflects the current wake (1-based) rather than the count of prior wakes.
    // -----------------------------------------------------------------------
    g_state.cycle_count += 1;

    bool  now_moving = readMotionMoving();
    float battery_mv = readBatteryMv();

    // -----------------------------------------------------------------------
    // Act on wake reason.
    //
    // wake_send_ok tracks whether every note.add that should have been queued
    // on this wake succeeded. It is evaluated at wake level rather than inside
    // individual send helpers so that a success on one note never masks a
    // failure on an earlier note emitted in the same wake (Item 4).
    //
    // Four mutually-exclusive branches, in priority order:
    //
    //  1. first_wake      — power-up: emit boot heartbeat, anchor deadline.
    //  2. pending != NONE — retry wake: resend the note that failed last time;
    //                       then re-evaluate the current motion state against
    //                       the pending note's own motion baseline so a single
    //                       opposite transition that occurred during the retry
    //                       period is emitted; finally check for an overdue
    //                       heartbeat.
    //  3. motion changed  — emit departed/arrived; check for overdue heartbeat.
    //  4. else            — heartbeat-due wake: emit heartbeat only when the
    //                       absolute deadline has actually arrived. A retry
    //                       wake that reaches this branch with no pending note
    //                       (failed_send_count cleared between wakes) is a
    //                       no-op — it falls through to enterSleep() without
    //                       emitting anything (Item 2).
    // -----------------------------------------------------------------------
    bool wake_send_ok = true;

    if (first_wake) {
        // ------------------------------------------------------------------
        // Branch 1: first wake after power-up
        // ------------------------------------------------------------------
        wake_send_ok = sendHeartbeat(REASON_BOOT, now_moving, battery_mv);
        if (now_epoch > 0) {
            g_state.next_heartbeat_epoch = now_epoch + g_heartbeat_hours * 3600UL;
        }

    } else if (g_state.pending_note_type != PENDING_NONE) {
        // ------------------------------------------------------------------
        // Branch 2: retry wake — resend the note that failed on the previous
        // wake using the original payload fields (event type, motion state,
        // battery_mv, cycle count — all captured at original send time).
        //
        // After a successful retry, re-evaluate the current motion state
        // against the pending note's motion baseline (g_state.pending_moving)
        // to catch a single opposite transition that occurred during the
        // retry period. This covers two cases:
        //   • Heartbeat pending + tote departed/arrived while retrying:
        //     now_moving differs from pending_moving but Branch 3 is skipped
        //     because pending_note_type was non-NONE.
        //   • Motion pending + opposite transition before retry succeeds:
        //     the original event (e.g. "departed") is replayed; if the
        //     container has since stopped, "arrived" is emitted too.
        //
        // pending_moving is the correct reference here, NOT was_moving.
        // was_moving is updated unconditionally at the end of every wake —
        // including failed retry wakes — so it drifts toward now_moving
        // across multiple retries and can suppress a legitimate transition.
        // pending_moving is written once when the pending record is created
        // and never overwritten during retry wakes, so it remains a stable
        // comparison anchor for the entire retry period regardless of how
        // many intermediate retries fail.
        //
        // Limitation: only one opposite transition per retry period can be
        // recovered. If the container transitions more than once before the
        // retry succeeds, the firmware evaluates the final state only;
        // additional intermediate transitions are not recoverable with this
        // single-pending-note model.
        // ------------------------------------------------------------------
        uint8_t pending_before = g_state.pending_note_type; // saved before doSend clears it
        wake_send_ok = resendPendingNote();

        if (wake_send_ok) {
            // Advance the heartbeat deadline when the retried note was a
            // heartbeat; the deadline must not advance until delivery is
            // confirmed.
            if (pending_before == PENDING_HEARTBEAT && now_epoch > 0) {
                g_state.next_heartbeat_epoch = now_epoch + g_heartbeat_hours * 3600UL;
            }

            // Re-evaluate motion state against the pending note's motion
            // baseline. pending_moving was captured when the original failed
            // note was queued and is never modified during retry wakes, so
            // comparing against it correctly detects a subsequent opposite
            // transition even after multiple failed retries — unlike
            // was_moving, which drifts toward now_moving on every wake.
            if (now_moving != g_state.pending_moving) {
                const char *evt = now_moving ? "departed" : "arrived";
                wake_send_ok = sendMotionEvent(evt, now_moving, battery_mv);
            }

            // After all preceding notes succeeded, check whether the heartbeat
            // deadline also passed during the retry cycle. When pending_before
            // was PENDING_HEARTBEAT the deadline was already advanced above, so
            // hb_due evaluates false and this block is a no-op in that case.
            if (wake_send_ok) {
                bool hb_due = (now_epoch > 0 &&
                               g_state.next_heartbeat_epoch > 0 &&
                               now_epoch >= g_state.next_heartbeat_epoch);
                if (hb_due) {
                    bool    low_batt = (battery_mv > 0.0f && battery_mv < g_low_battery_mv);
                    uint8_t rc       = low_batt ? REASON_LOW_BATTERY : REASON_HEARTBEAT;
                    wake_send_ok     = sendHeartbeat(rc, now_moving, battery_mv);
                    if (wake_send_ok && now_epoch > 0) {
                        g_state.next_heartbeat_epoch = now_epoch + g_heartbeat_hours * 3600UL;
                    }
                }
            }
        }

    } else if (now_moving != g_state.was_moving) {
        // ------------------------------------------------------------------
        // Branch 3: motion-state change — container departed or arrived.
        // "departed" = was stopped, now moving.
        // "arrived"  = was moving, now stopped.
        // ------------------------------------------------------------------
        const char *evt = now_moving ? "departed" : "arrived";
        wake_send_ok = sendMotionEvent(evt, now_moving, battery_mv);

        // Check whether the absolute heartbeat deadline has also passed.
        // Without this check, a container that moves at least once per
        // heartbeat_hours interval would never emit a daily heartbeat.
        bool hb_due = (now_epoch > 0 &&
                       g_state.next_heartbeat_epoch > 0 &&
                       now_epoch >= g_state.next_heartbeat_epoch);
        if (hb_due) {
            // Only attempt the heartbeat if the motion event succeeded.
            // If the motion send failed its pending record is set; attempting
            // another note.add here would overwrite that record with
            // PENDING_HEARTBEAT and permanently lose the motion event.
            if (wake_send_ok) {
                bool    low_batt = (battery_mv > 0.0f && battery_mv < g_low_battery_mv);
                uint8_t rc       = low_batt ? REASON_LOW_BATTERY : REASON_HEARTBEAT;
                wake_send_ok     = sendHeartbeat(rc, now_moving, battery_mv);
                if (wake_send_ok && now_epoch > 0) {
                    g_state.next_heartbeat_epoch = now_epoch + g_heartbeat_hours * 3600UL;
                }
                // If heartbeat failed: next_heartbeat_epoch unchanged (still
                // past-due), so the retry wake will resend the heartbeat and
                // advance the deadline at that point.
            }
            // else: motion failed; heartbeat deferred — next_heartbeat_epoch
            // stays past-due so the heartbeat is emitted after the motion
            // retry succeeds (handled in Branch 2 above).
        } else if (now_epoch > 0 && g_state.next_heartbeat_epoch == 0) {
            // First time a valid epoch is available: anchor the deadline so
            // subsequent wakes have a real absolute target to sleep toward.
            g_state.next_heartbeat_epoch = now_epoch + g_heartbeat_hours * 3600UL;
        }

    } else {
        // ------------------------------------------------------------------
        // Branch 4: no motion change, no pending note.
        //
        // Send a heartbeat only when the absolute deadline has actually
        // arrived. Conditions that allow sending:
        //   - next_heartbeat_epoch == 0: deadline was never anchored (no
        //     cellular time yet); trust the hardware timer that woke us.
        //   - now_epoch == 0: we have no epoch to validate against; trust
        //     the timer.
        //   - now_epoch >= next_heartbeat_epoch: epoch confirms it is due.
        //
        // A RETRY_WAKE_SEC wake that reaches this branch — possible only
        // when failed_send_count was cleared between wakes — is a no-op:
        // hb_due is false, nothing is emitted, and enterSleep() re-arms
        // for the remaining time to the real deadline (Item 2).
        // ------------------------------------------------------------------
        bool hb_due = (g_state.next_heartbeat_epoch == 0) ||
                      (now_epoch == 0)                    ||
                      (now_epoch >= g_state.next_heartbeat_epoch);
        if (hb_due) {
            bool    low_batt = (battery_mv > 0.0f && battery_mv < g_low_battery_mv);
            uint8_t rc       = low_batt ? REASON_LOW_BATTERY : REASON_HEARTBEAT;
            wake_send_ok     = sendHeartbeat(rc, now_moving, battery_mv);
            if (wake_send_ok && now_epoch > 0) {
                g_state.next_heartbeat_epoch = now_epoch + g_heartbeat_hours * 3600UL;
            }
        }
        // else: not due — fall through without emitting; enterSleep() will
        // sleep for the remaining time to the heartbeat deadline.
    }

    // -----------------------------------------------------------------------
    // Update the wake-level failure count (Item 4).
    // Cleared only when every note that should have been queued this wake
    // succeeded; incremented if any note.add failed. This prevents a later
    // success from masking an earlier failure in the same wake, which would
    // suppress the short-retry path and silently drop the first note.
    // -----------------------------------------------------------------------
    if (wake_send_ok) {
        g_state.failed_send_count = 0;
    } else if (g_state.failed_send_count < 0xFF) {
        g_state.failed_send_count++;
    }

    // Remember the motion state and sleep until the next event.
    g_state.was_moving = now_moving;
    enterSleep(now_epoch);
}
