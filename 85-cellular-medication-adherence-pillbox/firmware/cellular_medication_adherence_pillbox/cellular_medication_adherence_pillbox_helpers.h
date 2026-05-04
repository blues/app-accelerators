/*
 * cellular_medication_adherence_pillbox_helpers.h
 *
 * Shared constants, types, and helper-function declarations for the
 * cellular medication adherence pillbox firmware.
 *
 * To disable debug output for production/battery builds, comment out the
 * #define usbSerial line below. All Serial.print calls in the helpers and in
 * the main sketch are guarded by #ifdef usbSerial, so a single edit here
 * silences both translation units.
 */
#pragma once

#include <Notecard.h>
#include <string.h>

// ── Debug output ─────────────────────────────────────────────────────────────
// Comment out this line for production/battery builds. When undefined every
// usbSerial.print call is compiled out, keeping each 30-second wake tight.
#define usbSerial Serial

// ── Bench mode ───────────────────────────────────────────────────────────────
// Define PILLBOX_BENCH_MODE when testing on a bench setup where the Notecard
// ATTN pin is NOT connected to the host EN rail.
//
// Bench mode: if NotePayloadSaveAndSleep() returns, sleepHost() falls back to
// delay() + NVIC_SystemReset(), allowing bring-up without full power-gating
// hardware in place.
//
// Production (PILLBOX_BENCH_MODE undefined): a return from
// NotePayloadSaveAndSleep() is an ATTN->EN wiring fault. sleepHost() logs the
// condition, emits a pill_diag.qo Note so the fault is visible in Notehub,
// then halts. Halting rather than delaying prevents silent battery drain and
// makes the fault immediately obvious without needing a USB serial connection.
//
// Comment out this line before deploying to a battery-powered Notecarrier CX.
// #define PILLBOX_BENCH_MODE

// ── Tunable defaults (all overridable via Notehub environment variables) ─────
#define DEFAULT_POLL_SEC      30    // seconds between host wakes
#define DEFAULT_SUMMARY_HOUR   0    // UTC hour for daily summary (0 = midnight)
#define DEFAULT_OUTBOUND_MIN 720    // Notecard outbound sync cadence, minutes (12 h)
#define DEFAULT_INBOUND_MIN  120    // Notecard inbound sync cadence, minutes (2 h)

// ── Compartment hardware ─────────────────────────────────────────────────────
#define NUM_COMPARTMENTS 7

// ── Notefiles ────────────────────────────────────────────────────────────────
#define NOTEFILE_OPEN    "pill_open.qo"    // immediate open event, sync:true
#define NOTEFILE_SUMMARY "pill_summary.qo" // daily adherence summary, templated
#define NOTEFILE_DIAG    "pill_diag.qo"    // ATTN->EN fault diagnostic (production only)

// ── Persistent state ─────────────────────────────────────────────────────────
#define STATE_SEG_ID          "PIL"
#define PILLBOX_STATE_VERSION   4   // increment whenever PillboxState layout changes

// Maximum number of failed pill_open.qo events buffered across wakes. When
// the queue is full the oldest entry is evicted and pending_overflow is
// incremented so the loss is visible rather than silent. A pill_diag.qo Note
// is emitted to Notehub on the first overflow and again when the queue drains,
// providing cloud-visible evidence of any adherence data loss.
// 28 slots = four worst-case 7-compartment wakes.
#define PENDING_QUEUE_CAPACITY 28

// Per-event record stored when a pill_open.qo note.add fails after all retry
// attempts. Preserving the original per-event context (day_mask, poll_mask)
// means each event can be replayed and cleared individually, keeping
// duplicates and multi-wake failures independently identifiable.
struct PendingOpenEvent {
    uint8_t compartment;  // 0-based index (0..NUM_COMPARTMENTS-1)
    uint8_t day_mask;     // daily_opens bitmask at the moment the lid was detected open
    uint8_t poll_mask;    // opened_this_poll bitmask from that specific wake
};

// State persisted across sleep cycles via NotePayloadSaveAndSleep. The
// Notecard stores this struct in its own flash and returns it on the next
// wake via NotePayloadRetrieveAfterSleep — no external memory needed.
struct PillboxState {
    uint8_t  version;            // must equal PILLBOX_STATE_VERSION; mismatch reinits
    uint8_t  prev_pin_mask;      // pin bitmask from previous wake (0=closed, 1=open)
    uint8_t  daily_opens;        // compartments opened today (bitmask; feeds summary)
    uint8_t  prev_day_opens;     // opens_mask from previous day, awaiting summary emit
    uint32_t last_utc_day;       // UTC day ordinal (unix_sec / 86400) at last wake
    uint32_t summary_target_day; // UTC day on which the pending summary should be emitted
    uint32_t poll_sec;           // seconds between wakes (from env var)
    uint8_t  summary_hour;       // UTC hour threshold for daily summary emission
    uint16_t outbound_min;       // Notecard outbound sync cadence, minutes
    uint16_t inbound_min;        // Notecard inbound sync cadence, minutes
    bool     summary_pending;    // true when prev_day_opens has not yet been summarized

    // Ring buffer of failed pill_open.qo events. Each record preserves its
    // own per-event context so duplicates and multi-wake failures remain
    // individually identifiable on replay. Overflow is explicit (not silent):
    // pending_overflow increments whenever the oldest entry is evicted to make
    // room. The counter is cleared once the queue fully drains.
    PendingOpenEvent pending_queue[PENDING_QUEUE_CAPACITY];
    uint8_t          pending_count;    // occupied slots (0..PENDING_QUEUE_CAPACITY)
    uint8_t          pending_overflow; // events dropped due to full queue
};

// ── Notecard instance ────────────────────────────────────────────────────────
// Defined in the main .ino; the extern declaration here allows helper
// functions in the .cpp to use it without an additional parameter.
extern Notecard notecard;

// ── Helper function declarations ─────────────────────────────────────────────
void     setupPins();
void     initNotecard(const char *product_uid, uint32_t outbound_min,
                      uint16_t inbound_min);
void     defineTemplates();
void     fetchEnvOverrides(PillboxState &s);
uint8_t  sampleCompartments();
bool     emitOpenEvent(uint8_t idx, uint8_t day_mask, uint8_t poll_mask);
void     enqueuePendingEvent(PillboxState &s, uint8_t idx,
                             uint8_t day_mask, uint8_t poll_mask);
void     replayPendingOpenEvents(PillboxState &s);
bool     emitDailySummary(uint8_t opens_mask);
uint32_t utcDayAndHour(uint32_t *hour_out);
void     sleepHost(PillboxState &s);
