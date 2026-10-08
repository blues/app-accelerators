/*******************************************************************************
 * equipment_hours_tracker.ino — Heavy Equipment Hours-of-Use & Utilization Tracker
 *
 * Hardware: Blues Notecarrier CX (onboard STM32L433 host) + Notecard for Skylo
 *           (NOTE-NBGLWX) + Adafruit LSM6DSOX accelerometer (I2C, #4517)
 *           Connectivity uses automatic WiFi→cellular→Skylo-satellite (NTN)
 *           fallback so assets stay reportable beyond terrestrial coverage
 *           (enabled in notecardConfigure() via card.transport "wifi-cell-ntn").
 *
 * On each 30-second wake the host: fetches env vars, samples the accelerometer
 * for 2 s at 104 Hz, classifies vibration as IDLE / RUNNING / TRANSPORT using
 * RMS + coefficient-of-variation (CV = σ/μ), updates the engine-hour meter,
 * emits state-change events immediately and daily summaries on schedule, then
 * sleeps in STM32 STOP2 until the Notecard's ATTN pin wakes it (cx_sleep.h).
 * Execution resumes in place with RAM intact, so application state simply
 * lives in the AppState struct — nothing is persisted to the Notecard.
 *
 * Classifier rationale: diesel idle at 700 RPM → ~11.7 Hz periodic vibration
 * (low CV, ~0.10–0.25).  Road/transport shock → irregular spikes (high CV,
 * ~0.50–1.0+).  RMS gates on activity level; CV discriminates engine vs transport.
 *
 * Wiring for sleep: jumper the Notecarrier CX ATTN pin to D5 (both on the same
 * 16-pin header).  Leave EN unconnected — on the CX it enables the shared 3.3 V
 * VIO rail, so ATTN→EN browns out the board instead of sleeping the host.
 *
 * Build: Tools > USB support (if available) > None (usb=none).  With the USB
 * CDC stack enabled and no USB host attached, the USB wakeup interrupt exits
 * STOP2 immediately.  Debug output goes to the LPUART on the CX debug jack,
 * which an ST-LINK V3 exposes as a virtual COM port (debugSerial below).
 *
 * Source layout:
 *   equipment_hours_tracker.ino      — setup / loop / orchestration (this file)
 *   equipment_hours_tracker_helpers.h — types, constants, externs, prototypes
 *   equipment_hours_tracker_helpers.cpp — global definitions + helper implementations
 *   cx_sleep.h                        — STOP2 entry + ATTN wake for the Notecarrier CX
 *
 * Dependencies (install via Arduino Library Manager):
 *   Blues Wireless Notecard (note-arduino) — pin current stable release
 *   Adafruit LSM6DS + Adafruit Unified Sensor
 *   STM32duino Low Power (+ its dependency STM32duino RTC)
 *
 * Board: Blues boards → Cygnet (pnum=CYGNET), USB support None
 ******************************************************************************/

#include "equipment_hours_tracker_helpers.h"
#include "cx_sleep.h"

// Debug output: LPUART1 on the CX debug jack (ST-LINK virtual COM port).
// USB CDC is disabled for STOP2, so Serial is not available.
Uart debugSerial(PIN_VCP_RX, PIN_VCP_TX);

// True until the LSM6DSOX has been initialised; retried on each wake so a
// probe that is plugged in late still comes up without a power cycle.
static bool g_imu_ok = false;

// Bring up the LSM6DSOX.  Returns false when the IMU does not answer on I²C.
static bool initImu(void) {
    if (!sox.begin_I2C()) {
        debugSerial.println("[IMU] Not found — check SDA/SCL/VCC wiring");
        return false;
    }
    sox.setAccelRange(LSM6DS_ACCEL_RANGE_2_G);   // ±2g best for chassis vibration
    sox.setAccelDataRate(LSM6DS_RATE_104_HZ);     // 104 Hz captures engine harmonics
    sox.setGyroDataRate(LSM6DS_RATE_SHUTDOWN);    // gyro unused; shut down to save ~0.5 mA
    return true;
}

// ═════════════════════════════════════════════════════════════════════════════
// setup() — runs once at power-up.  The host resumes in place after each STOP2
// sleep, so one-time Notecard and sensor configuration lives here and every
// per-wake step lives in loop().
// ═════════════════════════════════════════════════════════════════════════════
void setup() {
    debugSerial.begin(115200);

    Wire.begin();
    notecard.begin();
#ifndef NDEBUG
    notecard.setDebugOutputStream(debugSerial);
#endif

    memset(&g_s, 0, sizeof(g_s));

    // Only mark configured after every required request is confirmed by the
    // Notecard; loop() retries until it succeeds.
    if (notecardConfigure() && defineTemplates()) {
        g_s.configured = true;
        debugSerial.println("[BOOT] Notecard configured");
    } else {
        debugSerial.println("[BOOT] Configuration failed — will retry on next wake");
    }

    g_imu_ok = initImu();

    // Arm the ATTN wake: the Notecard raises ATTN (D5) to end each sleep.
    cxSleepBegin();
}

// Sleep in STOP2 until the Notecard raises ATTN SAMPLE_INTERVAL_SEC from now.
static void goToSleep(void) {
    if (!cxSleepUntilAttn(notecard, SAMPLE_INTERVAL_SEC, NULL, &debugSerial)) {
        // The Notecard didn't take the sleep request, or ATTN never went low
        // (check the ATTN -> D5 jumper).  Keep the sample cadence and retry.
        debugSerial.println("[SLEEP] ATTN sleep failed — waiting out the interval awake");
        delay(SAMPLE_INTERVAL_SEC * 1000UL);
    }
}

// ═════════════════════════════════════════════════════════════════════════════
// loop() — one sample/wake cycle, then sleep until the Notecard raises ATTN.
// ═════════════════════════════════════════════════════════════════════════════
void loop() {
    // Retry one-time configuration if it did not complete at power-up.
    if (!g_s.configured) {
        if (notecardConfigure() && defineTemplates()) {
            g_s.configured = true;
            debugSerial.println("[BOOT] Notecard configured");
        } else {
            debugSerial.println("[BOOT] Configuration failed — will retry on next wake");
            goToSleep();
            return;
        }
    }

    // Seed runtime globals from last-good env reads before issuing env.get so
    // that a transient miss leaves previously-applied tuning and fence intact.
    fetchEnvOverrides();
    applyGeofenceIfChanged();

    if (!g_imu_ok) {
        g_imu_ok = initImu();
        if (!g_imu_ok) {
            goToSleep();
            return;
        }
    }

    // Fetch the epoch before classification so that updateHourAccumulator() can
    // compute the actual elapsed wall time between wakes instead of crediting a
    // fixed nominal interval (which systematically undercounts active time).
    uint32_t now = getEpoch();

    EquipState new_state = classifyVibration();
    updateHourAccumulator(now);  // credit actual elapsed time to prev_state before updating it

    // ── Event delivery — drain backlog, then handle any new transition ────────
    //
    // Drain one backlogged event per wake (oldest first).  New transitions
    // added later in this wake are enqueued after existing entries, preserving
    // FIFO ordering across retries.
    if (g_s.evq_count > 0) {
        debugSerial.print("[EVENT] retrying "); debugSerial.println(g_s.evq[g_s.evq_head].tag);
        sendNextPendingEvent();
    }

    if (new_state != g_s.prev_state) {
        const char *tag;
        if      (new_state == ST_RUNNING)   tag = "engine_start";
        else if (new_state == ST_TRANSPORT) tag = "transport_start";
        else  /* ST_IDLE */                 tag = (g_s.prev_state == ST_RUNNING)
                                                  ? "engine_stop" : "transport_stop";

        // Compute session_min at classification time so retrying on a later wake
        // replays the original value, not a stale recomputation.
        float session_min     = 0.0f;
        bool  session_closing = (new_state != ST_RUNNING &&
                                 g_s.run_session_start > 0 &&
                                 now > g_s.run_session_start);
        if (session_closing)
            session_min = (float)(now - g_s.run_session_start) / 60.0f;

        // Anchor a new run-session start when entering RUNNING.  Only when the
        // epoch is valid — a zero epoch on the very first run session after
        // power-on is harmless because session_closing will be false and
        // session_min stays 0.
        if (new_state == ST_RUNNING && now > 0)
            g_s.run_session_start = now;

        // ── Decouple classified state from event-delivery state ───────────────
        // Advance prev_state immediately so updateHourAccumulator() on the next
        // wake always credits the correct bucket — independent of whether the
        // note.add is acknowledged this wake.  The ring-buffer record carries
        // the event payload until the Notecard confirms delivery, eliminating
        // both skewed hour totals and duplicate transition events on retry.
        g_s.prev_state = new_state;

        // Enqueue the transition record.  Do NOT clear run_session_start until
        // the record is safely in the ring buffer: if the buffer is full the
        // event is dropped, but preserving run_session_start lets the next
        // session-close event recompute an approximate duration rather than
        // silently discarding the session entirely.  prev_state has already
        // advanced so the hour accumulator is unaffected by delivery outcome.
        if (enqueueEvent(tag, now, session_min, g_s.run_h_total)) {
            // Record is in the ring buffer; now safe to close the session window
            // (the duration is already captured in the pending record).
            if (session_closing) g_s.run_session_start = 0;
            sendNextPendingEvent();
        } else {
            // Ring buffer full — enqueueEvent() already logged a warning.
            // Increment the overflow counter so the fault is surfaced in the
            // next summary note sent to Notehub.
            g_s.evq_overflow_count++;
        }
    }

    bool want_summary =
        (g_s.last_summary_epoch == 0) ||
        (now > 0 && (now - g_s.last_summary_epoch) >= g_summary_interval_min * 60u);
    if (want_summary && now > 0) {
        // Only advance the summary window after the Notecard confirms the note
        // was queued — an unconfirmed send retries on the next wake.
        if (sendSummary()) {
            g_s.run_h_today        = 0.0f;
            g_s.transport_h_today  = 0.0f;
            g_s.last_summary_epoch = now;
            g_s.evq_overflow_count = 0;   // reset after fault count is reported in cloud
        }
    }

    goToSleep();
}
