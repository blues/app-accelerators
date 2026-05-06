/*
 * container_seal_notecard.h
 *
 * Notecard API declarations for the Cross-Border Container Electronic
 * Security Seal.  Declares every Notecard helper called from
 * container_seal.ino — note.add, note.template, hub.set, env.get,
 * card.time, card.voltage, and card.location.
 * All implementation is in container_seal_notecard.cpp.
 */

#pragma once

#include <Notecard.h>
#include <stdint.h>

// ---------------------------------------------------------------------------
// Configuration passed down from main sketch
// ---------------------------------------------------------------------------
struct SealConfig {
    uint32_t checkIntervalSec;     // door-state polling interval
    uint32_t heartbeatIntervalMin; // waypoint/health-check cadence
    uint32_t outboundMin;          // Notecard outbound sync cadence
    uint32_t inboundMin;           // Notecard inbound sync cadence
};

// ---------------------------------------------------------------------------
// Result returned by sealFetchEnvVars
// ---------------------------------------------------------------------------
typedef enum {
    SEAL_ENV_OK,        // env.get succeeded and at least one value changed
    SEAL_ENV_NO_CHANGE, // env.get succeeded but no values were updated
    SEAL_ENV_FAIL       // env.get returned NULL (I2C or Notecard error)
} SealEnvResult;

// ---------------------------------------------------------------------------
// Result returned by sealSendDoorEvent / sealSendHeartbeat / sealSendAuditGap
//
// Callers MUST differentiate these to avoid wedging the pending ring buffer:
//   NOTE_ADD_OK        — commit state, no further action needed.
//   NOTE_ADD_TRANSIENT — enqueue the event for retry on a later wake.
//   NOTE_ADD_PERMANENT — discard the event; do NOT enqueue.  Template
//                        recovery was already attempted inside the function;
//                        re-enqueueing would produce the same rejection on
//                        every subsequent wake and block all later events.
// ---------------------------------------------------------------------------
typedef enum {
    NOTE_ADD_OK,        // Notecard accepted the note
    NOTE_ADD_TRANSIENT, // NULL/I2C failure — safe to retry later
    NOTE_ADD_PERMANENT  // Notecard err rejection — recovery attempted; discard
} NoteAddResult;

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

/**
 * Configure Notecard hub.set, battery voltage mode, and motion settings.
 * Call once on first boot (when NotePayloadRetrieveAfterSleep returns false).
 * Returns true if hub.set was accepted by the Notecard; false on I2C failure
 * or a 5-second retry timeout.  Non-critical requests (card.voltage,
 * card.motion.mode) do not affect the return value.
 */
bool sealConfigureNotecard(Notecard &nc, const char *productUID,
                           const SealConfig &cfg);

/**
 * Register compact note.template entries for seal_event.qo and
 * seal_heartbeat.qo.  Compact format is required for efficient satellite
 * transmission over the Iridium satellite link.  seal_sig_full.qo is free-form JSON
 * and does not require a template.
 *
 * Call once on first boot AND again as template recovery after a permanent
 * note.add rejection (NOTE_ADD_PERMANENT).  sealSendDoorEvent() calls this
 * internally on the first permanent error; callers do not need to invoke it
 * directly for recovery.
 *
 * Returns true only if both note.template requests were accepted by the
 * Notecard without an error response.
 *
 * seal_event.qo body fields:
 *   open        — door state (bool); false = sealed, true = breach.
 *   breaches    — cumulative breach count at detection time (uint32).
 *                 Incremented at detection, before the send attempt, so it
 *                 is accurate in all subsequent heartbeats and re-seal events
 *                 regardless of this note's delivery outcome.
 *   batt_v      — battery voltage in V, -1.0 if unavailable (float16).
 *   event_time  — Unix epoch at detection time (int32).  Complements the
 *                 Notecard-auto-populated _time field, which reflects the
 *                 note.add time and will differ from event_time when the note
 *                 is delivered via the pending-retry path on a later wake.
 *   event_lat   — GPS latitude at detection time (float32); 0.0 = no fix.
 *   event_lon   — GPS longitude at detection time (float32); 0.0 = no fix.
 *                 Downstream consumers should prefer event_lat/event_lon for
 *                 chain-of-custody breach location.
 *   seal_broken — true only in seal-wire-break events (independent of door
 *                 state).  Absent/false in all door-open and door-close events.
 *                 A seal-break event with open:false means the continuity wire
 *                 was cut while the door remained physically closed.
 *   sig         — first 4 bytes of the 64-byte ECDSA P-256 signature computed
 *                 by the ATECC608A over the 19-byte canonical payload (see
 *                 container_seal_sign.h).  0 if the ATECC608A is absent or
 *                 not provisioned.  Downstream routes should alert on sig==0
 *                 for devices known to have a provisioned secure element.
 *                 Full 64-byte signature is in the companion seal_sig_full.qo
 *                 note correlated by event_time.
 *   audit_gap   — true only in audit-gap indicator notes emitted when the
 *                 pending ring buffer overflowed; absent/false in normal events.
 *   _lat/_lon/_time — auto-populated by the Notecard at note.add time.
 *
 * seal_heartbeat.qo body fields:
 *   open           — current door state at heartbeat time.
 *   seal_intact    — true if the seal continuity wire reads intact (LOW) at
 *                    heartbeat creation time; false if the wire is already broken.
 *   breaches       — cumulative breach count at heartbeat time.
 *   batt_v         — battery voltage in V at heartbeat time.
 *   heartbeat_time — Unix epoch at heartbeat creation time (int32).  Used as
 *                    the correlation key for the companion seal_sig_full.qo
 *                    note (where it appears as event_time).
 *   sig            — first 4 bytes of the ATECC608A ECDSA P-256 signature over
 *                    the 11-byte heartbeat payload.  Full signature in companion
 *                    seal_sig_full.qo note (correlated via heartbeat_time).
 *   _lat/_lon/_time — auto-populated by the Notecard.
 */
bool sealDefineTemplates(Notecard &nc);

/**
 * Query the Notecard's last known GPS fix.
 * Populates *lat and *lon and returns true on success.
 * Returns false (and leaves *lat/*lon unchanged) if no fix is available,
 * the Notecard returns an error, or the I2C call fails.
 */
bool sealGetLocation(Notecard &nc, float *lat, float *lon);

/**
 * Configure the Notecard's onboard GPS/GNSS to sample periodically.
 * Call once on first boot.
 * Returns true if card.location.mode was accepted by the Notecard.
 */
bool sealConfigureGPS(Notecard &nc, uint32_t periodSeconds);

/**
 * Pull environment variables from the Notecard (already fetched from Notehub
 * on the last inbound sync).  Missing or out-of-range values leave cfg
 * unchanged, preserving firmware defaults.
 * Returns SEAL_ENV_OK, SEAL_ENV_NO_CHANGE, or SEAL_ENV_FAIL.
 */
SealEnvResult sealFetchEnvVars(Notecard &nc, SealConfig &cfg);

/**
 * Re-apply the outbound and inbound sync cadences via a single atomic hub.set.
 * Returns true on success; false on I2C or Notecard-side error.
 */
bool sealApplyCadence(Notecard &nc, uint32_t outboundMin, uint32_t inboundMin);

/**
 * Add a door-event or seal-wire-break note to seal_event.qo with sync:true.
 * Also queues the full 64-byte ECDSA signature in a companion seal_sig_full.qo
 * note (free-form JSON, cellular delivery) when sigFull is non-NULL and non-
 * zero, so downstream systems can perform full cryptographic verification.
 *
 * doorOpen / sealBroken semantics:
 *   doorOpen=true,  sealBroken=false  → door-open (breach) event
 *   doorOpen=false, sealBroken=false  → door-close (re-seal) event
 *   doorOpen=false, sealBroken=true   → seal-wire break (door still closed)
 *   doorOpen=true,  sealBroken=true   → seal-wire break with door already open
 *
 * sig      4-byte truncated ECDSA prefix from sealSignEvent(), or 0 if
 *          the ATECC608A is absent / not provisioned.
 * sigFull  64-byte full ECDSA signature, or NULL / all-zeros if unavailable.
 *          When non-NULL and non-zero, a companion seal_sig_full.qo note is
 *          queued with the same retry policy as the primary event note.
 *
 * Retry policy: up to NOTE_ADD_RETRIES+1 total attempts.
 * Recovery: on first permanent rejection, calls sealDefineTemplates() and
 * retries once.  Returns NOTE_ADD_PERMANENT if recovery also fails.
 */
NoteAddResult sealSendDoorEvent(Notecard &nc, bool doorOpen,
                                uint32_t totalBreaches, float battV,
                                uint32_t eventEpoch,
                                float eventLat, float eventLon,
                                bool hasLocation,
                                bool sealBroken,
                                uint32_t sig,
                                const uint8_t *sigFull);

/**
 * Add a periodic waypoint/health note to seal_heartbeat.qo.
 * Heartbeats queue on the normal outbound cadence; no sync:true, to
 * conserve satellite capacity for breach alerts.
 *
 * Also queues the full 64-byte ECDSA signature in a companion seal_sig_full.qo
 * note when sigFull is non-NULL and non-zero.  The companion note uses hbTime
 * as its event_time correlation key — downstream consumers match
 * seal_sig_full.qo.event_time to seal_heartbeat.qo.heartbeat_time.
 *
 * hbTime   Unix epoch at heartbeat creation time.  Written into the
 *          heartbeat note body as heartbeat_time and into the companion
 *          seal_sig_full.qo note as event_time for correlation.
 * sig      4-byte truncated ECDSA prefix from sealSignHeartbeat(), or 0.
 * sigFull  64-byte full ECDSA signature, or NULL / all-zeros if unavailable.
 *
 * Returns NOTE_ADD_OK, NOTE_ADD_TRANSIENT, or NOTE_ADD_PERMANENT.
 */
NoteAddResult sealSendHeartbeat(Notecard &nc, bool doorOpen,
                                uint32_t totalBreaches, float battV,
                                bool sealIntact,
                                uint32_t sig,
                                const uint8_t *sigFull,
                                uint32_t hbTime);

/**
 * Queue the full 64-byte ECDSA P-256 signature (R‖S) to the companion
 * seal_sig_full.qo notefile for cellular delivery.  This note is free-form
 * JSON (not compact) so it does not need a template registration and is not
 * transmitted over the satellite link.
 *
 * eventEpoch  Unix epoch from the corresponding seal_event.qo or
 *             seal_heartbeat.qo note — used as the correlation key.
 *             For heartbeats, this is the heartbeat_time field value.
 * eventType   SEAL_EVENT_DOOR_OPEN / DOOR_CLOSE / WIRE_BREAK / HEARTBEAT.
 * sigFull64   64-byte ECDSA P-256 signature buffer (R‖S).  The R component
 *             (bytes 0–31) is stored as "sig_r" (64 hex chars) and the S
 *             component (bytes 32–63) as "sig_s" (64 hex chars).
 *             If all bytes are zero (ATECC608A was unavailable), no note is
 *             created and NOTE_ADD_OK is returned immediately.
 *
 * Uses the same retry policy as primary event notes (up to NOTE_ADD_RETRIES+1
 * attempts).  If all retries fail the companion note is silently lost — there
 * is no secondary pending queue for signature notes.  Route consumers should
 * alert if a seal_event.qo or seal_heartbeat.qo with non-zero sig has no
 * corresponding seal_sig_full.qo after the device returns to cellular coverage.
 *
 * Returns NOTE_ADD_OK if the Notecard accepted the note, NOTE_ADD_TRANSIENT
 * on I2C failure after all retries, or NOTE_ADD_PERMANENT on a rejection.
 */
NoteAddResult sealSendSigFull(Notecard &nc, uint32_t eventEpoch,
                              uint8_t eventType,
                              const uint8_t *sigFull64);

/**
 * Send a chain-of-custody audit-gap indicator note to seal_event.qo.
 * Call once after the pending ring buffer has fully drained following a
 * queue-overflow episode.
 * Returns NOTE_ADD_OK, NOTE_ADD_TRANSIENT, or NOTE_ADD_PERMANENT.
 */
NoteAddResult sealSendAuditGap(Notecard &nc, uint32_t gapEpoch);

/**
 * Return the current Unix epoch from the Notecard's time register.
 * Falls back to millis()/1000 if the Notecard has not yet completed a
 * sync with Notehub.  NOTE: millis() resets on every host power-up, so
 * the fallback is only self-consistent within a single wake cycle.
 */
uint32_t sealGetEpoch(Notecard &nc);

/**
 * Return the battery voltage as reported by card.voltage.
 * Returns -1.0f on any failure (NULL response, I2C error, non-empty err).
 */
float sealGetBattVoltage(Notecard &nc);
