/*
 * container_seal_sign.cpp
 *
 * Hardware-rooted ECDSA event signing for the Cross-Border Container
 * Electronic Security Seal.  See container_seal_sign.h for architecture,
 * canonical payload specification, provisioning instructions, and downstream
 * verification guidance.
 *
 * Dependencies (install via Arduino Library Manager):
 *   SparkFun_ATECCX08a_Arduino_Library  — ATECC608A driver (also provides
 *                                         on-chip SHA-256 used to hash the
 *                                         canonical payload before signing)
 */

#include "container_seal_sign.h"
#include <SparkFun_ATECCX08a_Arduino_Library.h>
#include <math.h>     // lroundf

static ATECCX08A g_atecc;
static bool      g_available = false;

// ---------------------------------------------------------------------------
// sealSignBegin
// ---------------------------------------------------------------------------
bool sealSignBegin(void) {
    // begin() wakes the ATECC608A and verifies I²C communication.
    // Returns true only if the device is present and responsive.
    // The shared Wire bus (I²C) must already be started; the Notecard library
    // calls Wire.begin() as part of notecard.begin(), so this is safe to call
    // after notecard.begin() in setup().
    g_available = g_atecc.begin(0x60);
    if (!g_available) {
        Serial.println("[seal] WARN: ATECC608A not found at 0x60 — sig field will be 0");
    }
    return g_available;
}

// ---------------------------------------------------------------------------
// Internal: sign a pre-built payload buffer
// Shared by sealSignEvent and sealSignHeartbeat.
// ---------------------------------------------------------------------------
static uint32_t signPayload(const uint8_t *payload, size_t len,
                             uint8_t *sigFull) {
    // ---- Compute SHA-256 of canonical payload -----------------------------
    // The ATECC608A's createSignature() expects a 32-byte digest, not raw
    // data.  We use the chip's on-chip SHA-256 engine via the SparkFun
    // library's sha256() helper so the same secure element that holds the
    // private key also produces the digest — no extra software-crypto
    // dependency required.
    uint8_t hash[32];
    if (!g_atecc.sha256((uint8_t *)payload, len, hash)) {
        Serial.println("[seal] WARN: ATECC608A sha256 failed — sig will be 0");
        if (sigFull) {
            memset(sigFull, 0, 64);
        }
        return 0;
    }

    // ---- Sign with ATECC608A ECC P-256 private key in slot 0 --------------
    // createSignature(data, slot):
    //   data — 32-byte SHA-256 digest loaded into TempKey, then signed
    //   slot — ATECC608A key slot holding the private ECC key
    // The 64-byte ECDSA signature (R‖S, big-endian) is written to the public
    // member g_atecc.signature[].  Returns true on success; false if the chip
    // is not provisioned (config or data zone not locked), the slot does not
    // hold an ECC key, or an I²C communication error occurred.
    if (!g_atecc.createSignature(hash, SEAL_SIGN_KEY_SLOT)) {
        Serial.println("[seal] WARN: ATECC608A createSignature failed — sig will be 0");
        if (sigFull) {
            memset(sigFull, 0, 64);
        }
        return 0;
    }
    const uint8_t *sig64 = g_atecc.signature;

    // Copy full signature to caller buffer if requested.
    if (sigFull) {
        memcpy(sigFull, sig64, 64);
    }

    // ---- Return 4-byte prefix for compact note field ----------------------
    // The first 4 bytes of the R-component serve as the satellite-efficient
    // `sig` field.  This prefix is a weak tamper indicator only — it is NOT
    // sufficient for ECDSA verification.  Use the full 64-byte signature from
    // seal_sig_full.qo for forensic chain-of-custody verification.
    return ((uint32_t)sig64[0] << 24) | ((uint32_t)sig64[1] << 16) |
           ((uint32_t)sig64[2] <<  8) |  (uint32_t)sig64[3];
}

// ---------------------------------------------------------------------------
// sealSignEvent
// ---------------------------------------------------------------------------
uint32_t sealSignEvent(uint32_t eventTime, uint8_t eventType,
                       bool sealBroken, bool doorOpen,
                       uint32_t breaches,
                       bool hasLoc, float lat, float lon,
                       uint8_t *sigFull) {
    if (!g_available) {
        if (sigFull) {
            memset(sigFull, 0, 64);
        }
        return 0;
    }

    // ---- Encode lat/lon as big-endian int32 (lroundf(degrees × 1 000 000)) -
    // lroundf rounds half-away-from-zero, matching the documented canonical
    // encoding rule.  A plain cast (int32_t)(val * 1e6) truncates toward zero
    // and would produce a different result for values like 1.3518°, causing
    // signature verification failures.  0x00000000 is used when no GPS fix
    // was available (hasLoc == false).
    int32_t latFixed = hasLoc ? (int32_t)lroundf(lat * 1000000.0f) : 0;
    int32_t lonFixed = hasLoc ? (int32_t)lroundf(lon * 1000000.0f) : 0;

    // ---- Build canonical 19-byte payload ----------------------------------
    // Layout is fixed and versioned by these constants; changing it
    // invalidates all previously issued signatures.
    // See container_seal_sign.h for the complete byte-order specification
    // and downstream verification procedure.
    uint8_t payload[19];
    // bytes 0–3: event_time (BE uint32)
    payload[0] = (uint8_t)((eventTime >> 24) & 0xFF);
    payload[1] = (uint8_t)((eventTime >> 16) & 0xFF);
    payload[2] = (uint8_t)((eventTime >>  8) & 0xFF);
    payload[3] = (uint8_t)( eventTime        & 0xFF);
    // byte 4: event_type
    payload[4] = eventType;
    // byte 5: seal_broken
    payload[5] = sealBroken ? 0x01 : 0x00;
    // byte 6: door_open — cryptographically binds door state for all event
    // types, including WIRE_BREAK where the door was still closed
    payload[6] = doorOpen ? 0x01 : 0x00;
    // bytes 7–10: breaches (BE uint32)
    payload[7]  = (uint8_t)((breaches >> 24) & 0xFF);
    payload[8]  = (uint8_t)((breaches >> 16) & 0xFF);
    payload[9]  = (uint8_t)((breaches >>  8) & 0xFF);
    payload[10] = (uint8_t)( breaches        & 0xFF);
    // bytes 11–14: event_lat (BE int32, lroundf(degrees × 1e6))
    payload[11] = (uint8_t)(((uint32_t)latFixed >> 24) & 0xFF);
    payload[12] = (uint8_t)(((uint32_t)latFixed >> 16) & 0xFF);
    payload[13] = (uint8_t)(((uint32_t)latFixed >>  8) & 0xFF);
    payload[14] = (uint8_t)( (uint32_t)latFixed        & 0xFF);
    // bytes 15–18: event_lon (BE int32, lroundf(degrees × 1e6))
    payload[15] = (uint8_t)(((uint32_t)lonFixed >> 24) & 0xFF);
    payload[16] = (uint8_t)(((uint32_t)lonFixed >> 16) & 0xFF);
    payload[17] = (uint8_t)(((uint32_t)lonFixed >>  8) & 0xFF);
    payload[18] = (uint8_t)( (uint32_t)lonFixed        & 0xFF);

    return signPayload(payload, sizeof(payload), sigFull);
}

// ---------------------------------------------------------------------------
// sealSignHeartbeat
// ---------------------------------------------------------------------------
uint32_t sealSignHeartbeat(uint32_t hbTime, bool doorOpen, bool sealIntact,
                            uint32_t breaches, uint8_t *sigFull) {
    if (!g_available) {
        if (sigFull) {
            memset(sigFull, 0, 64);
        }
        return 0;
    }

    // ---- Build canonical 11-byte heartbeat payload ------------------------
    // bytes 0–3: heartbeat_time (BE uint32)
    // byte  4:   SEAL_EVENT_HEARTBEAT (0x04)
    // byte  5:   door open flag (0x01=open, 0x00=closed)
    // byte  6:   seal intact flag (0x01=intact, 0x00=broken)
    // bytes 7–10: breaches (BE uint32)
    uint8_t payload[11];
    payload[0] = (uint8_t)((hbTime >> 24) & 0xFF);
    payload[1] = (uint8_t)((hbTime >> 16) & 0xFF);
    payload[2] = (uint8_t)((hbTime >>  8) & 0xFF);
    payload[3] = (uint8_t)( hbTime        & 0xFF);
    payload[4] = SEAL_EVENT_HEARTBEAT;
    payload[5] = doorOpen   ? 0x01 : 0x00;
    payload[6] = sealIntact ? 0x01 : 0x00;
    payload[7] = (uint8_t)((breaches >> 24) & 0xFF);
    payload[8] = (uint8_t)((breaches >> 16) & 0xFF);
    payload[9] = (uint8_t)((breaches >>  8) & 0xFF);
    payload[10]= (uint8_t)( breaches        & 0xFF);

    return signPayload(payload, sizeof(payload), sigFull);
}

// ---------------------------------------------------------------------------
// sealSignAvailable
// ---------------------------------------------------------------------------
bool sealSignAvailable(void) {
    return g_available;
}
