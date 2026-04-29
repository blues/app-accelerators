// sanitation_unit_monitor.ino
//
// Portable Sanitation Unit Service Monitor
// Host:       Blues Notecarrier CX (onboard STM32L433 Cygnet host)
// Notecard:   Blues Notecard Cell+WiFi (MBGLW)
// Sensors:    1x MaxBotix HRXL-MaxSonar-WR MB7389 sealed ultrasonic (holding-tank fill level)
//             NOTE: MB7389 is used here for bench/POC evaluation. Field deployments
//             require a certified intrinsically safe (IS) sensor or a mounting geometry
//             that keeps all energized electronics outside the hazardous-atmosphere zone.
//             See README §3 and §4.
//
// Reports holding-tank fill level to Notehub so service providers can route
// pump trucks on actual tank condition rather than a fixed schedule.
//
// Runtime cadence:
//   - Host wakes every SAMPLE_INTERVAL_SEC via card.attn.
//   - Each wake: read fill level, accumulate stats.
//   - After SUMMARY_INTERVAL_MIN, emit one sanitation_summary.qo note.
//   - When fill exceeds FILL_ALERT_PCT, emit sanitation_alert.qo (sync:true).
//   - Between wakes, both the host and the Notecard radio are in their
//     lowest-power idle states.

#include <Notecard.h>
#include <Wire.h>

#ifndef PRODUCT_UID
#define PRODUCT_UID "" // "com.my-company.my-name:sanitation-monitor"
#pragma message "PRODUCT_UID not set. Claim one in Notehub, then define it here."
#endif

// Compile-time opt-in for USB-serial debug output.
// Uncomment the line below (or pass -DENABLE_USB_DEBUG via build flags) to enable.
// Do NOT leave this defined in production firmware: it initialises USB serial on
// every wake and waits up to 3 s for host enumeration, which materially increases
// active time and current draw on a battery-powered unit.
// #define ENABLE_USB_DEBUG

// -------- Pin assignments (Notecarrier CX dual 16-pin header) --------
// PIN_PW: MaxBotix MB7389 PW output is driven by the sensor — no pull-up needed.
// INPUT_PULLDOWN is applied so the GPIO idles low whenever the sensor cable is
// absent or disconnected. Without a defined bias the pin floats, causing
// pulseIn() to return spurious non-zero widths that are indistinguishable from
// real distance pulses — breaking the documented "sensor absent → NAN → -9999
// sentinel" path and potentially triggering false fill readings or fill_high
// alerts. The STM32L433 Cygnet on the Notecarrier CX supports INPUT_PULLDOWN
// natively; no external resistor is required.
// The sensor runs continuously in free-running mode; no trigger pulse is required.
static const uint8_t PIN_PW    = 5;   // D5 — MaxBotix PW output (1 µs/mm PWM)

// -------- Sensor constants --------
// MB7389 PWM output: HIGH pulse width = 1 µs per mm of distance.
// Sensor cycle time ≈ 150 ms (6.66 Hz). pulseIn() must time out long enough
// to wait for the next rising edge (up to ~150 ms of LOW before the HIGH starts).
static const uint32_t PW_TIMEOUT_US = 250000UL;  // 250 ms covers full sensor cycle

// Readings below 30 cm (300 mm → 300 µs) fall inside the MB7389 dead zone;
// the sensor saturates to ~30 cm for any closer target — discard them as NAN.
// IMPORTANT: dead-zone readings do NOT register as 100 % fill — they disappear
// entirely from fill reporting. The installation MUST be configured so the waste
// surface never enters the 30 cm dead zone during normal service (see tank_full_cm
// below and §9 of the README).
static const uint32_t PW_MIN_US = 300UL;

// -------- Default thresholds / intervals (overridable via env vars) --------
// tank_empty_cm  — measured distance (cm) from sensor face to waste surface
//                  when the tank is considered empty. Default 65 cm assumes the
//                  MB7389 probe is mounted roughly 20 cm above a typical 60-gallon
//                  porta-potty tank (~45 cm internal headspace when empty).
//                  Measure and set this at install time.
// tank_full_cm   — measured distance when the tank is at pump-out capacity.
//                  Must be ≥ 35 cm (MB7389 minimum range is 30 cm; the 5 cm
//                  margin keeps the waste surface out of the dead zone at maximum
//                  fill). Readings between tank_full_cm and ~30 cm are outside the
//                  dead zone and clamp to 100 % in distanceToFillPct(), but readings
//                  strictly inside the dead zone (< 30 cm) are discarded as NAN and
//                  do NOT clamp to 100 %. Correct mechanical installation — waste
//                  surface at least 35 cm from the probe face at pump-out level —
//                  is required to keep fill reporting valid through the full service
//                  cycle. See §9 of the README.
static float    TANK_EMPTY_CM       = 65.0f;  // cm → 0% fill
static float    TANK_FULL_CM        = 35.0f;  // cm → 100% fill (must be ≥ 35 cm)
static float    FILL_ALERT_PCT      = 75.0f;  // % above which alert fires
static uint32_t SAMPLE_INTERVAL_SEC = 300;    // 5 min between wakes
static uint32_t SUMMARY_INTERVAL_MIN = 60;    // 1 hour between summaries
static uint32_t INBOUND_INTERVAL_MIN = 360;   // 6 h between env-var syncs from Notehub

// 4-hour alert cooldown: a fill-high condition fires once, then is suppressed
// until the tank is serviced (level drops) or 4 hours have passed. Prevents
// an already-full tank from paging dispatch every 5 minutes.
static const uint32_t ALERT_COOLDOWN_SEC = 14400UL;

// Sentinel: emitted in place of a summary field when no valid samples were
// collected for that metric in the window (sensor disconnected, out of range).
static const float INVALID_SENTINEL = -9999.0f;

// -------- State preserved across sleep cycles --------
// NotePayloadSaveAndSleep serialises this struct into Notecard flash before
// cutting host power; NotePayloadRetrieveAfterSleep restores it on the next
// wake. All mutable runtime state must live here.
struct PersistState {
    // Fill-level accumulation for the current summary window
    float    fill_sum;             // sum of valid fill-% readings
    uint32_t fill_n;               // count of valid readings
    float    fill_peak;            // highest fill % seen in this window

    // Relative elapsed-time counters (seconds accumulated since the event).
    // Using elapsed counters instead of wall-clock epochs avoids the unsigned-
    // subtraction wrap hazard that arises when card.time returns 0 on a newly-
    // provisioned or recently-rebooted device. No wall-clock epoch is used for
    // timer decisions anywhere in this firmware.
    //
    // summary_elapsed_sec: accumulated since the current summary window opened.
    //   Incremented by SAMPLE_INTERVAL_SEC on each wake. A summary Note is
    //   emitted and the counter reset to 0 when it reaches
    //   SUMMARY_INTERVAL_MIN * 60. Reset happens only after a successful enqueue.
    //
    // alert_elapsed_sec: accumulated since the most recent fill_high alert.
    //   Initialised to ALERT_COOLDOWN_SEC on cold boot so a fill-high condition
    //   on the very first wake fires immediately. Reset to 0 only after a
    //   successful alert enqueue, so a transient I2C error does not silently
    //   advance the cooldown without actually sending the alert.
    uint32_t summary_elapsed_sec;
    uint32_t alert_elapsed_sec;

    // Track the outbound and inbound cadences last sent to hub.set; re-apply
    // when either changes via env var so Notecard stays in sync.
    uint32_t last_applied_outbound_min;
    uint32_t last_applied_inbound_min;

    // Template registration status. Tracked in persistent state so that a
    // one-time cold-boot I2C failure does not silently leave the deployment
    // untemplated for its entire lifetime. defineTemplates() is retried on
    // every wake until it returns true.
    bool templates_ok;
};
static const char STATE_SEG_ID[] = "SANI";
static PersistState state;

Notecard notecard;

// -------- Notecard helpers --------

static void hubConfigure() {
    J *req = notecard.newRequest("hub.set");
    if (req) {
        if (PRODUCT_UID[0]) JAddStringToObject(req, "product", PRODUCT_UID);
        JAddStringToObject(req, "mode",     "periodic");
        JAddNumberToObject(req, "outbound", (int)SUMMARY_INTERVAL_MIN);
        JAddNumberToObject(req, "inbound",  (int)INBOUND_INTERVAL_MIN);
        // sendRequestWithRetry on the FIRST Notecard transaction avoids the
        // cold-boot I2C race condition documented in the note-arduino library.
        // Only cache the applied cadence values when the request succeeds so the
        // sketch doesn't falsely believe the Notecard is configured if the call
        // fails (e.g., Notecard still booting during cold start).
        if (notecard.sendRequestWithRetry(req, 10)) {
            state.last_applied_outbound_min = SUMMARY_INTERVAL_MIN;
            state.last_applied_inbound_min  = INBOUND_INTERVAL_MIN;
        }
    }
}

// Register both Note templates. Returns true only when both requests succeed.
// Result is stored in state.templates_ok and retried on subsequent wakes so a
// one-time cold-boot failure does not silently leave the device untemplated.
// Templates compress each Note from free-form JSON into a compact fixed-length
// binary record. Over a multi-year deployment on a prepaid SIM the 3-5x
// bandwidth saving is material. Template encoding:
//   14.1 → 4-byte float   12 → 2-byte signed integer   22 → 4-byte unsigned int
static bool defineTemplates() {
    bool ok1 = false, ok2 = false;

    J *req = notecard.newRequest("note.template");
    if (req) {
        JAddStringToObject(req, "file", "sanitation_summary.qo");
        JAddNumberToObject(req, "port", 50);
        J *body = JAddObjectToObject(req, "body");
        if (body) {
            JAddNumberToObject(body, "fill_pct_avg",  14.1); // avg fill % in window
            JAddNumberToObject(body, "fill_pct_peak", 14.1); // highest fill % in window
            JAddNumberToObject(body, "fill_cm_last",  14.1); // raw distance at last sample
        }
        ok1 = notecard.sendRequest(req);
    }

    // Alert Note template — fires immediately on threshold trip.
    req = notecard.newRequest("note.template");
    if (req) {
        JAddStringToObject(req, "file", "sanitation_alert.qo");
        JAddNumberToObject(req, "port", 51);
        J *body = JAddObjectToObject(req, "body");
        if (body) {
            // Use the actual alert string literal so the Notecard sizes the
            // template slot to exactly the right length. On pre-v3.2.1 firmware
            // the slot size equals the byte length of the example string
            // (9 bytes for "fill_high"). Update this literal if longer alert
            // names are added to the firmware.
            JAddStringToObject(body, "alert",    "fill_high");
            JAddNumberToObject(body, "fill_pct", 14.1);
            JAddNumberToObject(body, "fill_cm",  14.1);
        }
        ok2 = notecard.sendRequest(req);
    }

    return ok1 && ok2;
}

static void fetchEnvOverrides() {
    J *req = notecard.newRequest("env.get");
    if (!req) return;

    J *names = JAddArrayToObject(req, "names");
    JAddItemToArray(names, JCreateString("tank_empty_cm"));
    JAddItemToArray(names, JCreateString("tank_full_cm"));
    JAddItemToArray(names, JCreateString("fill_alert_pct"));
    JAddItemToArray(names, JCreateString("sample_interval_sec"));
    JAddItemToArray(names, JCreateString("summary_interval_min"));
    JAddItemToArray(names, JCreateString("inbound_interval_min"));

    J *rsp = notecard.requestAndResponse(req);
    if (!rsp) return;
    if (notecard.responseError(rsp)) { notecard.deleteResponse(rsp); return; }

    J *body = JGetObject(rsp, "body");
    if (body) {
        const char *v;

        // Stage tank_empty_cm and tank_full_cm as a pair and commit both only
        // when the resulting geometry is physically consistent (empty > full).
        // This prevents a partial or sequenced env-var update from inverting the
        // calibration pair, which would cause distanceToFillPct() to return NAN
        // for every sample and silently halt fill monitoring.
        float new_empty = TANK_EMPTY_CM;
        float new_full  = TANK_FULL_CM;

        v = JGetString(body, "tank_empty_cm");
        if (v && v[0]) {
            float f = (float)atof(v);
            // Lower bound > 35 cm ensures tank_empty stays above the MB7389
            // dead zone (30 cm) with margin and above the minimum valid
            // tank_full_cm (35 cm), keeping the full/empty geometry valid on
            // the empty side even when only tank_empty_cm is being updated.
            if (f > 35.0f && f < 500.0f) new_empty = f;
        }
        v = JGetString(body, "tank_full_cm");
        if (v && v[0]) {
            float f = (float)atof(v);
            // Enforce ≥ 35 cm: MB7389 dead zone is 30 cm; at least 5 cm margin
            // above it keeps the waste surface detectable at maximum fill level.
            if (f >= 35.0f) new_full = f;
        }
        // Commit both values only if the pair is physically consistent.
        if (new_empty > new_full) {
            TANK_EMPTY_CM = new_empty;
            TANK_FULL_CM  = new_full;
        }

        v = JGetString(body, "fill_alert_pct");
        if (v && v[0]) {
            float f = (float)atof(v);
            if (f > 0.0f && f <= 100.0f) FILL_ALERT_PCT = f;
        }
        v = JGetString(body, "sample_interval_sec");
        if (v && v[0]) {
            long l = atol(v);
            if (l >= 30 && l <= 86400) SAMPLE_INTERVAL_SEC = (uint32_t)l;
        }
        v = JGetString(body, "summary_interval_min");
        if (v && v[0]) {
            long l = atol(v);
            if (l >= 1 && l <= 1440) SUMMARY_INTERVAL_MIN = (uint32_t)l;
        }
        v = JGetString(body, "inbound_interval_min");
        if (v && v[0]) {
            long l = atol(v);
            // Clamp to [30, 1440] — faster than 30 min drains battery on inbound
            // connections; slower than 24 h is impractical for commissioning.
            if (l >= 30 && l <= 1440) INBOUND_INTERVAL_MIN = (uint32_t)l;
        }
    }
    notecard.deleteResponse(rsp);

    // If outbound or inbound cadence changed via env var, re-apply hub.set so
    // the Notecard's sync schedule stays in sync with the updated values.
    // PRODUCT_UID is included on every hub.set so a recovery call after a failed
    // cold-boot hubConfigure() can still claim the Notecard to the intended
    // Notehub project, not just update cadence.
    if (SUMMARY_INTERVAL_MIN != state.last_applied_outbound_min ||
        INBOUND_INTERVAL_MIN  != state.last_applied_inbound_min) {
        J *req2 = notecard.newRequest("hub.set");
        if (req2) {
            if (PRODUCT_UID[0]) JAddStringToObject(req2, "product", PRODUCT_UID);
            JAddStringToObject(req2, "mode",     "periodic");
            JAddNumberToObject(req2, "outbound", (int)SUMMARY_INTERVAL_MIN);
            JAddNumberToObject(req2, "inbound",  (int)INBOUND_INTERVAL_MIN);
            // Only update the cached applied values when the request succeeds so
            // the sketch doesn't drift out of sync with the Notecard on a failed
            // transaction (e.g., momentary I2C error).
            if (notecard.sendRequest(req2)) {
                state.last_applied_outbound_min = SUMMARY_INTERVAL_MIN;
                state.last_applied_inbound_min  = INBOUND_INTERVAL_MIN;
            }
        }
    }
}

// -------- Sensor reading --------

// Read the MaxBotix HRXL-MaxSonar-WR (MB7389) in free-running PWM mode.
// The sensor drives PIN_PW continuously at ~6.66 Hz (one reading per ~150 ms);
// each HIGH pulse width represents the measured distance at 1 µs/mm — no trigger
// pulse is needed. Three successive pulseIn() calls naturally catch three separate
// sensor cycles because pulseIn() waits for the next rising edge, synchronising
// itself to the sensor's timing.
// Returns the median of three readings in cm, or NAN when fewer than 2 of 3
// are valid (sensor absent, wiring fault, or target within the 30 cm dead zone).
//
// Dead-zone note: readings below PW_MIN_US (300 µs = 30 cm) are discarded as
// NAN. They do NOT register as 100 % fill — if the waste surface ever enters the
// dead zone, fill reporting disappears rather than saturating. The mechanical
// installation must ensure the waste surface stays ≥ 35 cm from the probe face
// throughout the service cycle (see tank_full_cm above and §9 of the README).
//
// Bench evaluation with an Adafruit RCWL-1601: replace PIN_PW (D5) with PIN_ECHO
// (D5) and add PIN_TRIG (D6), then substitute the HC-SR04 trigger/echo protocol:
//   pinMode(6, OUTPUT); digitalWrite(6, LOW);
//   digitalWrite(6, HIGH); delayMicroseconds(10); digitalWrite(6, LOW);
//   long dur = pulseIn(5, HIGH, 30000UL);
//   float dist_cm = (dur == 0) ? NAN : dur / 58.0f;
// The RCWL-1601 is a bare PCB module — see the bench-evaluation note in §4 of
// the README for the acoustic-aperture arrangement and its safety constraints.
// Do not deploy the RCWL-1601 in a real tank environment.
static float readDistanceCm() {
    float samples[3];
    for (uint8_t i = 0; i < 3; i++) {
        long pw = pulseIn(PIN_PW, HIGH, PW_TIMEOUT_US);
        if (pw == 0 || (uint32_t)pw < PW_MIN_US) {
            samples[i] = NAN;          // timeout or within 30 cm dead zone
        } else {
            samples[i] = (float)pw / 10.0f;   // 1 µs/mm ÷ 10 = cm
        }
        // No explicit inter-reading delay: pulseIn() synchronises to the next
        // sensor cycle by waiting for the rising edge (~150 ms between pulses).
    }

    // Median of 3: sort and return middle element. NAN sorts to top.
    for (uint8_t i = 0; i < 2; i++) {
        for (uint8_t j = i + 1; j < 3; j++) {
            bool j_is_nan = isnan(samples[j]);
            bool i_is_nan = isnan(samples[i]);
            if ((!j_is_nan && !i_is_nan && samples[j] < samples[i]) ||
                (i_is_nan && !j_is_nan)) {
                float tmp = samples[i]; samples[i] = samples[j]; samples[j] = tmp;
            }
        }
    }
    return samples[1]; // median; NAN if ≥2 of 3 reads timed out
}

// Convert a raw distance (cm) into a fill percentage [0–100].
// Out-of-range distances are clamped to 0 % (sensor farther than TANK_EMPTY_CM)
// or 100 % (sensor closer than TANK_FULL_CM but outside the 30 cm dead zone)
// rather than returning NAN, so downstream accumulation always gets a usable
// value when the sensor is live and the installation geometry is correct.
// Returns NAN when dist_cm is NAN (dead-zone reading or sensor absent) or when
// the calibration is inverted (TANK_EMPTY_CM <= TANK_FULL_CM).
// Note: readings inside the 30 cm dead zone arrive here as NAN from
// readDistanceCm() and are NOT clamped to 100 %. See the dead-zone note in
// readDistanceCm() and §9 of the README for installation requirements.
static float distanceToFillPct(float dist_cm) {
    if (isnan(dist_cm)) return NAN;
    // Guard against inverted or uncalibrated tank_full/empty configuration.
    if (TANK_EMPTY_CM <= TANK_FULL_CM) return NAN;
    float pct = (TANK_EMPTY_CM - dist_cm) / (TANK_EMPTY_CM - TANK_FULL_CM) * 100.0f;
    // Clamp to physical limits; don't propagate values outside the tank range.
    if (pct < 0.0f)   pct = 0.0f;
    if (pct > 100.0f) pct = 100.0f;
    return pct;
}

// -------- Note emission --------

// Enqueue a fill alert Note with sync:true for an immediate cellular session.
// Returns true if the Note was successfully enqueued in the Notecard queue.
// Retries once on failure because alerts are the highest-value events in this
// design — a dropped fill_high alert can leave an overflowing tank unserviced.
// The caller advances alert_elapsed_sec only on a true return.
static bool sendAlert(const char *kind, float fill_pct, float fill_cm) {
    for (uint8_t attempt = 0; attempt < 2; attempt++) {
        J *req = notecard.newRequest("note.add");
        if (!req) continue;
        JAddStringToObject(req, "file", "sanitation_alert.qo");
        JAddBoolToObject  (req, "sync", true);  // bypass outbound timer → immediate
        J *body = JAddObjectToObject(req, "body");
        if (body) {
            JAddStringToObject(body, "alert",    kind);
            JAddNumberToObject(body, "fill_pct", isnan(fill_pct) ? 0.0f : fill_pct);
            JAddNumberToObject(body, "fill_cm",  isnan(fill_cm)  ? 0.0f : fill_cm);
        }
        if (notecard.sendRequest(req)) return true;
    }
    return false;
}

static float safeAvg(float sum, uint32_t n) {
    return n > 0 ? (sum / (float)n) : INVALID_SENTINEL;
}

// Enqueue the hourly summary Note. Returns true if successfully enqueued.
// Window accumulators are reset by the caller only on a true return, so a
// transient I2C or Notecard error does not silently discard the window's data.
// Always emits a Note even when fill_n == 0 so downstream health checks get a
// consistent heartbeat: a summary with
// fill_pct_avg == INVALID_SENTINEL (-9999) unambiguously signals sensor failure,
// whereas a missing Note entirely is ambiguous (offline? sensor failed?).
static bool sendSummary(float last_fill_cm) {
    J *req = notecard.newRequest("note.add");
    if (!req) return false;
    JAddStringToObject(req, "file", "sanitation_summary.qo");
    // No sync:true — the Notecard's periodic outbound window batches summaries.
    J *body = JAddObjectToObject(req, "body");
    if (body) {
        JAddNumberToObject(body, "fill_pct_avg",  safeAvg(state.fill_sum, state.fill_n));
        JAddNumberToObject(body, "fill_pct_peak", state.fill_n > 0 ? state.fill_peak : INVALID_SENTINEL);
        JAddNumberToObject(body, "fill_cm_last",  isnan(last_fill_cm) ? INVALID_SENTINEL : last_fill_cm);
    }
    return notecard.sendRequest(req);
}

// -------- Main sample cycle (runs once per wake) --------

static void runSampleCycle() {
    // Advance elapsed-time counters by the configured sample interval.
    // These drive the summary window and alert cooldown without depending on
    // wall-clock epoch from card.time. Incrementing first means the very first
    // wake after cold boot counts as one interval elapsed — alert_elapsed_sec
    // starts at ALERT_COOLDOWN_SEC so the first alert is never suppressed, and
    // summary_elapsed_sec starts at 0 so the window does not expire prematurely.
    state.summary_elapsed_sec += SAMPLE_INTERVAL_SEC;
    state.alert_elapsed_sec   += SAMPLE_INTERVAL_SEC;

    // ---- Fill level ----
    float dist_cm  = readDistanceCm();
    float fill_pct = distanceToFillPct(dist_cm);

    if (!isnan(fill_pct)) {
        state.fill_sum += fill_pct;
        state.fill_n++;
        if (fill_pct > state.fill_peak) state.fill_peak = fill_pct;
    }

#if defined(ENABLE_USB_DEBUG)
    Serial.print("[sample] dist_cm="); Serial.print(dist_cm);
    Serial.print(" fill_pct=");        Serial.println(fill_pct);
    Serial.print("[timer] summary_elapsed_sec="); Serial.print(state.summary_elapsed_sec);
    Serial.print(" alert_elapsed_sec=");          Serial.println(state.alert_elapsed_sec);
#endif

    // ---- Alert evaluation ----
    // Fire once when fill_pct crosses FILL_ALERT_PCT, then suppress for
    // ALERT_COOLDOWN_SEC (4 h). Re-arm is cooldown-only: the alert will fire
    // again every 4 hours as long as the tank remains above the threshold.
    // alert_elapsed_sec is reset only after a successful enqueue, so a transient
    // I2C error cannot silently advance the cooldown without sending the alert.
    if (!isnan(fill_pct) &&
        fill_pct >= FILL_ALERT_PCT &&
        state.alert_elapsed_sec >= ALERT_COOLDOWN_SEC) {
        if (sendAlert("fill_high", fill_pct, dist_cm)) {
            state.alert_elapsed_sec = 0;
        }
    }

    // ---- Hourly summary ----
    // summary_elapsed_sec is reset only after a successful enqueue. A failed
    // enqueue keeps the counter running so the next wake re-attempts the summary
    // rather than silently discarding the window's accumulated data.
    if (state.summary_elapsed_sec >= SUMMARY_INTERVAL_MIN * 60UL) {
        if (sendSummary(dist_cm)) {
            // Reset the window accumulators.
            state.fill_sum = 0.0f;
            state.fill_n   = 0;
            state.fill_peak = 0.0f;
            state.summary_elapsed_sec = 0;
        }
    }
}

// -------- Setup / loop --------
//
// This sketch uses the "host-is-off-when-idle" pattern: card.attn cuts host
// power for SAMPLE_INTERVAL_SEC, so setup() runs on every wake. State is
// preserved across power cuts via NotePayloadSaveAndSleep/RetrieveAfterSleep.

void setup() {
#if defined(ENABLE_USB_DEBUG)
    // USB serial is opt-in at compile time (see ENABLE_USB_DEBUG above).
    // The enumeration wait is acceptable for bench work but must not reach
    // production firmware — it blocks setup() and draws USB peripheral current
    // on every wake for the duration of the timeout.
    Serial.begin(115200);
    { uint32_t t0 = millis(); while (!Serial && (millis() - t0) < 3000) {} }
#endif

    // Pin configuration for the ultrasonic sensor.
    // PIN_PW: INPUT_PULLDOWN biases D5 idle-low so a missing or disconnected
    // sensor cable holds the GPIO at GND rather than letting it float.
    // A floating input can produce spurious pulseIn() returns that are
    // indistinguishable from valid distance pulses, silently breaking the
    // "sensor absent → NAN → -9999 sentinel" path documented above.
    // The STM32L433 Cygnet supports INPUT_PULLDOWN natively; no external
    // resistor is needed.
    pinMode(PIN_PW, INPUT_PULLDOWN);

    Wire.begin();
    notecard.begin();
#if defined(ENABLE_USB_DEBUG)
    notecard.setDebugOutputStream(Serial);
#endif

    // Restore runtime state from the Notecard's flash store.
    NotePayloadDesc payload;
    bool restored = NotePayloadRetrieveAfterSleep(&payload);
    if (restored) {
        restored &= NotePayloadGetSegment(&payload, STATE_SEG_ID, &state, sizeof(state));
        NotePayloadFree(&payload);
    }
    if (!restored) {
        // Cold boot: initialise state, configure Notecard and templates.
        memset(&state, 0, sizeof(state));
        // Pre-arm the alert cooldown so a fill_high condition on the very first
        // wake fires immediately rather than being suppressed for 4 hours.
        state.alert_elapsed_sec = ALERT_COOLDOWN_SEC;
        hubConfigure();
        // defineTemplates() result stored in state.templates_ok. If it fails
        // here (e.g., Notecard still booting), the retry path below catches it
        // on subsequent wakes rather than leaving the device permanently
        // untemplated.
        state.templates_ok = defineTemplates();
        // Stop the Notecard's accelerometer so its motion-interrupt ISR doesn't
        // contaminate Mojo power traces during bench validation (see §8).
        J *req = notecard.newRequest("card.motion.mode");
        if (req) {
            JAddBoolToObject(req, "stop", true);
            notecard.sendRequest(req);
        }
    }

    // Retry template registration on every wake until both templates are
    // confirmed. A one-time cold-boot I2C failure does not permanently leave
    // the deployment without templates.
    if (!state.templates_ok) {
        state.templates_ok = defineTemplates();
    }

    // Check for updated env vars on every wake. Re-applying hub.set here if
    // summary_interval_min or inbound_interval_min changed keeps the Notecard
    // outbound and inbound cadences in sync with the env-var values.
    fetchEnvOverrides();

    runSampleCycle();
}

void loop() {
    // Serialize state and hand control back to the Notecard. The Notecard holds
    // the host in reset for SAMPLE_INTERVAL_SEC seconds, then releases ATTN to
    // power the host back on and re-enter setup().
    NotePayloadDesc payload = {0, 0, 0};
    NotePayloadAddSegment(&payload, STATE_SEG_ID, &state, sizeof(state));
    NotePayloadSaveAndSleep(&payload, SAMPLE_INTERVAL_SEC, NULL);

    // Reached only if the Notecarrier isn't gating host power through ATTN
    // (e.g., USB-powered bench bring-up). Mirror the normal per-wake sequence —
    // including fetchEnvOverrides() — so env-var changes propagate and sampling
    // behaviour matches the ATTN path. Arduino calls loop() again after the
    // cycle completes, repeating the save-sleep attempt indefinitely.
#if defined(ENABLE_USB_DEBUG)
    Serial.println("[loop] ATTN not gating host power — running in bench fallback mode.");
#endif
    // Chunked 1-second delays avoid 32-bit millisecond overflow: the product
    // SAMPLE_INTERVAL_SEC * 1000 overflows uint32_t for values above ~4294 s,
    // but the env-var clamp allows up to 86400 s. Looping in 1 s increments is
    // monotonically correct for the full [30, 86400] accepted range.
    for (uint32_t s = 0; s < SAMPLE_INTERVAL_SEC; s++) {
        delay(1000);
    }
    fetchEnvOverrides();
    runSampleCycle();
}
