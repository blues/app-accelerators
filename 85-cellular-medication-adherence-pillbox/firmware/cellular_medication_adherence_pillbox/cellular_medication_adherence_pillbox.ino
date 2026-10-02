/*
 * cellular_medication_adherence_pillbox.ino
 *
 * Monitors a 7-day pillbox via normally-open snap-action micro-switches wired
 * to the Notecarrier CX's D5, D6, and D9–D13 digital GPIO pins (onboard
 * STM32L433 host).
 *
 * Each time the host wakes (every 30 seconds by default), it reads all seven
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
 * Between polls the host sleeps in STM32 STOP2 (~1-2 µA, RAM retained) and is
 * woken by the Notecard's ATTN pin (card.attn "sleep"); see cx_sleep.h.
 * Because execution resumes in place, state (previous pin values, daily
 * bitmask, UTC day, retry queue) simply lives in RAM — no external EEPROM,
 * flash writes, or Notecard payload storage needed.
 *
 * Wiring for sleep: jumper the Notecarrier CX ATTN pin to A0. All seven
 * digital header pins (D5, D6, D9–D13) are used by the compartment switches,
 * so A0 serves as the ATTN wake input (it is an ordinary GPIO on the STM32).
 * Leave EN unconnected: on the CX it enables the shared 3.3 V VIO rail, so
 * driving it from ATTN browns out the whole board instead of sleeping the host.
 *
 * Build: Tools > USB support (if available) > None (usb=none). With the USB
 * CDC stack enabled and no USB host attached the host cannot stay in STOP2.
 * Debug output goes to the ST-LINK virtual COM port on the CX debug jack.
 *
 * Hardware: Notecarrier CX + Notecard Cell+WiFi (NOTE-MBGLW) + Blues Mojo
 * Host MCU: STM32L433 (onboard, Notecarrier CX)
 * Library:  Blues Wireless Notecard (note-arduino), STM32duino Low Power
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

// All seven D pins are compartment inputs, so ATTN is jumpered to A0 instead
// of the usual D5. Must be defined before cx_sleep.h is included.
#define CX_ATTN_PIN A0
#include "cx_sleep.h"

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

// Debug output: LPUART1 on the CX debug jack, which an ST-LINK exposes as a
// virtual COM port. (USB CDC must be disabled for STOP2 to work; see
// cx_sleep.h.) Referenced through the usbSerial macro in the helpers header.
#ifdef usbSerial
Uart dbgSerial(PIN_VCP_RX, PIN_VCP_TX);
#endif

// Application state. Lives in RAM; STOP2 retains it across every sleep/wake
// cycle, so it is initialized once in setup() and never serialized.
static PillboxState state;

// ════════════════════════════════════════════════════════════════════════════
// setup()
//
// Runs once at power-up: serial, pins, Notecard bring-up, state init, and
// the one-time ATTN wake arming. Everything that must run on every poll
// lives in loop().
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
    // connectivity issue. Make the misconfiguration obvious at boot in both
    // debug and production builds; the firmware still runs so the bench rig
    // can be exercised, but nothing will reach Notehub.
    if (!PRODUCT_UID[0]) {
#ifdef usbSerial
        usbSerial.println(
            "\n*** PRODUCT_UID is empty ***\n"
            "The Notecard will not associate with any Notehub project.\n"
            "Set PRODUCT_UID in cellular_medication_adherence_pillbox.ino\n"
            "and reflash.");
#endif
    }

    // ── Notecard readiness probe ──────────────────────────────────────────
    // On a cold power-on the host MCU may come up before the Notecard has
    // finished its own startup sequence. sendRequestWithRetry() polls with
    // back-off until the Notecard acknowledges a benign card.version request,
    // establishing that the I²C bus is live before the configuration calls
    // in loop(). If it still fails, loop() retries hub.set/templates on every
    // wake anyway, so this is informational.
    {
        J *req = notecard.newRequest("card.version");
        bool ready = req && notecard.sendRequestWithRetry(req, 5);
        if (!ready) {
#ifdef usbSerial
            usbSerial.println("[init] card.version probe failed — continuing; config retried each wake");
#endif
        }
    }

    // ── Initialize application state ──────────────────────────────────────
    memset(&state, 0, sizeof(state));
    state.poll_sec     = DEFAULT_POLL_SEC;
    state.summary_hour = DEFAULT_SUMMARY_HOUR;
    state.outbound_min = DEFAULT_OUTBOUND_MIN;
    state.inbound_min  = DEFAULT_INBOUND_MIN;

    // Snapshot current pin state so the first poll doesn't false-trigger
    // events for compartments already open at power-on.
    state.prev_pin_mask = sampleCompartments();

    // Disable the onboard accelerometer for cleaner power traces during
    // bench bring-up with Mojo. Has no effect on medication-adherence logic.
    {
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

    // Arm the ATTN wake: the Notecard raises ATTN (A0) to end each sleep.
    cxSleepBegin();
}

// ════════════════════════════════════════════════════════════════════════════
// loop()
//
// One poll cycle, then sleep in STOP2 until the Notecard raises ATTN
// poll_sec later. The host resumes here with all state intact.
// ════════════════════════════════════════════════════════════════════════════
void loop() {
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
                // note.add failed after all retries — keep this event in
                // the ring buffer so it can be replayed on the next wake with
                // its original context intact.
                enqueuePendingEvent(state, i, post_poll_mask, newly_opened);
            }
        }
    }

    state.prev_pin_mask = cur_mask;

    // ── Sleep until next poll (STOP2 until the Notecard raises ATTN) ──────
    sleepHost(state);
}
