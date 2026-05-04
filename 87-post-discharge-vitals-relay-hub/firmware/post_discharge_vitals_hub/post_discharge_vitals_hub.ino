/***************************************************************************
  post_discharge_vitals_hub.ino

  BLE-to-cellular relay hub for 30–60 day post-discharge patient monitoring.

  Host MCU : Adafruit Feather nRF52840 Express (on Notecarrier F)
  Cellular : Blues Notecard Cell+WiFi (NOTE-MBGLW)
  BLE role : Central — scans for and connects to patient-provided devices

  Supported BLE device types (Bluetooth SIG standard profiles):
    • Weight scale        — Weight Scale Service (0x181D)
    • Blood pressure cuff — Blood Pressure Service (0x1810)
    • Pulse oximeter      — Pulse Oximeter Service (0x1822)
    • Activity band       — Heart Rate Service (0x180D)

  Each completed reading is queued in a type-specific templated Notefile
  and uploaded to Notehub on the configured outbound cadence (default 15
  minutes).  Out-of-range readings emit an immediate sync alert to
  vitals_alert.qo so the care team is notified without waiting for the
  next scheduled upload.

  Alert thresholds are configurable without reflashing through Notehub
  environment variables fetched every hour.

  Device identity model (fail-closed by default):
    Primary gate  — BLE bonding: the SoftDevice resolves Resolvable Private
                    Addresses using stored IRKs; addr_id_peer=1 in the scan
                    report indicates a recognized bonded device.
    Secondary gate — MAC allow-list in vitals_config.h for devices with a
                    stable public/static Bluetooth address.
    Dev bypass    — Build with ALLOW_UNENROLLED_DEVICES_FOR_DEV=1 for initial
                    commissioning (pairing) or bench testing only.

  Build notes:
    • Select "Adafruit Feather nRF52840 Express" in the Arduino IDE Boards menu
      (via Adafruit nRF52 Arduino board support package).
    • Set PRODUCT_UID in vitals_config.h before flashing.
    • DEBUG_VITALS requires -DALLOW_DEBUG_BUILD; see vitals_config.h.
    • See README for full setup and commissioning instructions.
***************************************************************************/

#include <Notecard.h>
#include <bluefruit.h>
#include "vitals_config.h"
#include "ble_central.h"
#include "notecard_helpers.h"

// ─── Notecard I/O interface (I2C) ─────────────────────────────────────────────
Notecard notecard;

// ─── Configurable alert thresholds (overridable via Notehub env vars) ─────────
// Defaults are conservative starting points.  The care team should configure
// values appropriate for each patient via Notehub environment variables.
//
// CLINICAL DISCLAIMER: these defaults are illustrative for a proof-of-concept
// only.  They are not medical advice and must be reviewed by the clinical
// program team before use in any patient-facing setting.
float g_bp_systolic_high  = 160.0f;  // mmHg
float g_bp_diastolic_high = 100.0f;  // mmHg
float g_spo2_low          = 92.0f;   // %
float g_hr_high           = 130.0f;  // bpm (activity band Heart Rate Service only)
float g_hr_low            = 40.0f;   // bpm (activity band Heart Rate Service only)
float g_weight_delta_kg   = 2.3f;    // kg (~5 lbs) — standard CHF fluid-gain threshold

// ─── Buffered reading structs (type definitions in ble_central.h) ─────────────
// BLE indication/notification callbacks populate these; loop() drains them
// inside noInterrupts()/interrupts() critical sections to prevent torn reads.
WeightReading   g_weight   = {};
BpReading       g_bp       = {};
SpO2Reading     g_spo2     = {};
ActivityReading g_activity = {};

// Persists the last consumed weight reading across the valid-flag clear in
// loop(), enabling consecutive delta calculations.  Resets to 0 on power cycle.
float g_last_weight_kg = 0.0f;

// ─── Connection state ─────────────────────────────────────────────────────────
// Connection handle for the active Heart Rate Service peripheral.
uint16_t g_hrConnHandle        = BLE_CONN_HANDLE_INVALID;
// Connection handle for the currently connected one-shot peripheral (weight/BP/SpO2).
uint16_t g_oneshot_conn_handle = BLE_CONN_HANDLE_INVALID;
// Handle and start time of the currently active connection, used by the
// measurement timeout watchdog in loop().
uint16_t g_active_conn_handle  = BLE_CONN_HANDLE_INVALID;
uint32_t g_conn_start_ms       = 0;

// Millisecond timestamp of the most recent HR sample, used to gate reconnection
// in bleScanCallback.  Initialized so the first connection is never suppressed.
uint32_t g_last_hr_sample_ms = (uint32_t)(-HR_SAMPLE_INTERVAL_MS);

// ─── Alert cooldown timestamps ────────────────────────────────────────────────
// Pre-initialized to (uint32_t)(-COOLDOWN) so the first alert of each type fires
// immediately after boot without waiting for ALERT_COOLDOWN_MS to elapse.
// hr_high and hr_low use independent timestamps so a tachycardia alert cannot
// suppress a clinically distinct bradycardia alert, or vice versa.
uint32_t g_last_weight_alert_ms  = (uint32_t)(-ALERT_COOLDOWN_MS);
uint32_t g_last_bp_alert_ms      = (uint32_t)(-ALERT_COOLDOWN_MS);
uint32_t g_last_spo2_alert_ms    = (uint32_t)(-ALERT_COOLDOWN_MS);
uint32_t g_last_hr_high_alert_ms = (uint32_t)(-ALERT_COOLDOWN_MS);
uint32_t g_last_hr_low_alert_ms  = (uint32_t)(-ALERT_COOLDOWN_MS);

// Millisecond timestamp of the last env-var fetch
static uint32_t s_last_env_ms = 0;

// ─────────────────────────────────────────────────────────────────────────────
// ARDUINO SETUP
// ─────────────────────────────────────────────────────────────────────────────

void setup() {
    // Serial output and raw Notecard debug traffic are gated behind the
    // DEBUG_VITALS compile flag.  In production builds (DEBUG_VITALS=0, the
    // default), no patient data is printed over USB serial.
#if DEBUG_VITALS
    Serial.begin(115200);
    const uint32_t kSerialTimeoutMs = 3000;
    for (uint32_t t = millis(); !Serial && (millis() - t) < kSerialTimeoutMs; ) {}
    notecard.setDebugOutputStream(Serial);
#endif

    notecard.begin();   // I2C to Notecard (SDA/SCL via Notecarrier F Feather header)

    notecardConfigure();
    defineTemplates();
    fetchEnvVars();
    s_last_env_ms = millis();

    initBLE();

    DBG_PRINTLN("[APP] Post-discharge vitals hub running");
    DBG_PRINT("[APP]  Outbound: %d min  |  Inbound: %d min\n",
              OUTBOUND_INTERVAL_MIN, INBOUND_INTERVAL_MIN);
    DBG_PRINT("[APP]  Thresholds — BP: %g/%g mmHg  SpO2: %g%%  "
              "HR: %g/%g bpm  Weight Δ: %g kg\n",
              g_bp_systolic_high, g_bp_diastolic_high,
              g_spo2_low, g_hr_low, g_hr_high, g_weight_delta_kg);
}

// ─────────────────────────────────────────────────────────────────────────────
// MAIN LOOP
// ─────────────────────────────────────────────────────────────────────────────
// The nRF52840 SoftDevice processes BLE events asynchronously and populates
// the reading structs via the callbacks in ble_central.cpp.  The main loop
// drains any completed readings into Notecard notes, manages the one-shot
// disconnect and measurement timeout, and periodically refreshes the threshold
// env vars.
//
// Each copy-and-clear is wrapped in noInterrupts()/interrupts(), which map to
// FreeRTOS taskENTER_CRITICAL/EXIT_CRITICAL on the Adafruit nRF52 BSP.  This
// prevents a BLE callback from partially overwriting a multi-field struct
// while loop() is reading it, which could produce torn snapshots or lost samples.

void loop() {
    // ── Consume a completed weight reading ──────────────────────────────────
    if (g_weight.valid) {
        noInterrupts();
        WeightReading r = g_weight;
        g_weight.valid  = false;
        interrupts();
        g_last_weight_kg = r.kg;
        DBG_PRINT("[VITALS] Weight %.2f kg (prev %.2f kg)\n", r.kg, r.prev_kg);
        submitWeight(r.kg, r.prev_kg);
        // Weight scale is one-shot: disconnect after the first indication so
        // the single Central slot is freed for the next device.
        if (g_oneshot_conn_handle != BLE_CONN_HANDLE_INVALID) {
            Bluefruit.disconnect(g_oneshot_conn_handle);
        }
    }

    // ── Consume a completed blood pressure reading ───────────────────────────
    if (g_bp.valid) {
        noInterrupts();
        BpReading r = g_bp;
        g_bp.valid   = false;
        interrupts();
        DBG_PRINT("[VITALS] BP %d/%d mmHg  pulse %d bpm\n",
                  r.systolic, r.diastolic, r.pulse_bpm);
        submitBp(r.systolic, r.diastolic, r.pulse_bpm);
        if (g_oneshot_conn_handle != BLE_CONN_HANDLE_INVALID) {
            Bluefruit.disconnect(g_oneshot_conn_handle);
        }
    }

    // ── Consume a completed SpO2 reading ────────────────────────────────────
    if (g_spo2.valid) {
        noInterrupts();
        SpO2Reading r = g_spo2;
        g_spo2.valid  = false;
        interrupts();
        DBG_PRINT("[VITALS] SpO2 %d%%  pulse %d bpm\n", r.spo2_pct, r.pulse_bpm);
        submitSpO2(r.spo2_pct, r.pulse_bpm);
        if (g_oneshot_conn_handle != BLE_CONN_HANDLE_INVALID) {
            Bluefruit.disconnect(g_oneshot_conn_handle);
        }
    }

    // ── Consume a completed heart rate reading ───────────────────────────────
    // HR Service devices stream notifications continuously; disconnect after
    // the first sample and start the reconnect-suppression timer so a device
    // worn all day yields at most one sample per HR_SAMPLE_INTERVAL_MS.
    if (g_activity.valid) {
        noInterrupts();
        ActivityReading r = g_activity;
        g_activity.valid  = false;
        interrupts();
        g_last_hr_sample_ms = millis();
        if (g_hrConnHandle != BLE_CONN_HANDLE_INVALID) {
            Bluefruit.disconnect(g_hrConnHandle);
        }
        DBG_PRINT("[VITALS] Heart rate %d bpm\n", r.heart_rate_bpm);
        submitActivity(r.heart_rate_bpm);
    }

    // ── Measurement timeout watchdog ────────────────────────────────────────
    // Disconnect any peripheral that has been connected for
    // MEASUREMENT_TIMEOUT_MS without delivering a reading.  Covers hangs
    // inside discover() and one-shot devices that subscribe but never send.
    // Pre-clear g_active_conn_handle before calling disconnect to prevent a
    // second timeout-triggered disconnect on the next iteration while
    // bleDisconnectCallback is still pending.
    if (g_active_conn_handle != BLE_CONN_HANDLE_INVALID &&
        (millis() - g_conn_start_ms >= MEASUREMENT_TIMEOUT_MS)) {
        DBG_PRINTLN("[BLE] Measurement timeout — disconnecting idle peripheral");
        uint16_t h = g_active_conn_handle;
        g_active_conn_handle = BLE_CONN_HANDLE_INVALID;
        Bluefruit.disconnect(h);
    }

    // ── Periodic env-var refresh (every ENV_POLL_MS, default 2 minutes) ───────
    // env.get is resolved locally by the Notecard — no cellular round-trip.
    // Polling at a much shorter interval than the inbound sync window means
    // threshold changes pushed from Notehub are applied within 2 minutes of
    // the next inbound session rather than waiting up to a full additional hour.
    if (millis() - s_last_env_ms >= ENV_POLL_MS) {
        fetchEnvVars();
        s_last_env_ms = millis();
    }

    // Yield to the SoftDevice BLE stack.  The 100 ms delay keeps the main loop
    // from spinning and starving the radio task; BLE events continue to arrive
    // via callbacks during this delay.
    delay(100);
}
