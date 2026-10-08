/***************************************************************************
  solar_string_monitor.ino

  Blues Notecard + Notecarrier CX application for solar array string-level
  performance monitoring. Each wake cycle:
    1. Reads per-string DC V and I from a Modbus RTU string combiner/inverter
    2. Reads irradiance from an Apogee SP-110-SS analog pyranometer
    3. Reads module temperature from a waterproof DS18B20 probe
    4. Computes a temperature-derated expected power per string
    5. Derives a Performance Ratio (PR = actual W / expected W)
    6. Flags underperformers with a root-cause hypothesis (shading / soiling /
       string fault) and fires an immediate sync:true alert Note
    7. Accumulates window averages and emits a summary Note every hour
    8. Sleeps in STM32 STOP2 until the Notecard's ATTN pin wakes it for the
       next sample interval (card.attn "sleep"; see cx_sleep.h)

  Sleep wiring: jumper the Notecarrier CX ATTN pin to D6 (same 16-pin header).
  D5 carries the DS18B20 and D9 the RS-485 direction pin, so CX_ATTN_PIN is
  defined as D6 below before cx_sleep.h is included. Leave EN unconnected —
  on the CX it enables the shared 3.3 V VIO rail, and driving it from ATTN
  browns out the whole board.

  Build: Tools > USB support (if available) > None (usb=none). With the USB
  CDC stack enabled and no USB host attached the host cannot stay in STOP2.
  Debug output goes to the LPUART on the CX debug jack (ST-LINK VCP).

  Data-path helpers live in solar_string_monitor_helpers.{h,cpp}.

  Hardware
  --------
  - Blues Notecarrier CX  (onboard STM32L433 host MCU)
  - Blues Notecard Cell+WiFi MBGLW  (M.2 slot)
  - SparkFun RS-485 Transceiver Breakout BOB-10124 → UART TX/RX/D9
  - Apogee SP-110-SS pyranometer (0–400 mV analog) → A0
  - Adafruit waterproof DS18B20 (Product 381) → D5 (1-Wire)
  - ATTN → D6 jumper — Notecard wakes the host from STOP2

  Dependencies (install via Arduino Library Manager)
  --------------------------------------------------
  - Blues Wireless Notecard
  - STM32duino Low Power (+ its dependency STM32duino RTC)
  - ModbusMaster  (4-20-ma/ModbusMaster)
  - OneWire
  - DallasTemperature

  THIS FILE SHOULD BE EDITED AFTER GENERATION.
  IT IS PROVIDED AS A STARTING POINT FOR THE USER TO EDIT AND EXTEND.
***************************************************************************/

#include <Notecard.h>
#include <Wire.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <ModbusMaster.h>
#include "solar_string_monitor_helpers.h"

// D5 is the DS18B20 data line and D9 the RS-485 DE/RE pin, so the ATTN
// jumper lands on D6.
#define CX_ATTN_PIN D6
#include "cx_sleep.h"

// ---------------------------------------------------------------------------
// Debug output — LPUART1 on the Notecarrier CX debug jack, which an ST-LINK V3
// exposes as a virtual COM port. USB CDC is disabled in this build (usb=none)
// so the host can sleep; Serial is therefore not USB and is not used here.
// ---------------------------------------------------------------------------
Uart debugSerial(PIN_VCP_RX, PIN_VCP_TX);

// ---------------------------------------------------------------------------
// Debug build flag — uncomment to enable verbose Notecard request/response
// logging on debugSerial. Leave undefined for production: it adds measurable
// awake time on every 5-minute wake cycle and distorts Mojo/scope measurements.
// ---------------------------------------------------------------------------
// #define DEBUG_SERIAL

// ---------------------------------------------------------------------------
// Product UID — replace with your Notehub project UID before flashing
// ---------------------------------------------------------------------------
#ifndef PRODUCT_UID
#define PRODUCT_UID "com.your-company.your-name:solar-string-monitor"
#pragma message "PRODUCT_UID is not defined. Set it to your Notehub project UID."
#endif

// ---------------------------------------------------------------------------
// Pin assignments  (Notecarrier CX dual 16-pin header)
// ---------------------------------------------------------------------------
#define PIN_RS485_DE_RE  9  // DE and /RE tied together on SparkFun BOB-10124
#define PIN_ONE_WIRE     5  // DS18B20 data line (4.7 kΩ pull-up to 3V3)
// CX_ATTN_PIN (D6) above: Notecard ATTN jumpered here to wake the host.
// Pyranometer is on A0 — no define needed; referenced as A0 in helpers.cpp

// ---------------------------------------------------------------------------
// Alert cooldown: suppress repeat alerts for this wall-clock duration.
// The equivalent sample count is computed at runtime from g_sample_interval_sec
// so the window stays consistent even when sample_interval_sec is changed via
// a Notehub env var.  Overridable via the alert_cooldown_sec env var.
// ---------------------------------------------------------------------------
#define DEFAULT_ALERT_COOLDOWN_SEC  1800    // 30 minutes

// ---------------------------------------------------------------------------
// Compile-time defaults — all overridable via Notehub environment variables
// (see README §5 for the full env-var reference table)
// ---------------------------------------------------------------------------
#define DEFAULT_SAMPLE_INTERVAL_SEC   300   // 5 min between samples
#define DEFAULT_REPORT_INTERVAL_MIN    60   // 1 h between summary notes
#define DEFAULT_MODBUS_SLAVE_ID         1
#define DEFAULT_MODBUS_BAUD          9600
#define DEFAULT_MODBUS_PARITY        "none" // "none" | "even" | "odd"
#define DEFAULT_MODBUS_STOP_BITS        1   // 1 or 2
#define DEFAULT_N_STRINGS               4
#define DEFAULT_REG_BASE              100   // 0-based first holding register
// Scaling: raw register × (scale/divisor) = engineering units
// string_v_scale_x100=10  → 10/100 = 0.10 V/count
// string_a_scale_x1000=10 → 10/1000 = 0.010 A/count
#define DEFAULT_STRING_V_SCALE_X100    10
#define DEFAULT_STRING_A_SCALE_X1000   10
#define DEFAULT_STRING_STC_W         6000   // rated W per string at STC
#define DEFAULT_PERF_THRESH_PCT        80   // alert when PR < 0.80
#define DEFAULT_IRRADIANCE_MIN_WM2    100   // ignore perf below this irradiance
// Mono c-Si temperature coefficient: −0.35 %/°C → −35 per 10 000
#define DEFAULT_TEMP_COEFF_PER10000   -35
// Pyranometer sensitivity: 0.200 mV per W/m² is the Apogee SP-110-SS typical
// nominal value.  Replace with the per-unit calibration certificate value here
// or push it at runtime via the pyranometer_mv_per_wm2_x1000 env var
// (e.g. 195 → 0.195 mV/W/m²) to correct for unit-to-unit variation.
#define DEFAULT_PYRANOMETER_MV_PER_WM2  0.200f

// ---------------------------------------------------------------------------
// Runtime config (extern-declared in helpers.h; helpers read these directly)
// ---------------------------------------------------------------------------
uint32_t g_sample_interval_sec  = DEFAULT_SAMPLE_INTERVAL_SEC;
uint32_t g_report_interval_min  = DEFAULT_REPORT_INTERVAL_MIN;
uint8_t  g_modbus_slave_id      = DEFAULT_MODBUS_SLAVE_ID;
uint32_t g_modbus_baud          = DEFAULT_MODBUS_BAUD;
char     g_modbus_parity[8]     = DEFAULT_MODBUS_PARITY;
uint8_t  g_modbus_stop_bits     = DEFAULT_MODBUS_STOP_BITS;
uint8_t  g_n_strings            = DEFAULT_N_STRINGS;
uint16_t g_reg_base             = DEFAULT_REG_BASE;
float    g_string_v_scale       = DEFAULT_STRING_V_SCALE_X100  / 100.0f;
float    g_string_a_scale       = DEFAULT_STRING_A_SCALE_X1000 / 1000.0f;
float    g_string_stc_w         = (float)DEFAULT_STRING_STC_W;
float    g_perf_threshold       = DEFAULT_PERF_THRESH_PCT / 100.0f;
float    g_irradiance_min       = (float)DEFAULT_IRRADIANCE_MIN_WM2;
float    g_temp_coeff           = DEFAULT_TEMP_COEFF_PER10000 / 10000.0f;
uint32_t g_alert_cooldown_sec   = DEFAULT_ALERT_COOLDOWN_SEC;
float    g_pyranometer_sensitivity = DEFAULT_PYRANOMETER_MV_PER_WM2;

// ---------------------------------------------------------------------------
// Application state — lives in RAM. STOP2 retains SRAM, so it survives every
// sleep/wake cycle and is reset only by a power cycle (which re-runs setup()).
// ---------------------------------------------------------------------------
AppState g_state;

// ---------------------------------------------------------------------------
// Peripheral objects (extern-declared in helpers.h so helpers can use them)
// ---------------------------------------------------------------------------
Notecard        notecard;
OneWire         oneWire(PIN_ONE_WIRE);
DallasTemperature tempSensor(&oneWire);
ModbusMaster    modbus;

// ---------------------------------------------------------------------------
// RS-485 direction-control callbacks required by ModbusMaster.
// Both DE (driver enable) and /RE (receiver enable) are tied to PIN_RS485_DE_RE.
// HIGH = transmit mode; LOW = receive mode.
// ---------------------------------------------------------------------------
static void preTransmission()  { digitalWrite(PIN_RS485_DE_RE, HIGH); }
static void postTransmission() { digitalWrite(PIN_RS485_DE_RE, LOW);  }

// ---------------------------------------------------------------------------
// Sleep the host in STOP2 until the Notecard raises ATTN; fall back to waiting
// awake for the same interval if the sleep request fails or ATTN never goes low.
// ---------------------------------------------------------------------------
static void goToSleep(uint32_t seconds) {
    if (!cxSleepUntilAttn(notecard, seconds, NULL, &debugSerial)) {
        // The Notecard didn't take the sleep request, or ATTN never went low
        // (check the ATTN -> D6 jumper). Keep the sample cadence and retry.
        debugSerial.println(F("[app] ATTN sleep failed — waiting out the interval awake"));
        delay(seconds * 1000UL);
    }
}

// ===========================================================================
// setup() — runs once at power-up: one-time Notecard configuration, sensor
// init, and arming the ATTN wake. The host resumes in place after each STOP2
// sleep, so every per-wake step lives in loop().
// ===========================================================================
void setup() {
    debugSerial.begin(115200);

    pinMode(PIN_RS485_DE_RE, OUTPUT);
    digitalWrite(PIN_RS485_DE_RE, LOW); // idle in receive

    // 12-bit ADC resolution: 4096 counts at 3.3 V ref → ~0.8 mV/count.
    // Must be set before readIrradiance() runs; the conversion math assumes 4095 full scale.
    analogReadResolution(12);

    Wire.begin();
    notecard.begin();
#ifdef DEBUG_SERIAL
    // Verbose Notecard request/response logging — bench use only.
    // Enabling this in production adds measurable awake time on every wake
    // cycle and will distort Mojo/scope measurements during power validation.
    notecard.setDebugOutputStream(debugSerial);
#endif

    debugSerial.println(F("[app] Power-up — init Notecard"));
    memset(&g_state, 0, sizeof(g_state));
    // 0xFFFFFFFF ensures the first Modbus or temp-probe failure always fires
    // an alert (0/wlen != 0xFFFFFFFF/wlen for any realistic wlen value).
    g_state.last_err_sample        = 0xFFFFFFFFUL;
    g_state.last_temp_fault_sample = 0xFFFFFFFFUL;

    // hub.set: periodic mode, outbound hourly, inbound every 2 h.
    J *req = notecard.newRequest("hub.set");
    JAddStringToObject(req, "product", PRODUCT_UID);
    JAddStringToObject(req, "mode",    "periodic");
    JAddNumberToObject(req, "outbound", (int)g_report_interval_min);
    JAddNumberToObject(req, "inbound",  120);
    // Check the bool return: only record the outbound cadence when the
    // Notecard confirms success.
    // On failure, last_hub_outbound stays at 0, so the post-fetchEnvVars
    // hub.set block in loop() sees a mismatch on the very first wake and
    // re-attempts provisioning — no device is silently left unconfigured.
    if (!notecard.sendRequest(req)) {
        debugSerial.println(F("[app] hub.set (power-up) failed; will retry on next wake"));
        // g_state.last_hub_outbound remains 0 — triggers retry path in loop()
    } else {
        g_state.last_hub_outbound = g_report_interval_min;
        debugSerial.println(F("[app] hub.set applied"));
    }

    // Disable the Notecard's accelerometer to eliminate the interrupt
    // blips it adds to power traces during Mojo/scope measurements.
    req = notecard.newRequest("card.motion.mode");
    JAddBoolToObject(req, "stop", true);
    notecard.sendRequest(req);

    // Enforce cellular-only operation by clearing any previously stored
    // WiFi credentials (card.wifi with ssid:"-" / password:"-" erases
    // both).  This prevents a reused or previously provisioned MBGLW
    // Notecard from silently falling back to WiFi — a metal NEMA 4X
    // enclosure attenuates WiFi signals in any case, but the explicit
    // clear makes cellular-only behaviour a firmware guarantee rather
    // than an assumption about the device's prior provisioning state.
    req = notecard.newRequest("card.wifi");
    JAddStringToObject(req, "ssid",     "-");
    JAddStringToObject(req, "password", "-");
    notecard.sendRequest(req);

    // First attempt at template registration; loop() retries until it sticks.
    if (defineTemplates()) {
        g_state.templates_ok = true;
        debugSerial.println(F("[app] Templates registered"));
    }

    tempSensor.begin();
    tempSensor.setResolution(11); // 11-bit: 375 ms conversion, 0.125 °C steps

    // Wake from STOP2 on the ATTN rising edge (ATTN jumpered to D6).
    cxSleepBegin();
}

// ===========================================================================
// loop() — one complete sample cycle per wake; ends with goToSleep()
// ===========================================================================
void loop() {
    // Template registration is a must-succeed initialization step: template-backed
    // notes are a core data-efficiency feature (3–5× bandwidth reduction), and
    // untemplated notes would silently undermine that goal for the device's lifetime.
    //
    // templates_ok lives in AppState so a failure at power-up or any earlier wake
    // is retried here on every subsequent wake until both note.template calls
    // are confirmed by the Notecard.
    if (!g_state.templates_ok) {
        if (defineTemplates()) {
            g_state.templates_ok = true;
            debugSerial.println(F("[app] Templates registered"));
        } else {
            debugSerial.println(F("[app] WARN: Template registration incomplete — will retry on next wake"));
        }
    }

    // Fetch env-var overrides on every wake (catches Notehub-side changes
    // that arrived via inbound sync while the host was sleeping).
    fetchEnvVars();

    // Keep the Notecard outbound sync cadence aligned with the summary cadence.
    // If report_interval_min was changed via a Notehub env-var update, re-issue
    // hub.set so notes actually arrive at the new interval instead of remaining
    // stuck at the interval configured at power-up.
    if (g_report_interval_min != g_state.last_hub_outbound) {
        J *req = notecard.newRequest("hub.set");
        JAddStringToObject(req, "product", PRODUCT_UID);
        JAddStringToObject(req, "mode",    "periodic");
        JAddNumberToObject(req, "outbound", (int)g_report_interval_min);
        JAddNumberToObject(req, "inbound",  120);
        J *rsp = notecard.requestAndResponse(req);
        if (!rsp) {
            debugSerial.println(F("[app] WARN: hub.set (runtime) — no Notecard response"));
        } else {
            const char *hub_err = JGetString(rsp, "err");
            if (hub_err && *hub_err) {
                debugSerial.print(F("[app] hub.set err: ")); debugSerial.println(hub_err);
            } else {
                g_state.last_hub_outbound = g_report_interval_min;
                debugSerial.print(F("[app] hub.set outbound updated to "));
                debugSerial.println(g_report_interval_min);
            }
            notecard.deleteResponse(rsp);
        }
    }

    // Re-init the Modbus UART on every wake using potentially-updated config.
    // serialConfigFromEnv() translates g_modbus_parity / g_modbus_stop_bits
    // into the HardwareSerial SERIAL_8xx constant so framing changes pushed
    // via Notehub env vars take effect on the next wake without re-flashing.
    Serial1.begin(g_modbus_baud, serialConfigFromEnv());
    modbus.begin(g_modbus_slave_id, Serial1);
    modbus.preTransmission(preTransmission);
    modbus.postTransmission(postTransmission);

    // Template registration is a hard precondition for note emission.  If
    // templates_ok is still false after the attempt above, go straight back
    // to sleep without touching any sensors or Notefiles; keeping sample_count
    // unchanged means the window boundaries stay correct when templates
    // eventually succeed.
    if (!g_state.templates_ok) {
        debugSerial.println(F("[app] Templates not confirmed — skipping sample cycle"));
        goToSleep(g_sample_interval_sec);
        return;
    }

    float v[MAX_STRINGS] = {0.0f};
    float a[MAX_STRINGS] = {0.0f};

    float irr      = readIrradiance();
    // readModuleTemp returns -9999.0f on sensor fault and emits a rate-limited
    // temp_probe_fault alert.  The caller must treat -9999 as invalid data.
    float mod_temp       = readModuleTemp(irr);
    bool  mod_temp_valid = (mod_temp > -9990.0f);
    bool  ok             = readStrings(v, a, g_n_strings, irr, mod_temp);

    // Clear alert_active flags whenever irradiance is below the evaluation
    // threshold — independent of Modbus/probe success.  evaluateAndAlert is
    // gated on (ok && mod_temp_valid), so doing this clear only inside that
    // function would leave stale daytime fault flags in overnight summaries
    // any time telemetry happened to fail at sunset.  Clearing here makes the
    // summary's alert_flags reliably read 0 in low-light/overnight windows.
    if (irr < g_irradiance_min) {
        for (uint8_t i = 0; i < MAX_STRINGS; i++) g_state.alert_active[i] = false;
    }

    // Always accumulate irradiance — it is valid regardless of sensor or Modbus
    // state, so the summary window mean is never biased by probe or bus failures.
    // Accumulate mod_temp only when the probe returned a valid reading so no
    // fabricated sentinel values contaminate the window mean.
    g_state.irr_sum += irr;
    g_state.n_env++;
    if (mod_temp_valid) {
        g_state.mod_temp_sum += mod_temp;
        g_state.n_temp_valid++;
    }

    // PR evaluation and window accumulation both require a valid module
    // temperature; skip the entire per-string cycle when the probe is faulted
    // so no fabricated expected-power values reach the accumulators or Notehub.
    if (ok && mod_temp_valid) {
        accumulateWindow(v, a, irr, mod_temp);
        evaluateAndAlert(v, a, irr, mod_temp);
    }

    g_state.sample_count++;

    // Emit summary at the end of each complete report window.
    // Ceiling division: if report_interval_min × 60 is not an exact integer
    // multiple of sample_interval_sec, round up so the window covers the full
    // configured report period rather than being silently shortened by floor
    // division. sample_count is incremented before the modulo check so every
    // window — including the very first — contains exactly wlen samples.
    uint32_t report_sec = g_report_interval_min * 60UL;
    uint32_t wlen = (report_sec + g_sample_interval_sec - 1UL) / g_sample_interval_sec;
    if (wlen < 1) wlen = 1;
    if ((g_state.sample_count % wlen) == 0) {
        // Only clear accumulators after the note is successfully queued.
        // On failure the window data carries forward and is included in the
        // next summary — no data is silently dropped after an I²C glitch.
        if (sendSummary()) {
            memset(g_state.accum, 0, sizeof(g_state.accum));
            g_state.irr_sum      = 0.0f;
            g_state.mod_temp_sum = 0.0f;
            g_state.n_env        = 0;
            g_state.n_temp_valid = 0;
        }
    }

    // Sleep until the Notecard raises ATTN g_sample_interval_sec seconds from
    // now. Execution resumes at the top of loop(); g_state stays in RAM.
    goToSleep(g_sample_interval_sec);
}
