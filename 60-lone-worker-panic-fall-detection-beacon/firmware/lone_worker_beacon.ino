/*
 * lone_worker_beacon.ino — Lone Worker Panic & Fall Detection Safety Beacon
 *
 * Detects falls (two-stage: free-fall then impact), monitors a panic button
 * with 30 ms debounce and hold-to-confirm, drives a haptic motor for
 * acknowledgment, and transmits alerts via Notecard — falling back to Starnote
 * satellite when cellular coverage is absent.
 *
 * ── Two-note alert flow ───────────────────────────────────────────────────
 * On fall or panic:
 *   (1) beacon_alert.qo is queued immediately (sync:true) with the Notecard's
 *       cached location and loc_age_s freshness metadata — no GPS wait before
 *       the alert transmits. An event_id (monotonic counter) is written to
 *       the note body.
 *   (2) A background GPS search (beginGpsSearch / pollGpsSearch) runs without
 *       blocking the detection loop. If a fresh fix arrives within the timeout
 *       window, a beacon_location.qo follow-up note is queued (sync:true) with
 *       the event-time coordinates and the same event_id as the initial alert,
 *       so downstream systems can correlate the two notes by (device, event_id).
 *       beginGpsSearch() is called only after beacon_alert.qo is confirmed
 *       queued, so no follow-up search can be orphaned by a failed note.add.
 *       Only one GPS enrichment window is active at a time; a second alert
 *       that fires during an ongoing search receives its cached location only
 *       — no beacon_location.qo follow-up is queued for it.
 * See README Section 6 (GPS Design) for the full flow description.
 *
 * ── Power note ───────────────────────────────────────────────────────────
 * This POC keeps the host MCU awake continuously. Fall detection is sampled
 * at ~100 Hz via a fast inner loop (ACCEL_SAMPLES_PER_LOOP × ACCEL_SAMPLE_MS)
 * to reliably catch free-fall phases as short as DEFAULT_FREEFALL_MIN_MS
 * (80 ms). Notecard I/O and state checks run at the outer ~10 Hz cadence.
 * A production build should use STM32L433 STOP2 mode with LIS3DH INT1 and
 * button GPIO wakeup. See README Section 9 (Limitations).
 *
 * ── File layout ──────────────────────────────────────────────────────────
 * lone_worker_beacon.ino        — constants, globals, setup(), loop()
 * lone_worker_beacon_helpers.h  — externs, prototypes, env-var clamp ranges
 * lone_worker_beacon_helpers.cpp — all helper function implementations
 *
 * Notefiles:
 *   beacon_alert.qo    — fall or panic event (sync:true)
 *   beacon_location.qo — fresh GPS fix acquired after alert (sync:true)
 *
 * Hardware:
 *   Notecarrier CX (Cygnet STM32L433 host)
 *   Notecard Cell+WiFi (MBGLW)               — primary cellular/WiFi transport
 *   Starnote for Skylo                        — satellite failover
 *   SparkFun LIS3DH breakout (SEN-13963)     — 3-axis accelerometer
 *   Adafruit DRV2605L breakout (#2305)       — haptic motor driver
 *   Adafruit Vibrating Mini Motor Disc (#1201) — haptic actuator
 *   Panic button (momentary NO, active-low)
 *
 * Dependencies:
 *   Blues Wireless Notecard (note-arduino) v1.8.5
 *   SparkFun LIS3DH Arduino Library        v1.0.3+
 *   Adafruit DRV2605 Library               v1.2.3+
 *   Adafruit BusIO (DRV2605 dependency)    v1.15+
 */

#include <Wire.h>
#include "lone_worker_beacon_helpers.h"   // constants, externs, prototypes

// PRODUCT_UID is defined (with a fallback guard) in lone_worker_beacon_helpers.h
// so it is visible to both this .ino TU and lone_worker_beacon_helpers.cpp.
// Do not redefine it here — duplicate macro definitions cause compiler warnings
// and could silently shadow the real UID in the helpers translation unit.

// ── Objects ───────────────────────────────────────────────────────────────
Notecard          notecard;
LIS3DH            accel(I2C_MODE, 0x18);  // SDO pulled low → address 0x18
Adafruit_DRV2605  haptic;

// ── Runtime config (shadows env vars; refreshed every ENV_FETCH_INTERVAL_MS)
float    g_freefallG     = DEFAULT_FREEFALL_G;
float    g_impactG       = DEFAULT_IMPACT_G;
uint32_t g_fallWindowMs  = DEFAULT_FALL_WINDOW_MS;
uint32_t g_freefallMinMs = DEFAULT_FREEFALL_MIN_MS;
uint32_t g_panicHoldMs   = DEFAULT_PANIC_HOLD_MS;
// Fixed-size buffer (WORKER_ID_MAX chars + null) avoids Arduino String heap
// churn and prevents an oversized env-var from bloating Starnote packets.
char     g_workerId[WORKER_ID_MAX + 1] = "worker-001";

// ── Application state ─────────────────────────────────────────────────────
bool     g_accelReady  = false;
bool     g_hapticReady = false;
bool     g_setupFault  = false;   // latched if hub.set or any template fails

// Fall-detection two-stage state machine
bool     g_inFreefall       = false;
uint32_t g_freefallStart    = 0;
bool     g_watchingImpact   = false;
uint32_t g_impactWindowStart = 0;   // start of impact window (wraparound-safe)

// Alert timing
uint32_t g_lastAlertMs     = 0;

// Fixed-depth alert retry queue. Each entry stores its own event-time context
// (type + GPS cache epoch + event_id) so a second alert that fails before the
// first retry succeeds is queued independently — preventing the earlier,
// safety-critical alert from being silently dropped under transient
// Notecard/I²C failures.
AlertQueueEntry g_alertQueue[ALERT_QUEUE_DEPTH];   // zero-initialized at boot
uint8_t         g_alertQueueHead  = 0;   // index of oldest live entry
uint8_t         g_alertQueueCount = 0;   // number of live entries

// Panic button debounce + hold-to-confirm state machine
bool     g_btnRaw       = false;
bool     g_btnStable    = false;
uint32_t g_btnChangeMs  = 0;
uint32_t g_btnPressedAt = 0;
bool     g_btnHeld      = false;
bool     g_panicFired   = false;

// Non-blocking GPS state machine
bool     g_gpsSearching   = false;
uint32_t g_gpsSearchStart = 0;       // search start ms (wraparound-safe)
uint32_t g_gpsCacheEpoch  = 0;       // epoch of cached fix at alert time
char     g_gpsAlertType[16] = {0};   // alert type that triggered this search
uint32_t g_gpsLastPollMs  = 0;       // throttle: last card.location poll
// Latched when a card.location.mode:off request fails; pollGpsSearch() retries
// the disable each loop pass until it succeeds.
bool     g_gpsDisablePending = false;

// Event correlation counters.
// g_alertEventId is incremented once per new alert dispatch (NOT per retry)
// and written to beacon_alert.qo. g_gpsEventId carries the same value into
// beacon_location.qo so downstream systems can correlate notes by (device, event_id).
// event_id is device-local; it is not globally unique on its own.
uint32_t g_alertEventId = 0;
uint32_t g_gpsEventId   = 0;

// Accelerometer runtime health
uint8_t  g_accelFailCount   = 0;     // consecutive implausible/zero reads
bool     g_accelFaultLatched = false; // true after ACCEL_REINIT_MAX failed reinits

// Haptic state machine (non-blocking multi-pulse)
uint8_t  g_hapticEffect      = 0;
uint8_t  g_hapticPulsesLeft  = 0;
uint32_t g_hapticLastPulseMs = 0;
bool     g_hapticFirstPulse  = false;

// Environment-variable refresh timer. Set in setup() after the initial
// fetchEnvVars() call so loop() does not re-fetch on the first pass.
uint32_t g_lastEnvFetchMs = 0;

// Runtime accelerometer-fault buzz timer. loop() fires a single buzz every
// ACCEL_FAULT_BUZZ_INTERVAL_MS when g_accelFaultLatched is true post-boot.
// Boot-time accel failure halts in setup() and never reaches loop().
uint32_t g_accelFaultBuzzMs = 0;

// ─── Setup ────────────────────────────────────────────────────────────────
void setup()
{
#ifdef DEBUG_SERIAL
    DEBUG_SERIAL.begin(115200);
    const uint32_t usb_timeout_ms = 3000;
    for (const uint32_t start = millis();
         !DEBUG_SERIAL && (millis() - start) < usb_timeout_ms; )
        ;
#endif

    Wire.begin();
    notecard.begin();
#ifdef DEBUG_SERIAL
    notecard.setDebugOutputStream(DEBUG_SERIAL);
#endif

    memset(g_gpsAlertType, 0, sizeof(g_gpsAlertType));
    pinMode(PANIC_BUTTON_PIN, INPUT_PULLUP);

    // Configure Notecard and register compact templates. Both must succeed for
    // the device to be considered healthy — template failure means notes may
    // fall back to JSON instead of compact binary, breaking Starnote transport.
    bool cfgOk = notecardConfigure();
    bool tplOk = defineTemplates();
    g_setupFault = (!cfgOk || !tplOk);
    if (g_setupFault)
        DEBUG_PRINTLN("[FAULT] Notecard configuration or template registration failed.");

    // Initial env var fetch; arm the 2-hour refresh timer so loop() does not
    // immediately re-fetch on its first pass.
    fetchEnvVars();
    g_lastEnvFetchMs = millis();

    g_accelReady  = initAccel();
    g_hapticReady = initHaptic();

    if (!g_accelReady) {
        // Boot-time accel failure is a hard fault. Fall detection is core
        // project scope; allowing the beacon to run as panic-only without any
        // visible indication risks a worker trusting it for fall coverage it
        // cannot provide. Latch g_setupFault so setup() halts with the same
        // slow double-buzz pattern used for hub.set / template failures.
        g_accelFaultLatched = true;
        g_setupFault        = true;
        DEBUG_PRINTLN("[FAULT] LIS3DH init failed — fall detection unavailable; "
                      "beacon halted. Check wiring and I2C address (0x18).");
    }
    if (!g_hapticReady)
        DEBUG_PRINTLN("[WARN] DRV2605L not found — haptic feedback disabled.");

    // Runtime PRODUCT_UID guard. The pragma message catches empty UIDs at
    // compile time, but a device programmed without a UID set should be
    // obvious at power-on without needing debug serial attached.
    if (strlen(PRODUCT_UID) == 0) {
        DEBUG_PRINTLN("[FAULT] PRODUCT_UID is empty — notes will not route to "
                      "a Notehub project.");
        g_setupFault = true;
    }

    // Haptic boot pattern. Called directly (blocking) here because the
    // pollHaptic() state machine has not started yet. Fault: slow double-buzz
    // that is clearly distinct from the single-ramp startup pulse, so a
    // misconfigured device is obvious to whoever powers it on.
    if (g_hapticReady) {
        if (g_setupFault) {
            for (int rep = 0; rep < 2; rep++) {
                haptic.setWaveform(0, HAPTIC_BUZZ); haptic.setWaveform(1, 0);
                haptic.go(); delay(300);
                haptic.setWaveform(0, HAPTIC_BUZZ); haptic.setWaveform(1, 0);
                haptic.go(); delay(500);
            }
        } else {
            haptic.setWaveform(0, HAPTIC_STARTUP);
            haptic.setWaveform(1, 0);
            haptic.go();
        }
    }

    // A safety device must not arm alerting when deployment-critical
    // configuration has failed. A missing PRODUCT_UID, failed hub.set, or
    // failed template registration means notes may never route and satellite
    // compact transport may be broken. Block here rather than entering loop()
    // and presenting a false sense of protection. The recurring haptic pattern
    // makes an unconfigured device unmistakably obvious to the deployer.
    if (g_setupFault) {
        DEBUG_PRINTLN("[FAULT] Unrecoverable startup fault — beacon is halted. "
                      "Verify PRODUCT_UID, Notecard connectivity, and template "
                      "registration before deploying.");
        for (;;) {
            if (g_hapticReady) {
                haptic.setWaveform(0, HAPTIC_BUZZ); haptic.setWaveform(1, 0);
                haptic.go(); delay(300);
                haptic.setWaveform(0, HAPTIC_BUZZ); haptic.setWaveform(1, 0);
                haptic.go(); delay(300);
            }
            delay(4400);   // 5 s fault cycle: 600 ms of buzzes + 4400 ms quiet
        }
    }

    DEBUG_PRINTLN("[APP] Lone Worker Beacon active.");
}

// ─── Main Loop  (outer ~10 Hz for Notecard I/O; accel sampled at ~100 Hz) ──
void loop()
{
    uint32_t now = millis();

    pollHaptic();   // advance non-blocking haptic sequencer

    // Sample the accelerometer at ~100 Hz with a fast inner loop
    // (ACCEL_SAMPLES_PER_LOOP iterations, ACCEL_SAMPLE_MS apart). At the
    // default 100 Hz ODR the LIS3DH produces a new sample every 10 ms;
    // reading at that rate ensures that a free-fall phase as short as
    // DEFAULT_FREEFALL_MIN_MS (80 ms) is always observed. The inner-loop
    // delays also pace the outer loop to ~10 Hz without a separate delay().
    bool fell = false;
    for (uint8_t s = 0; s < ACCEL_SAMPLES_PER_LOOP; s++) {
        bool f = g_accelReady ? pollFallDetection() : false;
        if (f) { fell = true; break; }
        if (s < ACCEL_SAMPLES_PER_LOOP - 1) delay(ACCEL_SAMPLE_MS);
    }

    bool panicked = checkPanicButton();

    // Advance GPS fix search; never blocks the loop.
    pollGpsSearch();

    // Advance the non-blocking alert retry state machine before processing new
    // events so that a queued retry fires as early as possible and
    // g_lastAlertMs is updated before any new cooldown calculation.
    pollAlertRetry();

    // Re-capture `now` AFTER pollAlertRetry. A successful retry sets
    // g_lastAlertMs from its own millis() snapshot, which is later than the
    // `now` captured at the top of loop(). Computing (loop_now - g_lastAlertMs)
    // in uint32_t would underflow to a huge positive value, falsely signalling
    // an expired cooldown — bypassing the alert-storm protection on the same
    // loop pass as a successful retry.
    now = millis();

    if (fell || panicked) {
        uint32_t sinceLastSec = (now - g_lastAlertMs) / 1000UL;
        bool cooldownExpired  = (g_lastAlertMs == 0 ||
                                 sinceLastSec >= DEFAULT_ALERT_COOLDOWN_SEC);

        // Both fall and panic are gated by the 60-second cooldown window.
        // In-cooldown events are silently suppressed — no note, no GPS search —
        // matching the documented on-wire contract (README §7 "Alert triggers").
        // If both fire on the same loop pass, panic takes precedence as the
        // explicit worker signal.
        if (cooldownExpired) {
            const char *alertType;
            if (panicked) {
                alertType = "panic";
                // Haptic fires AFTER note.add outcome is known — see below.
            } else {
                alertType = "fall";
                triggerHaptic(HAPTIC_BUZZ, 2);   // "fall detected" indication
            }

            // Capture the pre-alert cached-fix epoch. This query always runs
            // so each alert gets its own independent epoch snapshot — a second
            // alert that fires during an active GPS search reads from the
            // current cache state, not from the first alert's stale snapshot.
            // g_gpsCacheEpoch (the freshness baseline used by pollGpsSearch()
            // to distinguish a new fix from the pre-alert cache) is only
            // overwritten when no GPS search is in flight — updating it mid-
            // search would corrupt the epoch comparison and risk misidentifying
            // a cached fix as a fresh acquisition.
            uint32_t thisCacheEpoch = 0;
            {
                J *_loc = notecard.requestAndResponse(
                              notecard.newRequest("card.location"));
                if (_loc && !notecard.responseError(_loc))
                    thisCacheEpoch = (uint32_t)JGetNumber(_loc, "time");
                if (_loc) notecard.deleteResponse(_loc);
            }
            if (!g_gpsSearching)
                g_gpsCacheEpoch = thisCacheEpoch;

            // Compute loc_age_s once at event-fire time so the value is
            // identical on every send attempt (first try or any retry).
            // Storing the pre-computed age in thisLocAgeS — rather than
            // recomputing it inside sendAlert() via a fresh card.time call —
            // prevents the reported fix age from growing with retry delay.
            float thisLocAgeS = -1.0f;
            if (thisCacheEpoch > 0) {
                J *tRsp = notecard.requestAndResponse(
                              notecard.newRequest("card.time"));
                if (tRsp && !notecard.responseError(tRsp)) {
                    uint32_t nowEpoch = (uint32_t)JGetNumber(tRsp, "time");
                    thisLocAgeS = (nowEpoch >= thisCacheEpoch)
                                  ? (float)(nowEpoch - thisCacheEpoch) : -1.0f;
                }
                if (tRsp) notecard.deleteResponse(tRsp);
            }

            // Assign a new event ID for this alert. The same value travels
            // into beacon_location.qo so downstream systems can correlate the
            // two notes using (device, event_id) as the join key.
            uint32_t thisEventId = ++g_alertEventId;

            bool sent = sendAlert(alertType, thisEventId, thisLocAgeS);
            if (sent) {
                g_lastAlertMs = now;
                // Panic triple-buzz fires once the alert is accepted for
                // transmission — here after a successful note.add. When
                // note.add fails the buzz still fires (see else branch below)
                // because the alert is in the firmware retry queue and will
                // be delivered as soon as the Notecard is reachable. In both
                // cases the worker receives "alert accepted" feedback, not
                // "note.add succeeded specifically". A suppressed panic (in-
                // cooldown) gets a single buzz below to distinguish the two.
                if (panicked) triggerHaptic(HAPTIC_BUZZ, 3);
                // If a GPS search is already active, beginGpsSearch() returns
                // immediately — this alert receives its cached location only;
                // no beacon_location.qo follow-up is queued for it.
                beginGpsSearch(alertType, thisEventId);
            } else {
                // sendAlert() makes one attempt. Enqueue for non-blocking
                // retry, preserving the original event-time locAgeS and
                // event_id so every retry reports the same fix age and the
                // same correlation key as this first attempt.
                enqueueAlert(alertType, thisCacheEpoch, thisEventId, thisLocAgeS);
                // Triple-buzz fires even on local-queue acceptance: the alert
                // is held in the firmware retry queue and transmitted as soon
                // as the Notecard is reachable — "accepted for transmission"
                // is the correct operator signal here, not "Notecard ACK".
                if (panicked) triggerHaptic(HAPTIC_BUZZ, 3);
            }
        } else if (panicked) {
            // Panic hold recognised but suppressed by the 60-second cooldown.
            // Single buzz (distinct from the triple-buzz "alert queued" pattern)
            // so the worker knows the hold was registered but nothing was sent.
            triggerHaptic(HAPTIC_BUZZ, 1);
        }
    }

    // ── Runtime accelerometer-fault indication ────────────────────────────
    // Only reached when g_accelFaultLatched was set post-boot (consecutive bad
    // reads). Boot-time accel failure halts in setup(). A single buzz every
    // ACCEL_FAULT_BUZZ_INTERVAL_MS signals degraded panic-only operation.
    if (g_accelFaultLatched) {
        if (now - g_accelFaultBuzzMs >= ACCEL_FAULT_BUZZ_INTERVAL_MS) {
            g_accelFaultBuzzMs = now;
            triggerHaptic(HAPTIC_BUZZ, 1);
            DEBUG_PRINTLN("[FAULT] Accel fault active — fall detection offline; "
                          "beacon in panic-only mode.");
        }
    }

    // ── Periodic env-var refresh ──────────────────────────────────────────
    // Pull updated thresholds / worker_id from the Notecard's local env cache
    // every 2 hours (ENV_FETCH_INTERVAL_MS). The Notecard's inbound: 120
    // setting in hub.set keeps its env cache refreshed from Notehub on the
    // same 2-hour cadence. New threshold values are applied only when this
    // poll runs — alert-triggered sync:true sessions flush outbound notes but
    // do not advance this schedule or cause env vars to take effect sooner.
    if ((now - g_lastEnvFetchMs) >= ENV_FETCH_INTERVAL_MS) {
        fetchEnvVars();
        g_lastEnvFetchMs = now;
    }
}
