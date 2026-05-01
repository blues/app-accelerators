/***************************************************************************
  diesel_gen_monitor_helpers.cpp

  Implementation of helper functions for the diesel_gen_monitor sketch:
    - Environment-variable overrides from Notehub
    - Modbus RTU serial bus management
    - Generator register polling with retry
    - Rolling statistics accumulation
    - Alert rule evaluation

  THIS FILE IS SPECIFIC TO THIS PROJECT AND IS NOT A GENERAL-PURPOSE LIBRARY.
***************************************************************************/

#include "diesel_gen_monitor_helpers.h"
#include <strings.h>   // strcasecmp — portability across Arduino cores

// -------- Environment-variable parsing helpers -----------------------------------------

static long envLong(J *rsp, const char *name, long fallback) {
    J *body = JGetObject(rsp, "body");
    if (!body) return fallback;
    const char *s = JGetString(body, name);
    if (!s || !*s) return fallback;
    return strtol(s, nullptr, 0);   // base 0: accepts decimal, and hex with 0x prefix
}

static double envFloat(J *rsp, const char *name, double fallback) {
    J *body = JGetObject(rsp, "body");
    if (!body) return fallback;
    const char *s = JGetString(body, name);
    if (!s || !*s) return fallback;
    return strtod(s, nullptr);
}

static void envStr(J *rsp, const char *name, char *dst, size_t len) {
    J *body = JGetObject(rsp, "body");
    if (!body) return;
    const char *s = JGetString(body, name);
    if (!s || !*s) return;
    strncpy(dst, s, len - 1);
    dst[len - 1] = '\0';
}

// Clamp helpers defend against pathological env-var values:
// a zero sample_minutes would create a tight polling loop;
// a slave_id outside 1–247 is illegal per the Modbus spec.
static uint32_t clampU32(long v, uint32_t lo, uint32_t hi, uint32_t fb) {
    if (v < (long)lo || v > (long)hi) return fb;
    return (uint32_t)v;
}

static float clampF(double v, float lo, float hi, float fb) {
    if (v < (double)lo || v > (double)hi) return fb;
    return (float)v;
}

// -------- Environment-variable overrides -----------------------------------------------

void fetchEnvOverrides() {
    J *req = notecard.newRequest("env.get");
    if (!req) return;
    J *names = JAddArrayToObject(req, "names");
    JAddItemToArray(names, JCreateString("sample_minutes"));
    JAddItemToArray(names, JCreateString("report_minutes"));
    JAddItemToArray(names, JCreateString("modbus_slave_id"));
    JAddItemToArray(names, JCreateString("modbus_baud"));
    JAddItemToArray(names, JCreateString("modbus_parity"));
    JAddItemToArray(names, JCreateString("modbus_stop_bits"));
    JAddItemToArray(names, JCreateString("reg_engine_rpm"));
    JAddItemToArray(names, JCreateString("reg_fuel_pct"));
    JAddItemToArray(names, JCreateString("reg_load_pct"));
    JAddItemToArray(names, JCreateString("reg_oil_kpa"));
    JAddItemToArray(names, JCreateString("reg_coolant_c"));
    JAddItemToArray(names, JCreateString("reg_run_hours"));
    JAddItemToArray(names, JCreateString("reg_alarm_word"));
    JAddItemToArray(names, JCreateString("fuel_low_pct"));
    JAddItemToArray(names, JCreateString("coolant_alarm_c"));
    JAddItemToArray(names, JCreateString("oil_low_kpa"));
    JAddItemToArray(names, JCreateString("alarm_mask_fts"));
    JAddItemToArray(names, JCreateString("rpm_running"));

    J *rsp = notecard.requestAndResponse(req);
    if (!rsp) return;
    if (notecard.responseError(rsp)) { notecard.deleteResponse(rsp); return; }

    // sample_minutes is written to g_pending_sample_minutes rather than g_sample_minutes
    // directly. Applying a new cadence mid-window would corrupt run_min / stop_min because
    // those are derived as sample_count * sample_minutes_active. The pending value is
    // promoted to g_sample_minutes (and stats.sample_minutes_active) in sendSummary() only
    // after the outgoing summary note has been confirmed, ensuring one consistent cadence
    // per window throughout.
    g_pending_sample_minutes = clampU32(envLong(rsp, "sample_minutes", g_pending_sample_minutes), 1, 1440, g_pending_sample_minutes);
    g_report_minutes         = clampU32(envLong(rsp, "report_minutes", g_report_minutes),         1, 1440, g_report_minutes);
    g_modbus_slave_id  = (uint8_t)clampU32(envLong(rsp, "modbus_slave_id", g_modbus_slave_id), 1, 247, g_modbus_slave_id);
    g_modbus_baud      = clampU32(envLong(rsp, "modbus_baud",     g_modbus_baud),  1200, 230400, g_modbus_baud);
    envStr(rsp, "modbus_parity", g_modbus_parity, sizeof(g_modbus_parity));
    g_modbus_stop_bits = (uint8_t)clampU32(envLong(rsp, "modbus_stop_bits", g_modbus_stop_bits), 1, 2, g_modbus_stop_bits);

    g_reg_engine_rpm   = (uint16_t)clampU32(envLong(rsp, "reg_engine_rpm",  g_reg_engine_rpm),  0, 65535, g_reg_engine_rpm);
    g_reg_fuel_pct     = (uint16_t)clampU32(envLong(rsp, "reg_fuel_pct",    g_reg_fuel_pct),    0, 65535, g_reg_fuel_pct);
    g_reg_load_pct     = (uint16_t)clampU32(envLong(rsp, "reg_load_pct",    g_reg_load_pct),    0, 65535, g_reg_load_pct);
    g_reg_oil_kpa      = (uint16_t)clampU32(envLong(rsp, "reg_oil_kpa",     g_reg_oil_kpa),     0, 65535, g_reg_oil_kpa);
    g_reg_coolant_c    = (uint16_t)clampU32(envLong(rsp, "reg_coolant_c",   g_reg_coolant_c),   0, 65535, g_reg_coolant_c);
    g_reg_run_hours    = (uint16_t)clampU32(envLong(rsp, "reg_run_hours",   g_reg_run_hours),   0, 65535, g_reg_run_hours);
    g_reg_alarm_word   = (uint16_t)clampU32(envLong(rsp, "reg_alarm_word",  g_reg_alarm_word),  0, 65535, g_reg_alarm_word);

    g_fuel_low_pct     = clampF(envFloat(rsp, "fuel_low_pct",    g_fuel_low_pct),    1.0f,  99.0f,   g_fuel_low_pct);
    g_coolant_alarm_c  = clampF(envFloat(rsp, "coolant_alarm_c", g_coolant_alarm_c), 20.0f, 130.0f,  g_coolant_alarm_c);
    g_oil_low_kpa      = clampF(envFloat(rsp, "oil_low_kpa",     g_oil_low_kpa),     0.0f,  2000.0f, g_oil_low_kpa);
    g_alarm_mask_fts   = (uint16_t)clampU32(envLong(rsp, "alarm_mask_fts", g_alarm_mask_fts), 0, 65535, g_alarm_mask_fts);
    g_rpm_running      = (uint16_t)clampU32(envLong(rsp, "rpm_running",    g_rpm_running),    1, 65535, g_rpm_running);

    // Ensure the sample cadence is no longer than the report window. If sample_minutes
    // exceeds report_minutes, at most zero complete samples fit in a window, producing
    // a guaranteed telemetry blackout. Clamp pending to the report window as a safeguard.
    if (g_pending_sample_minutes > g_report_minutes) {
        g_pending_sample_minutes = g_report_minutes;
    }

    notecard.deleteResponse(rsp);
}

// -------- Modbus serial bus management -------------------------------------------------

// Translate (g_modbus_parity, g_modbus_stop_bits) into the SERIAL_8xx constant
// ArduinoModbus / HardwareSerial expects on the OPTA's RS-485 port.
static uint16_t serialConfigFromEnv() {
    bool two_stop = (g_modbus_stop_bits == 2);
    if (strcasecmp(g_modbus_parity, "even") == 0) return two_stop ? SERIAL_8E2 : SERIAL_8E1;
    if (strcasecmp(g_modbus_parity, "odd")  == 0) return two_stop ? SERIAL_8O2 : SERIAL_8O1;
    return two_stop ? SERIAL_8N2 : SERIAL_8N1;
}

// Re-initialize the Modbus bus when baud or frame format changed, OR when a
// previous begin() never succeeded (g_modbus_ready == false).  The second
// condition is what drives the sample-cadence retry: loop() calls this function
// on every sample interval, so a failed begin() is re-attempted every minute
// (default) rather than waiting until the next report boundary.
void applyModbusSerialIfChanged() {
    uint16_t cfg = serialConfigFromEnv();
    // Skip only when config is unchanged AND the client is already up.
    if (g_modbus_baud == g_last_modbus_baud && cfg == g_last_serial_cfg && g_modbus_ready) return;
    ModbusRTUClient.end();
    RS485.setDelays(50, 50);
    if (!ModbusRTUClient.begin(g_modbus_baud, cfg)) {
        g_modbus_ready = false;
        Serial.println("[modbus] begin() failed; will retry on next sample");
    } else {
        g_last_modbus_baud = g_modbus_baud;
        g_last_serial_cfg  = cfg;
        g_modbus_ready     = true;
    }
}

// Re-issue hub.set when report_minutes changed OR when a full hub.set (product + mode)
// has not yet been confirmed. The second condition matters for factory-fresh Notecards
// that missed the boot-time notecardConfigure() call: retrying only outbound/inbound
// leaves product and mode unset, so the device never enters periodic mode.
// Both g_hub_provisioned and g_last_hubset_outbound are updated only on confirmed
// success so transient failures trigger a retry at the next report boundary.
void applyHubSetIfChanged() {
    const bool need_full    = !g_hub_provisioned;
    const bool need_cadence = (g_report_minutes != g_last_hubset_outbound);
    if (!need_full && !need_cadence) return;

    J *req = notecard.newRequest("hub.set");
    if (!req) return;

    if (need_full) {
        // Include product UID and mode so a factory-fresh Notecard that never received
        // the boot-time hub.set can still enter periodic mode on a retry.
        JAddStringToObject(req, "product", g_product_uid);
        JAddStringToObject(req, "mode",    "periodic");
    }
    JAddNumberToObject(req, "outbound", g_report_minutes);
    JAddNumberToObject(req, "inbound",  g_report_minutes * 2);

    J *rsp = notecard.requestAndResponse(req);
    bool ok = rsp && !notecard.responseError(rsp);
    notecard.deleteResponse(rsp);
    if (ok) {
        g_hub_provisioned      = true;
        g_last_hubset_outbound = g_report_minutes;
    }
}

// -------- Modbus polling ---------------------------------------------------------------

// Returns true on a successful single holding-register read into `out`.
// Returning bool (not raw int) prevents callers from silently treating
// a -1 error return as a valid zero reading — a common source of phantom data.
static bool modbusReadOne(uint16_t address, uint16_t &out) {
    long v = ModbusRTUClient.holdingRegisterRead(g_modbus_slave_id, address);
    if (v < 0) return false;
    out = (uint16_t)v;
    return true;
}

// Polls all seven generator registers with up to MAX_TRIES attempts.
// ALL seven must succeed before the sample is marked valid — partial success
// silently looks like real data with incorrect values, which is worse than no data.
bool pollGenerator(GenSample &out) {
    out.valid = false;
    const uint8_t MAX_TRIES = 3;

    for (uint8_t attempt = 0; attempt < MAX_TRIES; attempt++) {
        uint16_t rpm, fuel, load, oil, cool, hours, alarm;

        if (modbusReadOne(g_reg_engine_rpm,  rpm)   &&
            modbusReadOne(g_reg_fuel_pct,    fuel)  &&
            modbusReadOne(g_reg_load_pct,    load)  &&
            modbusReadOne(g_reg_oil_kpa,     oil)   &&
            modbusReadOne(g_reg_coolant_c,   cool)  &&
            modbusReadOne(g_reg_run_hours,   hours) &&
            modbusReadOne(g_reg_alarm_word,  alarm)) {

            out.engine_rpm = rpm;
            out.fuel_pct   = (float)fuel;         // 0–100% (whole units assumed)
            out.load_pct   = (float)load;         // 0–100% (whole units assumed)
            out.oil_kpa    = (float)oil;          // kPa (whole units; see §9 for scaled variants)
            out.coolant_c  = (int16_t)cool;       // signed °C
            out.run_hours  = hours;               // see §9: often 32-bit on real controllers
            out.alarm_word = alarm;
            out.valid      = true;
            return true;
        }

        Serial.print("[modbus] read failed, attempt ");
        Serial.println(attempt + 1);
        delay(100);
    }
    return false;
}

// -------- Statistics accumulation ------------------------------------------------------

void accumulate(const GenSample &s) {
    bool running = (s.engine_rpm >= g_rpm_running);

    // Load and oil pressure are only meaningful when the engine is spinning.
    // Accumulating stopped-state readings into their means dilutes the signal
    // with near-zero samples and can mask a genuine running anomaly.
    if (running) {
        stats.run_samples++;
        stats.load_sum += s.load_pct;
        if (s.load_pct > stats.load_peak) stats.load_peak = s.load_pct;
        stats.oil_sum  += s.oil_kpa;
        if (s.oil_kpa  > stats.oil_peak)  stats.oil_peak  = s.oil_kpa;
    } else {
        stats.stop_samples++;
    }

    // Fuel and coolant are informative in both states.
    stats.fuel_sum    += s.fuel_pct;
    stats.coolant_sum += s.coolant_c;
    if (s.coolant_c > stats.coolant_peak) stats.coolant_peak = s.coolant_c;
    stats.run_hours_last = s.run_hours;
}

// -------- Rule evaluation --------------------------------------------------------------

// Called once per report window, immediately before sendSummary() clears the stats.
// Each rule is edge-triggered: fires on the rising edge of the condition and rearms
// when the condition clears. This prevents a sustained anomaly from emitting a new
// alert on every report interval while the condition persists.
void evaluateRules() {
    const uint32_t total = stats.run_samples + stats.stop_samples;
    if (total == 0) return;

    const float fuel_mean = stats.fuel_sum / (float)total;

    // ---- Fuel level -----------------------------------------------------------------
    // Low fuel is dangerous regardless of running state: a tank that empties
    // during an extended outage means the generator stops before utility power
    // returns. Alert when the window mean crosses the threshold.
    // trigger_val = window mean fuel (%); trigger_threshold = g_fuel_low_pct.
    // The last-known sample fields provide machine-state context; trigger_val is
    // the aggregate that actually fired the rule, ensuring the alert is self-explaining
    // even when the most-recent poll reading differs from the window mean.
    if (fuel_mean < g_fuel_low_pct) {
        if (!g_active_fuel_low) {
            if (sendEvent("fuel_low", &g_last_known_sample, fuel_mean, g_fuel_low_pct)) {
                g_active_fuel_low = true;
            }
        }
    } else {
        g_active_fuel_low = false;
    }

    // ---- Coolant overtemperature ----------------------------------------------------
    // Peak temperature is the right metric here, not the window mean. An engine can
    // reach a dangerous temperature in seconds under load; the mean smears that spike
    // across many samples and may never breach the threshold even when a real overheat
    // occurred. Peak correctly catches the instantaneous worst case.
    // trigger_val = window peak coolant (°C); trigger_threshold = g_coolant_alarm_c.
    // The last-known sample's coolant_c may be below threshold if the peak was
    // transient; trigger_val carries the actual worst-case reading that fired the rule.
    if ((float)stats.coolant_peak > g_coolant_alarm_c) {
        if (!g_active_coolant_overtemp) {
            if (sendEvent("coolant_overtemp", &g_last_known_sample,
                          (float)stats.coolant_peak, g_coolant_alarm_c)) {
                g_active_coolant_overtemp = true;
            }
        }
    } else {
        g_active_coolant_overtemp = false;
    }

    // ---- Oil pressure ---------------------------------------------------------------
    // Only evaluated when the engine ran at least once this window. A standby
    // generator at rest has no oil pressure because the pump isn't spinning;
    // checking it during idle generates constant false positives.
    // trigger_val = window mean oil pressure (kPa, running samples only);
    // trigger_threshold = g_oil_low_kpa.
    if (stats.run_samples > 0) {
        float oil_mean = stats.oil_sum / (float)stats.run_samples;
        if (oil_mean < g_oil_low_kpa) {
            if (!g_active_oil_low) {
                if (sendEvent("oil_low_pressure", &g_last_known_sample,
                              oil_mean, g_oil_low_kpa)) {
                    g_active_oil_low = true;
                }
            }
        } else {
            g_active_oil_low = false;
        }
    } else {
        // No run samples this window: engine did not spin, so oil-pressure state
        // is undefined. Clear the edge-trigger flag so a genuine low-pressure event
        // on the next running window is not suppressed by a stale latched state.
        // Without this reset, one low-oil alert during a run latches g_active_oil_low
        // true through all subsequent stopped windows, suppressing the next real event.
        g_active_oil_low = false;
    }
}
