// Rooftop HVAC Unit Predictive Maintenance
//
// Host:       Blues Notecarrier CX (onboard STM32 Cygnet host)
// Notecard:   Blues Notecard Cell+WiFi (MBGLW / NBGLW)
// Sensors:    2x NTC 10k thermistors (supply/return duct air)
//             1x SCT-013-030 CT on the compressor hot leg (0-30A / 0-1V AC)
//             1x Sensirion SDP810-125Pa I2C differential pressure sensor
//
// Detects the three canonical RTU failure modes via rule-based thresholds:
//   1. Refrigerant loss    -> narrowing cooling delta-T
//   2. Short-cycling       -> many compressor starts per rolling hour
//   3. Clogged filter      -> rising filter differential pressure
//
// Runtime cadence:
//   - Host wakes every SAMPLE_INTERVAL_SEC via card.attn.
//   - Each wake: sample sensors, evaluate thresholds, queue any alert note.
//   - When SUMMARY_INTERVAL_MIN has elapsed, queue one rtu_summary.qo note.
//   - The Notecard transmits queued notes per the hub.set outbound cadence.

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

static const float    CT_AMPS_PER_VOLT = 30.0f;    // SCT-013-030: 1V RMS = 30A RMS
static const uint16_t CT_SAMPLES       = 1480;     // ~20 cycles @ 60Hz

static const uint8_t  SDP810_ADDR          = 0x25;
static const uint8_t  SDP810_CMD_START[2]  = { 0x36, 0x1E };
// Sensirion SDP810-125Pa: scale = 240 counts/Pa (per datasheet §3).
// (Note: the 500Pa variant uses 60 counts/Pa — don't mix them up.)
static const float    SDP810_SCALE_FACTOR  = 240.0f;

// Sentinel value emitted in the summary template when no valid samples
// were collected for a metric in the summary window. Chosen well outside
// any plausible physical range so downstream analytics can distinguish
// "sensor failed" from a true near-zero reading.
static const float    SUMMARY_INVALID_SENTINEL = -9999.0f;

// -------- Short-cycle sliding window --------
// Store the epoch of each compressor rising edge; count how many fall
// within the last 3600s on each evaluation. 16 slots covers >16 starts/hr
// comfortably — well above any plausible short-cycling threshold.
static const uint8_t  START_RING_SIZE = 16;

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

  // Per-metric running sums and *valid* sample counts for the current
  // summary window. Tracking counts per metric means a single bad read
  // on one sensor doesn't bias the averages of the others downward.
  float     sum_supply;   uint32_t n_supply;
  float     sum_return;   uint32_t n_return;
  float     sum_delta;    uint32_t n_delta;
  float     sum_amps;     uint32_t n_amps;
  float     sum_dp;       uint32_t n_dp;
  uint32_t  summary_window_start_epoch;
  uint32_t  summary_runtime_sec;

  // Short-cycle sliding window: ring buffer of recent start epochs.
  uint32_t  start_times[START_RING_SIZE];
  uint8_t   start_head; // index of next slot to write
  uint8_t   start_count; // capped at START_RING_SIZE

  uint32_t  last_alert_delta_t_epoch;
  uint32_t  last_alert_short_cycle_epoch;
  uint32_t  last_alert_filter_epoch;

  bool      compressor_was_on;

  // Value of SUMMARY_INTERVAL_MIN last sent to the Notecard via hub.set.
  // When env vars change the in-memory value, we re-apply hub.set so the
  // Notecard's outbound cadence tracks the local summary cadence.
  uint32_t  last_applied_outbound_min;
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
  state.last_applied_outbound_min = SUMMARY_INTERVAL_MIN;
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

  // Re-apply hub.set if the outbound cadence changed. Without this, env-var
  // changes to summary_interval_min update local summary timing but leave the
  // Notecard transmitting on its old schedule.
  if (SUMMARY_INTERVAL_MIN != state.last_applied_outbound_min) {
    hubConfigure();
  }
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
  Wire.read(); // CRC byte, unchecked here (see README limitations)
  return (float)raw / SDP810_SCALE_FACTOR;
}

// ---------- Short-cycle sliding window ----------

static void recordCompressorStart(uint32_t now_epoch) {
  state.start_times[state.start_head] = now_epoch;
  state.start_head = (state.start_head + 1) % START_RING_SIZE;
  if (state.start_count < START_RING_SIZE) state.start_count++;
}

static uint8_t startsInLastHour(uint32_t now_epoch) {
  uint8_t count = 0;
  for (uint8_t i = 0; i < state.start_count; i++) {
    uint32_t t = state.start_times[i];
    if (t != 0 && (now_epoch - t) <= 3600) count++;
  }
  return count;
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

static float safeAvg(float sum, uint32_t n) {
  return n > 0 ? (sum / (float)n) : SUMMARY_INVALID_SENTINEL;
}

static void sendSummary(uint32_t now_epoch) {
  // Emit a summary only if *some* data was collected this window.
  bool any_valid = state.n_supply || state.n_return || state.n_delta
                || state.n_amps   || state.n_dp;
  if (!any_valid) return;

  float runtime_min = state.summary_runtime_sec / 60.0f;

  J *req = notecard.newRequest("note.add");
  JAddStringToObject(req, "file", "rtu_summary.qo");
  J *body = JAddObjectToObject(req, "body");
  JAddNumberToObject(body, "supply_f",        safeAvg(state.sum_supply, state.n_supply));
  JAddNumberToObject(body, "return_f",        safeAvg(state.sum_return, state.n_return));
  JAddNumberToObject(body, "delta_t_f",       safeAvg(state.sum_delta,  state.n_delta));
  JAddNumberToObject(body, "compressor_amps", safeAvg(state.sum_amps,   state.n_amps));
  JAddNumberToObject(body, "filter_dp_pa",    safeAvg(state.sum_dp,     state.n_dp));
  JAddNumberToObject(body, "starts",          (int)startsInLastHour(now_epoch));
  JAddNumberToObject(body, "runtime_min",     runtime_min);
  notecard.sendRequest(req);

  // Reset running sums/counters for the next summary window. The short-cycle
  // ring buffer is *not* reset — it is a rolling hour regardless of cadence.
  state.sum_supply = state.sum_return = state.sum_delta = 0;
  state.sum_amps   = state.sum_dp     = 0;
  state.n_supply = state.n_return = state.n_delta = 0;
  state.n_amps   = state.n_dp     = 0;
  state.summary_runtime_sec = 0;
  state.summary_window_start_epoch = now_epoch;
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

  // Per-metric accumulation. Each count increments only when the read is valid.
  if (!isnan(supply))  { state.sum_supply += supply;  state.n_supply++; }
  if (!isnan(ret))     { state.sum_return += ret;     state.n_return++; }
  if (!isnan(delta_t)) { state.sum_delta  += delta_t; state.n_delta++;  }
  if (!isnan(amps))    { state.sum_amps   += amps;    state.n_amps++;   }
  if (!isnan(dp_pa))   { state.sum_dp     += dp_pa;   state.n_dp++;     }

  if (compressor_on) state.summary_runtime_sec += SAMPLE_INTERVAL_SEC;

  // Rising-edge detection: timestamp this start into the sliding window.
  // Note: transitions occurring entirely between SAMPLE_INTERVAL_SEC sample
  // points won't be detected — this is a sampled approximation, not a true
  // event-driven counter. See README §9 for the implications.
  if (compressor_on && !state.compressor_was_on) {
    recordCompressorStart(now_epoch);
  }
  state.compressor_was_on = compressor_on;

  uint8_t starts_last_hour = startsInLastHour(now_epoch);

  // ---- Threshold evaluations ----

  if (compressor_on && !isnan(delta_t) && delta_t < DELTA_T_MIN_F &&
      (now_epoch - state.last_alert_delta_t_epoch) > ALERT_COOLDOWN_SEC) {
    sendAlert("delta_t_low",
              "delta_t_f", delta_t,
              "supply_f",  supply,
              "return_f",  ret);
    state.last_alert_delta_t_epoch = now_epoch;
  }

  if (starts_last_hour > SHORT_CYCLE_STARTS_HR &&
      (now_epoch - state.last_alert_short_cycle_epoch) > ALERT_COOLDOWN_SEC) {
    sendAlert("short_cycling",
              "starts_per_hour", (float)starts_last_hour,
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
    sendSummary(now_epoch);
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
    // Quiet the accelerometer so Mojo traces aren't polluted by ISR wakes
    // during bench power-consumption validation (see README §8).
    J *req = notecard.newRequest("card.motion.mode");
    JAddBoolToObject(req, "stop", true);
    notecard.sendRequest(req);
  }

  startSdp810Continuous();

  // Re-read env on every wake; may re-apply hub.set if summary_interval_min
  // changed, keeping the Notecard's outbound cadence in sync with local summary
  // cadence.
  fetchEnvOverrides();

  uint32_t now = notecardEpoch();
  if (state.summary_window_start_epoch == 0) state.summary_window_start_epoch = now;

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
