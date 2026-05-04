/*
 * container_seal_sign.h
 *
 * Hardware-rooted event signing for the Cross-Border Container Electronic
 * Security Seal, using an ATECC608A secure element on the shared I²C bus.
 *
 * Architecture overview
 * ---------------------
 * Every door-state transition (breach, re-seal), every seal-wire-break event,
 * and every periodic heartbeat is signed before transmission.  The ATECC608A
 * holds an ECC P-256 private key in slot 0, locked at provisioning time and
 * never readable externally.  The host computes a SHA-256 digest of a
 * deterministic canonical payload (see sealSignEvent / sealSignHeartbeat),
 * submits it to the ATECC608A, and the chip returns a 64-byte ECDSA P-256
 * signature (R‖S format).
 *
 * Compact-note field
 * ------------------
 * The first 4 bytes of the ECDSA signature are stored in the `sig` field
 * (TUINT32) of the compact seal_event.qo / seal_heartbeat.qo body.  This
 * 32-bit prefix is a satellite-bandwidth optimisation; it is a weak
 * correlation hint and tamper indicator only — it is NOT sufficient to
 * perform cryptographic ECDSA verification.  See "Verification" below and
 * the "Quick tamper-detection" note in step 5 for details.
 *
 * Full-signature delivery
 * -----------------------
 * The full 64-byte signature is queued in a companion seal_sig_full.qo note
 * (free-form JSON, not compact) alongside event_time and event_type for
 * correlation.  seal_sig_full.qo is delivered over cellular at the next
 * outbound sync and is the authoritative record for forensic chain-of-custody
 * verification.  Over a satellite-only path, delivery may be deferred until
 * the device returns to cellular coverage.
 *
 * Event-type constants
 * --------------------
 * These values appear in byte 4 of the canonical signing payload and in the
 * event_type field of seal_sig_full.qo notes.  They must not change after
 * provisioning — changing them invalidates all previously issued signatures.
 *
 *   SEAL_EVENT_DOOR_OPEN   0x01  door-open (breach) event
 *   SEAL_EVENT_DOOR_CLOSE  0x02  door-close (re-seal) event
 *   SEAL_EVENT_WIRE_BREAK  0x03  seal-wire-break event (independent of door)
 *   SEAL_EVENT_HEARTBEAT   0x04  periodic health/waypoint note
 *
 * Signing payload (sealSignEvent) — 19 bytes, deterministic
 * ----------------------------------------------------------
 *   bytes  0– 3: event_time    (big-endian uint32, Unix epoch at detection)
 *   byte   4:    event_type    (SEAL_EVENT_DOOR_OPEN / DOOR_CLOSE / WIRE_BREAK)
 *   byte   5:    seal_broken   (0x01 = wire cut, 0x00 = intact)
 *   byte   6:    door_open     (0x01 = door open, 0x00 = door closed)
 *                               Cryptographically binds the door state at the
 *                               moment of each event, including the critical
 *                               "seal cut while door closed" (WIRE_BREAK with
 *                               door_open=0x00) chain-of-custody claim.
 *   bytes  7–10: breaches      (big-endian uint32, cumulative breach count)
 *   bytes 11–14: event_lat     (big-endian int32, lroundf(degrees × 1 000 000);
 *                               0x00000000 when no GPS fix was available)
 *   bytes 15–18: event_lon     (big-endian int32, lroundf(degrees × 1 000 000);
 *                               0x00000000 when no GPS fix was available)
 *
 * IMPORTANT — lat/lon encoding rule: use lroundf(value × 1 000 000) (round
 * half-away-from-zero) to convert to fixed-point.  Do NOT use a plain cast
 * (int32_t)(value × 1e6), which truncates toward zero.  Downstream verifiers
 * must apply the same lroundf rule when reconstructing the canonical payload.
 *
 * Signing payload (sealSignHeartbeat) — 11 bytes, deterministic
 * -------------------------------------------------------------
 *   bytes  0– 3: heartbeat_time (big-endian uint32, Unix epoch at creation)
 *   byte   4:    SEAL_EVENT_HEARTBEAT (0x04)
 *   byte   5:    open          (0x01 = door open, 0x00 = door closed)
 *   byte   6:    seal_intact   (0x01 = wire intact, 0x00 = wire broken)
 *   bytes  7–10: breaches      (big-endian uint32, cumulative breach count)
 *
 * Provisioning (one-time, at device manufacturing)
 * --------------------------------------------------
 * 1.  Install the SparkFun_ATECCX08a_Arduino_Library (Library Manager).
 * 2.  Run the ATECC608A provisioning sketch:
 *       a. ateccx08a.begin(0x60) — verify chip responds.
 *       b. ateccx08a.writeConfigSparkFun() — write the default SparkFun
 *          configuration (restricts slot 0 to ECC key use).
 *       c. ateccx08a.lockConfig() — permanently lock the configuration zone.
 *       d. ateccx08a.createNewKeyPair(0) — generate the P-256 key pair in
 *          slot 0; the private key is locked inside the chip.
 *       e. Read back and record ateccx08a.publicKey64Bytes[] — this is the
 *          64-byte uncompressed public key used for offline signature
 *          verification.  Store it in your provisioning database alongside
 *          the device serial number.
 *       f. ateccx08a.lockDataAndOTP() — lock the data zone so slot 0 cannot
 *          be overwritten.
 * 3.  Store the public key in the device's Notehub project (e.g., as a
 *     device environment variable `pub_key_hex`) for downstream route use.
 *
 * Verification (downstream, e.g., in a Notehub route or Lambda function)
 * -----------------------------------------------------------------------
 * Using the full 64-byte signature from seal_sig_full.qo:
 *
 * 1.  Retrieve the seal_sig_full.qo note whose event_time matches the
 *     seal_event.qo event_time (for door/wire-break events) or the
 *     seal_heartbeat.qo heartbeat_time (for heartbeat events).  The full
 *     64-byte ECDSA P-256 signature is stored as sig_r (32-byte R component,
 *     64 hex chars) and sig_s (32-byte S component, 64 hex chars).
 *
 * 2.  Reconstruct the canonical payload from the seal_event.qo body fields
 *     (sealSignEvent payload, 19 bytes):
 *       payload[ 0.. 3] = event_time  (big-endian uint32)
 *       payload[ 4]     = event_type  (1=door-open, 2=door-close, 3=wire-break)
 *       payload[ 5]     = seal_broken (0x01 if true, else 0x00)
 *       payload[ 6]     = door_open   (0x01 if door was open, 0x00 if closed)
 *       payload[ 7..10] = breaches    (big-endian uint32)
 *       payload[11..14] = lroundf(event_lat × 1e6) as big-endian int32
 *                         (0x00000000 if event_lat was absent / 0.0)
 *       payload[15..18] = lroundf(event_lon × 1e6) as big-endian int32
 *                         (0x00000000 if event_lon was absent / 0.0)
 *
 * 3.  Compute SHA-256(payload) → 32-byte digest.
 *
 * 4.  Verify the ECDSA P-256 signature (R‖S, 64 bytes) against the digest
 *     using the device's recorded public key.  A pass confirms the event was
 *     generated by the provisioned secure element and the payload has not
 *     been altered.
 *
 * 5.  Quick tamper-detection (compact-only satellite path, no sig_full yet):
 *     Compare the 4-byte `sig` field against the first 4 bytes of the
 *     R-component from any previously received full signature for the same
 *     device.  A mismatch is a strong indicator of corruption or tampering;
 *     a match is only a weak probabilistic hint.  FOUR BYTES ARE NOT
 *     SUFFICIENT TO PERFORM ECDSA VERIFICATION — the `sig` field is a
 *     satellite-bandwidth optimisation and correlation aid, not a
 *     cryptographic verification artifact.  Always use the full 64-byte
 *     signature from seal_sig_full.qo for forensic chain-of-custody.
 *
 * Dependencies
 * ------------
 *   SparkFun_ATECCX08a_Arduino_Library  (install via Arduino Library Manager)
 *     → provides both the ATECC608A driver and the on-chip SHA-256 helper
 *       used to hash the canonical payload before signing.
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>

// ---------------------------------------------------------------------------
// Event type constants — byte 4 of the canonical signing payload
// Do NOT change these after provisioning; they are baked into issued sigs.
// ---------------------------------------------------------------------------
#define SEAL_EVENT_DOOR_OPEN   0x01U  // door-open (breach) event
#define SEAL_EVENT_DOOR_CLOSE  0x02U  // door-close (re-seal) event
#define SEAL_EVENT_WIRE_BREAK  0x03U  // seal-wire-break (door state independent)
#define SEAL_EVENT_HEARTBEAT   0x04U  // periodic health/waypoint note

// ---------------------------------------------------------------------------
// ATECC608A key slot used for event signing.
// Slot 0 is an ECC P-256 key pair slot per the SparkFun default configuration.
// Do not change after provisioning — the generated key pair is permanently
// bound to this slot and cannot be moved.
// ---------------------------------------------------------------------------
#define SEAL_SIGN_KEY_SLOT 0

// ---------------------------------------------------------------------------
// sealSignBegin
// ---------------------------------------------------------------------------
/**
 * Initialize the ATECC608A over the shared I²C bus.
 * Must be called after Wire.begin() (which the Notecard library calls
 * internally before I²C transactions begin).  Call once on first boot and
 * once on each resumed wake (chip state is not preserved across host power-off).
 *
 * Returns true if the ATECC608A was found and responded at 0x60.
 * Returns false if the chip is absent, not wired, or failed to wake.
 *
 * Non-fatal failure path: if this returns false, sealSignEvent() and
 * sealSignHeartbeat() return sig=0 on every call.  Downstream routes should
 * alert on sig==0 for provisioned devices.
 */
bool sealSignBegin(void);

// ---------------------------------------------------------------------------
// sealSignEvent
// ---------------------------------------------------------------------------
/**
 * Sign a door-event or seal-wire-break payload using the ATECC608A ECC P-256
 * private key in SEAL_SIGN_KEY_SLOT.
 *
 * Parameters:
 *   eventTime   Unix epoch at detection time (seconds).
 *   eventType   SEAL_EVENT_DOOR_OPEN, SEAL_EVENT_DOOR_CLOSE, or
 *               SEAL_EVENT_WIRE_BREAK.
 *   sealBroken  true for seal-wire-break events; false for door events.
 *   doorOpen    true if the door was open at the moment of the event.
 *               Included in the signed payload for all event types so the
 *               "seal cut while door closed" chain-of-custody claim is
 *               cryptographically bound to the signature.
 *   breaches    Cumulative door-open breach count at detection time.
 *   hasLoc      true if lat/lon are a valid GPS fix; false if absent.
 *   lat         GPS latitude at detection time.  Encoded as
 *               lroundf(lat × 1 000 000) as big-endian int32 in the payload;
 *               0x00000000 is encoded when hasLoc is false.
 *   lon         GPS longitude at detection time.  Same encoding as lat.
 *   sigFull     If non-NULL, the full 64-byte ECDSA P-256 signature (R‖S)
 *               is written here.  Buffer must be at least 64 bytes.
 *               Ignored when NULL.  Capture this immediately — re-calling
 *               sealSignEvent produces a new, independently valid signature
 *               (ECDSA uses a per-signature random nonce).
 *
 * Returns the first 4 bytes of the ECDSA signature as a uint32 for the
 * compact note body `sig` field.  Returns 0 if the ATECC608A is unavailable,
 * not provisioned, or the signing call fails.
 */
uint32_t sealSignEvent(uint32_t eventTime, uint8_t eventType,
                       bool sealBroken, bool doorOpen,
                       uint32_t breaches,
                       bool hasLoc, float lat, float lon,
                       uint8_t *sigFull);

// ---------------------------------------------------------------------------
// sealSignHeartbeat
// ---------------------------------------------------------------------------
/**
 * Sign a periodic heartbeat payload using the ATECC608A ECC P-256 private
 * key in SEAL_SIGN_KEY_SLOT.
 *
 * Parameters:
 *   hbTime      Unix epoch at heartbeat creation time (seconds).
 *   doorOpen    Current door state: true = open/breach, false = closed.
 *   sealIntact  Current seal-wire state: true = intact, false = broken.
 *   breaches    Cumulative door-open breach count at heartbeat time.
 *   sigFull     If non-NULL, the full 64-byte ECDSA P-256 signature (R‖S)
 *               is written here.  Buffer must be at least 64 bytes.
 *
 * Returns the first 4 bytes of the ECDSA signature as a uint32 for the
 * compact heartbeat body `sig` field.  Returns 0 if the ATECC608A is
 * unavailable.
 */
uint32_t sealSignHeartbeat(uint32_t hbTime, bool doorOpen, bool sealIntact,
                           uint32_t breaches, uint8_t *sigFull);

// ---------------------------------------------------------------------------
// sealSignAvailable
// ---------------------------------------------------------------------------
/**
 * Returns true if the ATECC608A was successfully initialised by sealSignBegin
 * and is ready to sign events.  Returns false if the chip is absent or failed
 * to initialise, in which case all signing calls return sig=0.
 */
bool sealSignAvailable(void);
