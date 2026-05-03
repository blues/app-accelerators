// injection_molding_shot_monitor.ino — Injection Molding Shot-to-Shot Process Monitor
//
// Captures hydraulic injection pressure + mold temperature profile for every
// shot at the edge. Measures hydraulic pressure at the injection cylinder's
// manifold block (NOT in-cavity pressure). Extracts shot-level features:
//   peak pressure, fill time, pack pressure, cooling rate, and a per-boot-
//   session shot sequence number (g_cycle_count, RAM-only, resets on power loss).
// Transmits to Blues Notehub via a Notecard Cell+WiFi (MBGLW) on a Notecarrier CX.
//
// Scope: this project deliberately targets hydraulic manifold pressure rather
// than in-cavity pressure — no mold modification required. See README §1 for
// the full rationale and the trade-offs relative to piezoelectric cavity sensors.
//
// Hardware:
//   Blues Notecarrier CX (Cygnet STM32L433 host, 64 KB SRAM)
//   Blues Notecard Cell+WiFi (MBGLW) in M.2 slot
//   4-20 mA hydraulic injection pressure transducer → 150 Ω sense resistor → A0
//   SparkFun MAX31855K thermocouple breakout (SEN-13266) → SPI / D10
//
// Dependencies:
//   Blues Wireless Notecard (note-arduino) v1.8.5
//     arduino-cli lib install "Blues Wireless Notecard"
//   Arduino core for STM32 (stm32duino/Arduino_Core_STM32) via Boards Manager
//
// Set PRODUCT_UID to your Notehub project ProductUID before flashing.
// See https://dev.blues.io/notehub/notehub-walkthrough/#finding-a-productuid

#include <Notecard.h>
#include <Wire.h>
#include <SPI.h>

#ifndef PRODUCT_UID
#define PRODUCT_UID ""  // "com.my-company.my-name:injection_molding_monitor"
#pragma message "PRODUCT_UID is not defined. Set it before flashing."
#endif

// Set NOTE_DEBUG to 1 to enable Notecard I2C trace output on Serial.
// Leave at 0 for production builds — the STM32L433's serial overhead is
// measurable at 20 Hz continuous capture and is unnecessary on the shop floor.
#define NOTE_DEBUG 0

// -- Pin assignments (Notecarrier CX dual 16-pin header) ---------------------
#define PRESSURE_ADC_PIN  A0   // 4-20 mA transducer → 150 Ω → A0 (0–3 V)
#define TC_CS_PIN         D10  // MAX31855K chip select (active LOW)

// -- Pressure ADC calibration -------------------------------------------------
// 4-20 mA + 150 Ω sense resistor on 3.3 V / 12-bit ADC:
//   4 mA × 150 Ω = 0.60 V → count ≈  745 → 0 PSI
//  20 mA × 150 Ω = 3.00 V → count ≈ 3723 → full-scale
#define ADC_COUNTS_AT_4MA   745
#define ADC_COUNTS_AT_20MA  3723
#define ADC_AVERAGE_N       8     // ADC readings averaged per pressure sample

// -- Shot capture parameters --------------------------------------------------
#define SHOT_SAMPLE_MS       50       // 20 Hz during active shot
#define IDLE_POLL_MS         100      // 10 Hz between shots
#define MIN_SHOT_DURATION_MS 500      // Ignore sub-500 ms transients

// Profile buffer: 2048 × 50 ms = 102.4 s max; 2048 × 2 × 4 B = 16 KB in SRAM
#define SHOT_BUF_SIZE        2048

// Safety timeout set equal to buffer capacity so the timeout always fires at
// the moment the buffer would fill. This prevents the buffer from exhausting
// silently and being mistaken for a naturally-ended shot. Any shot longer than
// 102.4 s trips the timeout break before the while-condition can exit the loop.
#define SHOT_TIMEOUT_MS      ((uint32_t)SHOT_BUF_SIZE * SHOT_SAMPLE_MS)

// Pack phase ends when pressure drops to this fraction of peak
#define GATE_SEAL_FRAC       0.50f

// -- Default thresholds (all overridable via Notehub env vars) ----------------
#define DEFAULT_MAX_PRESSURE_PSI  2000.0f
#define DEFAULT_SHOT_DETECT_PSI   100.0f
#define DEFAULT_SHOT_END_PSI      50.0f
#define DEFAULT_PEAK_PSI_MIN      800.0f
#define DEFAULT_PEAK_PSI_MAX      1900.0f
#define DEFAULT_FILL_TIME_MIN_MS  200
#define DEFAULT_FILL_TIME_MAX_MS  3000
#define DEFAULT_MOLD_TEMP_MAX_C   80.0f
#define DEFAULT_OUTBOUND_MIN      60
#define DEFAULT_REPORT_EVERY_N    1

// 10-min cooldown prevents alarm fatigue during sustained drift
#define ALERT_COOLDOWN_MS    600000UL

// The host refreshes thresholds from the Notecard's local environment cache
// every 5 minutes. Cloud changes (made in Notehub) don't arrive instantly —
// they reach the Notecard on the configured inbound sync schedule (default
// 120 minutes) and are then available to the host on the next 5-minute poll.
#define ENV_CHECK_INTERVAL_MS 300000UL

// -- Runtime state ------------------------------------------------------------
Notecard notecard;

// Shot profile buffers allocated globally — keep them off the call stack
static float    g_pres_buf[SHOT_BUF_SIZE];
static float    g_temp_buf[SHOT_BUF_SIZE];
static int      g_shot_n        = 0;

// g_cycle_count is a per-boot-session shot sequence number — RAM only.
// It resets to 0 on every power loss or reboot. Not a persistent lifetime counter.
static uint32_t g_cycle_count = 0;
static uint32_t g_last_env_ms = 0;
// Per-alert cooldown timestamps — one entry per alert type so that simultaneous
// excursions (e.g. low pressure AND high temperature on the same shot) each get
// their own cooldown timer and each fire an independent alert Note.
// Index: 0=peak_pressure_low, 1=peak_pressure_high,
//        2=fill_time_short, 3=fill_time_long, 4=mold_temp_high
//
// Initialized to a value that wraps so the first qualifying shot after boot can
// alert immediately, rather than being suppressed for ALERT_COOLDOWN_MS while
// (millis() - 0) climbs past the cooldown window. Unsigned subtraction wraps:
// (millis() - (0 - ALERT_COOLDOWN_MS - 1)) = millis() + ALERT_COOLDOWN_MS + 1,
// which is already greater than ALERT_COOLDOWN_MS at t = 0.
static uint32_t g_last_alert_ms[5] = {
    (uint32_t)(0 - ALERT_COOLDOWN_MS - 1),
    (uint32_t)(0 - ALERT_COOLDOWN_MS - 1),
    (uint32_t)(0 - ALERT_COOLDOWN_MS - 1),
    (uint32_t)(0 - ALERT_COOLDOWN_MS - 1),
    (uint32_t)(0 - ALERT_COOLDOWN_MS - 1),
};

// Tunable thresholds (refreshed every ENV_CHECK_INTERVAL_MS)
static float g_max_psi      = DEFAULT_MAX_PRESSURE_PSI;
static float g_detect_psi   = DEFAULT_SHOT_DETECT_PSI;
static float g_end_psi      = DEFAULT_SHOT_END_PSI;
static float g_peak_min_psi = DEFAULT_PEAK_PSI_MIN;
static float g_peak_max_psi = DEFAULT_PEAK_PSI_MAX;
static int   g_fill_min_ms  = DEFAULT_FILL_TIME_MIN_MS;
static int   g_fill_max_ms  = DEFAULT_FILL_TIME_MAX_MS;
static float g_temp_max_c   = DEFAULT_MOLD_TEMP_MAX_C;
static int   g_outbound_min = DEFAULT_OUTBOUND_MIN;
static int   g_report_n     = DEFAULT_REPORT_EVERY_N;

// -- Forward declarations -----------------------------------------------------
bool  configureNotecard(void);
void  defineTemplates(void);
void  fetchEnvVars(void);
float readPressurePsi(void);
float readMoldTempC(void);
bool  captureShot(uint32_t *out_duration_ms);
void  computeFeatures(float *peak_psi, int *fill_ms, float *pack_psi,
                      float *cool_c_per_s, float *temp_avg_c);
void  sendShotNote(uint32_t cycle, float peak_psi, int fill_ms,
                   float pack_psi, float cool_c_per_s,
                   float temp_avg_c, uint32_t shot_ms);
bool  sendAlertNote(const char *alert_type, uint32_t cycle,
                    float peak_psi, int fill_ms, float temp_avg_c);

// =============================================================================
void setup() {
    Serial.begin(115200);
    const uint32_t t0 = millis();
    while (!Serial && (millis() - t0 < 3000)) {}
    Serial.println("[APP] Injection Molding Shot Monitor — starting");

    // Fail fast: a blank PRODUCT_UID lets hub.set succeed at the transport level
    // but leaves the device unprovisioned in Notehub. Catch it here so the device
    // never reaches the Ready state without a valid project identity.
    // See: https://dev.blues.io/notehub/notehub-walkthrough/#finding-a-productuid
    if (PRODUCT_UID[0] == '\0') {
        Serial.println("[APP] FATAL: PRODUCT_UID is not set — set it before flashing.");
        while (true) { delay(1000); }
    }

    analogReadResolution(12);  // STM32L433 supports 12-bit ADC natively

    // SPI bus + MAX31855K chip-select idle-high
    SPI.begin();
    pinMode(TC_CS_PIN, OUTPUT);
    digitalWrite(TC_CS_PIN, HIGH);

    // I2C to Notecard (routed through Notecarrier CX M.2 slot)
    Wire.begin();
    notecard.begin();
#if NOTE_DEBUG
    notecard.setDebugOutputStream(Serial);
#endif

    // Read cached env vars *before* the initial hub.set so that outbound_min
    // (and any other overrides) are applied from the very first connection
    // rather than requiring a second sync to take effect.
    fetchEnvVars();

    if (!configureNotecard()) {
        Serial.println("[APP] FATAL: Notecard configuration failed — check wiring and PRODUCT_UID.");
        while (true) { delay(1000); }
    }
    defineTemplates();

    Serial.println("[APP] Ready. Waiting for shot trigger...");
}

// =============================================================================
void loop() {
    // Refresh thresholds from the Notecard's env cache on schedule
    if (millis() - g_last_env_ms > ENV_CHECK_INTERVAL_MS) {
        fetchEnvVars();
        g_last_env_ms = millis();
    }

    // Idle: poll pressure at 10 Hz, return if no shot in progress
    float pres = readPressurePsi();
    if (pres < g_detect_psi) { delay(IDLE_POLL_MS); return; }

    Serial.print("[APP] Shot start at "); Serial.print(pres, 0);
    Serial.println(" PSI. Capturing...");

    uint32_t shot_ms = 0;
    if (!captureShot(&shot_ms)) {
        // Transient shorter than MIN_SHOT_DURATION_MS, or buffer-full discard
        Serial.println("[APP] Transient or buffer-full — skipped.");
        return;
    }

    g_cycle_count++;
    Serial.print("[APP] Shot #"); Serial.print(g_cycle_count);
    Serial.print(", "); Serial.print(g_shot_n); Serial.print(" samples, ");
    Serial.print(shot_ms); Serial.println(" ms");

    float peak_psi, pack_psi, cool_c_per_s, temp_avg_c;
    int   fill_ms;
    computeFeatures(&peak_psi, &fill_ms, &pack_psi, &cool_c_per_s, &temp_avg_c);

    if ((g_cycle_count % g_report_n) == 0) {
        sendShotNote(g_cycle_count, peak_psi, fill_ms,
                     pack_psi, cool_c_per_s, temp_avg_c, shot_ms);
    }

    // Evaluate all five alert conditions independently. Each condition has its
    // own 10-minute cooldown so a shot that simultaneously trips multiple rules
    // (e.g. low fill pressure and high mold temperature) produces a separate
    // alert Note for each, rather than only the first match.
    //
    // The cooldown timer is only advanced when the alert Note is successfully
    // queued. A transient I2C or Notecard failure therefore does not suppress
    // the next attempt for a full cooldown window — the retry will fire on the
    // next shot that trips the same condition.
    static const char * const kAlertTypes[] = {
        "peak_pressure_low", "peak_pressure_high",
        "fill_time_short",   "fill_time_long",
        "mold_temp_high"
    };
    bool conditions[5] = {
        peak_psi   < g_peak_min_psi,
        peak_psi   > g_peak_max_psi,
        fill_ms    < g_fill_min_ms,
        fill_ms    > g_fill_max_ms,
        temp_avg_c > g_temp_max_c,
    };
    for (int i = 0; i < 5; i++) {
        if (conditions[i] &&
            (millis() - g_last_alert_ms[i]) > ALERT_COOLDOWN_MS) {
            if (sendAlertNote(kAlertTypes[i], g_cycle_count,
                              peak_psi, fill_ms, temp_avg_c)) {
                // Only advance the cooldown on a confirmed successful note.add.
                g_last_alert_ms[i] = millis();
            }
        }
    }
}

// -- configureNotecard() ------------------------------------------------------
// Runs once at cold boot. hub.set is retried for up to 5 seconds to ride out
// the cold-boot I2C race where the Notecard MCU may not be ready immediately
// after power-on. requestAndResponse() is used (rather than sendRequestWithRetry)
// so the response err field can be inspected — a semantic failure such as an
// unrecognized PRODUCT_UID format is not mistaken for a transport success.
// Returns false if hub.set cannot be confirmed within the retry window.
bool configureNotecard() {
    bool hub_ok = false;
    const uint32_t t0 = millis();
    while ((millis() - t0) < 5000UL) {
        J *req = notecard.newRequest("hub.set");
        JAddStringToObject(req, "product", PRODUCT_UID);
        JAddStringToObject(req, "mode",    "periodic");
        JAddNumberToObject(req, "outbound", g_outbound_min);
        JAddNumberToObject(req, "inbound",  120);  // Pull env var updates every 2 h
        J *rsp = notecard.requestAndResponse(req);
        if (rsp) {
            hub_ok = !notecard.responseError(rsp);
            if (!hub_ok) {
                Serial.print("[APP] hub.set rejected: ");
                Serial.println(JGetString(rsp, "err"));
            }
            notecard.deleteResponse(rsp);
        }
        if (hub_ok) break;
        delay(500);
    }
    if (!hub_ok) {
        Serial.println("[APP] hub.set failed after 5 s — Notecard may not be ready or PRODUCT_UID is invalid.");
        return false;
    }

    // Disable the onboard accelerometer — not needed here, and suppressing it
    // reduces idle noise on the power rail during Mojo bench validation.
    J *req = notecard.newRequest("card.motion.mode");
    JAddBoolToObject(req, "stop", true);
    if (!notecard.sendRequest(req)) {
        Serial.println("[APP] card.motion.mode failed — accelerometer may remain active.");
    }
    return true;
}

// -- defineTemplates() --------------------------------------------------------
// Fixed-length Note templates shrink on-wire payload by 3–5× vs free-form JSON,
// helping keep the daily cellular data budget manageable at high cycle rates.
// requestAndResponse() is used for both calls so the err field is visible if a
// template definition is rejected (e.g. a bad type hint). Templates are
// idempotent — re-running them on the next boot corrects any missed registration.
void defineTemplates() {
    // shot.qo: one record per shot (or per Nth shot when report_every_n_shots > 1)
    J *req = notecard.newRequest("note.template");
    JAddStringToObject(req, "file", "shot.qo");
    JAddNumberToObject(req, "port", 50);
    J *body = JAddObjectToObject(req, "body");
    JAddNumberToObject(body, "cycle",      14);    // 4-byte signed int
    JAddNumberToObject(body, "peak_psi",   14.1);  // 4-byte float
    JAddNumberToObject(body, "fill_ms",    14);
    JAddNumberToObject(body, "pack_psi",   14.1);
    JAddNumberToObject(body, "cool_c_s",   14.1);  // °C/s; negative = cooling
    JAddNumberToObject(body, "temp_avg_c", 14.1);
    JAddNumberToObject(body, "shot_ms",    14);
    J *rsp = notecard.requestAndResponse(req);
    if (!rsp || notecard.responseError(rsp)) {
        Serial.print("[APP] note.template (shot.qo) failed");
        if (rsp) { Serial.print(": "); Serial.print(JGetString(rsp, "err")); }
        Serial.println(" — will retry on next boot.");
    }
    if (rsp) notecard.deleteResponse(rsp);

    // shot_alert.qo: out-of-spec shots, synced immediately with sync:true
    req = notecard.newRequest("note.template");
    JAddStringToObject(req, "file", "shot_alert.qo");
    JAddNumberToObject(req, "port", 51);
    body = JAddObjectToObject(req, "body");
    // The exemplar string for a string field sets the maximum field width.
    // Use the longest alert type name ("peak_pressure_high", 18 chars) so all
    // five alert values fit without truncation.
    JAddStringToObject(body, "alert",      "peak_pressure_high");
    JAddNumberToObject(body, "cycle",      14);
    JAddNumberToObject(body, "peak_psi",   14.1);
    JAddNumberToObject(body, "fill_ms",    14);
    JAddNumberToObject(body, "temp_avg_c", 14.1);
    rsp = notecard.requestAndResponse(req);
    if (!rsp || notecard.responseError(rsp)) {
        Serial.print("[APP] note.template (shot_alert.qo) failed");
        if (rsp) { Serial.print(": "); Serial.print(JGetString(rsp, "err")); }
        Serial.println(" — will retry on next boot.");
    }
    if (rsp) notecard.deleteResponse(rsp);
}

// -- fetchEnvVars() -----------------------------------------------------------
// Reads tunable thresholds from the Notecard's local environment cache.
// Compile-time defaults persist for any variable not set in Notehub.
//
// Notehub environment variables always arrive as JSON strings, regardless of
// the value's apparent type — JGetNumber would silently return 0 for every
// key, so each value is read with JGetString and parsed with strtod / strtol.
// endp is checked to confirm full consumption of the input ("80" parses but
// "80abc" is rejected); only positive values are applied so an operator-set
// "0" or junk leaves the in-RAM threshold at its last-good value.
//
// outbound_min is special: if it changes, hub.set is re-applied immediately
// so the Notecard's outbound cadence tracks the operator's intent in real time.
// Because fetchEnvVars() is also called before configureNotecard() at boot,
// g_outbound_min is updated with any cached value before the first hub.set runs.
void fetchEnvVars() {
    J *req = notecard.newRequest("env.get");
    J *rsp = notecard.requestAndResponse(req);
    if (rsp == nullptr) return;
    if (notecard.responseError(rsp)) { notecard.deleteResponse(rsp); return; }

    J *body = JGetObject(rsp, "body");
    if (body != nullptr) {
        const char *s;
        char       *endp;
        double      d;
        long        l;

        // Float thresholds
        s = JGetString(body, "max_pressure_psi");
        if (s && *s) { d = strtod(s, &endp); if (endp != s && *endp == '\0' && d > 0.0) g_max_psi = (float)d; }

        s = JGetString(body, "shot_detect_psi");
        if (s && *s) { d = strtod(s, &endp); if (endp != s && *endp == '\0' && d > 0.0) g_detect_psi = (float)d; }

        s = JGetString(body, "shot_end_psi");
        if (s && *s) { d = strtod(s, &endp); if (endp != s && *endp == '\0' && d > 0.0) g_end_psi = (float)d; }

        s = JGetString(body, "peak_psi_min");
        if (s && *s) { d = strtod(s, &endp); if (endp != s && *endp == '\0' && d > 0.0) g_peak_min_psi = (float)d; }

        s = JGetString(body, "peak_psi_max");
        if (s && *s) { d = strtod(s, &endp); if (endp != s && *endp == '\0' && d > 0.0) g_peak_max_psi = (float)d; }

        s = JGetString(body, "mold_temp_max_c");
        if (s && *s) { d = strtod(s, &endp); if (endp != s && *endp == '\0' && d > 0.0) g_temp_max_c = (float)d; }

        // Integer thresholds
        s = JGetString(body, "fill_time_min_ms");
        if (s && *s) { l = strtol(s, &endp, 10); if (endp != s && *endp == '\0' && l > 0) g_fill_min_ms = (int)l; }

        s = JGetString(body, "fill_time_max_ms");
        if (s && *s) { l = strtol(s, &endp, 10); if (endp != s && *endp == '\0' && l > 0) g_fill_max_ms = (int)l; }

        s = JGetString(body, "report_every_n_shots");
        if (s && *s) { l = strtol(s, &endp, 10); if (endp != s && *endp == '\0' && l >= 1) g_report_n = (int)l; }

        // Re-apply hub.set only when outbound_min actually changed so we don't
        // spam the Notecard with redundant requests every 5-minute env check.
        // Called from setup() before configureNotecard(), this updates
        // g_outbound_min so the subsequent hub.set uses the correct cadence.
        // If the Notecard has already been configured (subsequent boots, or
        // loop() calls), the partial hub.set here takes effect immediately.
        s = JGetString(body, "outbound_min");
        if (s && *s) {
            l = strtol(s, &endp, 10);
            if (endp != s && *endp == '\0' && l > 0 && (int)l != g_outbound_min) {
                g_outbound_min = (int)l;
                J *hub_req = notecard.newRequest("hub.set");
                JAddStringToObject(hub_req, "mode",    "periodic");
                JAddNumberToObject(hub_req, "outbound", g_outbound_min);
                if (!notecard.sendRequest(hub_req)) {
                    Serial.println("[APP] hub.set (outbound update) failed — will retry next env check.");
                } else {
                    Serial.print("[APP] outbound cadence updated to ");
                    Serial.print(g_outbound_min); Serial.println(" min");
                }
            }
        }
    }
    notecard.deleteResponse(rsp);
}

// -- readPressurePsi() --------------------------------------------------------
// Averages ADC_AVERAGE_N 12-bit samples and maps the count range that
// corresponds to 4–20 mA (through the 150 Ω sense resistor) to [0, g_max_psi].
float readPressurePsi() {
    int32_t sum = 0;
    for (int i = 0; i < ADC_AVERAGE_N; i++) sum += analogRead(PRESSURE_ADC_PIN);
    // Cast to float before dividing to preserve sub-count resolution.
    float counts = (float)sum / (float)ADC_AVERAGE_N;
    float psi = (counts - ADC_COUNTS_AT_4MA) /
                (float)(ADC_COUNTS_AT_20MA - ADC_COUNTS_AT_4MA) * g_max_psi;
    return constrain(psi, 0.0f, g_max_psi);
}

// -- readMoldTempC() ----------------------------------------------------------
// Reads the MAX31855K via raw SPI (no library needed — the IC is read-only).
// Returns °C or NAN if any fault bit (OC / SCG / SCV) is set.
float readMoldTempC() {
    SPI.beginTransaction(SPISettings(500000UL, MSBFIRST, SPI_MODE0));
    digitalWrite(TC_CS_PIN, LOW);
    delayMicroseconds(100);  // CS assertion hold time per MAX31855 datasheet

    uint32_t raw = 0;
    raw  = (uint32_t)SPI.transfer(0x00) << 24;
    raw |= (uint32_t)SPI.transfer(0x00) << 16;
    raw |= (uint32_t)SPI.transfer(0x00) << 8;
    raw |= (uint32_t)SPI.transfer(0x00);

    digitalWrite(TC_CS_PIN, HIGH);
    SPI.endTransaction();

    // Bits 2:0 are fault flags (OC, SCG, SCV); any nonzero = no valid reading
    if (raw & 0x00000007) return NAN;

    // TC temperature: bits 31:18, 14-bit two's complement, 0.25 °C per LSB.
    // Arithmetic right-shift preserves the sign bit.
    return (float)((int32_t)raw >> 18) * 0.25f;
}

// -- captureShot() ------------------------------------------------------------
// High-speed profile acquisition at SHOT_SAMPLE_MS cadence.
// Returns true when a valid shot (>= MIN_SHOT_DURATION_MS) was captured.
// Returns false if the shot was shorter than MIN_SHOT_DURATION_MS (transient)
// or if the buffer filled before the shot ended naturally — in the latter case
// the partial profile is discarded rather than silently computing features on
// truncated data and treating the result as a valid shot.
bool captureShot(uint32_t *out_duration_ms) {
    g_shot_n = 0;
    uint32_t t_start  = millis();
    uint32_t t_sample = t_start;

    while (g_shot_n < SHOT_BUF_SIZE) {
        uint32_t elapsed = millis() - t_start;

        g_pres_buf[g_shot_n] = readPressurePsi();
        g_temp_buf[g_shot_n] = readMoldTempC();
        g_shot_n++;

        // End condition: pressure back below g_end_psi after minimum duration
        if (elapsed > (uint32_t)MIN_SHOT_DURATION_MS &&
            g_pres_buf[g_shot_n - 1] < g_end_psi) break;

        if (elapsed >= SHOT_TIMEOUT_MS) break;  // Safety cap (= buffer capacity)

        // Pace sampling; account for time already spent in reads
        uint32_t spent = millis() - t_sample;
        if (spent < (uint32_t)SHOT_SAMPLE_MS) delay(SHOT_SAMPLE_MS - spent);
        t_sample = millis();
    }

    *out_duration_ms = millis() - t_start;

    // If the while-condition (not a break) terminated the loop, the buffer is
    // completely full. The shot ran past SHOT_TIMEOUT_MS without a natural
    // pressure-drop end, meaning features computed on this profile would be
    // misleading (fill time and peak location are likely correct, but pack and
    // cooling metrics cover only a truncated window). Discard and log.
    if (g_shot_n >= SHOT_BUF_SIZE) {
        Serial.print("[APP] ERROR: shot buffer full at ");
        Serial.print(*out_duration_ms);
        Serial.println(" ms — shot exceeded capture window, discarding.");
        return false;
    }

    return (*out_duration_ms >= (uint32_t)MIN_SHOT_DURATION_MS);
}

// -- computeFeatures() --------------------------------------------------------
// Extracts five shot-level metrics from the profile buffer.
//
//   Fill phase   : shot start → pressure peak
//   Pack phase   : pressure peak → gate-seal crossover (50 % of peak)
//   Cooling phase: gate seal → shot end
void computeFeatures(float *peak_psi, int *fill_ms, float *pack_psi,
                     float *cool_c_per_s, float *temp_avg_c) {
    *peak_psi = *pack_psi = *cool_c_per_s = *temp_avg_c = 0.0f;
    *fill_ms  = 0;
    if (g_shot_n < 2) return;

    // 1. Peak pressure + fill time
    int peak_idx = 0;
    for (int i = 0; i < g_shot_n; i++) {
        if (g_pres_buf[i] > *peak_psi) { *peak_psi = g_pres_buf[i]; peak_idx = i; }
    }
    *fill_ms = peak_idx * SHOT_SAMPLE_MS;

    // 2. Pack pressure: mean from peak through gate-seal crossover
    float gate_seal_psi = *peak_psi * GATE_SEAL_FRAC;
    int   pack_end_idx  = peak_idx;
    float pack_sum = 0.0f; int pack_cnt = 0;
    for (int i = peak_idx; i < g_shot_n; i++) {
        if (g_pres_buf[i] < gate_seal_psi) break;
        pack_sum += g_pres_buf[i]; pack_cnt++; pack_end_idx = i;
    }
    *pack_psi = (pack_cnt > 0) ? pack_sum / (float)pack_cnt : 0.0f;

    // 3. Mold temperature average across the whole shot (skips NAN samples)
    float temp_sum = 0.0f; int temp_valid = 0;
    for (int i = 0; i < g_shot_n; i++) {
        if (!isnan(g_temp_buf[i])) { temp_sum += g_temp_buf[i]; temp_valid++; }
    }
    *temp_avg_c = (temp_valid > 0) ? temp_sum / (float)temp_valid : 0.0f;

    // 4. Cooling rate: least-squares slope of temperature from gate-seal to
    //    end of shot (°C/s; a negative value means the mold is cooling normally).
    //    NOTE: A 1/8" sheath thermocouple has a thermal response time of several
    //    seconds, so this slope reflects multi-shot mold-surface trend rather
    //    than the fast within-shot cooling transient. Use as a drift indicator.
    int cool_start = pack_end_idx + 1;
    int cool_n = 0;
    double sx = 0, sy = 0, sxx = 0, sxy = 0;
    for (int i = cool_start; i < g_shot_n; i++) {
        if (isnan(g_temp_buf[i])) continue;
        double x = (double)(i - cool_start) * SHOT_SAMPLE_MS / 1000.0;
        double y = (double)g_temp_buf[i];
        sx += x; sy += y; sxx += x * x; sxy += x * y; cool_n++;
    }
    if (cool_n >= 2) {
        double denom = (double)cool_n * sxx - sx * sx;
        *cool_c_per_s = (fabsf((float)denom) > 1e-9f)
                        ? (float)(((double)cool_n * sxy - sx * sy) / denom)
                        : 0.0f;
    }
}

// -- sendShotNote() -----------------------------------------------------------
// Queues a shot-summary Note in shot.qo. Notes accumulate in the Notecard's
// on-device flash queue and are flushed on the outbound sync schedule.
// If note.add fails (e.g. Notecard busy or flash full) the Note is dropped for
// this cycle; it is not retried. The next shot will attempt a fresh note.add.
void sendShotNote(uint32_t cycle, float peak_psi, int fill_ms,
                  float pack_psi, float cool_c_per_s,
                  float temp_avg_c, uint32_t shot_ms) {
    J *req = notecard.newRequest("note.add");
    JAddStringToObject(req, "file", "shot.qo");
    J *body = JAddObjectToObject(req, "body");
    JAddNumberToObject(body, "cycle",      (double)cycle);
    JAddNumberToObject(body, "peak_psi",   peak_psi);
    JAddNumberToObject(body, "fill_ms",    fill_ms);
    JAddNumberToObject(body, "pack_psi",   pack_psi);
    JAddNumberToObject(body, "cool_c_s",   cool_c_per_s);
    JAddNumberToObject(body, "temp_avg_c", temp_avg_c);
    JAddNumberToObject(body, "shot_ms",    (double)shot_ms);
    if (!notecard.sendRequest(req)) {
        Serial.print("[APP] note.add (shot.qo) failed for cycle ");
        Serial.print(cycle); Serial.println(" — Note dropped.");
    }
}

// -- sendAlertNote() ----------------------------------------------------------
// Emits an out-of-spec alert with sync:true, which bypasses the outbound queue
// and triggers an immediate cellular session (~15–60 s to reach Notehub).
// Returns true if note.add was accepted by the Notecard, false otherwise.
// The caller advances the per-alert cooldown only on a true return, so a
// transient I2C or Notecard failure does not suppress retries for a full
// cooldown window.
bool sendAlertNote(const char *alert_type, uint32_t cycle,
                   float peak_psi, int fill_ms, float temp_avg_c) {
    J *req = notecard.newRequest("note.add");
    JAddStringToObject(req, "file", "shot_alert.qo");
    JAddBoolToObject(req, "sync", true);
    J *body = JAddObjectToObject(req, "body");
    JAddStringToObject(body, "alert",      alert_type);
    JAddNumberToObject(body, "cycle",      (double)cycle);
    JAddNumberToObject(body, "peak_psi",   peak_psi);
    JAddNumberToObject(body, "fill_ms",    fill_ms);
    JAddNumberToObject(body, "temp_avg_c", temp_avg_c);
    bool ok = notecard.sendRequest(req);
    if (!ok) {
        Serial.print("[APP] note.add (shot_alert.qo) failed for alert ");
        Serial.println(alert_type);
    } else {
        Serial.print("[APP] ALERT → "); Serial.println(alert_type);
    }
    return ok;
}
