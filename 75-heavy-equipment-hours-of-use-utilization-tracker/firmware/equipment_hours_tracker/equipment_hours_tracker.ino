/*******************************************************************************
 * equipment_hours_tracker.ino — Heavy Equipment Hours-of-Use & Utilization Tracker
 *
 * Hardware: Blues Notecarrier CX (Cygnet STM32L4) + Notecard for Skylo
 *           (NOTE-NBGLWX) + Adafruit LSM6DSOX accelerometer (I2C, #4517)
 *           Connectivity uses automatic WiFi→cellular→Skylo-satellite (NTN)
 *           fallback so assets stay reportable beyond terrestrial coverage
 *           (enabled in notecardConfigure() via card.transport "wifi-cell-ntn").
 *
 * On each 30-second wake the host: restores persisted state, fetches env vars,
 * samples accelerometer for 2 s at 104 Hz, classifies vibration as IDLE /
 * RUNNING / TRANSPORT using RMS + coefficient-of-variation (CV = σ/μ),
 * updates the engine-hour meter, emits state-change events immediately and
 * daily summaries on schedule, then sleeps via card.attn host power gating.
 *
 * Classifier rationale: diesel idle at 700 RPM → ~11.7 Hz periodic vibration
 * (low CV, ~0.10–0.25).  Road/transport shock → irregular spikes (high CV,
 * ~0.50–1.0+).  RMS gates on activity level; CV discriminates engine vs transport.
 *
 * Source layout:
 *   equipment_hours_tracker.ino      — setup / loop / orchestration (this file)
 *   equipment_hours_tracker_helpers.h — types, constants, externs, prototypes
 *   equipment_hours_tracker_helpers.cpp — global definitions + helper implementations
 *
 * Dependencies (install via Arduino Library Manager):
 *   Blues Wireless Notecard (note-arduino) — pin current stable release
 *   Adafruit LSM6DS + Adafruit Unified Sensor
 *
 * Board: Generic STM32L4 → Cygnet
 ******************************************************************************/

#include "equipment_hours_tracker_helpers.h"

// ═════════════════════════════════════════════════════════════════════════════
void setup() {
    Serial.begin(115200);
    const uint32_t kTout = 2500;
    for (const uint32_t t0 = millis(); !Serial && (millis() - t0 < kTout); ) {}

    Wire.begin();
    notecard.begin();
#ifndef NDEBUG
    notecard.setDebugOutputStream(Serial);
#endif

    // Restore persisted state; false on cold boot or first flash.
    // Zero-initialise the descriptor so NotePayloadFree is never called on
    // an uninitialised struct when the retrieve fails.
    NotePayloadDesc payload = {};
    bool restored = false;
    // Guard against the cold-boot I²C race described in the build spec: the
    // Notecard may not be ready to accept commands immediately after host
    // power-up.  A single immediate attempt that fails looks identical to a
    // genuine cold boot, which would zero g_s and permanently lose all
    // persisted hour totals and pending events.  Retry with increasing backoff
    // before concluding that no payload exists.
    for (int attempt = 0; attempt < 5 && !restored; attempt++) {
        if (attempt > 0) delay(250 * attempt);  // 250, 500, 750, 1000 ms
        restored = NotePayloadRetrieveAfterSleep(&payload);
    }
    if (restored) {
        restored &= NotePayloadGetSegment(&payload, SEG_ID, &g_s, sizeof(g_s));
        NotePayloadFree(&payload);  // release whether or not the segment parse succeeded
    }

    if (!restored || !g_s.configured) {
        memset(&g_s, 0, sizeof(g_s));
        // Only mark configured after every required request is confirmed by
        // the Notecard — a transient cold-boot I²C miss must not leave the
        // device permanently misconfigured on subsequent wakes.
        if (notecardConfigure() && defineTemplates()) {
            g_s.configured = true;
            Serial.println("[BOOT] Cold boot: Notecard configured");
        } else {
            Serial.println("[BOOT] Configuration failed — will retry on next wake");
            goToSleep();
            return;
        }
    }

    // Seed runtime globals from last-good env reads before issuing env.get so
    // that a transient miss leaves previously-applied tuning and fence intact.
    fetchEnvOverrides();
    applyGeofenceIfChanged();

    if (!sox.begin_I2C()) {
        Serial.println("[IMU] Not found — check SDA/SCL/VCC wiring");
        goToSleep();
        return;
    }
    sox.setAccelRange(LSM6DS_ACCEL_RANGE_2_G);   // ±2g best for chassis vibration
    sox.setAccelDataRate(LSM6DS_RATE_104_HZ);     // 104 Hz captures engine harmonics
    sox.setGyroDataRate(LSM6DS_RATE_SHUTDOWN);    // gyro unused; shut down to save ~0.5 mA

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
        Serial.print("[EVENT] retrying "); Serial.println(g_s.evq[g_s.evq_head].tag);
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

// If ATTN is not gating host power, loop() is the fallback
void loop() {
    delay(SAMPLE_INTERVAL_SEC * 1000UL);
    setup();
}
