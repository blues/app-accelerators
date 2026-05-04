/*
 * cellular_medication_adherence_pillbox_helpers.cpp
 *
 * Implementations of all sensor, Notecard, and state-management helpers for
 * the cellular medication adherence pillbox firmware.
 */

#include "cellular_medication_adherence_pillbox_helpers.h"

// ── Compartment pin assignments (Notecarrier CX dual 16-pin header) ──────────
// Compartments 0–6 (SUN–SAT) mapped to the seven available digital GPIO pins.
static const uint8_t kCompartmentPin[NUM_COMPARTMENTS] = {
    D5, D6, D9, D10, D11, D12, D13
};

// Human-readable label per compartment — written into each pill_open.qo body.
static const char * const kDayLabel[NUM_COMPARTMENTS] = {
    "SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT"
};

// ════════════════════════════════════════════════════════════════════════════
// setupPins — configure compartment GPIO with internal pull-ups
//
// Micro-switch NO terminal is connected to GND; the lid depresses the actuator
// when closed, connecting COM to NO and pulling the pin LOW. The internal
// pull-up holds the pin HIGH when the actuator is released (lid open).
// Call once per wake before the first sampleCompartments().
// ════════════════════════════════════════════════════════════════════════════
void setupPins() {
    for (uint8_t i = 0; i < NUM_COMPARTMENTS; i++) {
        pinMode(kCompartmentPin[i], INPUT_PULLUP);
    }
}

// ════════════════════════════════════════════════════════════════════════════
// initNotecard — configure Notecard hub association and sync cadence
//
// Called on every wake so a transient failure on any prior wake cannot leave
// the device permanently unassociated with Notehub or on a stale sync cadence.
// hub.set is idempotent: re-applying identical parameters has no side-effect.
// sendRequestWithRetry() papers over the cold-boot I2C race where the host
// MCU comes up before the Notecard is ready.
// ════════════════════════════════════════════════════════════════════════════
void initNotecard(const char *product_uid, uint32_t outbound_min,
                  uint16_t inbound_min) {
    J *req = notecard.newRequest("hub.set");
    if (!req) return;
    if (product_uid && product_uid[0]) {
        JAddStringToObject(req, "product", product_uid);
    }
    JAddStringToObject(req, "mode",     "periodic");
    JAddNumberToObject(req, "outbound", (int)outbound_min);
    JAddNumberToObject(req, "inbound",  (int)inbound_min);
    if (!notecard.sendRequestWithRetry(req, 5)) {
#ifdef usbSerial
        usbSerial.println("[init] hub.set failed — will retry next wake");
#endif
    }
}

// ════════════════════════════════════════════════════════════════════════════
// defineTemplates — register pill_summary.qo template for compact wire format
//
// Called on every wake alongside initNotecard(). note.template is idempotent:
// re-applying an identical template definition is a no-op on the Notecard.
// This ensures the template is registered even if a prior attempt failed.
// ════════════════════════════════════════════════════════════════════════════
void defineTemplates() {
    // pill_summary.qo fires once per day. A template stores each Note as a
    // fixed-length binary record rather than free-form JSON, reducing wire
    // size. TUINT8 (= 21) declares a 1-byte unsigned integer field.
    J *req = notecard.newRequest("note.template");
    if (!req) return;
    JAddStringToObject(req, "file", NOTEFILE_SUMMARY);
    JAddNumberToObject(req, "port", 50);
    J *body = JAddObjectToObject(req, "body");
    JAddNumberToObject(body, "opens_mask",  TUINT8); // 7-bit bitmask (bits 0–6)
    JAddNumberToObject(body, "opens_count", TUINT8); // 0–7 integer count
    J *rsp = notecard.requestAndResponse(req);
    if (rsp) {
        if (notecard.responseError(rsp)) {
#ifdef usbSerial
            usbSerial.println("[init] note.template failed — will retry next wake");
#endif
        }
        notecard.deleteResponse(rsp);
    }

    // pill_open.qo is left untemplated. Open events are low-frequency in
    // practice — at most one to a few per compartment per day in normal use,
    // though the firmware records every detected open regardless of count — so
    // the bandwidth overhead of free-form JSON is negligible, and avoiding a
    // template sidesteps template management overhead.
}

// ════════════════════════════════════════════════════════════════════════════
// sampleCompartments — read all 7 pins; return debounced bitmask
//   bit n = 1 → compartment n lid is OPEN  (pin HIGH, switch actuator released)
//   bit n = 0 → compartment n lid is CLOSED (pin LOW, switch actuator pressed)
//
// Three reads are taken ~3 ms apart and combined with a majority vote (bit
// set only when HIGH in ≥2 of 3 samples). This suppresses single-sample
// glitches from switches near their actuation threshold.
// ════════════════════════════════════════════════════════════════════════════
uint8_t sampleCompartments() {
    uint8_t votes[3] = {0, 0, 0};
    for (uint8_t r = 0; r < 3; r++) {
        for (uint8_t i = 0; i < NUM_COMPARTMENTS; i++) {
            if (digitalRead(kCompartmentPin[i]) == HIGH) {
                votes[r] |= (1u << i);
            }
        }
        if (r < 2) delay(3); // brief settling time between reads
    }
    // Majority vote: bit set only when HIGH in ≥2 of 3 reads.
    return (votes[0] & votes[1]) | (votes[1] & votes[2]) | (votes[0] & votes[2]);
}

// ════════════════════════════════════════════════════════════════════════════
// fetchEnvOverrides — pull latest environment variables from the Notecard
//
// Notecard environment-variable values are always text. Each variable is
// fetched as a string and parsed with strtoul(). A value is applied only when
// at least one digit was consumed and it falls within its valid range.
// hub.set re-application for cadence changes is committed to state only if
// the Notecard acknowledges, so a failed send is retried on the next wake.
// ════════════════════════════════════════════════════════════════════════════
void fetchEnvOverrides(PillboxState &s) {
    J *req = notecard.newRequest("env.get");
    if (!req) return;
    J *names = JAddArrayToObject(req, "names");
    JAddItemToArray(names, JCreateString("poll_interval_sec"));
    JAddItemToArray(names, JCreateString("summary_hour_utc"));
    JAddItemToArray(names, JCreateString("outbound_min"));
    JAddItemToArray(names, JCreateString("inbound_min"));

    J *rsp = notecard.requestAndResponse(req);
    if (!rsp) return;
    if (notecard.responseError(rsp)) {
        notecard.deleteResponse(rsp);
        return;
    }

    J *body = JGetObject(rsp, "body");
    if (body) {
        const char   *v;
        char         *end;
        unsigned long uv;

        v = JGetString(body, "poll_interval_sec");
        if (v && v[0]) {
            uv = strtoul(v, &end, 10);
            if (end != v && uv >= 15 && uv <= 3600) {
                s.poll_sec = (uint32_t)uv;
            }
        }

        v = JGetString(body, "summary_hour_utc");
        if (v && v[0]) {
            uv = strtoul(v, &end, 10);
            if (end != v && uv <= 23) {
                s.summary_hour = (uint8_t)uv;
            }
        }

        // ── Cadence overrides ─────────────────────────────────────────────
        // Parse both variables first so a single consolidated hub.set can be
        // sent. State is committed only after the Notecard acknowledges without
        // error; transport-level success alone is not sufficient.
        uint16_t new_out     = s.outbound_min;
        uint16_t new_in      = s.inbound_min;
        bool cadence_changed = false;

        v = JGetString(body, "outbound_min");
        if (v && v[0]) {
            uv = strtoul(v, &end, 10);
            if (end != v && uv >= 15 && uv <= 1440) {
                uint16_t candidate = (uint16_t)uv;
                if (candidate != s.outbound_min) {
                    new_out         = candidate;
                    cadence_changed = true;
                }
            }
        }

        v = JGetString(body, "inbound_min");
        if (v && v[0]) {
            uv = strtoul(v, &end, 10);
            if (end != v && uv >= 15 && uv <= 1440) {
                uint16_t candidate = (uint16_t)uv;
                if (candidate != s.inbound_min) {
                    new_in          = candidate;
                    cadence_changed = true;
                }
            }
        }

        if (cadence_changed) {
            J *hreq = notecard.newRequest("hub.set");
            if (hreq) {
                JAddStringToObject(hreq, "mode",     "periodic");
                JAddNumberToObject(hreq, "outbound", (int)new_out);
                JAddNumberToObject(hreq, "inbound",  (int)new_in);
                J *hrsp = notecard.requestAndResponse(hreq);
                if (hrsp) {
                    if (!notecard.responseError(hrsp)) {
                        s.outbound_min = new_out;
                        s.inbound_min  = new_in;
#ifdef usbSerial
                        usbSerial.print("[env] outbound_min=");
                        usbSerial.print(s.outbound_min);
                        usbSerial.print(" inbound_min=");
                        usbSerial.println(s.inbound_min);
#endif
                    } else {
#ifdef usbSerial
                        usbSerial.println("[env] hub.set failed — cadence unchanged");
#endif
                    }
                    notecard.deleteResponse(hrsp);
                }
            }
        }
    }
    notecard.deleteResponse(rsp);
}

// ════════════════════════════════════════════════════════════════════════════
// emitOpenEvent — send an immediate Note when a compartment lid is opened
//
// sync:true bypasses the outbound timer; the radio wakes within ~15–60 s.
//
// day_mask  — cumulative bitmask of all compartments opened today (including
//             this one); identical across all events emitted in the same wake.
// poll_mask — bitmask of compartments detected open in this specific wake.
//             More than one bit set indicates a potential refill event. The
//             original poll_mask is preserved in the ring buffer and replayed
//             as-is so the refill-detection heuristic works on retried events.
//
// Returns true on confirmed enqueue; false after all retry attempts.
// On failure the caller should call enqueuePendingEvent() to preserve the
// event for replay on the next wake.
// ════════════════════════════════════════════════════════════════════════════
bool emitOpenEvent(uint8_t idx, uint8_t day_mask, uint8_t poll_mask) {
    const uint8_t MAX_ATTEMPTS = 3;
    for (uint8_t attempt = 0; attempt < MAX_ATTEMPTS; attempt++) {
        J *req = notecard.newRequest("note.add");
        if (!req) {
#ifdef usbSerial
            usbSerial.println("[open] note.add alloc failed");
#endif
            delay(50);
            continue;
        }
        JAddStringToObject(req, "file", NOTEFILE_OPEN);
        JAddBoolToObject  (req, "sync", true);
        J *body = JAddObjectToObject(req, "body");
        JAddNumberToObject(body, "compartment",      (int)(idx + 1)); // 1–7
        JAddStringToObject(body, "label",            kDayLabel[idx]);
        JAddNumberToObject(body, "day_opens_mask",   (int)day_mask);
        JAddNumberToObject(body, "opened_this_poll", (int)poll_mask);

        J *rsp = notecard.requestAndResponse(req);
        if (!rsp) {
#ifdef usbSerial
            usbSerial.print("[open] no response, attempt ");
            usbSerial.println(attempt + 1);
#endif
            delay(50);
            continue;
        }
        bool ok = !notecard.responseError(rsp);
        notecard.deleteResponse(rsp);
        if (ok) {
#ifdef usbSerial
            usbSerial.print("[open] compartment=");
            usbSerial.print(idx + 1);
            usbSerial.print(" (");
            usbSerial.print(kDayLabel[idx]);
            usbSerial.print(") day_mask=0b");
            usbSerial.print(day_mask, BIN);
            usbSerial.print(" poll_mask=0b");
            usbSerial.println(poll_mask, BIN);
#endif
            return true;
        }
#ifdef usbSerial
        usbSerial.print("[open] note.add error, attempt ");
        usbSerial.println(attempt + 1);
#endif
        delay(50);
    }
#ifdef usbSerial
    usbSerial.print("[open] note.add failed after all retries: compartment=");
    usbSerial.println(idx + 1);
#endif
    return false;
}

// ════════════════════════════════════════════════════════════════════════════
// emitPendingOverflowDiag — emit a pill_diag.qo when the pending retry queue
// overflows or fully drains after an overflow episode.
//
// overflow_count — number of events dropped since the last queue drain.
// drained        — true when called after the queue empties following an
//                  overflow episode; false when called at the moment the first
//                  overflow in an episode occurs.
//
// Uses sendRequest (fire-and-forget) intentionally: this is called from an
// error path where the Notecard may also be degraded. The Note will be
// delivered once connectivity is restored; blocking on a response would delay
// the already-faulted polling loop with no practical benefit.
// ════════════════════════════════════════════════════════════════════════════
static void emitPendingOverflowDiag(uint8_t overflow_count, bool drained) {
    J *req = notecard.newRequest("note.add");
    if (!req) return;
    JAddStringToObject(req, "file", NOTEFILE_DIAG);
    JAddBoolToObject(req, "sync", true);
    J *body = JAddObjectToObject(req, "body");
    JAddStringToObject(body, "error",
                       drained ? "pending_overflow_cleared" : "pending_overflow");
    JAddNumberToObject(body, "dropped", (int)overflow_count);
    notecard.sendRequest(req);
}

// ════════════════════════════════════════════════════════════════════════════
// enqueuePendingEvent — push a failed open event onto the retry ring buffer
//
// Stores the full per-event context (compartment, day_mask, poll_mask) so
// each record can be replayed and removed individually. When the queue is
// full the oldest entry is evicted and pending_overflow is incremented,
// making the data loss explicit rather than silent. Callers can monitor
// pending_overflow in diagnostics to detect sustained Notecard failures.
// ════════════════════════════════════════════════════════════════════════════
void enqueuePendingEvent(PillboxState &s, uint8_t idx,
                         uint8_t day_mask, uint8_t poll_mask) {
    if (s.pending_count >= PENDING_QUEUE_CAPACITY) {
        // Queue full: evict the oldest entry (shift left) to make room.
        bool first_overflow = (s.pending_overflow == 0);
        s.pending_overflow++;
        memmove(&s.pending_queue[0], &s.pending_queue[1],
                (PENDING_QUEUE_CAPACITY - 1) * sizeof(PendingOpenEvent));
        s.pending_count = PENDING_QUEUE_CAPACITY - 1;
#ifdef usbSerial
        usbSerial.print("[open] WARN: pending queue overflow — oldest event dropped."
                        " total_overflow=");
        usbSerial.println(s.pending_overflow);
#endif
        // Surface the data loss to Notehub on the first overflow in an
        // episode. Best-effort: the Notecard may also be degraded, but the
        // Note is queued in Notecard flash and will be delivered once
        // connectivity is restored.
        if (first_overflow) {
            emitPendingOverflowDiag(s.pending_overflow, false);
        }
    }
    s.pending_queue[s.pending_count].compartment = idx;
    s.pending_queue[s.pending_count].day_mask    = day_mask;
    s.pending_queue[s.pending_count].poll_mask   = poll_mask;
    s.pending_count++;
}

// ════════════════════════════════════════════════════════════════════════════
// replayPendingOpenEvents — retry queued pill_open.qo events from prior wakes
//
// Iterates the ring buffer in FIFO order. Each record carries its original
// per-event context, so day_mask and poll_mask accurately reflect the wake
// when the lid was actually detected. Successfully replayed records are
// removed; still-failing records are compacted to the front of the queue.
// pending_overflow is cleared once the queue fully drains.
// ════════════════════════════════════════════════════════════════════════════
void replayPendingOpenEvents(PillboxState &s) {
    if (s.pending_count == 0) return;
#ifdef usbSerial
    usbSerial.print("[open] replaying ");
    usbSerial.print(s.pending_count);
    usbSerial.println(" pending event(s)");
#endif
    uint8_t write_idx = 0;
    for (uint8_t i = 0; i < s.pending_count; i++) {
        // Copy the record before any compaction shift moves it.
        const PendingOpenEvent e = s.pending_queue[i];
        if (emitOpenEvent(e.compartment, e.day_mask, e.poll_mask)) {
            // Successfully replayed — advance i without advancing write_idx,
            // which effectively removes this entry from the queue.
        } else {
            // Still failing — compact to the front so the queue stays dense.
            if (write_idx != i) {
                s.pending_queue[write_idx] = e;
            }
            write_idx++;
        }
    }
    s.pending_count = write_idx;
    if (s.pending_count == 0) {
        if (s.pending_overflow > 0) {
            // Queue drained after an overflow episode — emit a cloud-visible
            // summary Note so the total drop count is recorded in Notehub
            // before the counter is cleared.
            emitPendingOverflowDiag(s.pending_overflow, true);
        }
        s.pending_overflow = 0; // queue fully drained; reset overflow counter
    }
}

// ════════════════════════════════════════════════════════════════════════════
// emitDailySummary — queue the end-of-day adherence summary
// full:true preserves opens_count=0 despite template omitempty, so a day
// where the patient missed all doses still produces a Note.
// Returns true on confirmed success; false on failure (caller retries).
// ════════════════════════════════════════════════════════════════════════════
bool emitDailySummary(uint8_t opens_mask) {
    uint8_t count = (uint8_t)__builtin_popcount(opens_mask);

    J *req = notecard.newRequest("note.add");
    if (!req) return false;
    JAddStringToObject(req, "file", NOTEFILE_SUMMARY);
    JAddBoolToObject  (req, "full", true);
    J *body = JAddObjectToObject(req, "body");
    JAddNumberToObject(body, "opens_mask",  (int)opens_mask);
    JAddNumberToObject(body, "opens_count", (int)count);
    J *rsp = notecard.requestAndResponse(req);
    if (!rsp) return false;

    bool ok = !notecard.responseError(rsp);
    notecard.deleteResponse(rsp);
    if (!ok) {
#ifdef usbSerial
        usbSerial.println("[summary] note.add failed — retrying next wake");
#endif
        return false;
    }
#ifdef usbSerial
    usbSerial.print("[summary] mask=0b");
    usbSerial.print(opens_mask, BIN);
    usbSerial.print(" count=");
    usbSerial.println(count);
#endif
    return true;
}

// ════════════════════════════════════════════════════════════════════════════
// utcDayAndHour — query the Notecard for current UTC time
// Returns the day ordinal (unix_sec / 86400); sets *hour_out to UTC hour.
// Returns 0 when the Notecard hasn't synced yet or the request fails.
// ════════════════════════════════════════════════════════════════════════════
uint32_t utcDayAndHour(uint32_t *hour_out) {
    J *req = notecard.newRequest("card.time");
    if (!req) return 0;
    J *rsp = notecard.requestAndResponse(req);
    if (!rsp) return 0;
    if (notecard.responseError(rsp)) {
        notecard.deleteResponse(rsp);
        return 0;
    }
    uint32_t t = (uint32_t)JGetNumber(rsp, "time");
    notecard.deleteResponse(rsp);
    if (t == 0) return 0;
    if (hour_out) *hour_out = (t % 86400UL) / 3600UL;
    return t / 86400UL;
}

// ════════════════════════════════════════════════════════════════════════════
// sleepHost — persist state to the Notecard and gate host power off
//
// NotePayloadSaveAndSleep serializes PillboxState into Notecard flash, then
// issues a card.attn sleep request. On a correctly wired Notecarrier CX the
// Notecard cuts the host 3.3V rail (ATTN -> EN) and re-powers it after
// poll_sec seconds — this function should never return in production.
//
// PILLBOX_BENCH_MODE (defined in helpers.h): if the ATTN pin is not wired to
// EN, sleepHost() falls back to delay() + NVIC_SystemReset() so bring-up can
// proceed without full power-gating hardware in place.
//
// Production (PILLBOX_BENCH_MODE undefined): a return from
// NotePayloadSaveAndSleep() is an ATTN->EN wiring fault. The firmware logs
// the condition, emits a pill_diag.qo Note so the fault is visible in
// Notehub, then halts. Halting prevents silent battery drain; the device
// requires a manual power cycle after the wiring is inspected and corrected.
// ════════════════════════════════════════════════════════════════════════════
void sleepHost(PillboxState &s) {
    NotePayloadDesc payload = {0, 0, 0};
    NotePayloadAddSegment(&payload, STATE_SEG_ID, &s, sizeof(s));
    NotePayloadSaveAndSleep(&payload, s.poll_sec, NULL);

    // Reaching here means power gating did not cut the host rail as expected.

#ifdef PILLBOX_BENCH_MODE
    // Bench fallback: ATTN->EN is not wired on this test configuration. Wait
    // one poll interval so the Notecard's ATTN timer fires (making the saved
    // payload available on reboot), then soft-reset to re-enter setup().
    delay(s.poll_sec * 1000UL);
    NVIC_SystemReset();
#else
    // Production fault path: ATTN->EN power gating did not cut the host rail.
    // Emit a diagnostic Note so the fault is visible in Notehub, log it over
    // USB if available, then halt. The device must be manually power-cycled
    // after the ATTN and EN wiring is inspected and corrected.
#ifdef usbSerial
    usbSerial.println("[FATAL] NotePayloadSaveAndSleep returned — "
                      "ATTN->EN power-gating fault. Check ATTN/EN wiring. Halting.");
#endif
    {
        J *req = notecard.newRequest("note.add");
        if (req) {
            JAddStringToObject(req, "file", NOTEFILE_DIAG);
            JAddBoolToObject(req, "sync", true);
            J *body = JAddObjectToObject(req, "body");
            JAddStringToObject(body, "error", "attn_en_fault");
            notecard.sendRequest(req);
        }
    }
    while (true) { delay(1000); }
#endif
}
