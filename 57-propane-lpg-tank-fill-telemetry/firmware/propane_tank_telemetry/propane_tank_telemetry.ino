// propane_tank_telemetry.ino
//
// Propane / LPG Tank Fill Telemetry — 4–20 mA Float-Transmitter Variant
//
// Host:      Blues Notecarrier CX (onboard Cygnet STM32 host)
// Notecard:  Blues Notecard Cell+WiFi (MBGLW); at sites with no cellular
//            coverage, add a Starnote for Skylo connected to the Notecard's
//            6-pin JST port for automatic NTN satellite fallback — no firmware
//            changes required; the Notecard routes queued notes over satellite
//            when cellular is unavailable
// Sensors:   4-20 mA LP gauge-port level transmitter (float type, e.g. Rochester
//            Sensors M6300-LP + R6315-12) → 120 Ω shunt → A0
//            DS18B20 waterproof temperature probe (OneWire) → D2
//
// Sensor model — float-transmitter variant (see README §1 variant note and
// §6 "Sensor selection rationale"):
//
//   The original project specification called for a low-power ultrasonic or
//   pressure level sensor with the DS18B20 temperature probe used for
//   vapor-pressure compensation in the fill measurement path. This file
//   implements the 4–20 mA float-transmitter variant, which intentionally
//   departs from that specification. It should be treated as one concrete
//   implementation path, not a complete delivery of the original brief.
//   See README §6 for the full rationale; brief summary:
//
//   Ultrasonic: requires a dedicated top-of-tank port (extra tank penetration)
//     and temperature-compensated speed-of-sound correction whose accuracy
//     depends on LP vapor composition, which is unknown to the firmware.
//
//   Pressure: the standard gauge port accesses the vapor space, not the liquid
//     column bottom; resolving the ~5 kPa liquid head on top of ~850 kPa vapor
//     pressure demands unrealistic sensor accuracy. A differential-pressure
//     approach spanning a bottom port and vapor-space port is more accurate but
//     requires two tank connections and two field-wiring runs.
//
//   Float transmitter (selected): installs at the single existing 1¼″ NPT
//     gauge port, no additional tank penetrations. The float tracks the physical
//     liquid propane surface; its 4–20 mA output is directly proportional to
//     fill level regardless of liquid temperature or density. No vapor-pressure
//     or density compensation is applied in firmware — none is needed.
//
//   Level sensor: Rochester Sensors M6300-LP Magnetel® + R6315-12 LP gauge-port
//     float transmitter (4–20 mA, two-wire loop-powered) → 120 Ω shunt → A0.
//
//   Temperature sensor: DS18B20 waterproof probe clamped to the tank shell.
//     Read on every cycle and included in daily summary notes for cloud-side
//     seasonal demand analytics. NOT an input to the fill calculation.
//
// Runtime cadence:
//   - Host wakes every SAMPLE_INTERVAL_MIN via card.attn.
//   - Each wake: sample sensors, update consumption EWMA, evaluate thresholds.
//   - When REPORT_INTERVAL_HR has elapsed, queue one tank_status.qo note.
//   - Alerts (low_fill, high_consumption, sensor_fault) are sent sync:true.
//   - Between wakes the host is cut entirely; Notecard idles at ~18 µA
//     (NOTE-MBGLW Notecard Cell+WiFi published idle figure).
//
// Sensor math, fill-level calculation, and consumption tracking live in
// propane_tank_telemetry_helpers.h (included below).
//
// See README.md for wiring, Notehub setup, and calibration instructions.

#include <Notecard.h>
#include <Wire.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include "propane_tank_telemetry_helpers.h"

#ifndef PRODUCT_UID
#define PRODUCT_UID "" // "com.my-company.my-name:propane-tank-telemetry"
#pragma message "PRODUCT_UID not set. Claim one in Notehub, then define it here."
#endif

#define usbSerial Serial

// -------- Pin assignments (Notecarrier CX headers) --------
static const uint8_t PIN_TRANSMITTER = A0;   // 4-20 mA via 120 Ω shunt
static const uint8_t PIN_ONEWIRE     = 2;    // DS18B20 data line

// -------- Default thresholds / intervals (all overridable via Notehub env vars) --------
static float    TANK_CAPACITY_GAL     = 500.0f;
static float    SENSOR_EMPTY_MA       = 4.0f;    // mA from transmitter at 0 % fill (standard 4 mA live-zero)
static float    SENSOR_FULL_MA        = 20.0f;   // mA from transmitter at 100 % fill
static float    FILL_ALERT_PCT        = 20.0f;   // low-fill alert threshold (%)
static float    CONSUMPTION_ALERT_GPD = 100.0f;  // high-consumption alert threshold (gal/day)
static uint32_t SAMPLE_INTERVAL_MIN   = 15;
static uint32_t REPORT_INTERVAL_HR    = 24;
static uint32_t ALERT_COOLDOWN_HR     = 4;       // min hours between repeated alerts of same type
static uint8_t CONSUMPTION_ALERT_STREAK = 3;  // consecutive above-threshold wakes before high_consumption fires; overridable via consumption_alert_streak env var

// -------- State preserved across deep-sleep cycles --------
// NotePayloadSaveAndSleep serializes this struct to Notecard flash before
// cutting host power. NotePayloadRetrieveAfterSleep rehydrates it at the
// top of the next setup(). Field order matters for ABI compatibility —
// add new fields at the end only.
struct PersistState {
  // Summary window state (reset after each summary note).
  // last_fill_* hold the most-recent valid reading so the daily note carries
  // current tank state rather than a 24-hour average. n_fill is a has-data flag
  // (> 0 means at least one valid reading was collected this window).
  // min_fill_pct tracks the window's lowest fill level for analytics.
  float    last_fill_pct;    uint32_t n_fill;   // n_fill: 0 = no valid reading this window
  float    last_fill_gal;
  float    sum_temp_c;       uint32_t n_temp;
  float    last_xmtr_ma;
  uint32_t summary_window_start_epoch;

  // Consumption tracking (survives summary-window resets)
  ConsumptionState consumption;

  // Alert deduplication: epoch of last fire per type
  uint32_t last_low_fill_epoch;
  uint32_t last_high_consumption_epoch;
  uint32_t last_sensor_fault_epoch;

  // Tracks the last outbound cadence sent to hub.set so we can detect changes
  uint32_t last_applied_outbound_hr;

  uint32_t cycles;

  // Fields added after initial release — must remain at the end for ABI stability.
  // A size mismatch on first boot after a firmware upgrade causes a clean cold-start.
  bool    templates_defined;        // true once both note.template calls succeed
  uint8_t high_consumption_streak;  // consecutive wakes with gpd > CONSUMPTION_ALERT_GPD

  // Fix [1]: Last-applied calibration values. When any of these differ from the
  // current env-var values after a successful env.get, the consumption state and
  // summary accumulators are reset so pre-change readings (scaled to the old
  // capacity/span) cannot corrupt the post-change EWMA or rate projections.
  float   last_tank_capacity_gal;
  float   last_sensor_empty_ma;
  float   last_sensor_full_ma;

  // One-shot dedup flags for alerts queued before card.time is valid.
  // Cleared automatically the first time a valid epoch is available.
  bool    pre_time_low_fill_sent;
  bool    pre_time_sensor_fault_sent;

  float   min_fill_pct;   // lowest fill_pct seen in this reporting window (NAN = none yet)
};

static const char STATE_SEG_ID[] = "PLPG";
static PersistState state;

// -------- Peripheral objects --------
Notecard        notecard;
OneWire         oneWire(PIN_ONEWIRE);
DallasTemperature tempSensor(&oneWire);

// -------- Notecard helpers --------

static void hubConfigure() {
  J *req = notecard.newRequest("hub.set");
  if (PRODUCT_UID[0]) JAddStringToObject(req, "product", PRODUCT_UID);
  JAddStringToObject(req, "mode", "periodic");
  // Derive outbound/inbound in minutes and clamp defensively.  REPORT_INTERVAL_HR
  // is bounded to 168 by fetchEnvOverrides, so the products fit safely in uint32_t;
  // the explicit cap guards against hubConfigure() being called before env vars are
  // fetched (cold-start first wake) and against any future code path that could
  // bypass the env-var validation.
  uint32_t outbound_min = REPORT_INTERVAL_HR * 60u;
  uint32_t inbound_min  = REPORT_INTERVAL_HR * 120u;  // 2× outbound
  if (outbound_min > 10080u) outbound_min = 10080u;   // cap at 1 week (168 hr × 60)
  if (inbound_min  > 10080u) inbound_min  = 10080u;
  JAddNumberToObject(req, "outbound", (int)outbound_min);
  JAddNumberToObject(req, "inbound",  (int)inbound_min);
  // sendRequestWithRetry handles the cold-boot I2C race noted in note-arduino.
  // Only cache the new cadence after confirmed success — if hub.set fails we keep
  // the old last_applied_outbound_hr so the mismatch is detected and retried on
  // the next wake, preventing the Notecard from staying on a stale schedule.
  if (notecard.sendRequestWithRetry(req, 10)) {
    state.last_applied_outbound_hr = REPORT_INTERVAL_HR;
  } else {
#ifdef usbSerial
    usbSerial.println("[hub.set] failed — will retry on next wake");
#endif
  }
}

// Returns true only when both note.template requests succeed. Callers should
// store the return value and retry on the next wake if false — a one-time
// failure at cold-start would otherwise leave notes untemplatized indefinitely.
// note.template is idempotent on the Notecard, so repeating it is safe.
static bool defineTemplates() {
  bool ok_status = false;
  bool ok_alert  = false;

  // tank_status.qo: daily fill summary. Templates compress notes to fixed-
  // length records (~3-5x smaller than free-form JSON on the wire) — material
  // for a fleet transmitting 365 notes/tank/year on a prepaid SIM.
  {
    J *req = notecard.newRequest("note.template");
    JAddStringToObject(req, "file", "tank_status.qo");
    JAddNumberToObject(req, "port", 50);
    J *body = JAddObjectToObject(req, "body");
    JAddNumberToObject(body, "fill_pct",         14.1);  // 4-byte float; current fill at time of note
    JAddNumberToObject(body, "fill_gal",         14.1);  // current fill gallons
    JAddNumberToObject(body, "min_fill_pct",     14.1);  // lowest fill seen in this reporting window
    JAddNumberToObject(body, "temp_c",           14.1);  // window-averaged tank-shell temperature
    JAddNumberToObject(body, "gal_per_day",      14.1);
    JAddNumberToObject(body, "days_until_empty", 14.1);
    JAddNumberToObject(body, "transmitter_ma",   14.1);  // current transmitter output current
    ok_status = notecard.sendRequest(req);
    if (!ok_status) {
#ifdef usbSerial
      usbSerial.println("[note.template] tank_status.qo failed — will retry on next wake");
#endif
    }
  }
  // tank_alert.qo: immediate alerts sent with sync:true to bypass the daily
  // outbound window. Separate Notefile so routes can fan alerts to a high-
  // urgency destination (dispatch webhook, SMS gateway) while summaries go
  // to a time-series store.
  {
    J *req = notecard.newRequest("note.template");
    JAddStringToObject(req, "file", "tank_alert.qo");
    JAddNumberToObject(req, "port", 51);
    J *body = JAddObjectToObject(req, "body");
    JAddStringToObject(body, "alert", "high_consumption");  // longest real value → Notecard reserves 16 bytes
    JAddNumberToObject(body, "fill_pct",         14.1);
    JAddNumberToObject(body, "fill_gal",         14.1);
    JAddNumberToObject(body, "gal_per_day",      14.1);
    JAddNumberToObject(body, "days_until_empty", 14.1);
    ok_alert = notecard.sendRequest(req);
    if (!ok_alert) {
#ifdef usbSerial
      usbSerial.println("[note.template] tank_alert.qo failed — will retry on next wake");
#endif
    }
  }
  return ok_status && ok_alert;
}

// Returns the current Unix epoch from the Notecard, or 0 if:
//   - the I2C request failed (rsp == NULL), or
//   - the Notecard returned an error (time not yet synchronized), or
//   - the "time" field is absent or zero.
// Callers must treat 0 as "time unknown" and skip any wall-clock-dependent logic.
static uint32_t notecardEpoch() {
  J *rsp = notecard.requestAndResponse(notecard.newRequest("card.time"));
  if (!rsp) return 0;
  if (notecard.responseError(rsp)) { notecard.deleteResponse(rsp); return 0; }
  uint32_t epoch = (uint32_t)JGetNumber(rsp, "time");
  notecard.deleteResponse(rsp);
  return epoch;  // 0 means the Notecard has not yet acquired time
}

static bool fetchEnvOverrides() {
  J *req = notecard.newRequest("env.get");
  J *names = JAddArrayToObject(req, "names");
  JAddItemToArray(names, JCreateString("tank_capacity_gal"));
  JAddItemToArray(names, JCreateString("sensor_empty_ma"));
  JAddItemToArray(names, JCreateString("sensor_full_ma"));
  JAddItemToArray(names, JCreateString("fill_alert_pct"));
  JAddItemToArray(names, JCreateString("consumption_alert_gal_per_day"));
  JAddItemToArray(names, JCreateString("sample_interval_min"));
  JAddItemToArray(names, JCreateString("report_interval_hr"));
  JAddItemToArray(names, JCreateString("alert_cooldown_hr"));
  JAddItemToArray(names, JCreateString("consumption_alert_streak"));
  JAddItemToArray(names, JCreateString("wifi_ssid"));
  JAddItemToArray(names, JCreateString("wifi_password"));

  J *rsp = notecard.requestAndResponse(req);
  if (!rsp) return false;
  if (notecard.responseError(rsp)) { notecard.deleteResponse(rsp); return false; }

  J *body = JGetObjectItem(rsp, "body");
  if (body) {
    const char *v;
    float    fv;
    uint32_t uv;

    // tank_capacity_gal: must be a physically plausible positive tank size.
    // Upper bound of 30 000 gal covers the largest commercial bulk tanks;
    // rejecting values above it catches decimal-point typos that would
    // inflate the consumption deadband and silently break the EWMA.
    v = JGetString(body, "tank_capacity_gal");
    if (v && v[0]) {
      fv = atof(v);
      if (fv >= 10.0f && fv <= 30000.0f) {
        TANK_CAPACITY_GAL = fv;
      } else {
#ifdef usbSerial
        usbSerial.print("[env] tank_capacity_gal must be 10–30000, ignored: "); usbSerial.println(v);
#endif
      }
    }

    // sensor_empty_ma / sensor_full_ma: read together before validating.
    // A span < 1 mA causes computeFillPct to return NAN for every reading.
    // Both values must stay within the valid 4-20 mA loop range (1–25 mA
    // covers all standard transmitters plus fault-current headroom).
    {
      float new_empty = SENSOR_EMPTY_MA;
      float new_full  = SENSOR_FULL_MA;
      bool  changed   = false;
      v = JGetString(body, "sensor_empty_ma");
      if (v && v[0]) { new_empty = atof(v); changed = true; }
      v = JGetString(body, "sensor_full_ma");
      if (v && v[0]) { new_full  = atof(v); changed = true; }
      if (changed) {
        if (new_empty >= 1.0f && new_full <= 25.0f && (new_full - new_empty) >= 1.0f) {
          SENSOR_EMPTY_MA = new_empty;
          SENSOR_FULL_MA  = new_full;
        } else {
#ifdef usbSerial
          usbSerial.println("[env] sensor_empty/full_ma out of range or span <1 mA — ignored");
#endif
        }
      }
    }

    // fill_alert_pct: 0–100 % is the full valid range. 0 effectively disables
    // the alert (fill can never drop below 0 %); 100 would alert continuously —
    // both are accepted as deliberate operator choices.
    v = JGetString(body, "fill_alert_pct");
    if (v && v[0]) {
      fv = atof(v);
      if (fv >= 0.0f && fv <= 100.0f) {
        FILL_ALERT_PCT = fv;
      } else {
#ifdef usbSerial
        usbSerial.print("[env] fill_alert_pct must be 0–100, ignored: "); usbSerial.println(v);
#endif
      }
    }

    // consumption_alert_gal_per_day: must be > 0 (zero or negative would fire
    // the alert permanently) and ≤ 10 000 gal/day (catches magnitude typos;
    // no residential or light-commercial installation legitimately exceeds this).
    v = JGetString(body, "consumption_alert_gal_per_day");
    if (v && v[0]) {
      fv = atof(v);
      if (fv > 0.0f && fv <= 10000.0f) {
        CONSUMPTION_ALERT_GPD = fv;
      } else {
#ifdef usbSerial
        usbSerial.print("[env] consumption_alert_gal_per_day must be >0 and <=10000, ignored: "); usbSerial.println(v);
#endif
      }
    }

    // sample_interval_min: 1–1440.  Lower bound prevents a tight host wake loop
    // that drains the battery and spams the Notecard over I2C.  Upper bound of
    // 1440 (once per day) keeps the bench-mode delay() product (interval × 60 s
    // × 1000 ms) well within uint32_t and ensures at least one sample per
    // report window when report_interval_hr == 24.
    v = JGetString(body, "sample_interval_min");
    if (v && v[0]) {
      uv = (uint32_t)atol(v);
      if (uv >= 1 && uv <= 1440) {
        SAMPLE_INTERVAL_MIN = uv;
      } else {
#ifdef usbSerial
        usbSerial.print("[env] sample_interval_min must be 1–1440, ignored: "); usbSerial.println(v);
#endif
      }
    }

    // report_interval_hr: 1–168.  Lower bound prevents hub.set outbound=0
    // which is an invalid Notecard cadence and would cause continuous cellular
    // sessions.  Upper bound of 168 (one week) keeps the derived outbound_min
    // and inbound_min products in hubConfigure() within the Notecard's accepted
    // range and protects the summary-window epoch comparison from overflow.
    v = JGetString(body, "report_interval_hr");
    if (v && v[0]) {
      uv = (uint32_t)atol(v);
      if (uv >= 1 && uv <= 168) {
        REPORT_INTERVAL_HR = uv;
      } else {
#ifdef usbSerial
        usbSerial.print("[env] report_interval_hr must be 1–168, ignored: "); usbSerial.println(v);
#endif
      }
    }

    // alert_cooldown_hr: 1–168.  Lower bound prevents per-cycle alert spam on a
    // slowly-draining tank (zero would re-fire every sample cycle).  Upper bound
    // of 168 keeps cooldown_sec = ALERT_COOLDOWN_HR × 3600 comfortably within
    // uint32_t and avoids silently disabling alerts for more than a week at a time.
    v = JGetString(body, "alert_cooldown_hr");
    if (v && v[0]) {
      uv = (uint32_t)atol(v);
      if (uv >= 1 && uv <= 168) {
        ALERT_COOLDOWN_HR = uv;
      } else {
#ifdef usbSerial
        usbSerial.print("[env] alert_cooldown_hr must be 1–168, ignored: "); usbSerial.println(v);
#endif
      }
    }

    // consumption_alert_streak: 1–20.  Lower bound of 1 disables debouncing so
    // any single above-threshold reading fires the alert.  Upper bound of 20
    // bounds the worst-case alerting delay to 20 × SAMPLE_INTERVAL_MIN minutes
    // — at the 15-minute default that is 5 hours, which is the longest a
    // genuine sustained high-consumption event should be silenced before alert.
    v = JGetString(body, "consumption_alert_streak");
    if (v && v[0]) {
      uv = (uint32_t)atol(v);
      if (uv >= 1 && uv <= 20) {
        CONSUMPTION_ALERT_STREAK = (uint8_t)uv;
      } else {
#ifdef usbSerial
        usbSerial.print("[env] consumption_alert_streak must be 1–20, ignored: "); usbSerial.println(v);
#endif
      }
    }

    // WiFi credentials: when both wifi_ssid and wifi_password are present and
    // non-empty, provision WiFi on the Notecard via card.wifi. This is the
    // recommended provisioning path for deployed Notecarrier CX hardware where
    // the Notecard is not directly accessible via USB. The Notecard stores
    // credentials internally and connects over WiFi when it provides adequate
    // coverage; cellular remains the primary transport. card.wifi is idempotent
    // — the Notecard ignores the call if the credentials are already stored and
    // unchanged, so calling on every wake when both vars are set is harmless.
    {
      const char *ssid     = JGetString(body, "wifi_ssid");
      const char *password = JGetString(body, "wifi_password");
      if (ssid && ssid[0] && password && password[0]) {
        J *wifi_req = notecard.newRequest("card.wifi");
        JAddStringToObject(wifi_req, "ssid",     ssid);
        JAddStringToObject(wifi_req, "password", password);
        if (!notecard.sendRequest(wifi_req)) {
#ifdef usbSerial
          usbSerial.println("[wifi] card.wifi failed — WiFi credentials not updated");
#endif
        } else {
#ifdef usbSerial
          usbSerial.print("[wifi] credentials provisioned for SSID: "); usbSerial.println(ssid);
#endif
        }
      }
    }
  }
  notecard.deleteResponse(rsp);

  // Fix [1]: Detect calibration changes and reset consumption state.
  // Only executes after a successful env.get so a transient I2C failure (which
  // leaves globals at firmware defaults) cannot trigger a spurious reset when
  // the operator has previously commissioned the unit with custom values.
  {
    bool cal_changed = (TANK_CAPACITY_GAL != state.last_tank_capacity_gal ||
                        SENSOR_EMPTY_MA   != state.last_sensor_empty_ma   ||
                        SENSOR_FULL_MA    != state.last_sensor_full_ma);
    if (cal_changed) {
      // state.consumption.prev_fill_gal was recorded against the old scale, so
      // mixing it with post-change readings would yield a bogus delta_gal and
      // corrupt the EWMA. Reset the consumption state entirely and re-seed it
      // from the next valid reading. Clear the summary accumulators too — a
      // single reporting window must not span a calibration boundary.
      memset(&state.consumption, 0, sizeof(state.consumption));
      state.high_consumption_streak    = 0;
      state.n_fill                     = 0;
      state.sum_temp_c                 = 0.0f;
      state.n_temp                     = 0;
      state.min_fill_pct               = NAN;
      state.summary_window_start_epoch = 0;  // force re-anchor on next valid epoch
      state.last_tank_capacity_gal = TANK_CAPACITY_GAL;
      state.last_sensor_empty_ma   = SENSOR_EMPTY_MA;
      state.last_sensor_full_ma    = SENSOR_FULL_MA;
#ifdef usbSerial
      usbSerial.println("[cal] calibration changed — consumption state and summary window reset");
#endif
    }
  }

  // Re-issue hub.set if the operator changed the report cadence from Notehub.
  // Without this, the Notecard keeps transmitting on the old schedule even
  // after the firmware has adopted the new interval.
  if (REPORT_INTERVAL_HR != state.last_applied_outbound_hr) {
    hubConfigure();
  }

  return true;
}

// -------- Alert / summary emission --------

static bool sendAlert(const char *kind, float fill_pct, float fill_gal,
                      float gpd, float dte) {
  // Retry once on failure — alert notes with sync:true are high-value and should
  // survive a transient I2C hiccup. A 500 ms pause gives the Notecard time to
  // recover before the second attempt.
  bool sent = false;
  for (int attempt = 0; attempt < 2 && !sent; attempt++) {
    if (attempt > 0) delay(500);
    J *req = notecard.newRequest("note.add");
    JAddStringToObject(req, "file", "tank_alert.qo");
    JAddBoolToObject  (req, "sync", true);  // bypass daily outbound timer
    J *body = JAddObjectToObject(req, "body");
    JAddStringToObject(body, "alert",            kind);
    JAddNumberToObject(body, "fill_pct",         isnan(fill_pct) ? INVALID_SENTINEL : fill_pct);
    JAddNumberToObject(body, "fill_gal",         isnan(fill_gal) ? INVALID_SENTINEL : fill_gal);
    JAddNumberToObject(body, "gal_per_day",      (gpd < 0.01f || isnan(gpd)) ? INVALID_SENTINEL : gpd);
    JAddNumberToObject(body, "days_until_empty", isnan(dte)      ? INVALID_SENTINEL : dte);
    sent = notecard.sendRequest(req);
  }
#ifdef usbSerial
  if (sent) {
    usbSerial.print("[alert] "); usbSerial.println(kind);
  } else {
    usbSerial.print("[alert] FAILED after retry: "); usbSerial.println(kind);
  }
#endif
  return sent;
}

static void sendSummary(uint32_t now_epoch) {
  // Report the most-recent valid fill reading — not a window average. A 24-hour
  // average of fill_pct/fill_gal would materially misrepresent tank state after
  // a refill event or a sharp draw-down, undermining the route-dispatch use case.
  // Temperature is averaged to represent the day's thermal environment.
  // min_fill_pct captures the lowest point seen in the window for analytics.
  float cur_fill_pct = (state.n_fill > 0) ? state.last_fill_pct : INVALID_SENTINEL;
  float cur_fill_gal = (state.n_fill > 0) ? state.last_fill_gal : INVALID_SENTINEL;
  float cur_xmtr_ma  = (state.n_fill > 0) ? state.last_xmtr_ma  : INVALID_SENTINEL;
  float win_min_pct  = isnan(state.min_fill_pct) ? INVALID_SENTINEL : state.min_fill_pct;
  float avg_temp_c   = safeAvg(state.sum_temp_c, state.n_temp);
  float gpd          = state.consumption.gal_per_day;
  float dte          = daysUntilEmpty(cur_fill_gal, gpd);

  // Retry once on transient I2C failure. sendRequest always deletes the request
  // object on return, so a fresh J* must be built for each attempt.
  bool sent = false;
  for (int attempt = 0; attempt < 2 && !sent; attempt++) {
    if (attempt > 0) delay(500);
    J *req = notecard.newRequest("note.add");
    JAddStringToObject(req, "file", "tank_status.qo");
    J *body = JAddObjectToObject(req, "body");
    JAddNumberToObject(body, "fill_pct",         cur_fill_pct);
    JAddNumberToObject(body, "fill_gal",         cur_fill_gal);
    JAddNumberToObject(body, "min_fill_pct",     win_min_pct);
    JAddNumberToObject(body, "temp_c",           avg_temp_c);
    JAddNumberToObject(body, "gal_per_day",      gpd < 0.01f ? INVALID_SENTINEL : gpd);
    JAddNumberToObject(body, "days_until_empty", dte);
    JAddNumberToObject(body, "transmitter_ma",   cur_xmtr_ma);
    sent = notecard.sendRequest(req);  // no sync:true — periodic outbound window batches this
  }

  if (!sent) {
    // Preserve window state so the data is not lost. The next wake
    // will re-attempt the note with the most-recent readings still in place.
#ifdef usbSerial
    usbSerial.println("[summary] send failed after retry — window preserved");
#endif
    return;
  }

#ifdef usbSerial
  usbSerial.print("[summary] fill_pct="); usbSerial.print(cur_fill_pct);
  usbSerial.print(" fill_gal=");          usbSerial.print(cur_fill_gal);
  usbSerial.print(" gpd=");               usbSerial.print(gpd);
  usbSerial.print(" dte=");               usbSerial.println(dte);
#endif

  // Reset window state only after the note has been accepted by the Notecard
  // queue. last_fill_* are overwritten on the next valid sample — only the
  // has-data flag and min tracker need explicit reset. The consumption EWMA and
  // alert dedup timestamps are persistent state and are intentionally NOT reset.
  state.n_fill       = 0;
  state.sum_temp_c   = 0.0f;
  state.n_temp       = 0;
  state.min_fill_pct = NAN;
  state.summary_window_start_epoch = now_epoch;
}

// -------- One sample cycle --------

static void runSampleCycle() {
  uint32_t now_epoch  = notecardEpoch();
  bool     time_valid = (now_epoch != 0);

  float xmtr_ma  = readTransmitterMA(PIN_TRANSMITTER);
  // Temperature is read every cycle for inclusion in daily summary notes. It does
  // not affect the fill calculation: the float-type transmitter tracks liquid level
  // directly, independent of liquid density or temperature. See README §6 and the
  // top-of-file sensor model note for the full design rationale.
  float temp_c   = readTemperatureC(tempSensor);
  float fill_pct = computeFillPct(xmtr_ma, SENSOR_EMPTY_MA, SENSOR_FULL_MA);
  float fill_gal = computeFillGal(fill_pct, TANK_CAPACITY_GAL);

#ifdef usbSerial
  usbSerial.print("[sample] xmtr_ma="); usbSerial.print(xmtr_ma);
  usbSerial.print(" fill_pct=");        usbSerial.print(fill_pct);
  usbSerial.print(" fill_gal=");        usbSerial.print(fill_gal);
  usbSerial.print(" temp_c=");          usbSerial.println(temp_c);
#endif

  // Update summary window state. last_fill_* always hold the most-recent valid
  // reading so the daily note carries current tank state, not a window average.
  // min_fill_pct tracks the lowest fill seen in the window for analytics.
  // Readings are collected regardless of RTC validity so the window has data
  // the moment time becomes available.
  if (!isnan(fill_pct) && !isnan(fill_gal)) {
    state.last_fill_pct = fill_pct;
    state.last_fill_gal = fill_gal;
    state.last_xmtr_ma  = xmtr_ma;
    state.n_fill++;
    if (isnan(state.min_fill_pct) || fill_pct < state.min_fill_pct) {
      state.min_fill_pct = fill_pct;
    }
  }
  if (!isnan(temp_c)) {
    state.sum_temp_c += temp_c;
    state.n_temp++;
  }

  // Update the EWMA consumption rate only when the Notecard has a valid epoch.
  // Seeding the consumption accumulator against epoch 0 would corrupt gal/day
  // estimates once real time becomes available (prev_fill_epoch = 0 produces a
  // wildly long delta_day on the first valid reading).
  if (time_valid && !isnan(fill_gal)) {
    updateConsumption(state.consumption, fill_gal, now_epoch, TANK_CAPACITY_GAL);
  }

  float gpd = state.consumption.gal_per_day;
  float dte = daysUntilEmpty(fill_gal, gpd);

  // Fix [2]: Before the Notecard has a valid epoch, cooldown dedup and the
  // summary window cannot be used. However, a freshly installed unit with a
  // broken 4–20 mA loop or a genuinely low tank must not be silenced for the
  // entire initial sync window (which can span many hours on satellite-fallback
  // deployments). Allow one sensor_fault and one low_fill alert via per-type
  // boolean flags so the operator is notified without per-cycle alert spam.
  if (!time_valid) {
    if (isnan(xmtr_ma) && !state.pre_time_sensor_fault_sent) {
      if (sendAlert("sensor_fault", INVALID_SENTINEL, INVALID_SENTINEL,
                    INVALID_SENTINEL, INVALID_SENTINEL)) {
        state.pre_time_sensor_fault_sent = true;
      }
    }
    if (!isnan(fill_pct) && fill_pct < FILL_ALERT_PCT &&
        !state.pre_time_low_fill_sent) {
      if (sendAlert("low_fill", fill_pct, fill_gal,
                    INVALID_SENTINEL, INVALID_SENTINEL)) {
        state.pre_time_low_fill_sent = true;
      }
    }
#ifdef usbSerial
    usbSerial.println("[sample] time not yet valid — pre-time alert path active, skipping cooldowns and summary");
#endif
    return;
  }

  // Time is now valid. Clear pre-time dedup flags so they reset correctly if
  // the Notecard ever loses time and re-acquires it (edge case on power cycles).
  state.pre_time_sensor_fault_sent = false;
  state.pre_time_low_fill_sent     = false;

  uint32_t cooldown_sec = ALERT_COOLDOWN_HR * 3600UL;

  // ---- Threshold evaluations (each with its own cooldown dedup) ----

  // Sensor fault: transmitter current outside the valid 3.5-21 mA window.
  // Indicates open loop (broken wire) or a short circuit — needs a site visit.
  // Cooldown timestamp is only advanced when the Notecard accepted the note —
  // if queuing fails the alert can be retried on the next wake.
  if (isnan(xmtr_ma) &&
      (now_epoch - state.last_sensor_fault_epoch) > cooldown_sec) {
    if (sendAlert("sensor_fault", INVALID_SENTINEL, INVALID_SENTINEL,
                  gpd < 0.01f ? INVALID_SENTINEL : gpd, INVALID_SENTINEL)) {
      state.last_sensor_fault_epoch = now_epoch;
    }
  }

  // Low fill: tank approaching empty. Fire once per cooldown window.
  if (!isnan(fill_pct) && fill_pct < FILL_ALERT_PCT &&
      (now_epoch - state.last_low_fill_epoch) > cooldown_sec) {
    if (sendAlert("low_fill", fill_pct, fill_gal,
                  gpd < 0.01f ? INVALID_SENTINEL : gpd, dte)) {
      state.last_low_fill_epoch = now_epoch;
    }
  }

  // High consumption: smoothed daily rate above threshold. Most residential
  // tanks run 3–15 gal/day; values above 100 suggest a leak or piping fault.
  // The streak counter requires CONSUMPTION_ALERT_STREAK consecutive wakes
  // above the threshold before the alert fires, suppressing false positives
  // from level-sensor jitter at short sample intervals. The counter resets to
  // zero whenever the rate drops back below the threshold.
  //
  // Fix [3]: Gate on a current valid fill reading (!isnan(fill_pct)) so that
  // a stale EWMA accumulated before a sensor fault cannot keep advancing the
  // streak and eventually emit a false high_consumption alert. The streak resets
  // to zero on every wake where xmtr_ma / fill_pct is invalid (open loop,
  // short circuit, transmitter disconnected), forcing a fresh run of consecutive
  // above-threshold readings before the alert can fire again.
  if (!isnan(fill_pct) && gpd > CONSUMPTION_ALERT_GPD && state.consumption.seeded) {
    if (state.high_consumption_streak < 0xFFu) state.high_consumption_streak++;
  } else {
    state.high_consumption_streak = 0;
  }
  if (state.high_consumption_streak >= CONSUMPTION_ALERT_STREAK &&
      (now_epoch - state.last_high_consumption_epoch) > cooldown_sec) {
    if (sendAlert("high_consumption", fill_pct, fill_gal, gpd, dte)) {
      state.last_high_consumption_epoch = now_epoch;
    }
  }

  // Emit the daily summary when the report window has elapsed.
  // summary_window_start_epoch == 0 means the anchor was never set (time was
  // unavailable in every prior setup() call) — skip until it is initialized.
  if (state.summary_window_start_epoch != 0 &&
      (now_epoch - state.summary_window_start_epoch) >= REPORT_INTERVAL_HR * 3600UL) {
    sendSummary(now_epoch);
  }
}

// -------- Setup / loop --------
//
// Deep-sleep pattern: the Notecard brings the host back up every
// SAMPLE_INTERVAL_MIN via card.attn. setup() restores state and runs
// initialization; loop() performs one sample cycle, saves state to Notecard
// flash, and issues the sleep command. State survives the power cut via
// NotePayloadSaveAndSleep / NotePayloadRetrieveAfterSleep.
//
// Bench/USB mode: when card.attn does not physically gate VBAT, loop() returns
// from NotePayloadSaveAndSleep without cutting power, waits out the interval in
// delay(), then runs again — sampling correctly on every iteration with no
// special-case code.

void setup() {
#ifdef usbSerial
  usbSerial.begin(115200);
  for (uint32_t t0 = millis(); !usbSerial && (millis() - t0) < 3000; ) {}
#endif

  analogReadResolution(12);
  Wire.begin();
  notecard.begin();
#ifdef usbSerial
  notecard.setDebugOutputStream(usbSerial);
#endif

  tempSensor.begin();
  tempSensor.setResolution(12);  // 12-bit: 0.0625 °C resolution, 750 ms conversion

  // Rehydrate state from Notecard flash. On first boot, restored == false and
  // the else branch runs full Notecard configuration.
  NotePayloadDesc payload;
  bool restored = NotePayloadRetrieveAfterSleep(&payload);
  if (restored) {
    restored &= NotePayloadGetSegment(&payload, STATE_SEG_ID, &state, sizeof(state));
    NotePayloadFree(&payload);
  }

  if (!restored) {
    memset(&state, 0, sizeof(state));
    state.min_fill_pct = NAN;  // memset zeros floats to 0.0; NAN must be set explicitly
    hubConfigure();
    // Quiet the onboard accelerometer to prevent motion-interrupt wakes from
    // corrupting Mojo power traces during bench validation.
    J *req = notecard.newRequest("card.motion.mode");
    JAddBoolToObject(req, "stop", true);
    if (!notecard.sendRequest(req)) {
#ifdef usbSerial
      usbSerial.println("[card.motion.mode] stop failed");
#endif
    }
  }

  // Define note templates on every wake until both succeed. note.template is
  // idempotent on the Notecard, so repeating it is safe. Without this retry
  // loop a one-time failure at cold-start would leave the device running
  // untemplatized indefinitely across subsequent wakes.
  if (!state.templates_defined) {
    state.templates_defined = defineTemplates();
  }

  // Fetch env overrides on every wake. Re-applies hub.set if report_interval_hr
  // changed (handled inside fetchEnvOverrides).
  fetchEnvOverrides();

  // Seed the summary window anchor the first time we get a valid epoch. Also
  // discard any readings collected before time became valid — without a known
  // epoch anchor those readings have no meaningful window boundary and would make
  // the first summary span potentially much longer than REPORT_INTERVAL_HR.
  uint32_t now = notecardEpoch();
  if (now != 0 && state.summary_window_start_epoch == 0) {
    state.n_fill       = 0;
    state.sum_temp_c   = 0.0f;
    state.n_temp       = 0;
    state.min_fill_pct = NAN;
    state.summary_window_start_epoch = now;
    // Seed epoch cooldowns for any alerts that fired pre-time so they do not
    // double-fire immediately on the same wake the clock syncs. The epoch-based
    // cooldown will keep them quiet for a full ALERT_COOLDOWN_HR.
    if (state.pre_time_low_fill_sent)     state.last_low_fill_epoch     = now;
    if (state.pre_time_sensor_fault_sent) state.last_sensor_fault_epoch = now;
  }
  // runSampleCycle() and state.cycles++ are called from loop() so the sample
  // path runs on every iteration — both after a hardware power-cycle (deep-sleep
  // mode) and after the bench delay() fallback (USB-powered mode).
}

void loop() {
  // Sample sensors and evaluate thresholds, then serialize state to Notecard
  // flash and cut host power for SAMPLE_INTERVAL_MIN × 60 s.
  //
  // Deep-sleep mode (VBAT gated by card.attn): NotePayloadSaveAndSleep cuts
  // host power; setup() runs again on the next ATTN wake for the next sample.
  //
  // Bench/USB mode (no VBAT gating): NotePayloadSaveAndSleep returns without
  // cutting power; delay() waits out the interval; loop() runs again and
  // performs the next sample — identical cadence to deep-sleep mode.
  runSampleCycle();
  state.cycles++;

  NotePayloadDesc payload = {0, 0, 0};
  NotePayloadAddSegment(&payload, STATE_SEG_ID, &state, sizeof(state));
  NotePayloadSaveAndSleep(&payload, (uint32_t)SAMPLE_INTERVAL_MIN * 60UL, NULL);

  delay((uint32_t)SAMPLE_INTERVAL_MIN * 60UL * 1000UL);  // bench fallback: wait, then loop again
}
