/*
 * cellular_medication_adherence_pillbox.ino
 *
 * Monitors a 7-day pillbox via normally-open snap-action micro-switches wired
 * to the Notecarrier CX's D5, D6, and D9–D13 digital GPIO pins (onboard Cygnet
 * STM32L4 host).
 *
 * Each time the device wakes (every 30 seconds by default), it reads all seven
 * compartment pins and detects newly-opened lids by comparing pin state against
 * the previous wake. Every detected rising edge generates a pill_open.qo Note —
 * multiple opens of the same compartment in a day each produce a separate Note.
 * Note: a lid that is opened and re-closed between consecutive wakes is not
 * detected; the firmware observes state only at poll boundaries.
 *
 * A daily summary Note (pill_summary.qo) is queued once per UTC day at the
 * hour configured by the summary_hour_utc environment variable (default 0 =
 * midnight UTC). The summary records the bitmask and count of compartments
 * opened during the preceding UTC day.
 *
 * State (previous pin values, daily bitmask, UTC day) is persisted across
 * sleep cycles by NotePayloadSaveAndSleep / NotePayloadRetrieveAfterSleep —
 * no external EEPROM or flash writes needed.
 *
 * Hardware: Notecarrier CX + Notecard Cell+WiFi (NOTE-MBGLW) + Blues Mojo
 * Host MCU: Cygnet STM32L4 (onboard, Notecarrier CX)
 * Library:  Blues Wireless Notecard (note-arduino)
 *
 * Helper functions, struct definitions, pin/notefile constants, and the
 * usbSerial debug toggle live in:
 *   cellular_medication_adherence_pillbox_helpers.h
 *   cellular_medication_adherence_pillbox_helpers.cpp
 *
 * See README §4 for wiring details and README §5 for environment variables.
 */

#include <Notecard.h>
#include "cellular_medication_adherence_pillbox_helpers.h"

// ── Product UID ──────────────────────────────────────────────────────────────
// Replace "" with your Notehub project ProductUID before flashing, e.g.:
//   #define PRODUCT_UID "com.your-company.your-name:pillbox"
#ifndef PRODUCT_UID
#define PRODUCT_UID ""
#pragma message "PRODUCT_UID is not defined. Set it to your Notehub ProductUID before flashing."
#endif

// Notecard instance. Declared extern in the helpers header so all helper
// functions can use it without a separate parameter.
Notecard notecard;

// ════════════════════════════════════════════════════════════════════════════
// setup()
//
// Entered fresh on every wake — first power-on and each ATTN re-power after
// NotePayloadSaveAndSleep. All application logic lives here.
// ════════════════════════════════════════════════════════════════════════════
void setup() {
#ifdef usbSerial
    usbSerial.begin(115200);
#endif

    setupPins(); // configure compartment GPIO with internal pull-ups

    notecard.begin();
#ifdef usbSerial
    notecard.setDebugOutputStream(usbSerial);
#endif

    // ── PRODUCT_UID guard ─────────────────────────────────────────────────
    // An empty PRODUCT_UID produces a build that appears to run normally but
    // never associates with Notehub — easy to misdiagnose as a radio or
    // connectivity issue. Halt unconditionally here so the misconfiguration
    // is immediately obvious in both debug and production builds.
    if (!PRODUCT_UID[0]) {
#ifdef usbSerial
        usbSerial.println(
            "\n*** FATAL: PRODUCT_UID is empty ***\n"
            "The Notecard will not associate with any Notehub project.\n"
            "Set PRODUCT_UID in cellular_medication_adherence_pillbox.ino\n"
            "and reflash. Halting.");
#endif
        while (true) { delay(2000); } // unconditional halt — not gated on usbSerial
    }

    // ── Notecard readiness probe ──────────────────────────────────────────
    // On a cold power-on the host MCU may come up before the Notecard has
    // finished its own startup sequence. sendRequestWithRetry() polls with
    // back-off until the Notecard acknowledges a benign card.version request,
    // establishing that the I²C bus is live before the payload restore below.
    //
    // Without this guard a cold-boot I²C race causes
    // NotePayloadRetrieveAfterSleep() to return false, and the wake is
    // misclassified as first_boot — wiping prev_pin_mask, daily_opens, the
    // pending retry queue, and any not-yet-emitted summary state. Transient
    // bus unavailability must be distinguished from a genuine absence of a
    // saved payload. If the probe succeeds, a subsequent false from
    // NotePayloadRetrieveAfterSleep is a real cold boot; if it fails after
    // all retries, setup() resets rather than proceeding into payload restore
    // where state loss is certain.
    {
        J *req = notecard.newRequest("card.version");
        bool ready = req && notecard.sendRequestWithRetry(req, 5);
        if (!ready) {
            // Notecard did not respond after all retry attempts. Proceeding
            // into NotePayloadRetrieveAfterSleep() on a non-responsive I²C
            // bus would make the transient startup race indistinguishable from
            // a genuine cold boot, wiping all persistent state. Short-sleep
            // and soft-reset so the Cygnet retries after the Notecard has had
            // more time to finish its startup sequence.
#ifdef usbSerial
            usbSerial.println("[init] card.version probe failed — resetting in 5 s");
#endif
            delay(5000);
            NVIC_SystemReset();
        }
    }

    // ── Recover persistent state from the Notecard ────────────────────────
    NotePayloadDesc payload = {0, 0, 0};
    bool resumed = NotePayloadRetrieveAfterSleep(&payload);

    PillboxState state;
    bool first_boot = !resumed;

    if (resumed) {
        bool ok = NotePayloadGetSegment(&payload, STATE_SEG_ID,
                                        &state, sizeof(state));
        NotePayloadFree(&payload);
        // Treat a missing segment or stale version as first boot so that a
        // firmware update with a changed struct layout never restores misaligned
        // data and produces confusing behaviour.
        if (!ok || state.version != PILLBOX_STATE_VERSION) {
            first_boot = true;
        }
    }

    if (first_boot) {
        memset(&state, 0, sizeof(state));
        state.version      = PILLBOX_STATE_VERSION;
        state.poll_sec     = DEFAULT_POLL_SEC;
        state.summary_hour = DEFAULT_SUMMARY_HOUR;
        state.outbound_min = DEFAULT_OUTBOUND_MIN;
        state.inbound_min  = DEFAULT_INBOUND_MIN;

        // Snapshot current pin state so the first sample loop doesn't
        // false-trigger events for compartments already open at power-on.
        state.prev_pin_mask = sampleCompartments();

        // Disable the onboard accelerometer for cleaner power traces during
        // bench bring-up with Mojo. Has no effect on medication-adherence logic.
        J *req = notecard.newRequest("card.motion.mode");
        if (req) {
            JAddBoolToObject(req, "stop", true);
            J *rsp = notecard.requestAndResponse(req);
            if (rsp) {
                if (notecard.responseError(rsp)) {
#ifdef usbSerial
                    usbSerial.println("[init] card.motion.mode error (non-fatal)");
#endif
                }
                notecard.deleteResponse(rsp);
            }
        }
    }

    // ── Notecard configuration (applied every wake; all calls are idempotent) ─
    // hub.set and note.template re-applied unconditionally so a transient I2C
    // failure on any prior wake cannot leave the device permanently
    // unassociated with Notehub or sending untemplated notes.
    initNotecard(PRODUCT_UID, state.outbound_min, state.inbound_min);
    defineTemplates();

    // Fetch env overrides on every wake — thresholds and cadences may have
    // changed since the last inbound sync.
    fetchEnvOverrides(state);

    // ── Replay any pill_open.qo events that failed on the previous wake ───
    replayPendingOpenEvents(state);

    // ── Day rollover check ────────────────────────────────────────────────
    uint32_t utc_hour = 0;
    uint32_t utc_day  = utcDayAndHour(&utc_hour);

    if (utc_day != 0) {
        if (state.last_utc_day == 0) {
            // First valid UTC time-sync: record the current day. Any opens
            // already in daily_opens will be included in the first genuine
            // end-of-day summary. Emitting a summary here would produce a
            // spurious zero-adherence record for a partial commissioning day.
            state.last_utc_day = utc_day;
#ifdef usbSerial
            usbSerial.print("[day] first time-sync utc_day=");
            usbSerial.println(utc_day);
#endif
        } else if (utc_day != state.last_utc_day) {
            // Genuine day rollover: archive today's opens for summary emit.
            state.prev_day_opens     = state.daily_opens;
            state.summary_pending    = true;
            state.summary_target_day = utc_day;
            state.daily_opens        = 0;
            state.last_utc_day       = utc_day;
#ifdef usbSerial
            usbSerial.print("[day] new utc_day=");
            usbSerial.println(utc_day);
#endif
        }
    }

    // Emit the pending daily summary once summary_hour_utc has been reached
    // on the target day. Only clear summary_pending on a confirmed successful
    // note.add; if it fails the flag stays set for retry on the next wake.
    if (state.summary_pending && utc_day != 0 &&
        utc_day == state.summary_target_day &&
        utc_hour >= state.summary_hour) {
        if (emitDailySummary(state.prev_day_opens)) {
            state.summary_pending = false;
        }
    }

    // ── Compartment open detection ────────────────────────────────────────
    uint8_t cur_mask     = sampleCompartments();
    uint8_t newly_opened = cur_mask & ~state.prev_pin_mask; // LOW→HIGH rising edges

    // Compute the full post-poll day bitmask before the event loop so that
    // every pill_open.qo emitted this wake carries an identical day_opens_mask.
    // Incrementally OR-ing inside the loop would cause the first event in a
    // multi-compartment burst to report a smaller mask than the last, breaking
    // the downstream refill-detection heuristic.
    uint8_t post_poll_mask = state.daily_opens | newly_opened;
    state.daily_opens = post_poll_mask;

    for (uint8_t i = 0; i < NUM_COMPARTMENTS; i++) {
        if (newly_opened & (1u << i)) {
            if (!emitOpenEvent(i, post_poll_mask, newly_opened)) {
                // note.add failed after all retries — persist this event in
                // the ring buffer so it can be replayed on the next wake with
                // its original context intact.
                enqueuePendingEvent(state, i, post_poll_mask, newly_opened);
            }
        }
    }

    state.prev_pin_mask = cur_mask;

    // ── Sleep until next poll ─────────────────────────────────────────────
    sleepHost(state);
}

// loop() is intentionally empty.
// All logic runs in setup(), which re-enters each time the Notecard's ATTN
// pin re-powers the host MCU after NotePayloadSaveAndSleep.
void loop() {}
