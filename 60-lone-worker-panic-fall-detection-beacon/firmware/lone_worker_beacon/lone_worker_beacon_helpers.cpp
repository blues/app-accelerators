/*
 * lone_worker_beacon_helpers.cpp — Notecard, sensor, GPS, alert, and haptic
 * helper implementations for the Lone Worker Panic & Fall Detection Beacon.
 *
 * All globals and constants are declared in lone_worker_beacon_helpers.h and
 * defined in lone_worker_beacon.ino.
 */
#include "lone_worker_beacon_helpers.h"

// ─── Range-clamping helpers ───────────────────────────────────────────────
// Return the raw value only when it falls within [lo, hi]; otherwise log and
// return fallback, keeping the current runtime value unchanged.
static float clampF(const char *name, double v, float lo, float hi, float fallback)
{
    if ((float)v < lo || (float)v > hi) {
        DEBUG_PRINT("[CFG] "); DEBUG_PRINT(name);
        DEBUG_PRINT(" out of range ("); DEBUG_PRINT(v);
        DEBUG_PRINT(") — keeping "); DEBUG_PRINTLN(fallback);
        return fallback;
    }
    return (float)v;
}

static uint32_t clampU32(const char *name, double v, uint32_t lo, uint32_t hi,
                         uint32_t fallback)
{
    if (v < (double)lo || v > (double)hi) {
        DEBUG_PRINT("[CFG] "); DEBUG_PRINT(name);
        DEBUG_PRINT(" out of range ("); DEBUG_PRINT((long)v);
        DEBUG_PRINT(") — keeping "); DEBUG_PRINTLN((long)fallback);
        return fallback;
    }
    return (uint32_t)v;
}

// ─── Notecard Configuration ───────────────────────────────────────────────
// Retries hub.set up to 5 times to handle the cold-boot I²C race. Returns
// true only when the Notecard acknowledges without error. Failure latches
// g_setupFault in the caller.
//
// mode: "periodic" with outbound: 1440 (daily flush) and inbound: 120 (2-hour
// env-var refresh). All emergency notes use sync:true, which bypasses the
// outbound interval and triggers an immediate cellular or satellite session.
bool notecardConfigure()
{
    for (uint8_t attempt = 0; attempt < 5; attempt++) {
        J *req = notecard.newRequest("hub.set");
        if (!req) { delay(500); continue; }
        JAddStringToObject(req, "product",  PRODUCT_UID);
        JAddStringToObject(req, "mode",     "periodic");
        JAddNumberToObject(req, "outbound", 1440);   // daily; alerts use sync:true
        JAddNumberToObject(req, "inbound",  120);    // 2-hour env-var refresh
        J *rsp = notecard.requestAndResponse(req);
        bool ok = (rsp != NULL && !notecard.responseError(rsp));
        if (rsp) notecard.deleteResponse(rsp);
        if (ok) return true;
        DEBUG_PRINT("[CFG] hub.set attempt "); DEBUG_PRINT(attempt + 1);
        DEBUG_PRINTLN(" failed — retrying.");
        delay(500);
    }
    DEBUG_PRINTLN("[FAULT] hub.set failed after 5 attempts.");
    return false;
}

// ─── Notefile Templates ───────────────────────────────────────────────────
// compact + port are REQUIRED for Starnote NTN transport. _lat/_lon instruct
// the Notecard to embed its cached GPS fix at note.add time — the firmware
// never passes coordinates in the note body.
//
// Two templates are registered:
//   beacon_alert.qo    (port 50) — fall or panic event note
//   beacon_location.qo (port 52) — follow-up note carrying the fresh GPS fix
//                                   acquired after the initial alert (see
//                                   pollGpsSearch). Queued only when a fresh
//                                   fix arrives within the GPS timeout window.
//
// Both templates include event_id so downstream systems can correlate a
// beacon_alert.qo and its beacon_location.qo using (device, event_id) as
// the join key. event_id is device-local and resets on power cycle; it is
// not unique across devices on its own.
//
// Each template is retried up to 3 times. Returns false if any template fails
// all attempts — the caller latches g_setupFault.
bool defineTemplates()
{
    bool allOk = true;

    // ── beacon_alert.qo ──────────────────────────────────────────────────
    {
        bool ok = false;
        for (uint8_t attempt = 0; attempt < 3 && !ok; attempt++) {
            J *req = notecard.newRequest("note.template");
            if (!req) { delay(500); continue; }
            JAddStringToObject(req, "file",   "beacon_alert.qo");
            JAddNumberToObject(req, "port",   50);
            JAddStringToObject(req, "format", "compact");
            J *body = JAddObjectToObject(req, "body");
            JAddStringToObject(body, "type",      "s");
            JAddStringToObject(body, "worker_id", "s");
            JAddNumberToObject(body, "event_id",  14);     // 4-byte signed int32 correlation key
            JAddNumberToObject(body, "voltage",   14.1);   // 4-byte float
            JAddNumberToObject(body, "loc_age_s", 14.1);   // seconds; -1.0 = unknown
            JAddNumberToObject(body, "_lat",      14.1);   // from Notecard GPS cache
            JAddNumberToObject(body, "_lon",      14.1);
            J *rsp = notecard.requestAndResponse(req);
            ok = (rsp != NULL && !notecard.responseError(rsp));
            if (rsp) notecard.deleteResponse(rsp);
            if (!ok) delay(500);
        }
        if (!ok) {
            DEBUG_PRINTLN("[FAULT] beacon_alert.qo template registration failed.");
            allOk = false;
        }
    }

    // ── beacon_location.qo ───────────────────────────────────────────────
    // Follow-up note queued by pollGpsSearch() when a fresh fix arrives after
    // an alert. Carries the event-time coordinates that were not yet available
    // when the initial alert was queued. event_id matches the paired
    // beacon_alert.qo so downstream systems can join the two notes.
    {
        bool ok = false;
        for (uint8_t attempt = 0; attempt < 3 && !ok; attempt++) {
            J *req = notecard.newRequest("note.template");
            if (!req) { delay(500); continue; }
            JAddStringToObject(req, "file",   "beacon_location.qo");
            JAddNumberToObject(req, "port",   52);
            JAddStringToObject(req, "format", "compact");
            J *body = JAddObjectToObject(req, "body");
            JAddStringToObject(body, "type",      "s");   // echoes triggering alert type
            JAddStringToObject(body, "worker_id", "s");
            JAddNumberToObject(body, "event_id",  14);    // 4-byte signed int32; matches beacon_alert.qo
            JAddNumberToObject(body, "_lat",      14.1);
            JAddNumberToObject(body, "_lon",      14.1);
            J *rsp = notecard.requestAndResponse(req);
            ok = (rsp != NULL && !notecard.responseError(rsp));
            if (rsp) notecard.deleteResponse(rsp);
            if (!ok) delay(500);
        }
        if (!ok) {
            DEBUG_PRINTLN("[FAULT] beacon_location.qo template registration failed.");
            allOk = false;
        }
    }

    return allOk;
}

// ─── Environment Variable Fetch ───────────────────────────────────────────
// Pulls all env vars from Notehub and updates local runtime config with
// explicit range validation. Values outside the safe ranges are rejected and
// the current runtime value is kept, with a debug log. Called once at boot
// and then every ENV_FETCH_INTERVAL_MS (2 h) from loop().
void fetchEnvVars()
{
    J *rsp = notecard.requestAndResponse(notecard.newRequest("env.get"));
    if (!rsp) return;
    if (notecard.responseError(rsp)) { notecard.deleteResponse(rsp); return; }

    J *body = JGetObject(rsp, "body");
    if (body != NULL) {
        const char *wid = JGetString(body, "worker_id");
        // Hard-clamp to WORKER_ID_MAX chars. An oversized worker_id bloats
        // every Starnote compact packet; strncpy truncates silently so the
        // device remains functional even if a mis-configured env var is set.
        if (wid && strlen(wid) > 0) {
            strncpy(g_workerId, wid, WORKER_ID_MAX);
            g_workerId[WORKER_ID_MAX] = '\0';
        }

        double v;
        v = JGetNumber(body, "freefall_g");
        if (v != 0.0)
            g_freefallG = clampF("freefall_g", v,
                                 ENV_FREEFALL_G_MIN, ENV_FREEFALL_G_MAX, g_freefallG);

        v = JGetNumber(body, "impact_g");
        if (v != 0.0)
            g_impactG = clampF("impact_g", v,
                               ENV_IMPACT_G_MIN, ENV_IMPACT_G_MAX, g_impactG);

        v = JGetNumber(body, "fall_window_ms");
        if (v != 0.0)
            g_fallWindowMs = clampU32("fall_window_ms", v,
                                      ENV_FALL_WINDOW_MIN, ENV_FALL_WINDOW_MAX,
                                      g_fallWindowMs);

        v = JGetNumber(body, "freefall_min_ms");
        if (v != 0.0)
            g_freefallMinMs = clampU32("freefall_min_ms", v,
                                       ENV_FF_MIN_MS_MIN, ENV_FF_MIN_MS_MAX,
                                       g_freefallMinMs);

        v = JGetNumber(body, "panic_hold_ms");
        if (v != 0.0)
            g_panicHoldMs = clampU32("panic_hold_ms", v,
                                     ENV_PANIC_HOLD_MIN, ENV_PANIC_HOLD_MAX,
                                     g_panicHoldMs);
    }
    notecard.deleteResponse(rsp);
}

// ─── Sensor Initialization ────────────────────────────────────────────────
bool initAccel()
{
    accel.settings.adcEnabled      = 0;
    accel.settings.tempEnabled     = 0;
    accel.settings.accelSampleRate = 100;  // ODR 100 Hz; matches ACCEL_SAMPLE_MS
    accel.settings.accelRange      = 4;    // ±4 g; headroom for impacts
    accel.settings.xAccelEnabled   = 1;
    accel.settings.yAccelEnabled   = 1;
    accel.settings.zAccelEnabled   = 1;
    return (accel.begin() == IMU_SUCCESS);
}

bool initHaptic()
{
    if (!haptic.begin()) return false;
    haptic.selectLibrary(1);               // Library 1: ERM open-loop
    haptic.setMode(DRV2605_MODE_INTTRIG);  // software-triggered via go()
    return true;
}

// ─── Fall Detection  (two-stage software algorithm) ───────────────────────
// Stage 1 — free-fall: total-g < g_freefallG for >= g_freefallMinMs.
// Stage 2 — impact: total-g > g_impactG within g_fallWindowMs of stage 1 end.
//
// Runtime health: zero-vector reads (I2C fault) and implausible magnitude are
// detected. ACCEL_FAIL_THRESHOLD consecutive bad reads trigger tryReinitAccel();
// after ACCEL_REINIT_MAX failures g_accelFaultLatched is set and g_accelReady
// is cleared, permanently disabling fall detection until power-cycle.
//
// Impact window uses start-time + elapsed comparison (wraparound-safe).
bool pollFallDetection()
{
    float ax = accel.readFloatAccelX();
    float ay = accel.readFloatAccelY();
    float az = accel.readFloatAccelZ();
    float totalG = sqrtf(ax*ax + ay*ay + az*az);
    uint32_t now = millis();

    // Implausible-read detection.
    // All-zero vector: LIS3DH typically measures ~1 g at rest due to gravity;
    // an all-zero result indicates an I2C fault or sensor hang.
    // totalG > ACCEL_PLAUSIBLE_G_MAX: physically impossible on a ±4 g device.
    bool readingBad = (fabsf(ax) < 0.001f && fabsf(ay) < 0.001f &&
                       fabsf(az) < 0.001f) || (totalG > ACCEL_PLAUSIBLE_G_MAX);
    if (readingBad) {
        if (++g_accelFailCount >= ACCEL_FAIL_THRESHOLD) tryReinitAccel();
        return false;
    }
    g_accelFailCount = 0;

    // Stage 1: detect and time the free-fall (low-g) phase.
    if (!g_inFreefall && !g_watchingImpact) {
        if (totalG < g_freefallG) {
            g_inFreefall    = true;
            g_freefallStart = now;
        }
    } else if (g_inFreefall) {
        if (totalG >= g_freefallG) {
            if ((now - g_freefallStart) >= g_freefallMinMs) {
                g_watchingImpact    = true;
                g_impactWindowStart = now;   // start of window (wraparound-safe)
            }
            g_inFreefall = false;
        }
    }

    // Stage 2: impact spike within the window.
    if (g_watchingImpact) {
        if ((now - g_impactWindowStart) >= g_fallWindowMs) {
            g_watchingImpact = false;        // window expired — not a fall
        } else if (totalG > g_impactG) {
            g_watchingImpact = false;
            g_inFreefall     = false;
            DEBUG_PRINT("[FALL] Confirmed — peak g: "); DEBUG_PRINTLN(totalG, 2);
            return true;
        }
    }
    return false;
}

// ─── Accelerometer Reinitialization ──────────────────────────────────────
// Called by pollFallDetection() when consecutive bad reads exceed
// ACCEL_FAIL_THRESHOLD. Attempts initAccel() up to ACCEL_REINIT_MAX times
// (tracked via a static counter that resets on success). On permanent failure
// g_accelFaultLatched is set and g_accelReady is cleared; a debug log surfaces
// the fault (add accel_fault to beacon_alert.qo if runtime monitoring is needed).
void tryReinitAccel()
{
    static uint8_t reinitAttempts = 0;
    if (g_accelFaultLatched) return;

    DEBUG_PRINTLN("[ACCEL] Repeated bad reads — attempting reinitialization.");
    if (initAccel()) {
        g_accelFailCount = 0;
        reinitAttempts   = 0;
        DEBUG_PRINTLN("[ACCEL] Reinitialized successfully — fall detection restored.");
        return;
    }
    if (++reinitAttempts >= ACCEL_REINIT_MAX) {
        g_accelFaultLatched = true;
        g_accelReady        = false;
        DEBUG_PRINTLN("[FAULT] Accel reinit failed — fall detection permanently "
                      "disabled. Power-cycle required.");
    } else {
        DEBUG_PRINT("[ACCEL] Reinit attempt "); DEBUG_PRINT(reinitAttempts);
        DEBUG_PRINTLN(" failed — will retry on next failure threshold crossing.");
    }
    // Reset fail counter so the threshold must be crossed again before the
    // next reinit attempt — avoids calling initAccel() on every bad read.
    g_accelFailCount = 0;
}

// ─── Panic Button  (debounced hold-to-confirm) ────────────────────────────
// 30 ms stable-state debounce on both press and release edges. Hold timing
// starts from the first clean stable-press edge. Panic fires at threshold-
// crossing (not release) and is latched so it fires exactly once per press.
bool checkPanicButton()
{
    bool raw = (digitalRead(PANIC_BUTTON_PIN) == LOW);
    uint32_t now = millis();

    if (raw != g_btnRaw) {
        g_btnRaw      = raw;
        g_btnChangeMs = now;
    }

    if ((now - g_btnChangeMs) >= DEBOUNCE_MS && raw != g_btnStable) {
        g_btnStable = raw;
        if (g_btnStable) {
            g_btnPressedAt = now;
            g_btnHeld      = true;
            g_panicFired   = false;
            triggerHaptic(HAPTIC_CLICK);
        } else {
            g_btnHeld    = false;
            g_panicFired = false;
        }
    }

    if (g_btnHeld && !g_panicFired && (now - g_btnPressedAt) >= g_panicHoldMs) {
        g_panicFired = true;
        // Do NOT fire the confirmation haptic here — the cooldown gate and
        // note.add outcome are checked in loop() after this function returns.
        // The triple-buzz fires there once the alert is accepted for transmission
        // (either directly into the Notecard queue or into the firmware retry
        // queue); a suppressed panic (in-cooldown) gets a single buzz instead.
        DEBUG_PRINT("[PANIC] Held "); DEBUG_PRINT(now - g_btnPressedAt);
        DEBUG_PRINTLN(" ms — confirmed.");
        return true;
    }
    return false;
}

// ─── GPS Continuous-Mode Disable Helper ───────────────────────────────────
// Sends card.location.mode:off and checks the response. On failure
// g_gpsDisablePending is set so pollGpsSearch() retries on the next loop
// pass, preventing GNSS from remaining in continuous mode indefinitely.
// Always clears g_gpsSearching so the caller's search state is cleaned up.
static void disableGps()
{
    g_gpsSearching = false;
    J *req = notecard.newRequest("card.location.mode");
    if (!req) {
        DEBUG_PRINTLN("[GPS] Alloc failed for mode:off — GNSS disable pending.");
        g_gpsDisablePending = true;
        return;
    }
    JAddStringToObject(req, "mode", "off");
    J *rsp = notecard.requestAndResponse(req);
    bool ok = (rsp && !notecard.responseError(rsp));
    if (rsp) notecard.deleteResponse(rsp);
    if (ok) {
        g_gpsDisablePending = false;
        DEBUG_PRINTLN("[GPS] GNSS continuous mode disabled.");
    } else {
        g_gpsDisablePending = true;
        DEBUG_PRINTLN("[GPS] mode:off failed — GNSS disable pending; will retry.");
    }
}

// ─── Non-Blocking GPS Search ──────────────────────────────────────────────
// beginGpsSearch() enables continuous GPS and arms the search state, then
// returns immediately. g_gpsCacheEpoch (the freshness baseline the search uses
// to distinguish a new fix from the pre-alert cache) must be set by the caller
// before this function is called. The caller must also have confirmed the alert
// note was queued — GPS search is never started for an alert that failed.
//
// pollGpsSearch() (called once per outer loop) throttles card.location polls
// to once every 2 seconds. When a fix whose epoch post-dates g_gpsCacheEpoch
// arrives, a beacon_location.qo follow-up note is queued with sync:true and
// g_gpsEventId — the same event_id as the paired beacon_alert.qo.
//
// beginGpsSearch() is called only after sendAlert() confirms the initial
// beacon_alert.qo was queued, so no GPS search can be orphaned by a failed
// note.add.
//
// Only one GPS enrichment window can be active at a time. If beginGpsSearch()
// is called while a search is already running (e.g. a second alert fires during
// the 90-second GPS window, or a retry succeeds mid-search), the call returns
// immediately without restarting the search. The new alert is served by its
// cached location only — no beacon_location.qo follow-up is queued for it.
// The ongoing window is committed to the earlier alert's event_id.
//
// card.location.mode changes use requestAndResponse() with error checks. If
// mode:continuous fails, the search is not armed. If mode:off fails,
// g_gpsDisablePending is set and retried each loop pass.
//
// Timeout uses start-time + elapsed comparison (wraparound-safe).
void beginGpsSearch(const char *alertType, uint32_t eventId)
{
    // If a search is already active, return immediately. This alert is served
    // by its cached location only — no beacon_location.qo follow-up will be
    // queued for it. The ongoing search is committed to the earlier alert's
    // event_id and emits its follow-up note for that event exclusively.
    if (g_gpsSearching) {
        DEBUG_PRINTLN("[GPS] Search already active — this alert gets "
                      "cached-location only; no follow-up note will be queued.");
        return;
    }

    // Don't start a new search while a previous mode:off is still pending.
    // pollGpsSearch() retries the disable each loop pass.
    if (g_gpsDisablePending) {
        DEBUG_PRINTLN("[GPS] GPS disable pending from prior search — "
                      "cannot start new search yet.");
        return;
    }

    // Record the alert type and event_id for the follow-up note that
    // pollGpsSearch() queues on a fresh fix.
    strncpy(g_gpsAlertType, alertType, sizeof(g_gpsAlertType) - 1);
    g_gpsAlertType[sizeof(g_gpsAlertType) - 1] = '\0';
    g_gpsEventId = eventId;

    // Use requestAndResponse() and verify the mode change succeeded. A failed
    // mode:continuous request means no fix is forthcoming; do not arm the
    // search state so the 90-second timer does not expire silently.
    J *req = notecard.newRequest("card.location.mode");
    if (!req) {
        DEBUG_PRINTLN("[GPS] Failed to allocate mode request — search aborted.");
        return;
    }
    JAddStringToObject(req, "mode", "continuous");
    J *rsp = notecard.requestAndResponse(req);
    bool ok = (rsp && !notecard.responseError(rsp));
    if (rsp) notecard.deleteResponse(rsp);
    if (!ok) {
        DEBUG_PRINTLN("[GPS] Failed to enable continuous GNSS — search aborted.");
        return;
    }

    g_gpsSearching   = true;
    g_gpsSearchStart = millis();
    g_gpsLastPollMs  = 0;
    DEBUG_PRINT("[GPS] Search started for '"); DEBUG_PRINT(alertType);
    DEBUG_PRINT("' event_id="); DEBUG_PRINT(eventId);
    DEBUG_PRINTLN(" — initial alert queued with cached location.");
}

void pollGpsSearch()
{
    // Retry a deferred GPS disable before any other work. This prevents GNSS
    // from remaining in continuous mode after a failed mode:off.
    if (g_gpsDisablePending) {
        J *req = notecard.newRequest("card.location.mode");
        if (req) {
            JAddStringToObject(req, "mode", "off");
            J *rsp = notecard.requestAndResponse(req);
            bool ok = (rsp && !notecard.responseError(rsp));
            if (rsp) notecard.deleteResponse(rsp);
            if (ok) {
                g_gpsDisablePending = false;
                DEBUG_PRINTLN("[GPS] Deferred GPS disable succeeded.");
            } else {
                DEBUG_PRINTLN("[GPS] Deferred GPS disable still failing — will retry.");
            }
        }
        if (!g_gpsSearching) return;
    }

    if (!g_gpsSearching) return;
    uint32_t now = millis();

    // Timeout check (wraparound-safe: compare elapsed, not absolute deadline).
    if ((now - g_gpsSearchStart) >= (DEFAULT_GPS_TIMEOUT_SEC * 1000UL)) {
        DEBUG_PRINTLN("[GPS] Timeout — no fresh fix; initial alert location stands.");
        disableGps();
        return;
    }

    // Throttle card.location polls to once per 2 seconds — GNSS fixes do
    // not update at the 10 Hz loop rate; polling faster wastes I2C bandwidth.
    if (g_gpsLastPollMs != 0 && (now - g_gpsLastPollMs) < 2000UL) return;
    g_gpsLastPollMs = now;

    J *rsp = notecard.requestAndResponse(notecard.newRequest("card.location"));
    if (!rsp) return;
    if (notecard.responseError(rsp)) { notecard.deleteResponse(rsp); return; }

    uint32_t fixTime = (uint32_t)JGetNumber(rsp, "time");
    double   lat     = JGetNumber(rsp, "lat");
    double   lon     = JGetNumber(rsp, "lon");
    notecard.deleteResponse(rsp);

    // Accept only a fix whose epoch post-dates the pre-alert cache snapshot
    // to prevent a stale cached fix from being mistaken for a new acquisition.
    if (fixTime > g_gpsCacheEpoch && (lat != 0.0 || lon != 0.0)) {
        DEBUG_PRINT("[GPS] Fresh fix acquired — epoch "); DEBUG_PRINTLN(fixTime);

        // Retry beacon_location.qo note.add once before disabling GNSS so a
        // transient I2C hiccup doesn't permanently lose the fresh fix. Both
        // attempts fire without delay (back-to-back I2C roundtrips, ~100 ms
        // total) — acceptable since we are already holding the fresh
        // coordinates and the detection loop resumes on the next outer pass.
        // The compact template's _lat/_lon fields embed the fresh coordinates
        // automatically. event_id matches the paired beacon_alert.qo so
        // downstream systems can join the two notes without ambiguity.
        bool noteOk = false;
        for (uint8_t attempt = 0; attempt < 2 && !noteOk; attempt++) {
            J *req = notecard.newRequest("note.add");
            if (!req) continue;
            JAddStringToObject(req, "file", "beacon_location.qo");
            JAddBoolToObject(req,   "sync", true);
            J *body = JAddObjectToObject(req, "body");
            JAddStringToObject(body, "type",      g_gpsAlertType);
            JAddStringToObject(body, "worker_id", g_workerId);
            JAddNumberToObject(body, "event_id",  (double)g_gpsEventId);
            J *locRsp = notecard.requestAndResponse(req);
            noteOk = (locRsp != NULL && !notecard.responseError(locRsp));
            if (locRsp) notecard.deleteResponse(locRsp);
        }
        if (noteOk) {
            DEBUG_PRINT("[GPS] Location update queued for '");
            DEBUG_PRINT(g_gpsAlertType);
            DEBUG_PRINT("' event_id="); DEBUG_PRINT(g_gpsEventId);
            DEBUG_PRINTLN(".");
        } else {
            DEBUG_PRINTLN("[GPS] beacon_location.qo note.add failed after 2 "
                          "attempts — initial alert location stands.");
        }

        disableGps();
    }
}

// ─── Alert Note ───────────────────────────────────────────────────────────
// Queued immediately with the Notecard's current cached location (embedded
// via _lat/_lon in the compact template). loc_age_s records how old that fix
// was at alert time so downstream dispatch can judge location quality.
// event_id is written to the body so this note can be correlated with its
// beacon_location.qo follow-up using (device, event_id) as the join key.
//
// locAgeS is the cached-fix age in seconds computed by the caller at event-
// fire time (via card.time) and passed unchanged through every retry attempt.
// This guarantees every send — first try or last retry — reports the original
// fix age rather than the age relative to the retry timestamp. The sentinel
// value −1.0 means the fix age is unknown (no valid cached fix at alert time).
//
// beginGpsSearch() is called by the caller only after this function returns
// true, so no GPS search can be orphaned by a failed note.add.
//
// Single note.add attempt — no blocking retries. On failure the caller calls
// enqueueAlert() to add the alert to the fixed-depth retry queue so the
// detection loop is never stalled by Notecard I/O.
//
// voltage is initialised to -1.0 (sentinel) and updated only on a successful
// card.voltage read. A second attempt is made if the first fails (card.voltage
// occasionally loses I²C arbitration after a note.add burst) without delay so
// the detection loop is not stalled. Downstream rules should treat voltage < 0
// as "unknown" rather than "0 V".
bool sendAlert(const char *alertType, uint32_t eventId, float locAgeS)
{
    // Retry card.voltage once without delay before accepting failure.
    float voltage = -1.0f;
    for (uint8_t attempt = 0; attempt < 2 && voltage < 0.0f; attempt++) {
        J *vRsp = notecard.requestAndResponse(notecard.newRequest("card.voltage"));
        if (vRsp) {
            if (!notecard.responseError(vRsp))
                voltage = (float)JGetNumber(vRsp, "value");
            notecard.deleteResponse(vRsp);
        }
    }
    // locAgeS was computed by the caller at event-fire time and is used
    // unchanged here — no card.time call is made inside sendAlert() so
    // retried notes always carry the original fix age, not the retry-time age.

    // Single note.add attempt. On failure the caller calls enqueueAlert().
    J *req = notecard.newRequest("note.add");
    if (!req) return false;
    JAddStringToObject(req, "file", "beacon_alert.qo");
    JAddBoolToObject(req,   "sync", true);
    J *body = JAddObjectToObject(req, "body");
    JAddStringToObject(body, "type",      alertType);
    JAddStringToObject(body, "worker_id", g_workerId);
    JAddNumberToObject(body, "event_id",  (double)eventId);
    JAddNumberToObject(body, "voltage",   voltage);   // -1.0 = read failed
    JAddNumberToObject(body, "loc_age_s", locAgeS);
    // _lat/_lon populated from Notecard cache by compact template
    J *rsp = notecard.requestAndResponse(req);
    bool ok = (rsp != NULL && !notecard.responseError(rsp));
    if (rsp) notecard.deleteResponse(rsp);
    if (ok) {
        DEBUG_PRINT("[ALERT] type="); DEBUG_PRINT(alertType);
        DEBUG_PRINT(" event_id=");    DEBUG_PRINT(eventId);
        DEBUG_PRINT(" worker=");      DEBUG_PRINT(g_workerId);
        DEBUG_PRINT(" vbat=");        DEBUG_PRINT(voltage, 2);
        DEBUG_PRINT(" loc_age_s=");   DEBUG_PRINTLN((int)locAgeS);
    } else {
        DEBUG_PRINTLN("[ALERT] note.add failed — caller will retry.");
    }
    return ok;
}

// ─── Alert Enqueue ────────────────────────────────────────────────────────
// Appends a failed-send alert to the fixed-depth circular retry queue.
// cacheEpoch is the GPS-fix epoch captured at event time; it is kept so
// pollAlertRetry() can set g_gpsCacheEpoch before starting the GPS search
// after a successful retry. locAgeS is the cached-fix age computed once at
// event-fire time; it travels with the entry unchanged so every retry attempt
// reports the original fix age, not the age relative to the retry timestamp.
// event_id is preserved so the retried note carries the same correlation key
// as any eventual beacon_location.qo follow-up. If the queue is full
// (ALERT_QUEUE_DEPTH concurrent pending alerts — an extreme edge case), the
// entry is dropped with a debug log; the device cannot buffer unlimited alerts
// without dynamic allocation.
void enqueueAlert(const char *alertType, uint32_t cacheEpoch, uint32_t eventId,
                  float locAgeS)
{
    if (g_alertQueueCount >= ALERT_QUEUE_DEPTH) {
        DEBUG_PRINTLN("[ALERT] Retry queue full — alert dropped.");
        return;
    }
    uint8_t idx = (g_alertQueueHead + g_alertQueueCount) % ALERT_QUEUE_DEPTH;
    strncpy(g_alertQueue[idx].type, alertType,
            sizeof(g_alertQueue[idx].type) - 1);
    g_alertQueue[idx].type[sizeof(g_alertQueue[idx].type) - 1] = '\0';
    g_alertQueue[idx].cacheEpoch    = cacheEpoch;
    g_alertQueue[idx].event_id      = eventId;
    g_alertQueue[idx].locAgeS       = locAgeS;
    g_alertQueue[idx].retryCount    = 0;
    // Treat enqueue time as the "0th attempt" timestamp; the first retry fires
    // once (now - lastAttemptMs) >= ALERT_RETRY_INTERVAL_MS — wraparound-safe.
    g_alertQueue[idx].lastAttemptMs = millis();
    g_alertQueueCount++;
    DEBUG_PRINT("[ALERT] Enqueued '"); DEBUG_PRINT(alertType);
    DEBUG_PRINT("' event_id="); DEBUG_PRINT(eventId);
    DEBUG_PRINT(" for retry (queue depth: "); DEBUG_PRINT(g_alertQueueCount);
    DEBUG_PRINTLN(").");
}

// ─── Non-Blocking Alert Retry ─────────────────────────────────────────────
// Called once per outer loop pass before processing new fall/panic events.
// Services the head of the fixed-depth retry queue: waits until
// ALERT_RETRY_INTERVAL_MS has elapsed since the entry's last attempt
// (wraparound-safe), then makes one sendAlert() attempt. On success the
// entry is dequeued, g_lastAlertMs is updated, and a GPS follow-up search is
// started. On failure the retry count is incremented; after ALERT_RETRY_MAX
// attempts the entry is **discarded** — it was never successfully accepted by
// note.add, so the Notecard has nothing to reattempt. Only notes that
// note.add successfully queues are retried by the Notecard for delivery to
// Notehub. Only one attempt is made per outer loop pass so the detection path
// is never blocked by Notecard I/O.
//
// locAgeS and event_id are taken from the queued entry; both were captured
// at the original event-fire time so every retry attempt reports the same
// loc_age_s and correlation key as the first attempt.
void pollAlertRetry()
{
    if (g_alertQueueCount == 0) return;
    uint32_t now = millis();

    AlertQueueEntry &entry = g_alertQueue[g_alertQueueHead % ALERT_QUEUE_DEPTH];
    // Elapsed-time comparison (wraparound-safe). An absolute deadline would
    // misbehave around the 49.7-day millis() rollover.
    if ((now - entry.lastAttemptMs) < ALERT_RETRY_INTERVAL_MS) return;

    // Pass the event-time locAgeS directly to sendAlert() — it was pre-computed
    // at alert-fire time and stored in the queue entry, so the retried note
    // always carries the original fix age regardless of retry delay.
    // g_gpsCacheEpoch (the GPS freshness baseline for pollGpsSearch()) is only
    // updated here when no search is active and we are about to start one;
    // leaving it unchanged while a search runs prevents the epoch comparison
    // from being corrupted mid-search.
    bool sent = sendAlert(entry.type, entry.event_id, entry.locAgeS);
    if (sent) {
        g_lastAlertMs = now;
        if (!g_gpsSearching)
            g_gpsCacheEpoch = entry.cacheEpoch;
        beginGpsSearch(entry.type, entry.event_id);
        // Dequeue the successfully-sent entry.
        g_alertQueueHead  = (g_alertQueueHead + 1) % ALERT_QUEUE_DEPTH;
        g_alertQueueCount--;
        return;
    }

    if (++entry.retryCount >= ALERT_RETRY_MAX) {
        DEBUG_PRINT("[ALERT] Max retries ("); DEBUG_PRINT(ALERT_RETRY_MAX);
        DEBUG_PRINT(") reached for '"); DEBUG_PRINT(entry.type);
        DEBUG_PRINT("' event_id="); DEBUG_PRINT(entry.event_id);
        DEBUG_PRINTLN(" — alert dropped (note.add never succeeded).");
        // Discard and advance to the next queued alert.
        g_alertQueueHead  = (g_alertQueueHead + 1) % ALERT_QUEUE_DEPTH;
        g_alertQueueCount--;
    } else {
        entry.lastAttemptMs = now;
        DEBUG_PRINT("[ALERT] Retry "); DEBUG_PRINT(entry.retryCount);
        DEBUG_PRINT(" for '"); DEBUG_PRINT(entry.type);
        DEBUG_PRINTLN("' failed — rescheduled.");
    }
}

// ─── Haptic Feedback (non-blocking state machine) ─────────────────────────
// triggerHaptic() arms the sequencer; pollHaptic() (called from loop())
// advances it on each pass. The first pulse fires immediately on the next
// pollHaptic() call; subsequent pulses are spaced HAPTIC_PULSE_GAP_MS apart
// so they are always distinct, even at 10 Hz loop cadence.
//
// No blocking delays: the monitoring loop remains responsive to falls and
// button presses throughout a multi-buzz acknowledgment sequence.
void triggerHaptic(uint8_t effect, int pulses)
{
    if (!g_hapticReady) return;
    g_hapticEffect     = effect;
    g_hapticPulsesLeft = (uint8_t)pulses;
    g_hapticFirstPulse = true;   // skip gap before first pulse
}

void pollHaptic()
{
    if (!g_hapticReady || g_hapticPulsesLeft == 0) return;
    uint32_t now = millis();
    // Fire immediately on first pulse; enforce gap for subsequent pulses.
    if (!g_hapticFirstPulse && (now - g_hapticLastPulseMs) < HAPTIC_PULSE_GAP_MS)
        return;

    haptic.setWaveform(0, g_hapticEffect);
    haptic.setWaveform(1, 0);
    haptic.go();
    g_hapticPulsesLeft--;
    g_hapticLastPulseMs = now;
    g_hapticFirstPulse  = false;
}
