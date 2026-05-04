/*
 * Cross-Border Container Electronic Security Seal
 *
 * Hardware: Blues Notecarrier XI (Swan STM32 host) + Notecard Cell+WiFi
 *           (NOTE-NBGLW) + Starnote for Iridium + NC magnetic reed switch
 *           door sensor + NC break-wire seal-continuity loop + ATECC608A
 *           secure element + LiPo battery
 *
 * Operation:
 *   The host wakes every CHECK_INTERVAL_SEC seconds via card.attn and reads
 *   two sensors:
 *     1. Door-state GPIO (A0) — NC magnetic reed switch; sealed=LOW, open=HIGH.
 *     2. Seal-wire GPIO (A1)  — break-wire continuity loop; intact=LOW,
 *                               broken=HIGH.
 *   If either sensor changes state, the host builds a seal_event.qo Note,
 *   signs the payload with the ATECC608A ECC P-256 key, and submits it with
 *   sync:true so the Notecard attempts immediate transmission — cellular if
 *   in port, Iridium satellite if at sea.  The full 64-byte ECDSA signature
 *   is also queued in a companion seal_sig_full.qo note for cellular delivery.
 *   Every HEARTBEAT_INTERVAL_MIN minutes a seal_heartbeat.qo Note is logged
 *   with the current GPS position, door state, seal-wire state, battery
 *   voltage, and a hardware signature.  Both Notefiles use compact templates
 *   to minimise on-wire payload size over the satellite link.
 *
 *   All Notecard API calls are in container_seal_notecard.cpp.
 *   All signing code is in container_seal_sign.cpp.
 *
 * Pin assignment (Notecarrier XI headers):
 *   A0  — Door reed switch (NC): sealed (magnet present) → LOW; breach → HIGH.
 *         One terminal to A0, other to GND; INPUT_PULLUP active.
 *   A1  — Seal-wire continuity loop: intact → LOW; broken/cut → HIGH.
 *         One terminal to A1, other to GND; INPUT_PULLUP active.
 *         Wire the continuity loop through the physical seal body so that
 *         cutting the seal breaks the circuit independently of door state.
 *
 * Debug serial:
 *   By default, setup() does NOT wait for USB CDC — field builds wake,
 *   sample, queue, and sleep as fast as possible to preserve the battery
 *   budget.  Define SEAL_DEBUG_SERIAL (e.g. via -DSEAL_DEBUG_SERIAL) to
 *   enable a 2-second Serial wait and Notecard debug output on bench.
 *   Do not define this in production builds.
 */

#include <Notecard.h>
#include "container_seal_notecard.h"
#include "container_seal_sign.h"

// ---------------------------------------------------------------------------
// Product UID — replace with your Notehub project's ProductUID before flashing
// ---------------------------------------------------------------------------
#ifndef PRODUCT_UID
#define PRODUCT_UID ""  // e.g. "com.your-company.your-name:container-seal"
#pragma message "PRODUCT_UID is not defined. Set before flashing to associate " \
                "the device with a Notehub project."
#endif

// ---------------------------------------------------------------------------
// Hardware
// ---------------------------------------------------------------------------
#define DOOR_REED_PIN  A0   // NC reed switch: sealed=LOW, breach=HIGH
#define SEAL_WIRE_PIN  A1   // NC continuity loop: intact=LOW, broken/cut=HIGH

// ---------------------------------------------------------------------------
// Firmware defaults — all overridable at runtime via Notehub environment vars
// ---------------------------------------------------------------------------
#define DEFAULT_CHECK_INTERVAL_SEC     30     // door/seal-wire poll cadence
#define DEFAULT_HEARTBEAT_INTERVAL_MIN 360    // 6-hour waypoint / health check
#define DEFAULT_OUTBOUND_MIN           720    // 12-hour satellite outbound sync
#define DEFAULT_INBOUND_MIN            10080  // 7-day satellite inbound sync

// ---------------------------------------------------------------------------
// Pending door/seal-break event ring buffer
// ---------------------------------------------------------------------------
// Events that fail to reach the Notecard (transient I2C fault, Notecard
// temporarily busy) are stored in a small FIFO ring buffer so that multiple
// transitions can accumulate locally while the Notecard path is unhealthy.
// Events are retried oldest-first on every subsequent wake.
//
// PENDING_QUEUE_SIZE bounds the buffer.  When it fills, additional events are
// dropped and overflowOccurred is set.  Once the queue fully drains after an
// overflow, sealSendAuditGap() emits a chain-of-custody gap indicator.
//
// Choosing 4: worst-case with a 30 s check interval and a multi-minute I2C
// outage yields at most ~4 transitions before recovery.
//
// NOTE ON FIRMWARE UPGRADES: PendingEvent contains sigFull[64] and sealBroken
// fields.  A firmware upgrade that changes the PendingEvent layout shifts all
// subsequent fields in SealState; the pendingCount guard in setup() detects
// the corrupted value and clears the queue, setting overflowOccurred so
// downstream operators are notified.  Any events pending at upgrade time are
// counted as lost in the audit gap.
#define PENDING_QUEUE_SIZE 4

struct PendingEvent {
    uint32_t eventEpoch;     // Unix epoch at detection time (0 = unknown)
    uint32_t totalBreaches;  // cumulative breach count at detection time
    float    battV;          // battery voltage at detection time (-1.0 = unavail)
    float    eventLat;       // GPS latitude at detection time (0.0 = no fix)
    float    eventLon;       // GPS longitude at detection time (0.0 = no fix)
    uint32_t sig;            // 4-byte ECDSA sig prefix at detection time (0=unsigned)
    uint8_t  doorOpen;       // door state: 1 = open/breach, 0 = sealed
    uint8_t  hasLocation;    // 1 if eventLat/eventLon are a valid GPS fix
    uint8_t  sealBroken;     // 1 if this is a seal-wire-break event
    uint8_t  _pad;           // explicit padding — 28 bytes for the scalar fields
    uint8_t  sigFull[64];    // full 64-byte ECDSA P-256 signature (R‖S); all
                             // zeros if the ATECC608A was unavailable at
                             // detection time.  Preserved so the companion
                             // seal_sig_full.qo note can be queued on retry.
    // Total: 28 + 64 = 92 bytes, no alignment holes.
};

// ---------------------------------------------------------------------------
// Persistent state — saved to Notecard flash on each sleep, restored on wake.
//
// STRUCT LAYOUT INVARIANT: the first 12 bytes (offsets 0–11) must not change
// position between firmware versions:
//   offset 0: initialized         — init guard; relies on being at byte 0
//   offset 1: lastDoorOpen        — door state persisted across sleeps
//   offset 2: heartbeatPermFault  — heartbeat fault flag
//   offset 3: lastSealIntact      — seal-wire state (was _pad0 before seal
//                                   continuity was added; old value = 0 =
//                                   "broken" default, handled gracefully by
//                                   the detection logic without false alarms)
//   offsets 4–7:  totalBreaches
//   offsets 8–11: nextHeartbeatEpoch
//
// Fields after offset 11 may shift when PendingEvent or SealConfig grows.
// The pendingCount range-check and pendingHead/Tail bounds-check in setup()
// detect corruption from layout shifts and clear the queue safely.
// ---------------------------------------------------------------------------
struct SealState {
    uint8_t  initialized;           // offset  0: 1 after first-boot config
    uint8_t  lastDoorOpen;          // offset  1: last observed door state
    uint8_t  heartbeatPermFault;    // offset  2: 1 if heartbeat permanently rejected
    uint8_t  lastSealIntact;        // offset  3: 1 = wire intact on last wake
    uint32_t totalBreaches;         // offset  4: cumulative breach count
    uint32_t nextHeartbeatEpoch;    // offset  8: Unix epoch of next heartbeat

    // Ring buffer of pending door/seal-break events.
    PendingEvent pendingQueue[PENDING_QUEUE_SIZE];  // 368 bytes (4 × 92)
    uint8_t      pendingHead;
    uint8_t      pendingTail;
    uint8_t      pendingCount;
    uint8_t      overflowOccurred;  // 1 if ≥1 event was dropped
    uint32_t     overflowEpoch;     // epoch when the first overflow occurred

    SealConfig cfg;                 // active configuration (mirrors env vars)
};

static SealState g_state;
static const char kStateSegID[] = "SEAL";

Notecard notecard;

// ---------------------------------------------------------------------------
// cfgIsValid — sanity-check cfg after a binary restore from Notecard flash.
// ---------------------------------------------------------------------------
static bool cfgIsValid(const SealConfig &cfg) {
    return cfg.checkIntervalSec     >=     5 && cfg.checkIntervalSec     <=   300
        && cfg.heartbeatIntervalMin >=    15 && cfg.heartbeatIntervalMin <=  1440
        && cfg.outboundMin          >=    60 && cfg.outboundMin          <=  1440
        && cfg.inboundMin           >=    60 && cfg.inboundMin           <= 10080;
}

// ---------------------------------------------------------------------------
// setup() — runs on every wake from card.attn sleep, not just on power-on
// ---------------------------------------------------------------------------
void setup() {
    Serial.begin(115200);

#ifdef SEAL_DEBUG_SERIAL
    {
        const unsigned long t0 = millis();
        while (!Serial && (millis() - t0) < 2000) {}
    }
#endif

    notecard.begin();
#if defined(SEAL_DEBUG_SERIAL) && !defined(NOTE_C_LOW_MEM)
    notecard.setDebugOutputStream(Serial);
#endif

    // Reed switch: NC, wired to GND.  INPUT_PULLUP → sealed=LOW, breach=HIGH.
    pinMode(DOOR_REED_PIN, INPUT_PULLUP);
    // Seal-wire continuity loop: NC, wired to GND.  intact=LOW, broken=HIGH.
    pinMode(SEAL_WIRE_PIN, INPUT_PULLUP);

    // ---- Attempt to restore state from Notecard flash ---------------------
    memset(&g_state, 0, sizeof(g_state));
    NotePayloadDesc payload;
    bool resumed = NotePayloadRetrieveAfterSleep(&payload);
    if (resumed) {
        NotePayloadGetSegment(&payload, kStateSegID, &g_state, sizeof(g_state));
        NotePayloadFree(&payload);
    }

    // Guard against a firmware update that changed SealState layout after
    // the first 12 bytes (e.g. PendingEvent grew): cfg fields land at a
    // different offset and come back as garbage.  Reset to defaults if any
    // field is out of its valid range.
    bool cadenceResyncNeeded = false;
    if (resumed && g_state.initialized && !cfgIsValid(g_state.cfg)) {
        Serial.println("[seal] cfg out of range after restore — resetting to defaults");
        g_state.cfg.checkIntervalSec     = DEFAULT_CHECK_INTERVAL_SEC;
        g_state.cfg.heartbeatIntervalMin = DEFAULT_HEARTBEAT_INTERVAL_MIN;
        g_state.cfg.outboundMin          = DEFAULT_OUTBOUND_MIN;
        g_state.cfg.inboundMin           = DEFAULT_INBOUND_MIN;
        if (!sealApplyCadence(notecard, g_state.cfg.outboundMin, g_state.cfg.inboundMin)) {
            // Notecard's actual cadence is unknown until the next successful
            // hub.set.  Force a cadence resync after the env-var refresh below
            // even if env vars match the defaults we just installed in cfg.
            Serial.println("[seal] cfg-recovery hub.set failed; will retry this wake");
            cadenceResyncNeeded = true;
        }
    }

    // Guard against a corrupt or stale pendingCount.
    if (g_state.pendingCount > PENDING_QUEUE_SIZE) {
        Serial.println("[seal] pendingCount out of range after restore — clearing queue");
        g_state.pendingHead      = 0;
        g_state.pendingTail      = 0;
        g_state.pendingCount     = 0;
        g_state.overflowOccurred = 1;
    }

    // Guard against corrupted ring-buffer indices.
    if (g_state.pendingCount > 0 &&
        (g_state.pendingHead >= PENDING_QUEUE_SIZE ||
         g_state.pendingTail >= PENDING_QUEUE_SIZE)) {
        Serial.println("[seal] pendingHead/Tail out of range after restore — clearing queue");
        g_state.pendingHead      = 0;
        g_state.pendingTail      = 0;
        g_state.pendingCount     = 0;
        g_state.overflowOccurred = 1;
    }

    bool runMainLogic = true;

    if (!resumed || !g_state.initialized) {
        // ---- First boot: initialise state and configure the Notecard ------
        memset(&g_state, 0, sizeof(g_state));
        g_state.cfg.checkIntervalSec     = DEFAULT_CHECK_INTERVAL_SEC;
        g_state.cfg.heartbeatIntervalMin = DEFAULT_HEARTBEAT_INTERVAL_MIN;
        g_state.cfg.outboundMin          = DEFAULT_OUTBOUND_MIN;
        g_state.cfg.inboundMin           = DEFAULT_INBOUND_MIN;

        // Capture sensor states at deployment so first wake has baselines.
        g_state.lastDoorOpen    = (digitalRead(DOOR_REED_PIN) == HIGH) ? 1 : 0;
        g_state.lastSealIntact  = (digitalRead(SEAL_WIRE_PIN) == LOW)  ? 1 : 0;

        bool cfgOk = sealConfigureNotecard(notecard, PRODUCT_UID, g_state.cfg)
                  && sealDefineTemplates(notecard)
                  && sealConfigureGPS(notecard, 21600);

        if (cfgOk) {
            // Initialize ATECC608A secure element for event signing.
            // Non-fatal: if the chip is absent sig=0 on all events.
            sealSignBegin();

            uint32_t prevOutbound = g_state.cfg.outboundMin;
            uint32_t prevInbound  = g_state.cfg.inboundMin;
            sealFetchEnvVars(notecard, g_state.cfg);
            if (g_state.cfg.outboundMin != prevOutbound ||
                g_state.cfg.inboundMin  != prevInbound) {
                if (!sealApplyCadence(notecard,
                                      g_state.cfg.outboundMin,
                                      g_state.cfg.inboundMin)) {
                    Serial.println("[seal] Cadence apply failed on first boot; using defaults");
                    g_state.cfg.outboundMin = prevOutbound;
                    g_state.cfg.inboundMin  = prevInbound;
                }
            }

            uint32_t now = sealGetEpoch(notecard);
            g_state.nextHeartbeatEpoch = now + (g_state.cfg.heartbeatIntervalMin * 60UL);

            g_state.initialized = 1;
            Serial.println("[seal] First-boot configuration complete");
        } else {
            Serial.println("[seal] First-boot configuration failed; will retry next wake");
            runMainLogic = false;
        }
    } else {
        // Resumed wake: re-initialize the ATECC608A (chip state is not
        // persisted across host power-off; begin() must be called each wake).
        sealSignBegin();
    }

    // ---- Main logic — executed on every wake after successful init --------
    if (runMainLogic) {
        bool     doorOpen   = (digitalRead(DOOR_REED_PIN) == HIGH);
        bool     sealIntact = (digitalRead(SEAL_WIRE_PIN) == LOW);
        uint32_t now        = sealGetEpoch(notecard);

        // ---- Retry pending events (oldest first) --------------------------
        // Events that failed to reach the Notecard on a previous wake are
        // stored in a FIFO ring buffer.  Drain the queue oldest-first,
        // stopping on the first transient failure.
        while (g_state.pendingCount > 0) {
            PendingEvent &ev = g_state.pendingQueue[g_state.pendingHead];

            NoteAddResult res = sealSendDoorEvent(notecard,
                                                  (bool)ev.doorOpen,
                                                  ev.totalBreaches,
                                                  ev.battV,
                                                  ev.eventEpoch,
                                                  ev.eventLat,
                                                  ev.eventLon,
                                                  (bool)ev.hasLocation,
                                                  (bool)ev.sealBroken,
                                                  ev.sig,
                                                  ev.sigFull);
            if (res == NOTE_ADD_OK) {
                g_state.pendingHead   = (g_state.pendingHead + 1) % PENDING_QUEUE_SIZE;
                g_state.pendingCount--;
                Serial.println("[seal] Pending event retransmitted");

            } else if (res == NOTE_ADD_PERMANENT) {
                if (!g_state.overflowOccurred) {
                    g_state.overflowOccurred = 1;
                    g_state.overflowEpoch    = ev.eventEpoch;
                }
                Serial.println("[seal] WARN: permanently rejected pending event discarded; audit gap recorded");
                g_state.pendingHead  = (g_state.pendingHead + 1) % PENDING_QUEUE_SIZE;
                g_state.pendingCount--;

            } else {
                Serial.println("[seal] Pending event retry failed (transient); deferring to next wake");
                break;
            }
        }

        // ---- Audit-gap retry (post-drain) ---------------------------------
        if (g_state.pendingCount == 0 && g_state.overflowOccurred) {
            NoteAddResult gapRes = sealSendAuditGap(notecard, g_state.overflowEpoch);
            if (gapRes == NOTE_ADD_OK) {
                g_state.overflowOccurred = 0;
                g_state.overflowEpoch    = 0;
                Serial.println("[seal] Audit-gap note sent");
            } else if (gapRes == NOTE_ADD_PERMANENT) {
                g_state.overflowOccurred = 0;
                g_state.overflowEpoch    = 0;
                Serial.println("[seal] WARN: audit-gap note permanently rejected; gap indicator lost");
            }
            // NOTE_ADD_TRANSIENT: overflowOccurred stays set; retry next wake
        }

        // ---- Refresh environment variables on every wake ------------------
        {
            uint32_t prevOutbound  = g_state.cfg.outboundMin;
            uint32_t prevInbound   = g_state.cfg.inboundMin;
            uint32_t prevHeartbeat = g_state.cfg.heartbeatIntervalMin;

            SealEnvResult envResult = sealFetchEnvVars(notecard, g_state.cfg);

            if (envResult == SEAL_ENV_FAIL) {
                Serial.println("[seal] Env fetch failed (I2C error)");
            } else if (envResult == SEAL_ENV_OK) {
                Serial.println("[seal] Env vars updated");

                if (g_state.cfg.outboundMin != prevOutbound ||
                    g_state.cfg.inboundMin  != prevInbound ||
                    cadenceResyncNeeded) {
                    if (sealApplyCadence(notecard,
                                         g_state.cfg.outboundMin,
                                         g_state.cfg.inboundMin)) {
                        if (g_state.cfg.outboundMin != prevOutbound) {
                            Serial.print("[seal] Outbound cadence updated to ");
                            Serial.print(g_state.cfg.outboundMin);
                            Serial.println(" min");
                        }
                        if (g_state.cfg.inboundMin != prevInbound) {
                            Serial.print("[seal] Inbound cadence updated to ");
                            Serial.print(g_state.cfg.inboundMin);
                            Serial.println(" min");
                        }
                        cadenceResyncNeeded = false;
                    } else {
                        Serial.println("[seal] hub.set cadence update failed; will retry next wake");
                        g_state.cfg.outboundMin = prevOutbound;
                        g_state.cfg.inboundMin  = prevInbound;
                        // The next env-var refresh will re-detect any change
                        // and retry hub.set normally.  In the rare case where
                        // env vars exactly match defaults, the Notecard may
                        // run a stale cadence until the next env-var update.
                    }
                }
                if (g_state.cfg.heartbeatIntervalMin != prevHeartbeat) {
                    g_state.nextHeartbeatEpoch =
                        now + (g_state.cfg.heartbeatIntervalMin * 60UL);
                    Serial.print("[seal] Heartbeat interval updated to ");
                    Serial.print(g_state.cfg.heartbeatIntervalMin);
                    Serial.println(" min; schedule reset from now");
                }
            }
        }

        // ---- Helper: enqueue or discard a failed event --------------------
        // Extracted as a lambda-style inline to avoid repeating the full
        // enqueue/overflow pattern for both seal-break and door events.
        // Returns true if the event was enqueued, false if discarded.
        auto enqueuePending = [&](uint32_t epoch, uint32_t breaches,
                                  float batt, float lat, float lon,
                                  bool hasLoc, bool dOpen,
                                  bool sBroken, uint32_t eSig,
                                  const uint8_t *eSigFull) -> bool {
            if (g_state.pendingCount < PENDING_QUEUE_SIZE) {
                uint8_t slot = g_state.pendingTail;
                g_state.pendingQueue[slot].eventEpoch    = epoch;
                g_state.pendingQueue[slot].totalBreaches = breaches;
                g_state.pendingQueue[slot].battV         = batt;
                g_state.pendingQueue[slot].eventLat      = lat;
                g_state.pendingQueue[slot].eventLon      = lon;
                g_state.pendingQueue[slot].sig           = eSig;
                g_state.pendingQueue[slot].doorOpen      = dOpen ? 1 : 0;
                g_state.pendingQueue[slot].hasLocation   = hasLoc ? 1 : 0;
                g_state.pendingQueue[slot].sealBroken    = sBroken ? 1 : 0;
                if (eSigFull != NULL) {
                    memcpy(g_state.pendingQueue[slot].sigFull, eSigFull, 64);
                } else {
                    memset(g_state.pendingQueue[slot].sigFull, 0, 64);
                }
                g_state.pendingTail  = (g_state.pendingTail + 1) % PENDING_QUEUE_SIZE;
                g_state.pendingCount++;
                return true;
            }
            // Queue full — record overflow epoch and return false.
            if (!g_state.overflowOccurred) {
                g_state.overflowOccurred = 1;
                g_state.overflowEpoch    = epoch;
            }
            return false;
        };

        // ---- Detect seal-wire-break (independent of door state) -----------
        // A transition from intact→broken means the physical seal wire was
        // cut.  This fires independently of door-open state: a cut-and-
        // replaced seal before door opening generates a seal_broken:true event
        // without a door-open event.
        //
        // Breach count: totalBreaches is NOT incremented for seal-wire breaks
        // because the existing counter definition tracks door-open events only,
        // preserving backwards compatibility with downstream alert rules.
        // Seal breaks are distinguished in the event body by seal_broken:true.
        //
        // lastSealIntact is always updated at the end of this block so that
        // subsequent wakes see the current wire state.
        {
            uint8_t newSealIntact = sealIntact ? 1 : 0;
            if (!sealIntact && g_state.lastSealIntact) {
                // Seal wire just broke.
                Serial.println("[seal] SEAL WIRE BREAK detected");

                uint32_t eventEpoch = now;
                float    battV      = sealGetBattVoltage(notecard);
                float    eventLat   = 0.0f;
                float    eventLon   = 0.0f;
                bool     hasLoc     = sealGetLocation(notecard, &eventLat, &eventLon);

                // Sign the break event.  Canonical payload (19 bytes) includes
                // event_type (SEAL_EVENT_WIRE_BREAK), seal_broken=true,
                // door_open (current door state at break time — cryptographically
                // binds the "seal cut while door closed" claim), breach count,
                // and detection-time lat/lon in fixed-point (lroundf × 1e6).
                uint8_t  sigFull[64] = {0};
                uint32_t sig = sealSignEvent(eventEpoch,
                                             SEAL_EVENT_WIRE_BREAK,
                                             /*sealBroken=*/true,
                                             doorOpen,   // door state at wire-break time
                                             g_state.totalBreaches,
                                             hasLoc, eventLat, eventLon,
                                             sigFull);

                NoteAddResult res = sealSendDoorEvent(notecard, doorOpen,
                                                      g_state.totalBreaches,
                                                      battV, eventEpoch,
                                                      eventLat, eventLon,
                                                      hasLoc,
                                                      /*sealBroken=*/true,
                                                      sig, sigFull);
                if (res == NOTE_ADD_OK) {
                    g_state.lastSealIntact = newSealIntact;
                } else if (res == NOTE_ADD_TRANSIENT) {
                    bool enqueued = enqueuePending(eventEpoch, g_state.totalBreaches,
                                                   battV, eventLat, eventLon,
                                                   hasLoc, doorOpen,
                                                   /*sealBroken=*/true,
                                                   sig, sigFull);
                    if (!enqueued) {
                        Serial.println("[seal] WARN: pending queue full — seal-break event dropped; audit gap recorded");
                    } else {
                        Serial.println("[seal] Seal-break event enqueued for retry");
                    }
                    g_state.lastSealIntact = newSealIntact;
                } else {
                    // NOTE_ADD_PERMANENT: template recovery attempted; discard.
                    if (!g_state.overflowOccurred) {
                        g_state.overflowOccurred = 1;
                        g_state.overflowEpoch    = eventEpoch;
                    }
                    g_state.lastSealIntact = newSealIntact;
                    Serial.println("[seal] WARN: seal-break event permanently rejected; audit gap recorded");
                }
            } else {
                // No seal-wire state change — update lastSealIntact so the
                // next wake has a current baseline, including after firmware
                // upgrades where lastSealIntact starts at 0 (old _pad byte).
                g_state.lastSealIntact = newSealIntact;
            }
        }

        // ---- Detect door-state change -------------------------------------
        // A transition from sealed→open is a breach; open→sealed is a re-seal.
        // Both events are logged immediately with sync:true.
        //
        // Breach count: totalBreaches is incremented at detection time, before
        // the send attempt.  This makes totalBreaches the true lifetime count
        // regardless of whether the per-event note reaches Notehub.
        // Heartbeats and re-seal notes always carry an accurate running total
        // even after queue overflows or permanent note.add rejections.
        //
        // lastDoorOpen is committed only after the transition has a durable
        // record: either NOTE_ADD_OK (Notecard accepted), a successful local
        // enqueue (NOTE_ADD_TRANSIENT with queue space), or an unavoidable
        // discard (queue full or NOTE_ADD_PERMANENT after template recovery).
        // It is never advanced before the outcome is known.
        {
            uint8_t newDoorOpen = doorOpen ? 1 : 0;
            if (newDoorOpen != g_state.lastDoorOpen) {
                uint32_t eventEpoch = now;
                float    battV      = sealGetBattVoltage(notecard);
                float    eventLat   = 0.0f;
                float    eventLon   = 0.0f;
                bool     hasLoc     = sealGetLocation(notecard, &eventLat, &eventLon);

                // Breach count incremented at detection time — before the
                // send attempt.  Only open transitions count; re-seal events
                // carry the running total without changing it.
                if (doorOpen) {
                    g_state.totalBreaches++;
                }
                uint32_t proposedBreaches = g_state.totalBreaches;

                if (doorOpen) {
                    Serial.print("[seal] BREACH detected — count=");
                    Serial.println(proposedBreaches);
                } else {
                    Serial.println("[seal] Door re-sealed");
                }

                // Sign the door event.  Canonical payload (19 bytes) includes
                // event_type (DOOR_OPEN or DOOR_CLOSE), seal_broken=false,
                // door_open (same as doorOpen — redundant for door events but
                // included for a uniform payload layout), breach count, and
                // detection-time lat/lon in fixed-point (lroundf × 1e6).
                uint8_t  evType  = doorOpen ? SEAL_EVENT_DOOR_OPEN
                                            : SEAL_EVENT_DOOR_CLOSE;
                uint8_t  sigFull[64] = {0};
                uint32_t sig = sealSignEvent(eventEpoch,
                                             evType,
                                             /*sealBroken=*/false,
                                             doorOpen,   // door state for this event
                                             proposedBreaches,
                                             hasLoc, eventLat, eventLon,
                                             sigFull);

                NoteAddResult res = sealSendDoorEvent(notecard, doorOpen,
                                                      proposedBreaches, battV,
                                                      eventEpoch,
                                                      eventLat, eventLon,
                                                      hasLoc,
                                                      /*sealBroken=*/false,
                                                      sig, sigFull);
                if (res == NOTE_ADD_OK) {
                    // Note accepted — commit door state.  totalBreaches was
                    // already incremented above for breach events.
                    g_state.lastDoorOpen = newDoorOpen;

                } else if (res == NOTE_ADD_TRANSIENT) {
                    bool enqueued = enqueuePending(eventEpoch, proposedBreaches,
                                                   battV, eventLat, eventLon,
                                                   hasLoc, doorOpen,
                                                   /*sealBroken=*/false,
                                                   sig, sigFull);
                    if (!enqueued) {
                        Serial.println("[seal] WARN: pending queue full — door event dropped; audit gap recorded");
                    } else {
                        Serial.println("[seal] Door event enqueued for retry");
                    }
                    g_state.lastDoorOpen = newDoorOpen;

                } else {
                    // NOTE_ADD_PERMANENT: template recovery already attempted.
                    if (!g_state.overflowOccurred) {
                        g_state.overflowOccurred = 1;
                        g_state.overflowEpoch    = eventEpoch;
                    }
                    g_state.lastDoorOpen = newDoorOpen;
                    Serial.println("[seal] WARN: door event permanently rejected; audit gap recorded");
                }
            }
        }

        // ---- Periodic heartbeat (GPS waypoint + health) ------------------
        // Skipped while pending events exist to conserve satellite link
        // capacity for chain-of-custody breach and seal-break alerts.
        if (g_state.pendingCount == 0 && now >= g_state.nextHeartbeatEpoch) {
            bool doSendHeartbeat = true;

            if (g_state.heartbeatPermFault) {
                if (sealDefineTemplates(notecard)) {
                    g_state.heartbeatPermFault = 0;
                    Serial.println("[seal] Template re-registration succeeded; heartbeat fault cleared");
                } else {
                    g_state.nextHeartbeatEpoch =
                        now + (g_state.cfg.heartbeatIntervalMin * 60UL);
                    doSendHeartbeat = false;
                    Serial.println("[seal] Heartbeat: template re-registration failed; deferring one interval");
                }
            }

            if (doSendHeartbeat) {
                float battV = sealGetBattVoltage(notecard);

                // Sign the heartbeat.  Canonical payload (11 bytes) includes
                // heartbeat_time (now), open state, seal-wire state, and
                // cumulative breaches.
                uint8_t  hbSigFull[64] = {0};
                uint32_t hbSig = sealSignHeartbeat(now,
                                                    doorOpen,
                                                    (bool)g_state.lastSealIntact,
                                                    g_state.totalBreaches,
                                                    hbSigFull);

                // Pass `now` as hbTime so the heartbeat note body carries an
                // explicit heartbeat_time field and the companion
                // seal_sig_full.qo note is keyed by the same epoch.
                NoteAddResult hbRes = sealSendHeartbeat(notecard, doorOpen,
                                                        g_state.totalBreaches,
                                                        battV,
                                                        (bool)g_state.lastSealIntact,
                                                        hbSig,
                                                        hbSigFull,
                                                        now);
                if (hbRes == NOTE_ADD_OK) {
                    g_state.nextHeartbeatEpoch =
                        now + (g_state.cfg.heartbeatIntervalMin * 60UL);
                    g_state.heartbeatPermFault = 0;
                    Serial.print("[seal] Heartbeat sent — batt=");
                    Serial.print(battV, 2);
                    Serial.println("V");
                } else if (hbRes == NOTE_ADD_PERMANENT) {
                    g_state.heartbeatPermFault = 1;
                    g_state.nextHeartbeatEpoch =
                        now + (g_state.cfg.heartbeatIntervalMin * 60UL);
                    Serial.println("[seal] WARN: heartbeat permanently rejected; fault flagged");
                } else {
                    // NOTE_ADD_TRANSIENT — retry on very next wake.
                    Serial.println("[seal] Heartbeat failed (transient); will retry next wake");
                }
            }
        }
    }

    // ---- Save state and sleep until next poll interval --------------------
    // NotePayloadSaveAndSleep serialises g_state into Notecard flash and
    // issues a card.attn request that cuts host power for checkIntervalSec
    // seconds.  The next wake re-enters setup() from cold.
    //
    // NOTE ON DURABILITY: g_state (including any events enqueued above in
    // pendingQueue) lives in MCU RAM until this call.  A power loss between
    // event detection and this save point loses events that were enqueued but
    // not yet written to Notecard flash.  Events that received NOTE_ADD_OK
    // before this point are already in Notecard flash and survive any power
    // loss.  A mid-wake power loss that discards in-RAM pending events is
    // silent — no audit_gap note is generated automatically for this
    // scenario.  See §9 of the README for the mid-wake power-loss limitation.
    NotePayloadDesc outPayload = {0, 0, 0};
    NotePayloadAddSegment(&outPayload, kStateSegID, &g_state, sizeof(g_state));
    NotePayloadSaveAndSleep(&outPayload, g_state.cfg.checkIntervalSec, NULL);

    // Should not reach here under normal operation.  Loop at the configured
    // cadence to avoid spin-burning the battery if ATTN is not cutting power.
    for (;;) {
        delay(g_state.cfg.checkIntervalSec * 1000UL);
    }
}

// ---------------------------------------------------------------------------
// loop() — unreachable under normal operation
// ---------------------------------------------------------------------------
void loop() {
    // All application logic runs from setup() because each card.attn sleep
    // cycle fully cuts host power and re-enters setup() on the next wake.
}
