// Rooftop HVAC Unit Predictive Maintenance
//
// Host:       Blues Notecarrier CX (onboard STM32 Cygnet host)
// Notecard:   Blues Notecard Cell+WiFi (MBGLW / NBGLW)
// Sensors:    2x NTC 10k thermistors (supply/return duct air)
//             1x SCT-013-030 CT on the compressor hot leg (0-30A / 0-1V AC)
//             1x Sensirion SDP810-125Pa I2C differential pressure sensor
//
// Detects the three canonical RTU failure modes:
//   1. Refrigerant loss    -> narrowing cooling delta-T
//   2. Short-cycling       -> many compressor starts without sustained draw
//   3. Clogged filter      -> rising filter differential pressure
//
// Runtime cadence:
//   - Host wakes every SAMPLE_INTERVAL_SEC via card.attn.
//   - Each wake: sample sensors, evaluate thresholds, queue any alert note.
//   - When SUMMARY_INTERVAL_MIN has elapsed, queue one rtu_summary.qo note.
//   - The Notecard transmits queued notes to cellular per hub.set outbound.

#include <Notecard.h>
#include <Wire.h>

#ifndef PRODUCT_UID
#define PRODUCT_UID "" // "com.my-company.my-name:rtu-pdm"
#pragma message "PRODUCT_UID not set. Claim one in Notehub, then define it here."
#endif

#define usbSerial Serial

// -------- Pin assignments (Notecarrier CX headers) --------
static const uint8_t PIN_THERM_SUPPLY = A0;
static const uint8_t PIN_THERM_RETURN = A1;
static const uint8_t PIN_CT_IN        = A2;

// -------- Sensor constants --------
static const float    ADC_VREF_V       = 3.30f;
static const uint16_t ADC_COUNTS       = 4095;     // STM32L4 12-bit ADC
static const float    NTC_BETA         = 3950.0f;
static const float    NTC_T0_K         = 298.15f;
static const float    NTC_R0_OHM       = 10000.0f;
static const float    NTC_SERIES_OHM   = 10000.0f;

static const float    CT_AMPS_PER_VOLT = 30.0f;    // SCT-013-030: 1V = 30A
static const uint16_t CT_SAMPLES       = 1480;     // ~20 cycles @ 60Hz

static const uint8_t  SDP810_ADDR          = 0x25;
static const uint8_t  SDP810_CMD_START[2]  = { 0x36, 0x1E };
static const float    SDP810_SCALE_FACTOR  = 60.0f; // 125Pa variant: Pa = raw/60

// -------- Default thresholds / intervals (overridable via env) --------
static float    DELTA_T_MIN_F         = 12.0f;
static uint8_t  SHORT_CYCLE_STARTS_HR = 8;
static float    FILTER_DP_ALERT_PA    = 90.0f;
static float    COMPRESSOR_ON_AMPS    = 3.0f;
static uint32_t SAMPLE_INTERVAL_SEC   = 60;
static uint32_t SUMMARY_INTERVAL_MIN  = 60;
static uint32_t ALERT_COOLDOWN_SEC    = 1800;

// -------- State preserved across sleeps --------
struct PersistState {
  uint32_t  cycles;
  uint32_t  compressor_starts_this_hour;
  uint32_t  start_window_epoch;
  uint32_t  last_alert_delta_t_epoch;
  uint32_t  last_alert_short_cycle_epoch;
  uint32_t  last_alert_filter_epoch;
  uint32_t  summary_window_start_epoch;
  float     summary_amps_sum;
  float     summary_delta_sum;
  float     summary_dp_sum;
  uint32_t  summary_samples;
  uint32_t  summary_runtime_sec;
  bool      compressor_was_on;
};
static const char STATE_SEG_ID[] = "RTUS";
static PersistState state;

Notecard notecard;

// ---------- Notecard helpers ----------

static void hubConfigure() {
  J *req = notecard.newRequest("hub.set");
  if (PRODUCT_UID[0]) JAddStringToObject(req, "product", PRODUCT_UID);
  JAddStringToObject(req, "mode", "periodic");
  JAddNumberToObject(req, "outbound", SUMMARY_INTERVAL_MIN);
  JAddNumberToObject(req, "inbound", 360);
  notecard.sendRequestWithRetry(req, 10);
}

static void defineTemplates() {
  J *req = notecard.newRequest("note.template");
  JAddStringToObject(req, "file", "rtu_summary.qo");
  JAddNumberToObject(req, "port", 50);
  J *body = JAddObjectToObject(req, "body");
  JAddNumberToObject(body, "supply_f",        14.1);
  JAddNumberToObject(body, "return_f",        14.1);
  JAddNumberToObject(body, "delta_t_f",       14.1);
  JAddNumberToObject(body, "compressor_amps", 14.1);
  JAddNumberToObject(body, "filter_dp_pa",    14.1);
  JAddNumberToObject(body, "starts",          12);
  JAddNumberToObject(body, "runtime_min",     14.1);
  notecard.sendRequest(req);
}

static uint32_t notecardEpoch() {
  uint32_t epoch = 0;
  J *rsp = notecard.requestAndResponse(notecard.newRequest("card.time"));
  if (rsp) {
    epoch = (uint32_t)JGetNumber(rsp, "time");
    notecard.deleteResponse(rsp);
  }
  return epoch;
}

static void fetchEnvOverrides() {
  J *req = notecard.newRequest("env.get");
  J *names = JAddArrayToObject(req, "names");
  JAddItemToArray(names, JCreateString("delta_t_min_f"));
  JAddItemToArray(names, JCreateString("short_cycle_starts_per_hour"));
  JAddItemToArray(names, JCreateString("filter_dp_alert_pa"));
  JAddItemToArray(names, JCreateString("compressor_on_amps"));
  JAddItemToArray(names, JCreateString("sample_interval_sec"));
  JAddItemToArray(names, JCreateString("summary_interval_min"));

  J *rsp = notecard.requestAndResponse(req);
  if (!rsp) return;

  J *body = JGetObjectItem(rsp, "body");
  if (body) {
    const char *v;
    v = JGetString(body, "delta_t_min_f");
    if (v[0]) DELTA_T_MIN_F = atof(v);
    v = JGetString(body, "short_cycle_starts_per_hour");
    if (v[0]) SHORT_CYCLE_STARTS_HR = (uint8_t)atoi(v);
    v = JGetString(body, "filter_dp_alert_pa");
    if (v[0]) FILTER_DP_ALERT_PA = atof(v);
    v = JGetString(body, "compressor_on_amps");
    if (v[0]) COMPRESSOR_ON_AMPS = atof(v);
    v = JGetString(body, "sample_interval_sec");
    if (v[0]) SAMPLE_INTERVAL_SEC = (uint32_t)atol(v);
    v = JGetString(body, "summary_interval_min");
    if (v[0]) SUMMARY_INTERVAL_MIN = (uint32_t)atol(v);
  }
  notecard.deleteResponse(rsp);
}

// ---------- Sensor reading ----------

static float readThermistorF(uint8_t pin) {
  uint32_t acc = 0;
  for (int i = 0; i < 16; i++) acc += analogRead(pin);
  float counts = acc / 16.0f;
  float v = (counts / (float)ADC_COUNTS) * ADC_VREF_V;
  if (v <= 0.001f || v >= (ADC_VREF_V - 0.001f)) return NAN;
  // Thermistor is the low leg of the divider:  Vout = Vref * Rt / (Rseries + Rt)
  float r_therm = NTC_SERIES_OHM * v / (ADC_VREF_V - v);
  float invT = 1.0f / NTC_T0_K + (1.0f / NTC_BETA) * logf(r_therm / NTC_R0_OHM);
  float t_c = (1.0f / invT) - 273.15f;
  return t_c * 9.0f / 5.0f + 32.0f;
}

static float readCompressorAmps() {
  // Estimate DC offset from the running mean of this sample window, then
  // compute RMS of the AC component. The bias network nominally centers the
  // signal at Vref/2, but deriving the mean each read tolerates divider drift.
  uint32_t mean_acc = 0;
  for (uint16_t i = 0; i < 256; i++) mean_acc += analogRead(PIN_CT_IN);
  int32_t mean = mean_acc / 256;

  uint64_t sum_sq = 0;
  for (uint16_t i = 0; i < CT_SAMPLES; i++) {
    int32_t s = (int32_t)analogRead(PIN_CT_IN) - mean;
    sum_sq += (uint64_t)(s * s);
  }
  float rms_counts = sqrtf((float)sum_sq / (float)CT_SAMPLES);
  float rms_v = rms_counts * ADC_VREF_V / (float)ADC_COUNTS;
  return rms_v * CT_AMPS_PER_VOLT;
}

static void startSdp810Continuous() {
  Wire.beginTransmission(SDP810_ADDR);
  Wire.write(SDP810_CMD_START[0]);
  Wire.write(SDP810_CMD_START[1]);
  Wire.endTransmission();
  delay(25); // first-conversion latency per datasheet
}

static float readFilterDpPa() {
  Wire.requestFrom((int)SDP810_ADDR, 3);
  if (Wire.available() < 3) return NAN;
  int16_t raw = (int16_t)((Wire.read() << 8) | Wire.read());
  Wire.read(); // CRC byte, unchecked here
  return (float)raw / SDP810_SCALE_FACTOR;
}

// ---------- Alerts & summaries ----------

static void sendAlert(const char *kind,
                      const char *a_key, float a,
                      const char *b_key, float b,
                      const char *c_key, float c) {
  J *req = notecard.newRequest("note.add");
  JAddStringToObject(req, "file", "rtu_alert.qo");
  JAddBoolToObject(req, "sync", true);  // alerts bypass the outbound timer
  J *body = JAddObjectToObject(req, "body");
  JAddStringToObject(body, "alert", kind);
  if (a_key) JAddNumberToObject(body, a_key, a);
  if (b_key) JAddNumberToObject(body, b_key, b);
  if (c_key) JAddNumberToObject(body, c_key, c);
  notecard.sendRequest(req);
}

static void sendSummary(uint32_t now_epoch, float supply, float ret) {
  if (state.summary_samples == 0) return;
  float avg_amps  = state.summary_amps_sum  / state.summary_samples;
  float avg_delta = state.summary_delta_sum / state.summary_samples;
  float avg_dp    = state.summary_dp_sum    / state.summary_samples;
  float runtime_min = state.summary_runtime_sec / 60.0f;

  J *req = notecard.newRequest("note.add");
  JAddStringToObject(req, "file", "rtu_summary.qo");
  J *body = JAddObjectToObject(req, "body");
  JAddNumberToObject(body, "supply_f",        isnan(supply) ? 0.0 : supply);
  JAddNumberToObject(body, "return_f",        isnan(ret)    ? 0.0 : ret);
  JAddNumberToObject(body, "delta_t_f",       avg_delta);
  JAddNumberToObject(body, "compressor_amps", avg_amps);
  JAddNumberToObject(body, "filter_dp_pa",    avg_dp);
  JAddNumberToObject(body, "starts",          (int)state.compressor_starts_this_hour);
  JAddNumberToObject(body, "runtime_min",     runtime_min);
  notecard.sendRequest(req);

  state.summary_window_start_epoch = now_epoch;
  state.summary_amps_sum    = 0;
  state.summary_delta_sum   = 0;
  state.summary_dp_sum      = 0;
  state.summary_samples     = 0;
  state.summary_runtime_sec = 0;
  state.compressor_starts_this_hour = 0;
  state.start_window_epoch  = now_epoch;
}

// ---------- One sample cycle ----------

static void runSampleCycle() {
  uint32_t now_epoch = notecardEpoch();

  float supply = readThermistorF(PIN_THERM_SUPPLY);
  float ret    = readThermistorF(PIN_THERM_RETURN);
  float amps   = readCompressorAmps();
  float dp_pa  = readFilterDpPa();

  bool  compressor_on = amps > COMPRESSOR_ON_AMPS;
  float delta_t = (isnan(supply) || isnan(ret)) ? NAN : (ret - supply);

#ifdef usbSerial
  usbSerial.print("[sample] supply_f="); usbSerial.print(supply);
  usbSerial.print(" return_f=");          usbSerial.print(ret);
  usbSerial.print(" delta_t_f=");         usbSerial.print(delta_t);
  usbSerial.print(" amps=");              usbSerial.print(amps);
  usbSerial.print(" dp_pa=");             usbSerial.print(dp_pa);
  usbSerial.print(" on=");                usbSerial.println(compressor_on);
#endif

  if (!isnan(delta_t)) state.summary_delta_sum += delta_t;
  state.summary_amps_sum += amps;
  if (!isnan(dp_pa))  state.summary_dp_sum    += dp_pa;
  state.summary_samples++;
  if (compressor_on) state.summary_runtime_sec += SAMPLE_INTERVAL_SEC;

  if (compressor_on && !state.compressor_was_on) {
    state.compressor_starts_this_hour++;
  }
  state.compressor_was_on = compressor_on;

  // Roll the rolling-hour window if it has aged past an hour.
  if (now_epoch - state.start_window_epoch >= 3600) {
    state.start_window_epoch = now_epoch;
    state.compressor_starts_this_hour = 0;
  }

  // Delta-T is only diagnostic while the compressor is running.
  if (compressor_on && !isnan(delta_t) && delta_t < DELTA_T_MIN_F &&
      (now_epoch - state.last_alert_delta_t_epoch) > ALERT_COOLDOWN_SEC) {
    sendAlert("delta_t_low",
              "delta_t_f", delta_t,
              "supply_f",  supply,
              "return_f",  ret);
    state.last_alert_delta_t_epoch = now_epoch;
  }

  if (state.compressor_starts_this_hour > SHORT_CYCLE_STARTS_HR &&
      (now_epoch - state.last_alert_short_cycle_epoch) > ALERT_COOLDOWN_SEC) {
    sendAlert("short_cycling",
              "starts_per_hour", (float)state.compressor_starts_this_hour,
              "compressor_amps", amps,
              NULL, 0);
    state.last_alert_short_cycle_epoch = now_epoch;
  }

  if (!isnan(dp_pa) && dp_pa > FILTER_DP_ALERT_PA &&
      (now_epoch - state.last_alert_filter_epoch) > ALERT_COOLDOWN_SEC) {
    sendAlert("filter_dp_high",
              "filter_dp_pa", dp_pa,
              NULL, 0, NULL, 0);
    state.last_alert_filter_epoch = now_epoch;
  }

  if (now_epoch - state.summary_window_start_epoch >= SUMMARY_INTERVAL_MIN * 60) {
    sendSummary(now_epoch, supply, ret);
  }
}

// ---------- Setup / loop ----------
//
// This sketch uses the "host-is-off-when-idle" pattern. The Notecard powers
// the host back on every SAMPLE_INTERVAL_SEC via ATTN (see card.attn in loop()),
// so setup() runs on every wake, does one sample cycle, and loop() issues the
// next sleep command. State survives the power cut via NotePayloadSaveAndSleep.

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

  NotePayloadDesc payload;
  bool restored = NotePayloadRetrieveAfterSleep(&payload);
  if (restored) {
    restored &= NotePayloadGetSegment(&payload, STATE_SEG_ID, &state, sizeof(state));
    NotePayloadFree(&payload);
  }
  if (!restored) {
    memset(&state, 0, sizeof(state));
    hubConfigure();
    defineTemplates();
    // Quiet the accelerometer so Mojo traces aren't polluted by ISR wakes.
    J *req = notecard.newRequest("card.motion.mode");
    JAddBoolToObject(req, "stop", true);
    notecard.sendRequest(req);
  }

  startSdp810Continuous();

  // Re-read env on every wake so operators can retune thresholds without reflash.
  fetchEnvOverrides();

  uint32_t now = notecardEpoch();
  if (state.summary_window_start_epoch == 0) state.summary_window_start_epoch = now;
  if (state.start_window_epoch == 0)         state.start_window_epoch = now;

  runSampleCycle();
  state.cycles++;
}

void loop() {
  NotePayloadDesc payload = {0, 0, 0};
  NotePayloadAddSegment(&payload, STATE_SEG_ID, &state, sizeof(state));
  NotePayloadSaveAndSleep(&payload, SAMPLE_INTERVAL_SEC, NULL);

  // Reached only if the Notecarrier isn't gating host power via ATTN.
  delay(SAMPLE_INTERVAL_SEC * 1000UL);
}
