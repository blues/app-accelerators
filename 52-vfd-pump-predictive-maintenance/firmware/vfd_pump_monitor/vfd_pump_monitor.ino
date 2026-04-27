// vfd_pump_monitor.ino
//
// VFD-Driven Pump Predictive Maintenance via Modbus
// Target hardware: Arduino OPTA RS485 (or OPTA WiFi) + Blues Wireless for OPTA
// Notecard:        Cell+WiFi (NOTE-WBNAW), I2C via the expansion's AUX connector
//
// Polls six Modbus holding registers from a VFD on a configurable cadence,
// accumulates rolling hourly statistics, evaluates four anomaly-detection rules
// locally, and emits summary + alert Notes through the Notecard.
//
// Dependencies:
//   - Blues Wireless Notecard (note-arduino)  - install via Library Manager
//   - ArduinoModbus, ArduinoRS485             - install via Library Manager
//   - Arduino Mbed OS Opta Boards (core)      - install via Boards Manager

#include <Arduino.h>
#include <strings.h>   // strcasecmp — portability across cores
#include <Notecard.h>
#include <ArduinoRS485.h>
#include <ArduinoModbus.h>

// -------- Project configuration -----------------------------------------------------
// Replace with the ProductUID from your Notehub project. See:
//   https://dev.blues.io/notehub/notehub-walkthrough/#finding-a-productuid
#define PRODUCT_UID  "com.your-company.your-name:vfd_pump_monitor"

#define usbSerial    Serial
#define DEBUG_BAUD   115200

// Default firmware values; every one of these can be overridden by an environment
// variable on Notehub. Defaults are illustrative for a fictional contiguous map.
// Real VFDs differ in register addresses, scaling, signedness, and word counts —
// see the README's "VFD register-map gotchas" callout before deploying.
static uint32_t g_sample_minutes              = 1;
static uint32_t g_report_minutes              = 60;
static uint8_t  g_modbus_slave_id             = 1;
static uint32_t g_modbus_baud                 = 19200;
static char     g_modbus_parity[8]            = "none";   // "none" | "even" | "odd"
static uint8_t  g_modbus_stop_bits            = 1;        // 1 or 2
static uint16_t g_reg_freq                    = 259;   // 0.01 Hz units (assumed)
static uint16_t g_reg_current                 = 260;   // 0.01 A units (assumed)
static uint16_t g_reg_torque                  = 261;   // % of nominal (assumed)
static uint16_t g_reg_drive_temp              = 262;   // deg C (assumed)
static uint16_t g_reg_runtime_hours           = 263;   // single 16-bit register (assumed)
static uint16_t g_reg_fault_code              = 264;   // active fault, 0 = no fault
static char     g_vfd_profile[32]             = "demo_contiguous";
static float    g_current_alarm_factor        = 1.20f;
static uint8_t  g_transient_fault_window_hrs  = 4;
static uint8_t  g_transient_fault_count       = 3;
static float    g_drive_temp_alarm_c          = 75.0f;
static float    g_expected_run_hours_per_day  = 12.0f;

// -------- State ---------------------------------------------------------------------
Notecard notecard;

struct VfdSample {
    bool     valid;
    float    frequency_hz;
    float    current_a;
    int16_t  torque_pct;
    int16_t  drive_temp_c;
    uint32_t runtime_hours;       // uint32_t so 16-bit reads can be widened
                                  // cleanly when a vendor-specific build adds
                                  // proper 32-bit (two-register) reads
    uint16_t fault_code;
};

// Rolling stats accumulated between summary emissions. Run-time samples and
// stopped samples are tracked separately because a hot drive at 0 Hz tells a
// different story than a hot drive under load.
//
// `last_fault_seen_this_hour` is the per-hour summary field — the last non-zero
// fault code seen in the just-completed hour. It IS reset by stats.reset(). The
// persistent fault-state tracker (g_current_fault_code, below) is separate and
// is NOT reset, so a fault that's been active for hours doesn't get re-counted
// as a new event when the hour rolls over.
struct RollingStats {
    uint32_t run_samples;
    uint32_t stop_samples;
    float    f_hz_sum;       float f_hz_peak;
    float    i_a_sum;        float i_a_peak;
    int32_t  trq_sum;        int16_t trq_peak;
    int32_t  drv_c_sum;      int16_t drv_c_peak;
    uint32_t hrs_total_last;
    uint8_t  fault_count_hour;
    uint16_t last_fault_seen_this_hour;
    void reset() { memset(this, 0, sizeof(*this)); }
};

static RollingStats stats;

// Persistent fault-state tracker. Survives stats.reset() at the hour boundary.
// Without this, a fault that stays asserted across an hour boundary would be
// re-detected as a 0 -> nonzero transition on the first sample of the next
// hour and double-counted.
static uint16_t g_current_fault_code = 0;

// Frequency-binned current baselines. The README's load-anomaly rule is
// "rising current at *comparable* output frequency" — but current at 30 Hz and
// current at 60 Hz aren't comparable on the same pump. Each 5 Hz bin keeps its
// own EWMA-tracked baseline. Bin index = clamp(floor(hz/5), 0, FREQ_BINS-1).
#define FREQ_BIN_HZ   5
#define FREQ_BINS    14   // covers 0..70 Hz; >70 Hz lands in the top bin
static float g_current_baseline_by_bin[FREQ_BINS];
static bool  g_baseline_seeded[FREQ_BINS];

// Transient-fault clustering: store recent fault timestamps in a small ring
// buffer and count distinct entries within the configured window.
#define FAULT_RING_SIZE 16
static uint32_t fault_history_ms[FAULT_RING_SIZE];
static uint16_t fault_history_code[FAULT_RING_SIZE];
static uint8_t  fault_history_idx = 0;

// Daily runtime tracking. Initialized from the first valid sample (NOT from 0)
// so the first 24h check doesn't compare current lifetime hours against zero.
static uint32_t day_start_runtime_hours = 0;
static uint32_t day_start_ms            = 0;
static bool     g_runtime_baseline_initialized = false;

// Edge-trigger state for each rule. Each alert fires once on the rising edge
// (condition newly true) and rearms when the condition clears, instead of
// re-firing every summary interval while the condition persists.
static bool g_active_load_anomaly    = false;
static bool g_active_transient_flt   = false;
static bool g_active_drive_overtemp  = false;

// Track the last serial config we passed to ModbusRTUClient.begin() so we can
// detect env-var changes and re-initialize the bus.
static uint32_t g_last_modbus_baud      = 0;
static uint16_t g_last_serial_cfg       = 0;

// Track the last hub.set we issued so we re-issue when report_minutes changes.
static uint32_t g_last_hubset_outbound  = 0;

// Emission scheduler
static uint32_t last_sample_ms          = 0;
static uint32_t last_summary_ms         = 0;
static uint32_t last_modbus_alert_ms    = 0;
static bool     g_modbus_alert_sent     = false;  // track first-ever fire so the
                                                  // initial alert isn't delayed
                                                  // by an hour of uptime

// -------- Forward declarations ------------------------------------------------------
static void notecardConfigure();
static void defineTemplates();
static void fetchEnvOverrides();
static bool pollVfd(VfdSample &out);
static void accumulate(const VfdSample &s);
static void evaluateRules();
static void sendSummary();
static void sendEvent(const char *alert, const VfdSample *trigger,
                      float v1 = NAN, float v2 = NAN, float v3 = NAN);
static void recordFault(uint16_t code);
static uint8_t countFaultsInWindow(uint8_t window_hours);
static uint16_t serialConfigFromEnv();
static void applyModbusSerialIfChanged();
static void applyHubSetIfChanged();
static uint8_t freqBin(float hz);

// -------- Arduino entry points ------------------------------------------------------
void setup() {
    usbSerial.begin(DEBUG_BAUD);
    const uint32_t serial_wait_ms = 3000;
    for (uint32_t t0 = millis(); !usbSerial && millis() - t0 < serial_wait_ms; ) {}

    notecard.begin();
    notecard.setDebugOutputStream(usbSerial);

    // Order matters: define templates first (needed regardless), then fetch env
    // overrides BEFORE the initial hub.set so the first sync uses any operator-
    // tuned cadence, then run hub.set, then bring up the Modbus bus with the
    // operator-tuned serial config.
    defineTemplates();
    fetchEnvOverrides();
    notecardConfigure();

    // The OPTA acts as the Modbus client (master); the VFD is the server (slave).
    // applyModbusSerialIfChanged() does the actual ModbusRTUClient.begin() and
    // remembers the config so a later env-var change can re-init cleanly.
    applyModbusSerialIfChanged();

    stats.reset();
    day_start_ms = millis();
    last_summary_ms = millis();
}

void loop() {
    const uint32_t now = millis();

    // Refresh environment variables on each summary boundary so operators can
    // retune thresholds without rebooting. CRITICAL: evaluate rules BEFORE
    // sendSummary(), because sendSummary() resets the rolling stats buffer
    // that the rules read from.
    if (now - last_summary_ms >= (uint32_t)g_report_minutes * 60UL * 1000UL) {
        fetchEnvOverrides();
        applyModbusSerialIfChanged();   // re-init bus if baud/parity changed
        applyHubSetIfChanged();         // re-issue hub.set if cadence changed
        evaluateRules();                // must run BEFORE the reset in sendSummary()
        sendSummary();                  // resets stats at the end
        last_summary_ms = now;
    }

    if (now - last_sample_ms >= (uint32_t)g_sample_minutes * 60UL * 1000UL) {
        VfdSample s = {};
        bool ok = pollVfd(s);
        if (ok) {
            // Initialize the daily-runtime baseline from the FIRST valid sample,
            // not from zero. Without this, the first 24h window compares the
            // VFD's lifetime runtime hours against zero and falsely fires
            // runtime_drift on any drive with existing hours.
            if (!g_runtime_baseline_initialized) {
                day_start_runtime_hours = s.runtime_hours;
                day_start_ms = now;
                g_runtime_baseline_initialized = true;
            }

            accumulate(s);

            // Count fault EVENTS (transitions), not samples. Persistent state
            // lives in g_current_fault_code (NOT cleared at hour boundary), so
            // a fault asserted across hours doesn't double-count.
            if (s.fault_code != g_current_fault_code) {
                if (s.fault_code != 0) {
                    recordFault(s.fault_code);
                    stats.fault_count_hour++;
                    stats.last_fault_seen_this_hour = s.fault_code;
                }
                g_current_fault_code = s.fault_code;
            }
        } else {
            // Fire the FIRST modbus_unreachable immediately (don't wait for
            // last_modbus_alert_ms's initial zero to age out an hour of uptime
            // — that would silently swallow first-light commissioning failures).
            // After that, rate-limit to once per hour so a drive that's off for
            // service doesn't generate dozens of identical alerts.
            if (!g_modbus_alert_sent ||
                now - last_modbus_alert_ms >= 60UL * 60UL * 1000UL) {
                sendEvent("modbus_unreachable", nullptr);
                last_modbus_alert_ms = now;
                g_modbus_alert_sent = true;
            }
        }
        last_sample_ms = now;
    }
}

// -------- Notecard configuration ----------------------------------------------------
static void notecardConfigure() {
    // hub.set establishes the device's relationship with Notehub. Periodic mode
    // batches outbound traffic into hourly sessions; alerts override with sync:true.
    J *req = notecard.newRequest("hub.set");
    if (req) {
        JAddStringToObject(req, "product", PRODUCT_UID);
        JAddStringToObject(req, "mode", "periodic");
        JAddNumberToObject(req, "outbound", g_report_minutes);
        JAddNumberToObject(req, "inbound", g_report_minutes * 2);
        // sendRequestWithRetry on first contact dodges the cold-boot I2C race.
        notecard.sendRequestWithRetry(req, 5);
        g_last_hubset_outbound = g_report_minutes;
    }
}

static void defineTemplates() {
    // Templates compress Notes from free-form JSON into fixed-length records.
    // For a device that ships >8000 summary Notes per year over a prepaid SIM,
    // the 3-5x bandwidth savings is the difference between viable and not.
    {
        J *req = notecard.newRequest("note.template");
        if (req) {
            JAddStringToObject(req, "file", "vfd_summary.qo");
            JAddNumberToObject(req, "port", 50);
            J *body = JAddObjectToObject(req, "body");
            JAddNumberToObject(body, "f_hz_mean", 14.1);
            JAddNumberToObject(body, "f_hz_peak", 14.1);
            JAddNumberToObject(body, "i_a_mean",  14.1);
            JAddNumberToObject(body, "i_a_peak",  14.1);
            JAddNumberToObject(body, "trq_mean",  12);
            JAddNumberToObject(body, "trq_peak",  12);
            JAddNumberToObject(body, "drv_c_mean", 12);
            JAddNumberToObject(body, "drv_c_peak", 12);
            JAddNumberToObject(body, "run_min",    12);
            JAddNumberToObject(body, "stop_min",   12);
            JAddNumberToObject(body, "hrs_total",  24);
            JAddNumberToObject(body, "fault_count_hour", 11);
            JAddStringToObject(body, "last_fault", "8");
            notecard.sendRequest(req);
        }
    }
    {
        J *req = notecard.newRequest("note.template");
        if (req) {
            JAddStringToObject(req, "file", "vfd_event.qo");
            JAddNumberToObject(req, "port", 51);
            J *body = JAddObjectToObject(req, "body");
            JAddStringToObject(body, "alert", "24");
            JAddNumberToObject(body, "f_hz",  14.1);
            JAddNumberToObject(body, "i_a",   14.1);
            JAddNumberToObject(body, "v1",    14.1);
            JAddNumberToObject(body, "v2",    14.1);
            JAddNumberToObject(body, "v3",    14.1);
            JAddNumberToObject(body, "fault_code", 22);
            JAddNumberToObject(body, "hrs_total",  24);
            notecard.sendRequest(req);
        }
    }
}

// -------- Environment-variable overrides --------------------------------------------
static long envLong(J *rsp, const char *name, long fallback) {
    J *body = JGetObject(rsp, "body");
    if (!body) return fallback;
    const char *s = JGetString(body, name);
    if (!s || !*s) return fallback;
    return strtol(s, nullptr, 10);
}

static double envFloat(J *rsp, const char *name, double fallback) {
    J *body = JGetObject(rsp, "body");
    if (!body) return fallback;
    const char *s = JGetString(body, name);
    if (!s || !*s) return fallback;
    return strtod(s, nullptr);
}

static void envCopyString(J *rsp, const char *name, char *dst, size_t dst_len) {
    J *body = JGetObject(rsp, "body");
    if (!body) return;
    const char *s = JGetString(body, name);
    if (!s || !*s) return;
    strncpy(dst, s, dst_len - 1);
    dst[dst_len - 1] = '\0';
}

// Translate the (g_modbus_parity, g_modbus_stop_bits) env settings into the
// SERIAL_* config constant ArduinoModbus expects.
static uint16_t serialConfigFromEnv() {
    bool two_stop = (g_modbus_stop_bits == 2);
    if (strcasecmp(g_modbus_parity, "even") == 0) {
        return two_stop ? SERIAL_8E2 : SERIAL_8E1;
    }
    if (strcasecmp(g_modbus_parity, "odd") == 0) {
        return two_stop ? SERIAL_8O2 : SERIAL_8O1;
    }
    return two_stop ? SERIAL_8N2 : SERIAL_8N1;
}

// Re-initialize the Modbus bus if baud, parity, or stop-bits have changed
// since the last setup. Called after every fetchEnvOverrides() so operator
// changes via Notehub take effect without a reboot.
static void applyModbusSerialIfChanged() {
    uint16_t cfg = serialConfigFromEnv();
    if (g_modbus_baud == g_last_modbus_baud && cfg == g_last_serial_cfg) {
        return;
    }
    ModbusRTUClient.end();
    RS485.setDelays(50, 50);
    if (!ModbusRTUClient.begin(g_modbus_baud, cfg)) {
        usbSerial.println("[modbus] re-begin failed; will retry on next read");
    } else {
        g_last_modbus_baud = g_modbus_baud;
        g_last_serial_cfg  = cfg;
    }
}

// Re-issue hub.set if g_report_minutes changed since the last call. Avoids
// drifting the Notecard's outbound/inbound cadence away from the firmware's
// scheduler when an operator updates report_minutes mid-deployment.
static void applyHubSetIfChanged() {
    if (g_report_minutes == g_last_hubset_outbound) {
        return;
    }
    J *req = notecard.newRequest("hub.set");
    if (req) {
        JAddNumberToObject(req, "outbound", g_report_minutes);
        JAddNumberToObject(req, "inbound", g_report_minutes * 2);
        notecard.sendRequest(req);
        g_last_hubset_outbound = g_report_minutes;
    }
}

// Bucket a frequency into a baseline bin. Returns clamped index in [0, FREQ_BINS).
static uint8_t freqBin(float hz) {
    if (hz < 0.0f) hz = 0.0f;
    int bin = (int)(hz / (float)FREQ_BIN_HZ);
    if (bin < 0) return 0;
    if (bin >= FREQ_BINS) return FREQ_BINS - 1;
    return (uint8_t)bin;
}

// Clamp helpers used to defend against pathological env-var values.
static uint32_t clampU32(long v, uint32_t minv, uint32_t maxv, uint32_t fallback) {
    if (v < (long)minv || v > (long)maxv) return fallback;
    return (uint32_t)v;
}
static float clampF(double v, float minv, float maxv, float fallback) {
    if (v < (double)minv || v > (double)maxv) return fallback;
    return (float)v;
}

static void fetchEnvOverrides() {
    J *req = notecard.newRequest("env.get");
    if (!req) return;
    J *names = JAddArrayToObject(req, "names");
    JAddItemToArray(names, JCreateString("sample_minutes"));
    JAddItemToArray(names, JCreateString("report_minutes"));
    JAddItemToArray(names, JCreateString("modbus_slave_id"));
    JAddItemToArray(names, JCreateString("modbus_baud"));
    JAddItemToArray(names, JCreateString("modbus_parity"));
    JAddItemToArray(names, JCreateString("modbus_stop_bits"));
    JAddItemToArray(names, JCreateString("vfd_profile"));
    JAddItemToArray(names, JCreateString("reg_freq"));
    JAddItemToArray(names, JCreateString("reg_current"));
    JAddItemToArray(names, JCreateString("reg_torque"));
    JAddItemToArray(names, JCreateString("reg_drive_temp"));
    JAddItemToArray(names, JCreateString("reg_runtime_hours"));
    JAddItemToArray(names, JCreateString("reg_fault_code"));
    JAddItemToArray(names, JCreateString("current_alarm_factor"));
    JAddItemToArray(names, JCreateString("transient_fault_window_hours"));
    JAddItemToArray(names, JCreateString("transient_fault_count"));
    JAddItemToArray(names, JCreateString("drive_temp_alarm_c"));
    JAddItemToArray(names, JCreateString("expected_run_hours_per_day"));

    J *rsp = notecard.requestAndResponse(req);
    if (!rsp) return;
    if (notecard.responseError(rsp)) { notecard.deleteResponse(rsp); return; }

    // Clamp every value to a safe range. A bad env var must not be able to
    // create a tight loop (sample_minutes = 0), an invalid Modbus address
    // (slave_id = 0 or > 247), or a uint8_t overflow (window_hours > 255).
    g_sample_minutes              = clampU32(envLong(rsp, "sample_minutes", g_sample_minutes),
                                             1, 1440, g_sample_minutes);
    g_report_minutes              = clampU32(envLong(rsp, "report_minutes", g_report_minutes),
                                             1, 1440, g_report_minutes);
    g_modbus_slave_id             = (uint8_t) clampU32(envLong(rsp, "modbus_slave_id", g_modbus_slave_id),
                                                       1, 247, g_modbus_slave_id);
    g_modbus_baud                 = clampU32(envLong(rsp, "modbus_baud", g_modbus_baud),
                                             1200, 230400, g_modbus_baud);
    envCopyString(rsp, "modbus_parity", g_modbus_parity, sizeof(g_modbus_parity));
    g_modbus_stop_bits            = (uint8_t) clampU32(envLong(rsp, "modbus_stop_bits", g_modbus_stop_bits),
                                                       1, 2, g_modbus_stop_bits);
    envCopyString(rsp, "vfd_profile", g_vfd_profile, sizeof(g_vfd_profile));
    g_reg_freq                    = (uint16_t) clampU32(envLong(rsp, "reg_freq",          g_reg_freq),         0, 65535, g_reg_freq);
    g_reg_current                 = (uint16_t) clampU32(envLong(rsp, "reg_current",       g_reg_current),      0, 65535, g_reg_current);
    g_reg_torque                  = (uint16_t) clampU32(envLong(rsp, "reg_torque",        g_reg_torque),       0, 65535, g_reg_torque);
    g_reg_drive_temp              = (uint16_t) clampU32(envLong(rsp, "reg_drive_temp",    g_reg_drive_temp),   0, 65535, g_reg_drive_temp);
    g_reg_runtime_hours           = (uint16_t) clampU32(envLong(rsp, "reg_runtime_hours", g_reg_runtime_hours),0, 65535, g_reg_runtime_hours);
    g_reg_fault_code              = (uint16_t) clampU32(envLong(rsp, "reg_fault_code",    g_reg_fault_code),   0, 65535, g_reg_fault_code);
    g_current_alarm_factor        = clampF(envFloat(rsp, "current_alarm_factor", g_current_alarm_factor),
                                           1.01f, 5.0f, g_current_alarm_factor);
    g_transient_fault_window_hrs  = (uint8_t) clampU32(envLong(rsp, "transient_fault_window_hours", g_transient_fault_window_hrs),
                                                      1, 24, g_transient_fault_window_hrs);
    g_transient_fault_count       = (uint8_t) clampU32(envLong(rsp, "transient_fault_count", g_transient_fault_count),
                                                      1, FAULT_RING_SIZE, g_transient_fault_count);
    g_drive_temp_alarm_c          = clampF(envFloat(rsp, "drive_temp_alarm_c", g_drive_temp_alarm_c),
                                           20.0f, 150.0f, g_drive_temp_alarm_c);
    g_expected_run_hours_per_day  = clampF(envFloat(rsp, "expected_run_hours_per_day", g_expected_run_hours_per_day),
                                           0.1f, 24.0f, g_expected_run_hours_per_day);

    notecard.deleteResponse(rsp);
}

// -------- Modbus polling ------------------------------------------------------------
//
// One transaction reads all six registers contiguously when the user's drive
// happens to be ABB-style (frequency at offset 0, current at offset 1, etc.).
// On other vendor maps where the registers aren't contiguous, fall back to
// per-register reads using the individual reg_* environment variables.
static bool isContiguousMap() {
    return (g_reg_current      == g_reg_freq + 1) &&
           (g_reg_torque       == g_reg_freq + 2) &&
           (g_reg_drive_temp   == g_reg_freq + 3) &&
           (g_reg_runtime_hours== g_reg_freq + 4) &&
           (g_reg_fault_code   == g_reg_freq + 5);
}

// Returns true on a successful single-register read. The previous version
// returned 0 on failure, which silently turned partial-failure cycles into
// samples with valid-looking zero values — a real source of bad data.
static bool modbusReadOne(uint16_t address, uint16_t &out) {
    long v = ModbusRTUClient.holdingRegisterRead(g_modbus_slave_id, address);
    if (v < 0) return false;
    out = (uint16_t)v;
    return true;
}

static bool pollVfd(VfdSample &out) {
    out.valid = false;
    const uint8_t MAX_TRIES = 3;

    for (uint8_t attempt = 0; attempt < MAX_TRIES; attempt++) {
        if (isContiguousMap() &&
            ModbusRTUClient.requestFrom(g_modbus_slave_id, HOLDING_REGISTERS, g_reg_freq, 6)) {
            out.frequency_hz   = ModbusRTUClient.read() / 100.0f;
            out.current_a      = ModbusRTUClient.read() / 100.0f;
            out.torque_pct     = (int16_t)ModbusRTUClient.read();
            out.drive_temp_c   = (int16_t)ModbusRTUClient.read();
            out.runtime_hours  = (uint32_t)(uint16_t)ModbusRTUClient.read();
            out.fault_code     = (uint16_t)ModbusRTUClient.read();
            out.valid = true;
            return true;
        }

        // Fallback: six individual reads. Slower, but works on every register
        // map. ALL six must succeed before the sample is marked valid.
        if (!isContiguousMap()) {
            uint16_t v_freq, v_curr, v_torq, v_temp, v_run, v_flt;
            if (modbusReadOne(g_reg_freq,          v_freq) &&
                modbusReadOne(g_reg_current,       v_curr) &&
                modbusReadOne(g_reg_torque,        v_torq) &&
                modbusReadOne(g_reg_drive_temp,    v_temp) &&
                modbusReadOne(g_reg_runtime_hours, v_run ) &&
                modbusReadOne(g_reg_fault_code,    v_flt )) {
                out.frequency_hz  = v_freq / 100.0f;
                out.current_a     = v_curr / 100.0f;
                out.torque_pct    = (int16_t)v_torq;
                out.drive_temp_c  = (int16_t)v_temp;
                out.runtime_hours = (uint32_t)v_run;
                out.fault_code    = v_flt;
                out.valid = true;
                return true;
            }
        }

        usbSerial.print("[modbus] read failed, attempt ");
        usbSerial.println(attempt + 1);
        delay(100);
    }
    return false;
}

// -------- Statistics accumulation ---------------------------------------------------
static void accumulate(const VfdSample &s) {
    bool running = s.frequency_hz > 1.0f;
    if (running) {
        stats.run_samples++;
        stats.f_hz_sum  += s.frequency_hz;  if (s.frequency_hz  > stats.f_hz_peak) stats.f_hz_peak = s.frequency_hz;
        stats.i_a_sum   += s.current_a;     if (s.current_a     > stats.i_a_peak ) stats.i_a_peak  = s.current_a;
        stats.trq_sum   += s.torque_pct;    if (s.torque_pct    > stats.trq_peak ) stats.trq_peak  = s.torque_pct;
    } else {
        stats.stop_samples++;
    }
    // Drive temperature is meaningful in both states.
    stats.drv_c_sum  += s.drive_temp_c;  if (s.drive_temp_c > stats.drv_c_peak) stats.drv_c_peak = s.drive_temp_c;
    stats.hrs_total_last = s.runtime_hours;
}

// -------- Rule evaluation -----------------------------------------------------------
static void recordFault(uint16_t code) {
    fault_history_idx = (fault_history_idx + 1) % FAULT_RING_SIZE;
    fault_history_ms  [fault_history_idx] = millis();
    fault_history_code[fault_history_idx] = code;
}

static uint8_t countFaultsInWindow(uint8_t window_hours) {
    const uint32_t window_ms = (uint32_t)window_hours * 60UL * 60UL * 1000UL;
    const uint32_t now = millis();
    uint8_t count = 0;
    for (uint8_t i = 0; i < FAULT_RING_SIZE; i++) {
        if (fault_history_code[i] != 0 && now - fault_history_ms[i] <= window_ms) {
            count++;
        }
    }
    return count;
}

static void evaluateRules() {
    // Compute the just-completed hour's running-state means before resetting.
    const uint32_t run = stats.run_samples;
    const float i_a_mean   = run ? stats.i_a_sum   / (float)run : 0.0f;
    const float f_hz_mean  = run ? stats.f_hz_sum  / (float)run : 0.0f;
    const float drv_c_mean = (run + stats.stop_samples)
                             ? (float)stats.drv_c_sum / (float)(run + stats.stop_samples) : 0.0f;

    // ---- Load anomaly --------------------------------------------------------
    // Hourly mean current exceeds the rolling baseline by the alarm factor —
    // compared *within the same frequency bin*, since current at 30 Hz and
    // current at 60 Hz aren't comparable on the same pump. This is a *load*
    // signal: many root causes (bearing drag, fouling, valve position,
    // viscosity, debris, some impeller conditions) can drive it. Edge-
    // triggered: fires once on the rising edge, rearms when the bin's mean
    // returns below the threshold.
    if (run > 5 && f_hz_mean > 1.0f) {
        const uint8_t bin = freqBin(f_hz_mean);
        const float baseline = g_current_baseline_by_bin[bin];
        if (!g_baseline_seeded[bin]) {
            // Cold-start for this frequency bin: seed from the first valid hour
            // we observed running in this bin.
            g_current_baseline_by_bin[bin] = i_a_mean;
            g_baseline_seeded[bin] = true;
        } else if (i_a_mean > baseline * g_current_alarm_factor) {
            if (!g_active_load_anomaly) {
                sendEvent("load_anomaly", nullptr, f_hz_mean, i_a_mean, baseline);
                g_active_load_anomaly = true;
            }
        } else {
            // EWMA update on the bin's baseline (alpha = 0.05 over hourly
            // samples) and rearm if we were active.
            g_current_baseline_by_bin[bin] = 0.95f * baseline + 0.05f * i_a_mean;
            g_active_load_anomaly = false;
        }
    }

    // ---- Transient faults ----------------------------------------------------
    // Edge-triggered cluster of distinct fault transitions within the window.
    const uint8_t fault_count = countFaultsInWindow(g_transient_fault_window_hrs);
    if (fault_count >= g_transient_fault_count) {
        if (!g_active_transient_flt) {
            sendEvent("transient_faults", nullptr,
                      (float)fault_count, (float)g_transient_fault_window_hrs);
            g_active_transient_flt = true;
        }
    } else {
        g_active_transient_flt = false;
    }

    // ---- Drive overtemp ------------------------------------------------------
    // Peak (not mean) drive heatsink temperature breached the limit at any
    // point in the hour. Peak is the right metric — a thermal trip is
    // determined by instantaneous overshoot, not a smoothed average. Edge-
    // triggered: rearms once the hour's peak comes back below the limit.
    if ((float)stats.drv_c_peak > g_drive_temp_alarm_c) {
        if (!g_active_drive_overtemp) {
            sendEvent("drive_overtemp", nullptr,
                      (float)stats.drv_c_peak, drv_c_mean);
            g_active_drive_overtemp = true;
        }
    } else {
        g_active_drive_overtemp = false;
    }

    // ---- Runtime drift -------------------------------------------------------
    // Every 24 hours, compare actual vs expected runtime hours. Skip until the
    // baseline has been initialized from a real sample (see loop()).
    const uint32_t now = millis();
    if (g_runtime_baseline_initialized &&
        now - day_start_ms >= 24UL * 60UL * 60UL * 1000UL) {
        uint32_t observed = stats.hrs_total_last - day_start_runtime_hours;
        if (g_expected_run_hours_per_day > 0.0f &&
            (float)observed > g_expected_run_hours_per_day * 1.25f) {
            sendEvent("runtime_drift", nullptr,
                      (float)observed, g_expected_run_hours_per_day);
        }
        day_start_runtime_hours = stats.hrs_total_last;
        day_start_ms = now;
    }
}

// -------- Outbound Note emission ----------------------------------------------------
static void sendSummary() {
    const uint32_t total = stats.run_samples + stats.stop_samples;
    if (total == 0) return;

    J *req = notecard.newRequest("note.add");
    if (!req) return;
    JAddStringToObject(req, "file", "vfd_summary.qo");
    J *body = JAddObjectToObject(req, "body");

    const float f_hz_mean   = stats.run_samples ? stats.f_hz_sum / stats.run_samples : 0.0f;
    const float i_a_mean    = stats.run_samples ? stats.i_a_sum  / stats.run_samples : 0.0f;
    const float trq_mean    = stats.run_samples ? (float)stats.trq_sum  / stats.run_samples : 0.0f;
    const float drv_c_mean  = (float)stats.drv_c_sum / (float)total;

    JAddNumberToObject(body, "f_hz_mean", f_hz_mean);
    JAddNumberToObject(body, "f_hz_peak", stats.f_hz_peak);
    JAddNumberToObject(body, "i_a_mean",  i_a_mean);
    JAddNumberToObject(body, "i_a_peak",  stats.i_a_peak);
    JAddNumberToObject(body, "trq_mean",  (int)trq_mean);
    JAddNumberToObject(body, "trq_peak",  stats.trq_peak);
    JAddNumberToObject(body, "drv_c_mean",(int)drv_c_mean);
    JAddNumberToObject(body, "drv_c_peak",stats.drv_c_peak);
    JAddNumberToObject(body, "run_min",   stats.run_samples * g_sample_minutes);
    JAddNumberToObject(body, "stop_min",  stats.stop_samples * g_sample_minutes);
    JAddNumberToObject(body, "hrs_total", stats.hrs_total_last);
    JAddNumberToObject(body, "fault_count_hour", stats.fault_count_hour);

    // last_fault_seen_this_hour is a per-hour summary field (not the persistent
    // fault state — that's g_current_fault_code). Reports the last non-zero
    // fault code observed in the just-completed hour, "" if no faults.
    char fault_str[12];
    snprintf(fault_str, sizeof(fault_str), "%u", stats.last_fault_seen_this_hour);
    JAddStringToObject(body, "last_fault", fault_str);

    notecard.sendRequest(req); // no sync:true — the periodic outbound interval will batch this.
    stats.reset();
}

static void sendEvent(const char *alert, const VfdSample *trigger,
                      float v1, float v2, float v3) {
    J *req = notecard.newRequest("note.add");
    if (!req) return;
    JAddStringToObject(req, "file", "vfd_event.qo");
    JAddBoolToObject  (req, "sync", true); // ship immediately on trigger
    J *body = JAddObjectToObject(req, "body");
    JAddStringToObject(body, "alert", alert);

    // Populate every templated field with a sentinel when we don't have a real
    // value, so downstream analytics can rely on column presence and NaN never
    // hits the wire (templated Notes validate types and JSON has no NaN).
    JAddNumberToObject(body, "f_hz",       trigger ? trigger->frequency_hz : 0.0f);
    JAddNumberToObject(body, "i_a",        trigger ? trigger->current_a   : 0.0f);
    JAddNumberToObject(body, "fault_code", trigger ? trigger->fault_code  : g_current_fault_code);
    JAddNumberToObject(body, "hrs_total",  trigger ? trigger->runtime_hours : stats.hrs_total_last);
    JAddNumberToObject(body, "v1", isnan(v1) ? 0.0f : v1);
    JAddNumberToObject(body, "v2", isnan(v2) ? 0.0f : v2);
    JAddNumberToObject(body, "v3", isnan(v3) ? 0.0f : v3);
    notecard.sendRequest(req);
}
