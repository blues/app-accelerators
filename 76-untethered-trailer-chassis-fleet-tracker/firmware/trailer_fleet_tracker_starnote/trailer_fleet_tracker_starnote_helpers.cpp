/*******************************************************************************
 * trailer_fleet_tracker_starnote_helpers.cpp
 *
 * Global variable definitions and helper-function implementations for the
 * Untethered Trailer & Chassis Fleet Tracker firmware — ocean-capable variant
 * using Notecard Cellular + Starnote for Iridium LEO.
 * See trailer_fleet_tracker_starnote_helpers.h for types and prototypes.
 ******************************************************************************/

#include "trailer_fleet_tracker_starnote_helpers.h"

// ---------------------------------------------------------------------------
// Global definitions
// ---------------------------------------------------------------------------
Notecard   notecard;
const char kStateSegId[] = "TRST";

// ===========================================================================
// sendAndCheck — send a pre-built request and confirm acceptance.
//
// Returns true when the Notecard replied without an "err" field.
// Handles NULL req (allocation failure) before any J* field access, and
// NULL rsp (I²C / timeout failure) after the send attempt.
// ===========================================================================
bool sendAndCheck(J *req, const char *tag)
{
    if (!req) {
#ifdef usbSerial
        usbSerial.print("[config] alloc failed: ");
        usbSerial.println(tag);
#endif
        return false;
    }
    J *rsp = notecard.requestAndResponse(req);
    if (!rsp) {
#ifdef usbSerial
        usbSerial.print("[config] no response: ");
        usbSerial.println(tag);
#endif
        return false;
    }
    const char *err = JGetString(rsp, "err");
    bool ok = !(err && err[0]);
#ifdef usbSerial
    if (!ok) {
        usbSerial.print("[config] ");
        usbSerial.print(tag);
        usbSerial.print(" err: ");
        usbSerial.println(err);
    }
#endif
    notecard.deleteResponse(rsp);
    return ok;
}

// ===========================================================================
// notecardConfigure — one-time Notecard configuration.
//
// Called on first boot and after any firmware update that bumps
// FIRMWARE_CONFIG_VERSION.  Returns true only when every Notecard request
// is confirmed so that config_version is never committed on a partial
// failure; a transient I²C error causes a retry on the next wake instead of
// leaving Notecard-side settings permanently out of sync.
// ===========================================================================
bool notecardConfigure()
{
    if (!PRODUCT_UID[0]) {
#ifdef usbSerial
        usbSerial.println("[config] PRODUCT_UID is empty — set it before flashing");
#endif
        return false;
    }

    // hub.set — periodic sync with voltage-variable outbound cadence.
    // The voutbound / vinbound profiles stretch sync intervals as the solar
    // battery drains, keeping the trailer visible even in extended cloudy
    // dwell periods.  Transition events use sync:true to bypass this schedule.
    J *req = notecard.newRequest("hub.set");
    if (!req) return false;
    JAddStringToObject(req, "product",   PRODUCT_UID);
    JAddStringToObject(req, "mode",      "periodic");
    JAddStringToObject(req, "voutbound", VOUTBOUND_PROFILE);
    JAddStringToObject(req, "vinbound",  VINBOUND_PROFILE);
    if (!notecard.sendRequestWithRetry(req, 5)) {
#ifdef usbSerial
        usbSerial.println("[config] hub.set failed");
#endif
        return false;
    }

    // card.transport — cellular first (LTE-M / NB-IoT); after two consecutive
    // cellular failures the Notecard falls back to Iridium LEO satellite via
    // the Starnote for Iridium module on the Notecarrier XI.  Iridium LEO
    // provides pole-to-pole coverage with no geographic exclusions — including
    // trans-oceanic routes, polar corridors, and all land routes — unlike
    // geostationary satellite networks that are restricted to defined regional
    // footprints.  Cellular is retried automatically once coverage recovers.
    req = notecard.newRequest("card.transport");
    if (!req) return false;
    JAddStringToObject(req, "method", "cell-ntn");
    if (!sendAndCheck(req, "card.transport")) return false;

    // card.location.mode — start with GPS off.  The state machine issues
    // card.location.mode {"mode":"periodic"} explicitly on each PARKED→MOVING
    // departure and {"mode":"off"} on each MOVING→PARKED arrival, so the GPS
    // module never runs during parked dwells.  This firmware-enforced toggling
    // does not rely on the implicit motion-gating behaviour of periodic mode,
    // which is documented only for the Notecard's own GPS module and not
    // guaranteed for the Starnote for Iridium's combined GPS hardware path.
    // On this hardware GPS is provided by the Starnote for Iridium via the
    // Notecarrier XI; the standard card.location API applies.
    req = notecard.newRequest("card.location.mode");
    if (!req) return false;
    JAddStringToObject(req, "mode", "off");
    if (!sendAndCheck(req, "card.location.mode")) return false;

    // card.motion.mode — enable the built-in accelerometer and configure the
    // motion-status-change detector.  When ≥5 motion events accumulate in a
    // 60-second bucket, card.motion returns "mode":"moving"; when the bucket
    // is quiet it returns "mode":"stopped".  sensitivity:2 selects 25 Hz /
    // ±4G — enough to catch road vibration without triggering on wind buffeting
    // of a parked trailer.
    req = notecard.newRequest("card.motion.mode");
    if (!req) return false;
    JAddBoolToObject(req,   "start",       true);
    JAddNumberToObject(req, "motion",      5);
    JAddNumberToObject(req, "seconds",     60);
    JAddNumberToObject(req, "sensitivity", 2);
    if (!sendAndCheck(req, "card.motion.mode")) return false;

    return true;
}

// ===========================================================================
// defineTemplates — register compact note templates.
//
// All three Notefiles use "format":"compact" to strip the full JSON envelope
// and store/transmit as fixed-length binary records.  This is important for
// satellite paths: Iridium enforces a maximum payload size per message, and
// compact format ensures Notes are well within that limit.  Port numbers are
// required for compact templates (range 1–100).
//
// The _lat / _lon / _time keywords instruct the Notecard to embed the current
// GPS fix and timestamp from its internal state into each compact note.
//
// Returns true only after all three templates are confirmed by the Notecard
// so that config_version is not committed on a partial failure.
// ===========================================================================
bool defineTemplates()
{
    // trailer_event.qo — departure / arrival transitions (sync:true, immediate)
    J *req = notecard.newRequest("note.template");
    if (!req) return false;
    JAddStringToObject(req, "file",   NOTEFILE_EVENT);
    JAddNumberToObject(req, "port",   PORT_EVENT);
    JAddStringToObject(req, "format", "compact");
    J *body = JAddObjectToObject(req, "body");
    JAddNumberToObject(body, "type",      21);    // TUINT8: 1=departed, 2=arrived
    JAddNumberToObject(body, "dwell_h",   12.1);  // TFLOAT16: parked dwell hours
    JAddNumberToObject(body, "gps_valid", 21);    // TUINT8: 1=valid fix, 0=no fix
    // lat, lon, evt_time are explicit fields written from values captured at
    // transition time rather than auto-populated keywords (_lat/_lon/_time).
    // This ensures retried events carry the original transition location and
    // timestamp rather than the Notecard's current GPS state at retry time.
    JAddNumberToObject(body, "lat",       14.1);  // TFLOAT32: GPS lat at transition
    JAddNumberToObject(body, "lon",       14.1);  // TFLOAT32: GPS lon at transition
    JAddNumberToObject(body, "evt_time",  14);    // TINT32: Unix epoch at transition
    if (!sendAndCheck(req, "note.template " NOTEFILE_EVENT)) return false;

    // trailer_location.qo — periodic GPS position while moving
    req = notecard.newRequest("note.template");
    if (!req) return false;
    JAddStringToObject(req, "file",   NOTEFILE_LOCATION);
    JAddNumberToObject(req, "port",   PORT_LOCATION);
    JAddStringToObject(req, "format", "compact");
    body = JAddObjectToObject(req, "body");
    JAddNumberToObject(body, "_lat",  14.1);
    JAddNumberToObject(body, "_lon",  14.1);
    JAddNumberToObject(body, "_time", 14);
    if (!sendAndCheck(req, "note.template " NOTEFILE_LOCATION)) return false;

    // trailer_heartbeat.qo — periodic alive check while parked
    req = notecard.newRequest("note.template");
    if (!req) return false;
    JAddStringToObject(req, "file",   NOTEFILE_HEARTBEAT);
    JAddNumberToObject(req, "port",   PORT_HEARTBEAT);
    JAddStringToObject(req, "format", "compact");
    body = JAddObjectToObject(req, "body");
    JAddNumberToObject(body, "volt",      12.1);  // TFLOAT16: LiPo battery voltage
    JAddNumberToObject(body, "gps_valid", 21);    // TUINT8: 1=valid fix, 0=no fix
    JAddNumberToObject(body, "_lat",      14.1);
    JAddNumberToObject(body, "_lon",      14.1);
    JAddNumberToObject(body, "_time",     14);
    if (!sendAndCheck(req, "note.template " NOTEFILE_HEARTBEAT)) return false;

    return true;
}

// ===========================================================================
// fetchEnvOverrides — pull fleet-level tunables from Notehub.
//
// Returns true when a valid env.get response was processed, false on request
// failure or a Notecard-reported error (e.g. no Notehub session yet), so the
// caller can decide whether to record the poll timestamp or schedule a retry.
//
// moving_ping_secs is committed to `s` immediately when changed so it takes
// effect on the host's next wake.  If the trailer is currently MOVING, the
// matching card.location.mode call is issued right away; if parked, it is
// applied on the next PARKED→MOVING departure by the state-machine code.
// ===========================================================================
bool fetchEnvOverrides(AppState &s)
{
    J *req = notecard.newRequest("env.get");
    if (!req) return false;
    J *rsp = notecard.requestAndResponse(req);
    if (!rsp) return false;

    const char *err = JGetString(rsp, "err");
    if (err && err[0]) {
        notecard.deleteResponse(rsp);
        return false;
    }

    J *env = JGetObject(rsp, "body");
    if (env) {
        const char *val;

        // parked_check_mins: motion poll interval while parked (1–60 min)
        val = JGetString(env, "parked_check_mins");
        if (val && val[0]) {
            uint32_t v = (uint32_t)strtoul(val, NULL, 10);
            if (v >= 1 && v <= 60) s.parked_check_secs = v * 60;
        }

        // moving_ping_mins: GPS report interval while moving (5–60 min).
        // The stored value is updated immediately so it takes effect on the
        // host's next wake and on the next PARKED→MOVING transition.
        // If the trailer is currently MOVING (GPS in periodic mode), also
        // re-sync card.location.mode right away so the Notecard's acquisition
        // cadence matches the new interval without waiting for a transition.
        // When parked (GPS off), the new period is applied automatically by
        // the PARKED→MOVING departure code — no action needed here.
        val = JGetString(env, "moving_ping_mins");
        if (val && val[0]) {
            uint32_t v = (uint32_t)strtoul(val, NULL, 10);
            if (v >= 5 && v <= 60) {
                uint32_t new_secs = v * 60;
                if (new_secs != s.moving_ping_secs) {
                    s.moving_ping_secs = new_secs;
                    if (s.current_state == STATE_MOVING) {
                        J *loc = notecard.newRequest("card.location.mode");
                        if (loc) {
                            JAddStringToObject(loc, "mode",    "periodic");
                            JAddNumberToObject(loc, "seconds", (double)new_secs);
                            sendAndCheck(loc, "card.location.mode (env update)");
                            // Failure is acceptable: the correct period will be
                            // re-applied on the next PARKED→MOVING transition.
                        }
                    }
                }
            }
        }

        // heartbeat_hours: alive-ping interval while parked (1–24 hr)
        val = JGetString(env, "heartbeat_hours");
        if (val && val[0]) {
            uint32_t v = (uint32_t)strtoul(val, NULL, 10);
            if (v >= 1 && v <= 24) s.heartbeat_secs = v * 3600;
        }
    }

    notecard.deleteResponse(rsp);
    return true;
}

// ===========================================================================
// Sensor helpers
// ===========================================================================

// Query the Notecard's built-in accelerometer motion status.
bool isMoving(bool &out_moving)
{
    out_moving = false;
    J *req = notecard.newRequest("card.motion");
    if (!req) return false;
    J *rsp = notecard.requestAndResponse(req);
    if (!rsp) return false;

    const char *err = JGetString(rsp, "err");
    if (err && err[0]) {
        notecard.deleteResponse(rsp);
        return false;
    }

    const char *mode = JGetString(rsp, "mode");
    out_moving = (mode && strcmp(mode, "moving") == 0);
    notecard.deleteResponse(rsp);
    return true;
}

// Return the current Unix epoch from the Notecard's real-time clock.
// Sets out_time to 0 if the Notecard hasn't yet synced time from Notehub.
// Returns false on request failure or a Notecard-reported error.
bool getEpoch(uint32_t &out_time)
{
    out_time = 0;
    J *req = notecard.newRequest("card.time");
    if (!req) return false;
    J *rsp = notecard.requestAndResponse(req);
    if (!rsp) return false;

    const char *err = JGetString(rsp, "err");
    if (err && err[0]) {
        notecard.deleteResponse(rsp);
        return false;
    }

    out_time = (uint32_t)JGetInt(rsp, "time");
    notecard.deleteResponse(rsp);
    return true;
}

// Return the LiPo voltage from the Notecard's onboard ADC.
bool getBatteryVoltage(float &out_volt)
{
    out_volt = 0.0f;
    J *req = notecard.newRequest("card.voltage");
    if (!req) return false;
    J *rsp = notecard.requestAndResponse(req);
    if (!rsp) return false;

    const char *err = JGetString(rsp, "err");
    if (err && err[0]) {
        notecard.deleteResponse(rsp);
        return false;
    }

    out_volt = (float)JGetNumber(rsp, "value");
    notecard.deleteResponse(rsp);
    return true;
}

// Query card.location and return true only when the Notecard reports a valid
// GNSS fix — non-zero lat/lon, no error field.  On this hardware GPS is
// provided by the Starnote for Iridium's combined antenna; the Notecard
// exposes the fix through the standard card.location API regardless.
bool hasValidGnssFix()
{
    J *req = notecard.newRequest("card.location");
    if (!req) return false;
    J *rsp = notecard.requestAndResponse(req);
    if (!rsp) return false;

    const char *err = JGetString(rsp, "err");
    if (err && err[0]) {
        notecard.deleteResponse(rsp);
        return false;
    }

    double lat = JGetNumber(rsp, "lat");
    double lon = JGetNumber(rsp, "lon");
    notecard.deleteResponse(rsp);
    return (lat != 0.0 || lon != 0.0);
}

// Capture the Notecard's current GNSS state for transition event stamping.
//
// Distinct from hasValidGnssFix(): returns the raw lat/lon values so callers
// can persist them in the pending-event queue.  Called once per detected
// transition; the stored values are passed through every delivery attempt so
// retried departure/arrival notes carry the detection-time location and epoch
// — not the GPS state at retry time.
//
// Calling context and GPS mode at capture time:
//   PARKED→MOVING (departure): called before GPS is re-enabled, so GPS mode
//     is still "off".  card.location returns the Notecard's last cached fix
//     from the prior trip, which may be stale after a long dwell.  gps_valid
//     is still 1 if a fix exists; its freshness is bounded by the prior trip.
//   MOVING→PARKED (arrival): called while GPS is still in periodic mode, so
//     the cached fix is within one moving_ping_secs interval of the actual stop.
//
// card.location always returns the last cached fix regardless of GPS mode
// (periodic or off); no additional GPS-on time is incurred by this call.
// Timestamp accuracy is bounded by parked_check_secs (departures) or
// moving_ping_secs (arrivals).  On this hardware GPS is provided by the
// Starnote for Iridium's combined Iridium+GPS antenna; the standard
// card.location API returns the fix transparently.
//
// Sets out_gps_valid=0 and out_lat=out_lon=0 when no fix is available or
// when the card.location request fails.
void captureGnssState(float &out_lat, float &out_lon, uint8_t &out_gps_valid)
{
    out_lat       = 0.0f;
    out_lon       = 0.0f;
    out_gps_valid = 0;

    J *req = notecard.newRequest("card.location");
    if (!req) return;
    J *rsp = notecard.requestAndResponse(req);
    if (!rsp) return;

    const char *err = JGetString(rsp, "err");
    if (err && err[0]) {
        notecard.deleteResponse(rsp);
        return;
    }

    double lat = JGetNumber(rsp, "lat");
    double lon = JGetNumber(rsp, "lon");
    notecard.deleteResponse(rsp);

    if (lat != 0.0 || lon != 0.0) {
        out_lat       = (float)lat;
        out_lon       = (float)lon;
        out_gps_valid = 1;
    }
}

// ===========================================================================
// Note emission
// ===========================================================================

// Send a transition event (departed / arrived) with sync:true.
//
// All location and time fields are passed in from values captured at
// transition detection time (the wake where the state change was observed),
// via captureGnssState / getEpoch in setup().  No GPS state queries are made
// inside this function so that retried events always carry the original
// detection-time location and timestamp, not whatever the Notecard happens to
// have cached at retry time.  Timestamp accuracy is bounded by
// parked_check_secs (departures) or moving_ping_secs (arrivals).
// Transition events are never suppressed; the receiver uses gps_valid to
// distinguish a confirmed location (1) from a no-fix placeholder (0).
//
// Retries up to three times so a transient I²C hiccup or busy Notecard does
// not silently drop the highest-value business event.
// Returns true only when the Notecard confirms a successful queue operation.
bool sendTransitionEvent(uint8_t type, float dwell_hours,
                          uint8_t gps_valid, float lat, float lon,
                          uint32_t evt_epoch)
{
#ifdef usbSerial
    usbSerial.print("[event] type=");
    usbSerial.print(type);
    usbSerial.print(" gps_valid=");
    usbSerial.println(gps_valid);
#endif

    for (int attempt = 0; attempt < 3; ++attempt) {
        if (attempt > 0) delay(500);

        J *req = notecard.newRequest("note.add");
        if (!req) continue;
        JAddStringToObject(req, "file", NOTEFILE_EVENT);
        JAddBoolToObject(req, "sync", true);
        J *body = JAddObjectToObject(req, "body");
        JAddNumberToObject(body, "type",      (double)type);
        JAddNumberToObject(body, "dwell_h",   (double)dwell_hours);
        JAddNumberToObject(body, "gps_valid", (double)gps_valid);
        // lat, lon, evt_time are written explicitly from the values captured
        // at transition time — not from the Notecard's current GPS state.
        JAddNumberToObject(body, "lat",       (double)lat);
        JAddNumberToObject(body, "lon",       (double)lon);
        JAddNumberToObject(body, "evt_time",  (double)evt_epoch);
        J *rsp = notecard.requestAndResponse(req);
        if (!rsp) continue;

        const char *err = JGetString(rsp, "err");
        bool ok = !(err && err[0]);
        notecard.deleteResponse(rsp);
        if (ok) return true;
    }

#ifdef usbSerial
    usbSerial.println("[event] failed to queue transition note after 3 attempts");
#endif
    return false;
}

// Queue a GPS position note for batched cellular (or Iridium satellite) delivery.
// Suppressed when no valid GNSS fix is available; last_location_at is not
// advanced on suppression so the note is retried on the next moving-state wake.
bool sendLocationNote()
{
    if (!hasValidGnssFix()) {
#ifdef usbSerial
        usbSerial.println("[location] no valid GNSS fix; note suppressed");
#endif
        return false;
    }

    J *req = notecard.newRequest("note.add");
    if (!req) return false;
    JAddStringToObject(req, "file", NOTEFILE_LOCATION);
    JAddObjectToObject(req, "body");

    J *rsp = notecard.requestAndResponse(req);
    if (!rsp) return false;

    const char *err = JGetString(rsp, "err");
    bool ok = !(err && err[0]);
    notecard.deleteResponse(rsp);
    return ok;
}

// Queue a parked heartbeat note carrying the current LiPo voltage.
bool sendHeartbeatNote(float volt)
{
    uint8_t gps_valid = hasValidGnssFix() ? 1 : 0;

    J *req = notecard.newRequest("note.add");
    if (!req) return false;
    JAddStringToObject(req, "file", NOTEFILE_HEARTBEAT);
    J *body = JAddObjectToObject(req, "body");
    JAddNumberToObject(body, "volt",      (double)volt);
    JAddNumberToObject(body, "gps_valid", (double)gps_valid);

    J *rsp = notecard.requestAndResponse(req);
    if (!rsp) return false;

    const char *err = JGetString(rsp, "err");
    bool ok = !(err && err[0]);
    notecard.deleteResponse(rsp);
    return ok;
}

// ===========================================================================
// enqueuePendingEvent — push a transition event onto the FIFO ring buffer.
//
// All location/time fields must be supplied from values captured at
// transition detection time (via captureGnssState and getEpoch in setup())
// so that every delivery attempt carries the detection-time metadata rather
// than whatever the Notecard's GPS state happens to be at retry time.
//
// Returns true when the event was added, false when the FIFO is full.
// A full FIFO (PENDING_QUEUE_DEPTH consecutive undelivered events) indicates
// a persistent Notecard communication failure; the oldest queued event is
// retained in its position and the new one is dropped with a log line.  In
// normal operation the FIFO drains to empty on every wake and never exceeds
// a depth of 2.
// ===========================================================================
bool enqueuePendingEvent(AppState &s, uint8_t type, float dwell_h,
                          uint32_t event_epoch, float event_lat,
                          float event_lon, uint8_t gps_valid)
{
    if (s.pending_count >= PENDING_QUEUE_DEPTH) {
#ifdef usbSerial
        usbSerial.println("[event] pending queue full; event dropped");
#endif
        return false;
    }
    uint8_t tail = (s.pending_head + s.pending_count) % PENDING_QUEUE_DEPTH;
    PendingEvent &slot  = s.pending_events[tail];
    slot.type           = type;
    slot.gps_valid      = gps_valid;
    slot._pad[0]        = 0;
    slot._pad[1]        = 0;
    slot.dwell_h        = dwell_h;
    slot.event_epoch    = event_epoch;
    slot.event_lat      = event_lat;
    slot.event_lon      = event_lon;
    s.pending_count++;
    return true;
}

// ===========================================================================
// drainPendingQueue — attempt to deliver all queued transition events.
//
// Processes the FIFO from head (oldest) toward tail (newest).  Each
// successful delivery dequeues the head event and advances to the next.
// Draining stops at the first failed delivery so that events are never
// delivered out of order; the failed event remains at the head for retry
// on the next wake.
//
// Called twice per wake cycle: once before the motion state machine (drains
// any stale events from prior wakes) and once after (immediately attempts
// delivery of any event just enqueued by a fresh state transition).
// ===========================================================================
void drainPendingQueue(AppState &s)
{
    while (s.pending_count > 0) {
        PendingEvent &e = s.pending_events[s.pending_head];
        float dh = (e.type == EVENT_DEPARTED) ? e.dwell_h : 0.0f;
#ifdef usbSerial
        usbSerial.print("[event] draining pending event type=");
        usbSerial.println(e.type);
#endif
        if (!sendTransitionEvent(e.type, dh,
                                  e.gps_valid, e.event_lat, e.event_lon,
                                  e.event_epoch)) {
            break;
        }
        e.type    = 0;
        e.dwell_h = 0.0f;
        s.pending_head = (s.pending_head + 1) % PENDING_QUEUE_DEPTH;
        s.pending_count--;
    }
}
