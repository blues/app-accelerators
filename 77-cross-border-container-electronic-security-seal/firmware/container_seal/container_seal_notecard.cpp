/*
 * container_seal_notecard.cpp
 *
 * Notecard API implementation for the Cross-Border Container Electronic
 * Security Seal.  Implements every Notecard helper declared in
 * container_seal_notecard.h; container_seal.ino contains only application
 * logic.
 */

#include "container_seal_notecard.h"
#include "container_seal_sign.h"  // event type constants for sealSendSigFull
#include <stdlib.h>  // atol, atof

// ---------------------------------------------------------------------------
// note.add retry policy
// ---------------------------------------------------------------------------
// A security-seal device must not silently drop breach or seal-break events
// on transient I2C faults.  Two immediate re-attempts with a short
// inter-attempt delay catch the overwhelming majority of transient bus
// glitches without adding perceptible latency to the wake cycle (~50 ms
// total overhead on failure).  If all attempts fail the caller enqueues the
// pending event in SealState so it is retried on the next wake — see the
// pending ring-buffer logic in container_seal.ino.
//
// The companion seal_sig_full.qo note uses the same retry policy.  If all
// retries fail there is no secondary pending queue for signature notes; a
// loss is warned on Serial.  Route consumers should alert if a sealed event
// with non-zero sig has no matching seal_sig_full.qo after cellular sync.
#define NOTE_ADD_RETRIES        2    // additional attempts after the first try
#define NOTE_ADD_RETRY_DELAY_MS 25   // ms between attempts

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------

// Clamp a uint32 to a valid range; leave value unchanged if out of range.
static bool clampU32(uint32_t val, uint32_t lo, uint32_t hi, uint32_t *out) {
    if (val >= lo && val <= hi) { *out = val; return true; }
    return false;
}

// Send req via requestAndResponse, check for a Notecard error field, free
// the response, and return true on clean acceptance.  Consumes req.
// label is used only in the error log — pass the request name as a string.
static bool sendAndCheck(Notecard &nc, J *req, const char *label) {
    J *rsp = nc.requestAndResponse(req);
    if (rsp == NULL) {
        Serial.print("[seal] No response from Notecard for ");
        Serial.println(label);
        return false;
    }
    const char *err = JGetString(rsp, "err");
    if (err && *err) {
        Serial.print("[seal] ");
        Serial.print(label);
        Serial.print(" error: ");
        Serial.println(err);
        nc.deleteResponse(rsp);
        return false;
    }
    nc.deleteResponse(rsp);
    return true;
}

// ---------------------------------------------------------------------------
// sealConfigureNotecard
// ---------------------------------------------------------------------------
bool sealConfigureNotecard(Notecard &nc, const char *productUID,
                           const SealConfig &cfg) {
    // hub.set — periodic mode with satellite-appropriate outbound/inbound
    // cadences.  sendRequestWithRetry papers over the cold-boot I2C race
    // condition where the Swan comes up slightly before the Notecard's bus
    // is ready; it retries for up to 5 seconds before returning false.
    J *req = nc.newRequest("hub.set");
    if (productUID && productUID[0]) {
        JAddStringToObject(req, "product", productUID);
    }
    JAddStringToObject(req, "mode", "periodic");
    JAddNumberToObject(req, "outbound", (int)cfg.outboundMin);
    JAddNumberToObject(req, "inbound",  (int)cfg.inboundMin);
    if (!nc.sendRequestWithRetry(req, 5)) {
        Serial.println("[seal] hub.set failed");
        return false;
    }

    // Tell the Notecard this is a LiPo-powered device so its voltage-variable
    // sync behavior uses the correct discharge curve thresholds.
    // Non-critical — failure does not affect core event delivery.
    req = nc.newRequest("card.voltage");
    JAddStringToObject(req, "mode", "lipo");
    nc.sendRequest(req);

    // NOTE: card.motion.mode is intentionally NOT set to stop:true here.
    // card.location.mode periodic requires the Notecard's onboard accelerometer
    // to detect motion before triggering a GPS acquisition attempt.  Disabling
    // the accelerometer (stop:true) would prevent periodic GPS from acquiring
    // a position fix.  Ship/road vibration is sufficient to satisfy the motion
    // requirement throughout a voyage.  The accelerometer's idle current draw
    // is negligible in the overall power budget (~<1 µA).

    return true;
}

// ---------------------------------------------------------------------------
// sealDefineTemplates
// ---------------------------------------------------------------------------
bool sealDefineTemplates(Notecard &nc) {
    // ---- seal_event.qo : door breach, re-seal, seal-break, and audit-gap --
    // compact format required for satellite (Iridium SBD via Starnote) transmission.
    // port is required for NTN; values 1–100 are user-defined.
    //
    // Template field hints (note-c macro equivalents):
    //   true  → TBOOL    (1-byte boolean)
    //   24    → TUINT32  (4-byte unsigned integer)
    //   12.1  → TFLOAT16 (2-byte IEEE 754 float, ~3 decimal digits)
    //   14.1  → TFLOAT32 (4-byte IEEE 754 float) — used for lat/lon precision
    //   14    → TINT32   (4-byte signed integer) — used for Unix timestamp
    //
    // _lat, _lon, _time are compact-template reserved keywords: the Notecard
    // automatically fills them from its GPS fix and RTC on each note.add.
    //
    // event_time carries the Unix epoch at the moment the transition was
    // detected by the MCU.  For notes delivered immediately it matches _time
    // closely.  For notes delivered via the pending-retry path on a later
    // wake, event_time is the detection epoch while _time is the retry epoch.
    //
    // event_lat/event_lon carry the GPS fix snapshotted at detection time.
    // They are 0.0 when no fix was available; downstream consumers should
    // check both fields before use.
    //
    // seal_broken is false/absent in normal door events and true only in
    // seal-wire-break events.
    //
    // sig carries the first 4 bytes of the ATECC608A ECDSA P-256 signature
    // over the 19-byte canonical payload.  0 when the ATECC608A is absent or
    // not provisioned.  Full 64-byte signature is in the companion
    // seal_sig_full.qo note (correlated by event_time).
    //
    // audit_gap is false/absent in normal events and true only in
    // audit-gap indicator notes emitted after a pending-queue overflow.
    {
        J *req = nc.newRequest("note.template");
        JAddStringToObject(req, "file", "seal_event.qo");
        JAddNumberToObject(req, "port", 1);
        JAddStringToObject(req, "format", "compact");
        J *body = JAddObjectToObject(req, "body");
        JAddBoolToObject(body,   "open",        true);   // door-open state
        JAddNumberToObject(body, "breaches",    24);     // cumulative breach count (uint32)
        JAddNumberToObject(body, "batt_v",      12.1);   // battery voltage (V); -1.0 = unavailable
        JAddNumberToObject(body, "event_time",  14);     // detection epoch (int32)
        JAddNumberToObject(body, "event_lat",   14.1);   // GPS lat at detection time (0.0 = no fix)
        JAddNumberToObject(body, "event_lon",   14.1);   // GPS lon at detection time (0.0 = no fix)
        JAddBoolToObject(body,   "seal_broken", true);   // true only in seal-wire-break events
        JAddNumberToObject(body, "sig",         24);     // 4-byte ECDSA sig prefix (uint32); 0 = unsigned
        JAddBoolToObject(body,   "audit_gap",   true);   // true only in overflow-gap notes
        JAddNumberToObject(body, "_lat",        14.1);   // GPS latitude at note.add time (auto)
        JAddNumberToObject(body, "_lon",        14.1);   // GPS longitude at note.add time (auto)
        JAddNumberToObject(body, "_time",       14);     // note.add epoch (auto)
        if (!sendAndCheck(nc, req, "note.template seal_event.qo")) {
            return false;
        }
    }

    // ---- seal_heartbeat.qo : periodic waypoints and health checks ---------
    // seal_intact is the current state of the seal-wire continuity circuit
    // at heartbeat creation time.  false here means the wire is already
    // broken; operators should cross-check against seal_event.qo for the
    // corresponding seal_broken:true event.
    //
    // heartbeat_time is the Unix epoch at heartbeat creation time.  It is
    // written explicitly into the note body so downstream consumers can
    // correlate the companion seal_sig_full.qo note (where it appears as
    // event_time) without relying on the auto-populated _time field (which
    // reflects the note.add time, not the detection time).
    //
    // sig carries the first 4 bytes of the ATECC608A ECDSA P-256 signature
    // over the 11-byte heartbeat canonical payload.
    {
        J *req = nc.newRequest("note.template");
        JAddStringToObject(req, "file", "seal_heartbeat.qo");
        JAddNumberToObject(req, "port", 2);
        JAddStringToObject(req, "format", "compact");
        J *body = JAddObjectToObject(req, "body");
        JAddBoolToObject(body,   "open",           true);  // current door state
        JAddBoolToObject(body,   "seal_intact",    true);  // seal-wire state (true = intact)
        JAddNumberToObject(body, "breaches",       24);    // total breach count (uint32)
        JAddNumberToObject(body, "batt_v",         12.1);  // battery voltage (V)
        JAddNumberToObject(body, "heartbeat_time", 14);    // creation epoch (int32) — correlation key
        JAddNumberToObject(body, "sig",            24);    // 4-byte ECDSA heartbeat sig prefix
        JAddNumberToObject(body, "_lat",           14.1);  // GPS latitude (auto)
        JAddNumberToObject(body, "_lon",           14.1);  // GPS longitude (auto)
        JAddNumberToObject(body, "_time",          14);    // heartbeat epoch (auto)
        if (!sendAndCheck(nc, req, "note.template seal_heartbeat.qo")) {
            return false;
        }
    }

    return true;
}

// ---------------------------------------------------------------------------
// sealConfigureGPS
// ---------------------------------------------------------------------------
bool sealConfigureGPS(Notecard &nc, uint32_t periodSeconds) {
    // Set GPS to periodic mode.  Per Blues documentation, card.location.mode
    // periodic triggers a GPS acquisition attempt every `seconds` seconds ONLY
    // when the Notecard's onboard accelerometer has detected motion during that
    // period.  For a container in transit, ship hull vibration and road motion
    // are sufficient to satisfy the motion requirement.  The accelerometer is
    // intentionally left active (not disabled with card.motion.mode stop:true)
    // so that this motion-gating mechanism functions correctly — see
    // sealConfigureNotecard() for the explanation.  The acquired position is
    // injected into compact-template notes as _lat/_lon.
    J *req = nc.newRequest("card.location.mode");
    JAddStringToObject(req, "mode", "periodic");
    JAddNumberToObject(req, "seconds", (int)periodSeconds);
    return sendAndCheck(nc, req, "card.location.mode");
}

// ---------------------------------------------------------------------------
// sealFetchEnvVars
// ---------------------------------------------------------------------------
SealEnvResult sealFetchEnvVars(Notecard &nc, SealConfig &cfg) {
    // Retrieve ALL environment variables in one round-trip.
    // env.get reads the Notecard's local cache — no network round-trip occurs.
    // The Notecard only has updated values after it completes an inbound sync
    // with Notehub, so the MCU calling this on every wake picks up new values
    // on the very first wake after a Notecard inbound window.
    J *rsp = nc.requestAndResponse(nc.newRequest("env.get"));
    if (rsp == NULL) return SEAL_ENV_FAIL;

    // Treat a Notecard-side error the same as a NULL response.
    const char *envErr = JGetString(rsp, "err");
    if (envErr && *envErr) {
        Serial.print("[seal] env.get error: ");
        Serial.println(envErr);
        nc.deleteResponse(rsp);
        return SEAL_ENV_FAIL;
    }

    J *body = JGetObjectItem(rsp, "body");
    bool anyChange = false;

    if (body != NULL) {
        const char *v;
        uint32_t    prev;

        // check_interval_sec: 5 s – 300 s
        v = JGetString(body, "check_interval_sec");
        if (v && *v) {
            prev = cfg.checkIntervalSec;
            clampU32((uint32_t)atol(v), 5, 300, &cfg.checkIntervalSec);
            if (cfg.checkIntervalSec != prev) anyChange = true;
        }

        // heartbeat_interval_min: 15 min – 1440 min (1 day)
        v = JGetString(body, "heartbeat_interval_min");
        if (v && *v) {
            prev = cfg.heartbeatIntervalMin;
            clampU32((uint32_t)atol(v), 15, 1440, &cfg.heartbeatIntervalMin);
            if (cfg.heartbeatIntervalMin != prev) anyChange = true;
        }

        // outbound_min: 60 min – 1440 min
        v = JGetString(body, "outbound_min");
        if (v && *v) {
            prev = cfg.outboundMin;
            clampU32((uint32_t)atol(v), 60, 1440, &cfg.outboundMin);
            if (cfg.outboundMin != prev) anyChange = true;
        }

        // inbound_min: 60 min – 10080 min (7 days)
        v = JGetString(body, "inbound_min");
        if (v && *v) {
            prev = cfg.inboundMin;
            clampU32((uint32_t)atol(v), 60, 10080, &cfg.inboundMin);
            if (cfg.inboundMin != prev) anyChange = true;
        }
    }

    nc.deleteResponse(rsp);
    return anyChange ? SEAL_ENV_OK : SEAL_ENV_NO_CHANGE;
}

// ---------------------------------------------------------------------------
// sealApplyCadence
// ---------------------------------------------------------------------------
bool sealApplyCadence(Notecard &nc, uint32_t outboundMin, uint32_t inboundMin) {
    // Send both outbound and inbound in one hub.set so the Notecard's sync
    // schedule is updated atomically.  Splitting into two requests would
    // leave a window where outbound is new but inbound is still old (or vice
    // versa), which can confuse the Notecard's sync planner on devices using
    // a custom inbound cadence for fast env-var delivery.
    J *req = nc.newRequest("hub.set");
    JAddStringToObject(req, "mode", "periodic");
    JAddNumberToObject(req, "outbound", (int)outboundMin);
    JAddNumberToObject(req, "inbound",  (int)inboundMin);
    return sendAndCheck(nc, req, "hub.set cadence");
}

// ---------------------------------------------------------------------------
// Internal: single note.add attempt
// ---------------------------------------------------------------------------
// Consumes req.  Classifies the outcome:
//   NOTE_ADD_OK        — Notecard accepted (no err in response).
//   NOTE_ADD_TRANSIENT — NULL response or I2C failure; retry is appropriate.
//   NOTE_ADD_PERMANENT — Notecard returned a non-empty err field.  The most
//                        common causes are a compact-template field-type
//                        mismatch or a missing template registration.
static NoteAddResult noteAddOnce(Notecard &nc, J *req) {
    J *rsp = nc.requestAndResponse(req);
    if (rsp == NULL) return NOTE_ADD_TRANSIENT;
    const char *err = JGetString(rsp, "err");
    if (err && *err) {
        Serial.print("[seal] note.add rejected: ");
        Serial.println(err);
        nc.deleteResponse(rsp);
        return NOTE_ADD_PERMANENT;
    }
    nc.deleteResponse(rsp);
    return NOTE_ADD_OK;
}

// ---------------------------------------------------------------------------
// sealSendSigFull
// ---------------------------------------------------------------------------
NoteAddResult sealSendSigFull(Notecard &nc, uint32_t eventEpoch,
                              uint8_t eventType,
                              const uint8_t *sigFull64) {
    if (sigFull64 == NULL) return NOTE_ADD_OK;

    // If the signature is all-zeros the ATECC608A was unavailable — no
    // companion note is created.  The sig==0 value in the main event note
    // is the only indicator in that case.
    bool allZero = true;
    for (int i = 0; i < 64; i++) {
        if (sigFull64[i]) { allZero = false; break; }
    }
    if (allZero) return NOTE_ADD_OK;

    // Encode R (bytes 0–31) and S (bytes 32–63) as 64-character hex strings.
    // Using lowercase hex for consistent parsing downstream.
    static const char kHex[] = "0123456789abcdef";
    char sigR[65] = {0};
    char sigS[65] = {0};
    for (int i = 0; i < 32; i++) {
        sigR[i * 2]     = kHex[sigFull64[i]      >> 4];
        sigR[i * 2 + 1] = kHex[sigFull64[i]      & 0xF];
        sigS[i * 2]     = kHex[sigFull64[i + 32] >> 4];
        sigS[i * 2 + 1] = kHex[sigFull64[i + 32] & 0xF];
    }

    // seal_sig_full.qo is free-form JSON — no template needed, not compact,
    // so it will not be transmitted over the Iridium satellite link (which
    // requires compact-format notes with a port number).  It delivers over
    // cellular at the next outbound sync.  eventEpoch and eventType are the correlation keys that
    // tie this note back to its corresponding seal_event.qo or
    // seal_heartbeat.qo note.  For heartbeat companion notes, eventEpoch is
    // the heartbeat_time field value from the heartbeat note body.
    //
    // Same retry policy as primary event notes: up to NOTE_ADD_RETRIES+1
    // attempts.  On permanent rejection or exhausted retries the companion
    // note is silently lost — there is no secondary pending queue for
    // signature notes.
    for (int attempt = 0; attempt <= NOTE_ADD_RETRIES; attempt++) {
        if (attempt > 0) {
            delay(NOTE_ADD_RETRY_DELAY_MS);
            Serial.print("[seal] note.add seal_sig_full.qo retry ");
            Serial.println(attempt);
        }

        J *req = nc.newRequest("note.add");
        if (req == NULL) continue;  // OOM — try again on next iteration
        JAddStringToObject(req, "file", "seal_sig_full.qo");
        // No sync:true — deliver at the next outbound sync over cellular.
        J *body = JAddObjectToObject(req, "body");
        JAddNumberToObject(body, "event_time",  (double)eventEpoch);
        JAddNumberToObject(body, "event_type",  (double)eventType);
        JAddStringToObject(body, "sig_r",       sigR);
        JAddStringToObject(body, "sig_s",       sigS);

        NoteAddResult r = noteAddOnce(nc, req);
        if (r == NOTE_ADD_OK) return NOTE_ADD_OK;
        if (r == NOTE_ADD_PERMANENT) {
            Serial.println("[seal] WARN: seal_sig_full.qo permanently rejected; full sig not queued");
            return NOTE_ADD_PERMANENT;
        }
        // NOTE_ADD_TRANSIENT — continue to next retry
    }

    Serial.println("[seal] WARN: seal_sig_full.qo failed after all retries; full sig not queued");
    return NOTE_ADD_TRANSIENT;
}

// ---------------------------------------------------------------------------
// sealSendDoorEvent
// ---------------------------------------------------------------------------
NoteAddResult sealSendDoorEvent(Notecard &nc, bool doorOpen,
                                uint32_t totalBreaches, float battV,
                                uint32_t eventEpoch,
                                float eventLat, float eventLon,
                                bool hasLocation,
                                bool sealBroken,
                                uint32_t sig,
                                const uint8_t *sigFull) {
    // sync:true requests an immediate outbound transmission.  Over cellular
    // (in port) this fires within seconds.  Over Iridium satellite (at sea)
    // the Notecard queues the note, powers up the satellite modem via the
    // Starnote, and transmits once an Iridium session can be established.
    //
    // Template recovery: on the first permanent rejection, sealDefineTemplates()
    // is called to re-register templates and the same attempt is retried once.
    bool templateRecoveryDone = false;

    for (int attempt = 0; attempt <= NOTE_ADD_RETRIES; attempt++) {
        if (attempt > 0) {
            delay(NOTE_ADD_RETRY_DELAY_MS);
            Serial.print("[seal] note.add seal_event.qo retry ");
            Serial.println(attempt);
        }

        J *req = nc.newRequest("note.add");
        if (req == NULL) continue;  // OOM — try again on next iteration

        JAddStringToObject(req, "file", "seal_event.qo");
        JAddBoolToObject(req,   "sync", true);
        // full:true preserves false boolean values (omitempty would drop them),
        // ensuring a re-seal event shows open:false rather than omitting it.
        JAddBoolToObject(req,   "full", true);
        J *body = JAddObjectToObject(req, "body");
        JAddBoolToObject(body,   "open",       doorOpen);
        JAddNumberToObject(body, "breaches",   (double)totalBreaches);
        JAddNumberToObject(body, "batt_v",     battV);
        // event_time carries the detection-epoch snapshot.
        if (eventEpoch != 0) {
            JAddNumberToObject(body, "event_time", (double)eventEpoch);
        }
        // event_lat/event_lon: only include when a valid GPS fix was available.
        if (hasLocation) {
            JAddNumberToObject(body, "event_lat", (double)eventLat);
            JAddNumberToObject(body, "event_lon", (double)eventLon);
        }
        // seal_broken: only set in seal-wire-break events; omit from normal
        // door-open/door-close events to save satellite bytes.
        if (sealBroken) {
            JAddBoolToObject(body, "seal_broken", true);
        }
        // sig: always present; 0 signals that the ATECC608A was unavailable.
        JAddNumberToObject(body, "sig", (double)sig);
        // audit_gap is omitted from normal door events; only sealSendAuditGap
        // sets it.

        NoteAddResult r = noteAddOnce(nc, req);
        if (r == NOTE_ADD_OK) {
            // Queue the full 64-byte signature companion note for cellular
            // delivery.  Derive event type from doorOpen/sealBroken flags.
            uint8_t evType = sealBroken ? SEAL_EVENT_WIRE_BREAK
                                        : (doorOpen ? SEAL_EVENT_DOOR_OPEN
                                                    : SEAL_EVENT_DOOR_CLOSE);
            sealSendSigFull(nc, eventEpoch, evType, sigFull);
            return NOTE_ADD_OK;
        }

        if (r == NOTE_ADD_PERMANENT && !templateRecoveryDone) {
            Serial.println("[seal] Permanent note.add rejection; attempting template re-registration");
            templateRecoveryDone = true;
            if (sealDefineTemplates(nc)) {
                attempt--;  // cancel loop increment — re-run same slot after recovery
            }
            continue;
        }

        if (r == NOTE_ADD_PERMANENT) {
            Serial.println("[seal] note.add seal_event.qo permanently rejected after template recovery");
            return NOTE_ADD_PERMANENT;
        }

        // NOTE_ADD_TRANSIENT — continue to next retry attempt
    }

    Serial.println("[seal] note.add seal_event.qo failed after all retries");
    return NOTE_ADD_TRANSIENT;
}

// ---------------------------------------------------------------------------
// sealSendHeartbeat
// ---------------------------------------------------------------------------
NoteAddResult sealSendHeartbeat(Notecard &nc, bool doorOpen,
                                uint32_t totalBreaches, float battV,
                                bool sealIntact,
                                uint32_t sig,
                                const uint8_t *sigFull,
                                uint32_t hbTime) {
    // No sync:true — heartbeats ride the normal outbound cadence to conserve
    // satellite link budget.
    //
    // seal_intact reflects the seal-wire continuity state at heartbeat time.
    // A persistent false here (without a corresponding seal_broken:true event
    // in seal_event.qo) may indicate the wire was cut before this firmware
    // version was deployed or the state was lost across a firmware upgrade.
    //
    // heartbeat_time is written into the note body so downstream consumers
    // can correlate the companion seal_sig_full.qo note (event_time in that
    // note equals heartbeat_time here) without relying on the auto-populated
    // _time field, which reflects the note.add time rather than creation time.
    bool templateRecoveryDone = false;

    for (int attempt = 0; attempt <= NOTE_ADD_RETRIES; attempt++) {
        if (attempt > 0) {
            delay(NOTE_ADD_RETRY_DELAY_MS);
            Serial.print("[seal] note.add seal_heartbeat.qo retry ");
            Serial.println(attempt);
        }

        J *req = nc.newRequest("note.add");
        if (req == NULL) continue;
        JAddStringToObject(req, "file", "seal_heartbeat.qo");
        JAddBoolToObject(req,   "full", true);
        J *body = JAddObjectToObject(req, "body");
        JAddBoolToObject(body,   "open",           doorOpen);
        JAddBoolToObject(body,   "seal_intact",    sealIntact);
        JAddNumberToObject(body, "breaches",       (double)totalBreaches);
        JAddNumberToObject(body, "batt_v",         battV);
        // heartbeat_time is the explicit creation epoch for this heartbeat.
        // Downstream consumers match seal_sig_full.qo.event_time to this field.
        if (hbTime != 0) {
            JAddNumberToObject(body, "heartbeat_time", (double)hbTime);
        }
        // sig: 4-byte prefix of the ATECC608A ECDSA P-256 signature over
        // the 11-byte heartbeat canonical payload.
        JAddNumberToObject(body, "sig", (double)sig);

        NoteAddResult r = noteAddOnce(nc, req);
        if (r == NOTE_ADD_OK) {
            // Queue the full 64-byte signature companion note, keyed by
            // hbTime so downstream consumers can correlate it to this
            // heartbeat via seal_sig_full.qo.event_time == heartbeat_time.
            sealSendSigFull(nc, hbTime, SEAL_EVENT_HEARTBEAT, sigFull);
            return NOTE_ADD_OK;
        }

        if (r == NOTE_ADD_PERMANENT && !templateRecoveryDone) {
            Serial.println("[seal] Permanent heartbeat rejection; attempting template re-registration");
            templateRecoveryDone = true;
            if (sealDefineTemplates(nc)) {
                attempt--;
            }
            continue;
        }

        if (r == NOTE_ADD_PERMANENT) {
            Serial.println("[seal] note.add seal_heartbeat.qo permanently rejected after template recovery");
            return NOTE_ADD_PERMANENT;
        }

        // NOTE_ADD_TRANSIENT — continue
    }

    Serial.println("[seal] note.add seal_heartbeat.qo failed after all retries");
    return NOTE_ADD_TRANSIENT;
}

// ---------------------------------------------------------------------------
// sealSendAuditGap
// ---------------------------------------------------------------------------
NoteAddResult sealSendAuditGap(Notecard &nc, uint32_t gapEpoch) {
    // Emits a chain-of-custody audit-gap indicator to seal_event.qo.
    // audit_gap:true distinguishes this note from normal door or seal-break
    // events so downstream Notehub routes can flag the gap without
    // treating it as a breach or re-seal.
    // Audit-gap notes carry no event payload to sign; no sig field is included.
    bool templateRecoveryDone = false;

    for (int attempt = 0; attempt <= NOTE_ADD_RETRIES; attempt++) {
        if (attempt > 0) {
            delay(NOTE_ADD_RETRY_DELAY_MS);
            Serial.print("[seal] note.add audit-gap retry ");
            Serial.println(attempt);
        }

        J *req = nc.newRequest("note.add");
        if (req == NULL) continue;
        JAddStringToObject(req, "file", "seal_event.qo");
        JAddBoolToObject(req,   "sync", true);
        J *body = JAddObjectToObject(req, "body");
        JAddBoolToObject(body, "audit_gap", true);
        if (gapEpoch != 0) {
            JAddNumberToObject(body, "event_time", (double)gapEpoch);
        }

        NoteAddResult r = noteAddOnce(nc, req);
        if (r == NOTE_ADD_OK) return NOTE_ADD_OK;

        if (r == NOTE_ADD_PERMANENT && !templateRecoveryDone) {
            Serial.println("[seal] Permanent audit-gap rejection; attempting template re-registration");
            templateRecoveryDone = true;
            if (sealDefineTemplates(nc)) {
                attempt--;
            }
            continue;
        }

        if (r == NOTE_ADD_PERMANENT) {
            Serial.println("[seal] note.add audit-gap permanently rejected after template recovery");
            return NOTE_ADD_PERMANENT;
        }
        // NOTE_ADD_TRANSIENT — continue
    }

    Serial.println("[seal] note.add audit-gap failed after all retries");
    return NOTE_ADD_TRANSIENT;
}

// ---------------------------------------------------------------------------
// sealGetEpoch
// ---------------------------------------------------------------------------
uint32_t sealGetEpoch(Notecard &nc) {
    J *rsp = nc.requestAndResponse(nc.newRequest("card.time"));
    uint32_t epoch = 0;
    if (rsp != NULL) {
        const char *err = JGetString(rsp, "err");
        if (!err || !*err) {
            epoch = (uint32_t)JGetInt(rsp, "time");
        }
        nc.deleteResponse(rsp);
    }

    if (epoch == 0) {
        // Notecard has no time yet (no cellular sync completed).
        // Fall back to millis() so timing logic is at least self-consistent
        // within a single wake cycle until the first session gives a real epoch.
        epoch = (uint32_t)(millis() / 1000UL);
    }
    return epoch;
}

// ---------------------------------------------------------------------------
// sealGetLocation
// ---------------------------------------------------------------------------
bool sealGetLocation(Notecard &nc, float *lat, float *lon) {
    J *rsp = nc.requestAndResponse(nc.newRequest("card.location"));
    if (rsp == NULL) return false;

    const char *err = JGetString(rsp, "err");
    if (err && *err) {
        nc.deleteResponse(rsp);
        return false;
    }

    // lat/lon are absent if no fix is available — check for field existence.
    J *latItem = JGetObjectItem(rsp, "lat");
    J *lonItem = JGetObjectItem(rsp, "lon");
    if (latItem == NULL || lonItem == NULL) {
        nc.deleteResponse(rsp);
        return false;
    }

    *lat = (float)JGetNumber(rsp, "lat");
    *lon = (float)JGetNumber(rsp, "lon");
    nc.deleteResponse(rsp);
    return true;
}

// ---------------------------------------------------------------------------
// sealGetBattVoltage
// ---------------------------------------------------------------------------
float sealGetBattVoltage(Notecard &nc) {
    // -1.0f is the documented sentinel for "reading unavailable".
    // A healthy LiPo never reads below 3.0 V, so -1.0 is unambiguous.
    J *rsp = nc.requestAndResponse(nc.newRequest("card.voltage"));
    if (rsp == NULL) return -1.0f;

    const char *err = JGetString(rsp, "err");
    if (err && *err) {
        Serial.print("[seal] card.voltage error: ");
        Serial.println(err);
        nc.deleteResponse(rsp);
        return -1.0f;
    }

    float v = (float)JGetNumber(rsp, "value");
    nc.deleteResponse(rsp);
    return v;
}
