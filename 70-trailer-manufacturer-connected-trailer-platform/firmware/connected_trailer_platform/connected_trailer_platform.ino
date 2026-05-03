/***************************************************************************
  connected_trailer_platform.ino — Blues Connected Trailer Platform
  Notecarrier CX (Cygnet STM32L433) + Notecard for Skylo (NOTE-NBGLWX)

  Three sensor paths are fully implemented: cargo-air temperature via two NTC
  thermistors (A0/A1), rear-door state via a reed switch (D9), and GPS asset
  tracking via the Notecard's built-in GNSS. Two additional channels are wired
  into the data pipeline as integration-point stubs pending the vendor
  engineering described in README §9: J2497 reefer telemetry (Serial1) and
  TPMS tire pressure (SoftwareSerial D5/D6) — each parses a simplified POC
  frame format that no production hardware produces. The trailer state machine
  drives the Notehub sync cadence.

  Power model — dwell-capable host sleep:
    The Notecard for Skylo controls host power via the ATTN line on the
    Notecarrier CX. After each sample cycle the host serialises PersistState
    into the Notecard and calls NotePayloadSaveAndSleep(), which issues
    card.attn and then cuts host power. The Notecard wakes the host after
    sampleIntervalSec seconds by pulsing ATTN; setup() re-runs on every
    wakeup. During the sleep interval the host draws zero current — only the
    Notecard's radio-idle floor (~8–18 µA per Blues documentation) remains.

    UART acquisition during wakes:
      Both UART peripherals are offline while the host is powered off. On
      each wakeup Serial1 (J2497) and tpmsSerial (TPMS) are re-initialised
      in setup(), and both channels are drained for WAKE_UART_DRAIN_MS (250
      ms) before the sample cycle begins. Additional drains run between every
      blocking Notecard I2C call within the sample cycle. The built-in 64-byte
      STM32 hardware ring buffer on Serial1 captures bytes arriving during
      blocking I2C calls; loss is possible only if a Notecard response takes
      longer than ~67 ms (64 bytes × ~1 ms/byte at 9600 baud). SoftwareSerial
      for TPMS requires the CPU to be active; its drain window is limited to
      the host-awake period.

    State persistence across wakes:
      PersistState (g_ps) is serialised into the Notecard's _storage.qo at
      sleep time via NotePayloadAddSegment / NotePayloadSaveAndSleep, and
      restored via NotePayloadRetrieveAfterSleep on wakeup. It carries all
      inter-sample context: temperature accumulators, alert cooldown epochs,
      door state, TPMS last-known pressures (tpmsPsiLast[]), summary window
      start epoch (summaryWindowStartEpoch), the J2497 commissioning gate
      (j2497Commissioned), the reefer frame-received flag (reeferFrameSeen),
      and the current hub.set outbound cadence.

    Fallback (no ATTN host-power control):
      If ATTN is not wired for host power control (e.g., during bench
      commissioning without the ATTN connection), NotePayloadSaveAndSleep()
      returns without cutting power and loop() falls through to a delay()
      that mimics the sample cadence. The host draws continuous active
      current in this mode (~5–15 mA); it is not suitable for extended
      DC-dwell operation.

  J2497 and TPMS packet formats are simplified POC representations.
  Production deployments need a full J2497 protocol stack, reefer-OEM message
  mapping, and vendor-specific TPMS decode logic. See README §9.

  Door metrics (door_open_min, door_event_count) are accumulated at sample-
  cycle granularity: only transitions observed across consecutive wake
  boundaries are counted. An open/close cycle that begins and ends within a
  single inter-sample interval is not captured.
***************************************************************************/

#include <Arduino.h>
#include <SoftwareSerial.h>
#include <Notecard.h>

// Paste your Notehub ProductUID between the quotes below. Must be defined
// before #include "trailer_sensors.h" so the header's #ifndef guard picks it up.
#define PRODUCT_UID ""  // "com.your-company.your-name:connected_trailer"

#include "trailer_sensors.h"

// Define usbSerial to enable USB debug output and the Notecard debug stream.
// Comment out in production builds to eliminate the Serial wait penalty.
#define usbSerial Serial

// =========================================================================
// Global instances — declared extern in trailer_sensors.h
// =========================================================================

Notecard       notecard;
SoftwareSerial tpmsSerial(PIN_TPMS_RX, PIN_TPMS_TX);
Config         g_cfg;           // reset to compiled defaults at the start of
                                // each fetchEnvOverrides() call; not persisted
Sensors        g_sensors;       // freshly initialised (−9999 sentinels) each
                                // wakeup; restored from g_ps.tpmsPsiLast[] in
                                // setup() before the sample cycle
PersistState   g_ps;            // serialised to Notecard at sleep, restored at
                                // wakeup via NotePayload API
// g_reeferFrameReceived removed: reefer frame freshness is now tracked by the
// persisted g_ps.reeferFrameSeen field so it survives host-off sleep intervals.

static const char *kAlertNames[ALERT_COUNT] = {
    "reefer_temp_high", "reefer_temp_low",
    "tpms_pressure_low", "tpms_pressure_high",
    "door_open_transit", "reefer_sensor_loss"
};

// Session-scoped millis() fallbacks for alert cooldown and door-transit timer.
// These reset to 0 on every wakeup (millis() is 0 after host power-on).
// They enforce cooldowns only within the current wake session and are used
// only when nowEpoch == 0 (Notecard has not yet acquired a time lock).
// The epoch-based values in g_ps (lastAlertEpoch[], doorOpenTransitStartEpoch)
// are the primary cooldown mechanism and persist correctly across wakes.
static uint32_t g_lastAlertMs[ALERT_COUNT] = {};
static uint32_t g_doorOpenTransitStartMs   = 0;

// =========================================================================
// Utility helpers
// =========================================================================

// Clamp helpers guard against pathological env-var values (e.g. interval=0
// collapsing into a tight loop, or float thresholds outside physical range).
static uint32_t clampU32(long v, uint32_t lo, uint32_t hi, uint32_t def) {
    if (v < (long)lo || v > (long)hi) return def;
    return (uint32_t)v;
}
static float clampF(double v, float lo, float hi, float def) {
    if (v < (double)lo || v > (double)hi) return def;
    return (float)v;
}

// Retrieve the current Notecard epoch. Returns 0 when the Notecard has not
// acquired a time lock yet, or when the card.time response indicates an error.
// All callers must treat 0 as "time unavailable" and skip or degrade
// epoch-dependent features until a non-zero epoch is returned.
static uint32_t notecardEpoch() {
    J *rsp = notecard.requestAndResponse(notecard.newRequest("card.time"));
    if (!rsp) return 0;
    if (notecard.responseError(rsp)) { notecard.deleteResponse(rsp); return 0; }
    uint32_t epoch = (uint32_t)JGetNumber(rsp, "time");
    notecard.deleteResponse(rsp);
    return epoch;  // 0 = Notecard not yet time-synced
}

// =========================================================================
// Notecard configuration helpers
// =========================================================================

// Define the binary summary note template. Returns true on success.
static bool defineTemplate() {
    J *req = notecard.newRequest("note.template");
    if (!req) return false;
    JAddStringToObject(req, "file", NOTE_SUMMARY);
    JAddNumberToObject(req, "port", TEMPLATE_PORT);
    J *body = JAddObjectToObject(req, "body");
    JAddNumberToObject(body, "reefer_set_f",     14.1);
    JAddNumberToObject(body, "reefer_min_f",     14.1);
    JAddNumberToObject(body, "reefer_max_f",     14.1);
    JAddNumberToObject(body, "reefer_mean_f",    14.1);
    JAddNumberToObject(body, "air_t1_min_f",     14.1);
    JAddNumberToObject(body, "air_t1_max_f",     14.1);
    JAddNumberToObject(body, "air_t1_mean_f",    14.1);
    JAddNumberToObject(body, "air_t2_min_f",     14.1);
    JAddNumberToObject(body, "air_t2_max_f",     14.1);
    JAddNumberToObject(body, "air_t2_mean_f",    14.1);
    JAddNumberToObject(body, "door_open_min",    14.1);
    JAddNumberToObject(body, "door_event_count", 12);
    JAddNumberToObject(body, "tpms_0_psi",       14.1);
    JAddNumberToObject(body, "tpms_1_psi",       14.1);
    JAddNumberToObject(body, "tpms_2_psi",       14.1);
    JAddNumberToObject(body, "tpms_3_psi",       14.1);
    JAddNumberToObject(body, "tpms_0_age",       12);
    JAddNumberToObject(body, "tpms_1_age",       12);
    JAddNumberToObject(body, "tpms_2_age",       12);
    JAddNumberToObject(body, "tpms_3_age",       12);
    JAddNumberToObject(body, "trailer_state",    12);
    JAddNumberToObject(body, "sample_count",     12);
    J *rsp = notecard.requestAndResponse(req);
    if (!rsp) return false;
    bool ok = !notecard.responseError(rsp);
#ifdef usbSerial
    if (!ok) usbSerial.println("[init] note.template rejected — will retry next boot");
#endif
    notecard.deleteResponse(rsp);
    return ok;
}

// Apply card.location.mode — motion-gated GPS keeps GNSS off during DC dwells
// and powers it on only after the Notecard's built-in accelerometer detects
// trailer movement. Periodic mode is inherently motion-gated: the Notecard only
// powers the GNSS module when its accelerometer has detected motion since the
// last fix attempt.
static bool applyLocationMode() {
    J *req = notecard.newRequest("card.location.mode");
    if (!req) return false;
    JAddStringToObject(req, "mode",    "periodic");
    JAddNumberToObject(req, "seconds", 300);
    J *rsp = notecard.requestAndResponse(req);
    if (!rsp) return false;
    bool ok = !notecard.responseError(rsp);
#ifdef usbSerial
    if (!ok) usbSerial.println("[init] card.location.mode failed — will retry next boot");
#endif
    notecard.deleteResponse(rsp);
    return ok;
}

// Build the initial hub.set request from compile-time defaults.
// Called by sendRequestWithRetry() on every attempt; a new J* is required for
// each attempt because requestAndResponse() always frees the object it receives.
static J *buildInitHubSetReq() {
    J *req = notecard.newRequest("hub.set");
    if (!req) return nullptr;
    if (PRODUCT_UID[0]) JAddStringToObject(req, "product", PRODUCT_UID);
    JAddStringToObject(req, "mode",     "periodic");
    JAddNumberToObject(req, "outbound", (int)g_cfg.outboundParkedMin);
    JAddNumberToObject(req, "inbound",  (int)g_cfg.outboundParkedMin * 2);
    return req;
}

// Issue a Notecard request with up to maxAttempts tries (500 ms back-off
// between each). Returns true when the Notecard confirms the request.
static bool sendRequestWithRetry(J *(*factory)(), int maxAttempts) {
    for (int attempt = 0; attempt < maxAttempts; attempt++) {
        J *req = factory();
        if (!req) { delay(500); continue; }
        J *rsp = notecard.requestAndResponse(req);
        if (!rsp) { delay(500); continue; }
        bool ok = !notecard.responseError(rsp);
        notecard.deleteResponse(rsp);
        if (ok) return true;
        delay(500);
    }
    return false;
}

// First-boot Notecard configuration. Called once in the !restored branch
// of setup(). hub.set is issued with up to 10 retries to survive the cold-boot
// I2C-readiness race on Notecarrier CX. note.template and card.location.mode
// are attempted once; if they fail they will succeed on a subsequent power
// cycle once the Notecard I2C bus is stable.
static void notecardInit() {
    bool hubOk = sendRequestWithRetry(buildInitHubSetReq, 10);
    if (hubOk) {
        g_ps.currentOutboundMin = g_cfg.outboundParkedMin;
    }
#ifdef usbSerial
    else {
        usbSerial.println("[init] hub.set failed after 10 attempts — check I2C/power");
    }
#endif
    applyLocationMode();
    defineTemplate();
}

// Re-issue hub.set only when the cadence changes. Confirms the Notecard
// accepted the change before updating g_ps.currentOutboundMin so local state
// never drifts ahead of the Notecard on a transient I2C failure.
static bool applyHubSetIfChanged(uint32_t newOutboundMin) {
    if (newOutboundMin == g_ps.currentOutboundMin) return true;
    J *req = notecard.newRequest("hub.set");
    if (!req) return false;
    JAddStringToObject(req, "mode",     "periodic");
    JAddNumberToObject(req, "outbound", (int)newOutboundMin);
    JAddNumberToObject(req, "inbound",  (int)newOutboundMin * 2);
    J *rsp = notecard.requestAndResponse(req);
    if (!rsp) return false;
    bool ok = !notecard.responseError(rsp);
    notecard.deleteResponse(rsp);
    if (ok) g_ps.currentOutboundMin = newOutboundMin;  // update only on success
    return ok;
}

// Fetch env vars and apply them. g_cfg is reset to compiled defaults first
// so env vars deleted from Notehub revert to their compiled defaults on the
// same cycle that receives the inbound sync. Cadence changes are applied
// immediately via applyHubSetIfChanged().
static void fetchEnvOverrides() {
    g_cfg = Config();  // reset to compiled defaults before applying overrides

    J *req = notecard.newRequest("env.get");
    if (!req) return;
    J *rsp = notecard.requestAndResponse(req);
    if (!rsp) return;
    if (notecard.responseError(rsp)) { notecard.deleteResponse(rsp); return; }

    J *body = JGetObject(rsp, "body");
    if (!body) { notecard.deleteResponse(rsp); return; }

    #define GETF(k,v,lo,hi) { const char *s=JGetString(body,k); \
        if(s&&s[0]) v=clampF(atof(s),(lo),(hi),v); }
    #define GETU(k,v,lo,hi) { const char *s=JGetString(body,k); \
        if(s&&s[0]) v=clampU32(atol(s),(lo),(hi),v); }

    GETF("reefer_max_f",          g_cfg.reeferMaxF,         -60.0f,  120.0f)
    GETF("reefer_min_f",          g_cfg.reeferMinF,         -60.0f,  120.0f)
    GETF("tpms_min_psi",          g_cfg.tpmsMinPsi,          10.0f,  200.0f)
    GETF("tpms_max_psi",          g_cfg.tpmsMaxPsi,          10.0f,  200.0f)
    GETU("door_open_transit_sec", g_cfg.doorOpenTransitSec,     10u, 86400u)
    GETU("sample_interval_sec",   g_cfg.sampleIntervalSec,      30u,  3600u)
    GETU("summary_interval_min",  g_cfg.summaryIntervalMin,      1u,  1440u)
    GETU("outbound_transit_min",  g_cfg.outboundTransitMin,      1u,  1440u)
    GETU("outbound_parked_min",   g_cfg.outboundParkedMin,       1u,  1440u)
    GETU("alert_cooldown_sec",    g_cfg.alertCooldownSec,       60u, 86400u)
    #undef GETF
    #undef GETU

    notecard.deleteResponse(rsp);

    // Apply updated cadence immediately.
    applyHubSetIfChanged(
        g_ps.trailerState == STATE_IN_TRANSIT ? g_cfg.outboundTransitMin
                                              : g_cfg.outboundParkedMin);
}

// =========================================================================
// Trailer state machine
// =========================================================================

static void updateTrailerState(uint32_t nowEpoch) {
    (void)nowEpoch;
    J *rsp = notecard.requestAndResponse(notecard.newRequest("card.location"));
    if (!rsp) return;

    const char *errStr = JGetString(rsp, "err");
    bool hasErr = (errStr && errStr[0] != '\0');

    float lat = (float)JGetNumber(rsp, "lat");
    float lon = (float)JGetNumber(rsp, "lon");
    notecard.deleteResponse(rsp);

    bool validFix = !hasErr && (lat != 0.0f || lon != 0.0f);
    bool hasPrev  = (g_ps.lastLat != 0.0f || g_ps.lastLon != 0.0f);

    if (validFix && hasPrev) {
        float dL = lat - g_ps.lastLat, dO = lon - g_ps.lastLon;
        bool moving = (dL * dL + dO * dO) > 4.0e-7f;  // ≈ (50 m)² threshold
        g_ps.trailerState = moving             ? STATE_IN_TRANSIT :
                            g_sensors.doorOpen ? STATE_LOADING : STATE_PARKED;
    }
    // No valid fix (GNSS gap, first boot, or error): preserve the current
    // trailer state. Reclassifying from door state alone would incorrectly
    // demote an in-transit trailer to parked or loading cadence during a
    // temporary GNSS outage and would suppress door_open_transit alerts.
    if (validFix) { g_ps.lastLat = lat; g_ps.lastLon = lon; }

    applyHubSetIfChanged(
        g_ps.trailerState == STATE_IN_TRANSIT ? g_cfg.outboundTransitMin
                                              : g_cfg.outboundParkedMin);
}

// =========================================================================
// Alerts
// =========================================================================

// Returns true when the cooldown window has elapsed and it is safe to fire
// this alert type again. Epoch-based cooldown is the primary path once time
// is synced; millis()-based monotonic fallback applies only before time lock.
// The millis() fallback resets to 0 on every wakeup (host power-on); it
// enforces cooldown only within the current wake session. The epoch-based
// g_ps.lastAlertEpoch[] persists across wakes and is the reliable source.
static bool alertCooldownOk(uint8_t idx, uint32_t nowEpoch) {
    if (nowEpoch != 0) {
        if (g_ps.lastAlertEpoch[idx] == 0) return true;  // never sent; fire now
        return (nowEpoch - g_ps.lastAlertEpoch[idx]) >= g_cfg.alertCooldownSec;
    }
    // Monotonic fallback before time sync.
    if (g_lastAlertMs[idx] == 0) return true;  // never sent this session
    return (millis() - g_lastAlertMs[idx]) >= g_cfg.alertCooldownSec * 1000UL;
}

// Queue an immediate-sync alert note. Cooldown epoch stamped only after the
// Notecard confirms receipt so a failed send doesn't suppress the alert for
// the full cooldown window.
static void sendAlert(uint8_t idx, uint32_t nowEpoch) {
    if (!alertCooldownOk(idx, nowEpoch)) return;
    J *req = notecard.newRequest("note.add");
    if (!req) return;
    JAddStringToObject(req, "file", NOTE_ALERT);
    JAddBoolToObject(req,   "sync", true);
    J *body = JAddObjectToObject(req, "body");
    JAddStringToObject(body, "alert",           kAlertNames[idx]);
    JAddNumberToObject(body, "reefer_actual_f", g_sensors.reeferActualF);
    JAddNumberToObject(body, "reefer_set_f",    g_sensors.reeferSetF);
    JAddNumberToObject(body, "air_t1_f",        g_sensors.airT1F);
    JAddNumberToObject(body, "air_t2_f",        g_sensors.airT2F);
    JAddBoolToObject(body,   "door_open",       g_sensors.doorOpen);
    JAddNumberToObject(body, "tpms_0_psi",      g_sensors.tpmsPsi[0]);
    JAddNumberToObject(body, "tpms_1_psi",      g_sensors.tpmsPsi[1]);
    JAddNumberToObject(body, "tpms_2_psi",      g_sensors.tpmsPsi[2]);
    JAddNumberToObject(body, "tpms_3_psi",      g_sensors.tpmsPsi[3]);
    J *rsp = notecard.requestAndResponse(req);
    if (!rsp) return;
    bool ok = !notecard.responseError(rsp);
    notecard.deleteResponse(rsp);
    if (ok) {
        // Stamp both clocks on confirmed delivery.
        if (nowEpoch != 0) g_ps.lastAlertEpoch[idx] = nowEpoch;
        g_lastAlertMs[idx] = millis();
    }
}

static void evaluateAlerts(uint32_t nowEpoch) {
    if (g_sensors.reeferActualF > -1000.0f) {
        if (g_sensors.reeferActualF > g_cfg.reeferMaxF) sendAlert(A_REEFER_HIGH, nowEpoch);
        if (g_sensors.reeferActualF < g_cfg.reeferMinF) sendAlert(A_REEFER_LOW,  nowEpoch);
    }
    // reefer_sensor_loss is gated on j2497Commissioned: the alert fires only
    // after at least one valid J2497 frame has been accepted by drainReeferUart().
    // A bench build with no J2497 modem connected never sets this flag, so it
    // never fires reefer_sensor_loss regardless of how many wake cycles pass
    // with no frames. This matches the documented §8 expected baseline behavior.
    if (g_ps.j2497Commissioned && g_ps.reeferMissCount >= REEFER_MISS_MAX)
        sendAlert(A_REEFER_LOSS, nowEpoch);

    // Drain between alert groups to recover bytes that arrived during note.add.
    drainReeferUart(); drainTpmsUart();

    // Gate TPMS alerts on freshness: skip positions whose stale count has
    // reached TPMS_STALE_COUNT.
    bool tLow = false, tHigh = false;
    for (int i = 0; i < NUM_TPMS_POS; i++) {
        if (g_sensors.tpmsPsi[i] < -1000.0f) continue;          // sentinel
        if (g_ps.tpmsStaleCounts[i] >= TPMS_STALE_COUNT) continue;  // stale
        if (g_sensors.tpmsPsi[i] < g_cfg.tpmsMinPsi) tLow  = true;
        if (g_sensors.tpmsPsi[i] > g_cfg.tpmsMaxPsi) tHigh = true;
    }
    if (tLow)  sendAlert(A_TPMS_LOW,  nowEpoch);
    if (tHigh) sendAlert(A_TPMS_HIGH, nowEpoch);
    // Drain between TPMS and door alert groups.
    drainReeferUart(); drainTpmsUart();

    // Door-open transit alert. Uses epoch when synced (g_ps.doorOpenTransitStartEpoch
    // persists across wakes), millis() session fallback before time lock.
    if (g_sensors.doorOpen && g_ps.trailerState == STATE_IN_TRANSIT) {
        if (nowEpoch != 0) {
            if (g_ps.doorOpenTransitStartEpoch == 0) {
                g_ps.doorOpenTransitStartEpoch = nowEpoch;
                g_doorOpenTransitStartMs       = millis();
            }
        } else {
            if (g_doorOpenTransitStartMs == 0)
                g_doorOpenTransitStartMs = millis();
        }
        bool transitElapsed =
            (nowEpoch != 0 && g_ps.doorOpenTransitStartEpoch != 0)
                ? (nowEpoch - g_ps.doorOpenTransitStartEpoch >= g_cfg.doorOpenTransitSec)
                : (g_doorOpenTransitStartMs != 0 &&
                   millis() - g_doorOpenTransitStartMs >= g_cfg.doorOpenTransitSec * 1000UL);
        if (transitElapsed) sendAlert(A_DOOR_TRANSIT, nowEpoch);
    } else {
        g_ps.doorOpenTransitStartEpoch = 0;
        g_doorOpenTransitStartMs       = 0;
    }
}

// =========================================================================
// Summary note — window-aggregate statistics
// =========================================================================

static void sendSummary(uint32_t nowEpoch) {
    // Stage TPMS age increments and pressure invalidations in locals.
    // Mutations are committed to global state only after note.add succeeds.
    uint8_t newStaleCounts[NUM_TPMS_POS];
    uint8_t tpmsAges[NUM_TPMS_POS];
    float   newTpmsPsi[NUM_TPMS_POS];
    for (int i = 0; i < NUM_TPMS_POS; i++) {
        newTpmsPsi[i] = g_sensors.tpmsPsi[i];
        if (g_ps.tpmsSeenThisWindow[i]) {
            newStaleCounts[i] = 0;
            tpmsAges[i]       = 0;
        } else {
            newStaleCounts[i] = g_ps.tpmsStaleCounts[i] + 1;
            tpmsAges[i]       = newStaleCounts[i];
            if (newStaleCounts[i] >= TPMS_STALE_COUNT && newTpmsPsi[i] > -1000.0f)
                newTpmsPsi[i] = -9999.0f;
        }
    }

    // Credit any door-open duration spanning into this summary boundary.
    float doorMin = g_ps.doorOpenMinAccum;
    if (nowEpoch != 0 && g_ps.prevDoorOpen && g_ps.doorOpenSinceEpoch != 0)
        doorMin += (nowEpoch - g_ps.doorOpenSinceEpoch) / 60.0f;

    J *req = notecard.newRequest("note.add");
    if (!req) return;
    JAddStringToObject(req, "file", NOTE_SUMMARY);
    J *body = JAddObjectToObject(req, "body");
    JAddNumberToObject(body, "reefer_set_f",     g_ps.reeferSetLast);
    JAddNumberToObject(body, "reefer_min_f",     g_ps.reeferAccum.winMin());
    JAddNumberToObject(body, "reefer_max_f",     g_ps.reeferAccum.winMax());
    JAddNumberToObject(body, "reefer_mean_f",    g_ps.reeferAccum.winMean());
    JAddNumberToObject(body, "air_t1_min_f",     g_ps.airT1Accum.winMin());
    JAddNumberToObject(body, "air_t1_max_f",     g_ps.airT1Accum.winMax());
    JAddNumberToObject(body, "air_t1_mean_f",    g_ps.airT1Accum.winMean());
    JAddNumberToObject(body, "air_t2_min_f",     g_ps.airT2Accum.winMin());
    JAddNumberToObject(body, "air_t2_max_f",     g_ps.airT2Accum.winMax());
    JAddNumberToObject(body, "air_t2_mean_f",    g_ps.airT2Accum.winMean());
    JAddNumberToObject(body, "door_open_min",    doorMin);
    JAddNumberToObject(body, "door_event_count", (int)g_ps.doorEventCount);
    JAddNumberToObject(body, "tpms_0_psi",       newTpmsPsi[0]);
    JAddNumberToObject(body, "tpms_1_psi",       newTpmsPsi[1]);
    JAddNumberToObject(body, "tpms_2_psi",       newTpmsPsi[2]);
    JAddNumberToObject(body, "tpms_3_psi",       newTpmsPsi[3]);
    JAddNumberToObject(body, "tpms_0_age",       (int)tpmsAges[0]);
    JAddNumberToObject(body, "tpms_1_age",       (int)tpmsAges[1]);
    JAddNumberToObject(body, "tpms_2_age",       (int)tpmsAges[2]);
    JAddNumberToObject(body, "tpms_3_age",       (int)tpmsAges[3]);
    JAddNumberToObject(body, "trailer_state",    (int)g_ps.trailerState);
    JAddNumberToObject(body, "sample_count",     (int)g_ps.sampleCount);

    J *rsp = notecard.requestAndResponse(req);
    if (!rsp) return;  // Notecard unreachable — keep accumulators, retry next window
    bool ok = !notecard.responseError(rsp);
    notecard.deleteResponse(rsp);
    if (!ok) return;   // note rejected — keep accumulators, retry next window

    // Commit TPMS mutations on confirmed note.add.
    for (int i = 0; i < NUM_TPMS_POS; i++) {
        g_ps.tpmsStaleCounts[i]    = newStaleCounts[i];
        g_sensors.tpmsPsi[i]       = newTpmsPsi[i];
        g_ps.tpmsPsiLast[i]        = newTpmsPsi[i];  // keep persist copy in sync
        g_ps.tpmsSeenThisWindow[i] = false;
    }

    // Roll doorOpenSinceEpoch forward to the summary boundary.
    if (g_ps.prevDoorOpen && nowEpoch != 0 && g_ps.doorOpenSinceEpoch != 0)
        g_ps.doorOpenSinceEpoch = nowEpoch;

    // Reset window accumulators. g_ps.prevDoorOpen is intentionally NOT reset
    // — edge-detection continuity across the window boundary requires carrying
    // the last known door state.
    g_ps.reeferAccum.reset();
    g_ps.airT1Accum.reset();
    g_ps.airT2Accum.reset();
    g_ps.doorOpenMinAccum = 0.0f;
    g_ps.doorEventCount   = 0;
    g_ps.sampleCount      = 0;

    // Advance the epoch-based window boundary. Used by runSampleCycle() to
    // determine when the next summary is due across sleep intervals.
    if (nowEpoch != 0) g_ps.summaryWindowStartEpoch = nowEpoch;
}

// =========================================================================
// One sample cycle
// =========================================================================

// Perform one complete sense → accumulate → alert → summarize pass.
//
// Precondition: drainReeferUart() and drainTpmsUart() have already run during
// the WAKE_UART_DRAIN_MS window in setup() and have latched the most recent
// valid frames into g_sensors. Thermistors and door state are read here;
// the reads are fast (no buffering needed) and the sample-time value is the
// correct input for statistics accumulation and threshold evaluation.
static void runSampleCycle(uint32_t nowEpoch) {
    // Resolve door state: prefer the ISR-latched value (captures intra-sample
    // transitions) over a bare digitalRead().
    if (g_doorIsrFired) {
        g_sensors.doorOpen = g_doorIsrState;
        g_doorIsrFired     = false;
    } else {
        g_sensors.doorOpen = (digitalRead(PIN_DOOR) == LOW);
    }

    readThermistors();
    accumulateSampleStats(nowEpoch);
    drainReeferUart(); drainTpmsUart();
    updateTrailerState(nowEpoch);      // blocking: card.location request
    drainReeferUart(); drainTpmsUart();
    // Assess reefer health after all major blocking I2C drains — the wakeup
    // drain window (setup) and the two inter-call drains above have had their
    // chance to set g_ps.reeferFrameSeen. Evaluating here means evaluateAlerts()
    // uses the current wake's miss count rather than the previous wake's.
    // Any frame arriving after this call (inside evaluateAlerts() internal drains
    // or the final loop() drain) persists in g_ps.reeferFrameSeen and is credited
    // on the next wake's updateReeferMissCount() call.
    updateReeferMissCount();
    evaluateAlerts(nowEpoch);          // may block: up to 6 note.add calls
    drainReeferUart(); drainTpmsUart();

    // Summary window boundary: epoch-based when time is synced, sample-count
    // fallback otherwise. The epoch boundary persists correctly across wakes
    // because g_ps.summaryWindowStartEpoch is part of PersistState. The sample-
    // count fallback fires when the expected number of samples per window have
    // accumulated without a valid epoch, preventing a stuck window on devices
    // that never acquire a time lock before the first summary is due.
    //
    // The sample-count fallback is approximate: it is only active before the
    // Notecard acquires a time lock, and env-var changes to sample_interval_sec
    // or summary_interval_min mid-window alter the expected count without
    // resetting sampleCount. samplesPerWindow is clamped to at least 1 so
    // that a sample_interval_sec greater than summary_interval_min * 60 does
    // not collapse to samplesPerWindow == 0 and fire a summary on every wake.
    bool summaryDue;
    if (nowEpoch != 0 && g_ps.summaryWindowStartEpoch != 0) {
        summaryDue = (nowEpoch - g_ps.summaryWindowStartEpoch >=
                      g_cfg.summaryIntervalMin * 60UL);
    } else {
        uint32_t windowSec        = g_cfg.summaryIntervalMin * 60UL;
        uint32_t sampleSec        = g_cfg.sampleIntervalSec;
        // Round up so a slow sample rate still triggers at least one summary.
        uint32_t samplesPerWindow = max(1UL, (windowSec + sampleSec - 1) / sampleSec);
        summaryDue = (g_ps.sampleCount >= samplesPerWindow);
    }
    if (summaryDue)
        sendSummary(nowEpoch);         // blocking: note.add request
}

// =========================================================================
// Arduino entry points
// =========================================================================

// setup() runs on every wakeup (whether fresh power-on or ATTN-gated resume).
// It restores PersistState from the Notecard's sleep payload when available,
// runs the full one-time Notecard initialisation on fresh boot only, fetches
// env-var overrides, drains both UART channels for WAKE_UART_DRAIN_MS, and
// then runs one complete sample cycle. loop() saves state and sleeps.
void setup() {
#ifdef usbSerial
    usbSerial.begin(115200);
    for (uint32_t t0 = millis(); !usbSerial && (millis() - t0) < 3000; ) {}
#endif

    // Initialise hardware on every wakeup (peripherals are re-powered each time).
    Serial1.begin(REEFER_BAUD);
    tpmsSerial.begin(TPMS_BAUD);
    pinMode(PIN_DOOR, INPUT_PULLUP);
    analogReadResolution(12);

    notecard.begin();
#ifdef usbSerial
    notecard.setDebugOutputStream(usbSerial);
#endif

    // Attempt to restore PersistState from the Notecard sleep payload.
    // NotePayloadRetrieveAfterSleep() returns true when a valid payload exists
    // (normal wake from NotePayloadSaveAndSleep). !restored means either first
    // power-on, payload corruption, or no prior NotePayloadSaveAndSleep call.
    NotePayloadDesc payload = {};
    bool restored = NotePayloadRetrieveAfterSleep(&payload);
    if (restored) {
        restored &= NotePayloadGetSegment(&payload, STATE_SEG_ID,
                                          &g_ps, sizeof(g_ps));
        NotePayloadFree(&payload);
    }

    if (!restored) {
        // Fresh boot: zero-initialise state and configure the Notecard.
        memset(&g_ps, 0, sizeof(g_ps));
        g_ps.trailerState  = STATE_PARKED;
        g_ps.reeferSetLast = -9999.0f;
        g_ps.reeferAccum.reset();
        g_ps.airT1Accum.reset();
        g_ps.airT2Accum.reset();
        for (int i = 0; i < NUM_TPMS_POS; i++)
            g_ps.tpmsPsiLast[i] = -9999.0f;
        // g_ps.j2497Commissioned = false (memset)
        // g_ps.summaryWindowStartEpoch = 0 (memset; seeded below after epoch)
        // g_ps.lastAlertEpoch[] = {0} (memset; "never sent" on first wake)
        notecardInit();
    }
#ifdef usbSerial
    usbSerial.println(restored ? "[boot] state restored from sleep payload"
                               : "[boot] fresh start — Notecard configured");
#endif

    // Fetch env overrides on every wake to apply any OTA configuration changes
    // that arrived while the host was sleeping (Notehub pushes env vars on the
    // Notecard's configured inbound sync cadence; the host doesn't need to be
    // awake when they arrive).
    fetchEnvOverrides();

    // Attach the door interrupt on every wakeup — required after hardware
    // re-initialisation, not just at fresh boot.
    setupDoorInterrupt();

    // Restore last-known TPMS pressures from PersistState into g_sensors.
    // g_sensors.tpmsPsi[] is freshly initialised to −9999 on each wakeup (the
    // global is re-constructed after host power-on). Without this restoration,
    // sendSummary() would report −9999 for any TPMS position that did not send
    // a new frame in the current drain window, even if it reported in a prior
    // wake during the same summary window. Positions that reported a fresh frame
    // in the drain window below will overwrite these restored values correctly.
    for (int i = 0; i < NUM_TPMS_POS; i++)
        g_sensors.tpmsPsi[i] = g_ps.tpmsPsiLast[i];

    // Post-wakeup UART drain window. Both UART peripherals were offline while
    // the host was powered down; this window captures frames arriving in the
    // first WAKE_UART_DRAIN_MS milliseconds after Serial1 and tpmsSerial are
    // re-initialised. Additional drains run between Notecard I2C calls inside
    // runSampleCycle() to recover bytes arriving during blocking transactions.
    {
        uint32_t drainUntil = millis() + WAKE_UART_DRAIN_MS;
        while (millis() < drainUntil) {
            drainReeferUart();
            drainTpmsUart();
        }
    }

    uint32_t nowEpoch = notecardEpoch();
    drainReeferUart(); drainTpmsUart();  // drain during card.time I2C call

    // Seed the summary window start epoch on the first wake that has a valid
    // time lock. Subsequent updates happen inside sendSummary().
    if (g_ps.summaryWindowStartEpoch == 0 && nowEpoch != 0)
        g_ps.summaryWindowStartEpoch = nowEpoch;

    runSampleCycle(nowEpoch);
}

// loop() runs once per wakeup: drains both UART channels a final time,
// serialises PersistState into the Notecard, and calls NotePayloadSaveAndSleep()
// to schedule the next wakeup and cut host power. If ATTN is not wired for
// host power control (bench commissioning without the ATTN jumper), the function
// returns and the delay() fallback mimics the sample cadence without true sleep.
void loop() {
    // Final drain before sleeping: captures any bytes that arrived during or
    // after runSampleCycle(). These frames update g_sensors and g_ps (including
    // tpmsPsiLast[]) so the most current values are serialised in the payload.
    drainReeferUart();
    drainTpmsUart();

    // Serialise PersistState and sleep. NotePayloadSaveAndSleep() issues
    // card.attn with mode=sleep and sleepSeconds=sampleIntervalSec, saves the
    // payload to the Notecard's _storage.qo notefile, and then signals the
    // Notecarrier CX to cut host power via ATTN. The Notecard wakes the host
    // after sampleIntervalSec seconds; setup() re-runs on the next wakeup.
    // Only g_ps is serialised — g_cfg is re-derived from env vars each wake.
    NotePayloadDesc payload = {};
    NotePayloadAddSegment(&payload, STATE_SEG_ID, &g_ps, sizeof(g_ps));
    NotePayloadSaveAndSleep(&payload, g_cfg.sampleIntervalSec, NULL);

    // Reached only if host power is not being cut via ATTN. The delay() mimics
    // the sample cadence but the host remains fully powered throughout, drawing
    // continuous active current (~5–15 mA). Not suitable for extended DC-dwell
    // operation; use only during bench commissioning without the ATTN connection.
#ifdef usbSerial
    usbSerial.println("[sleep] ATTN not cutting host power — using delay fallback");
#endif
    delay(g_cfg.sampleIntervalSec * 1000UL);
}
