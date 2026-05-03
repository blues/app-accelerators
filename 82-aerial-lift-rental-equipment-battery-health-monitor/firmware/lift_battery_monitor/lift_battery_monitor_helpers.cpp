/***************************************************************************
  lift_battery_monitor_helpers.cpp — Notecard interface, sensor reading,
  SoC/SoH estimation, and alert logic for the Aerial Lift Battery Monitor.

  THIS FILE SHOULD BE EDITED AFTER GENERATION.
  IT IS PROVIDED AS A STARTING POINT FOR THE USER TO EDIT AND EXTEND.
***************************************************************************/

#include "lift_battery_monitor_helpers.h"
#include <math.h>

// ─── Hardware constants (shared with .ino via build-unit visibility) ──────────
#define PIN_THERMISTOR     A0
#define THERM_SERIES_R  10000.0f
#define THERM_NOMINAL_R 10000.0f
#define THERM_NOMINAL_T    25.0f
#define THERM_BETA       3950.0f
#define ADC_COUNTS       4096.0f

#define STATUS_PORT   50
#define ALERT_PORT    51

#define ALERT_COOLDOWN_SEC  (30 * 60)

// Maximum consecutive note.add failures before a stale summary window is
// discarded.  Prevents unbounded accumulator growth during a prolonged I²C or
// Notecard fault while still giving the uplink a few attempts to recover first.
#define MAX_SUMM_RETRIES  3

// ─── OCV→SoC lookup tables ────────────────────────────────────────────────────
// Each row: [pack OCV in V, SoC in %], ordered high→low voltage.
// Sized for a 48V pack. Scale voltage entries by (pack_nominal / 48) for other
// pack voltages, or expose rated_voltage_v as an env var (see README §9).

static const float SOC_TABLE_LITHIUM[][2] = {
    // LiFePO4 16S × 3.65V full → 58.4V;  × 3.0V cutoff → 48.0V
    {58.4f, 100.0f}, {57.6f, 90.0f}, {56.8f, 80.0f},
    {56.0f,  70.0f}, {55.2f, 60.0f}, {54.4f, 50.0f},
    {53.6f,  40.0f}, {52.8f, 30.0f}, {52.0f, 20.0f},
    {51.2f,  10.0f}, {48.0f,  0.0f}
};
static const int SOC_LITHIUM_ROWS =
    (int)(sizeof(SOC_TABLE_LITHIUM) / sizeof(SOC_TABLE_LITHIUM[0]));

static const float SOC_TABLE_LEAD_ACID[][2] = {
    // 24-cell flooded lead-acid OCV at rest
    {51.8f, 100.0f}, {51.2f, 90.0f}, {50.6f, 80.0f},
    {50.0f,  70.0f}, {49.4f, 60.0f}, {48.8f, 50.0f},
    {48.2f,  40.0f}, {47.6f, 30.0f}, {47.0f, 20.0f},
    {46.4f,  10.0f}, {46.0f,  0.0f}
};
static const int SOC_LEAD_ACID_ROWS =
    (int)(sizeof(SOC_TABLE_LEAD_ACID) / sizeof(SOC_TABLE_LEAD_ACID[0]));

// ─────────────────────────────────────────────────────────────────────────────
// notecardConfigure — hub.set + suppress accelerometer; runs on cold boot and
// whenever report_interval_m changes.  Returns true on success; the caller
// must treat false as a commissioning fault and avoid silently continuing.
// ─────────────────────────────────────────────────────────────────────────────
bool notecardConfigure(const char *productUID) {
    J *req = notecard.newRequest("hub.set");
    JAddStringToObject(req, "product", productUID);
    JAddStringToObject(req, "mode", "periodic");
    JAddNumberToObject(req, "outbound", (int)cfg.report_interval_m);
    JAddNumberToObject(req, "inbound", 120);   // pull env var updates every 2 h
    // retry handles the cold-boot I²C race where the Notecard can take ~10 s
    // to become ready after power-on.
    bool ok = notecard.sendRequestWithRetry(req, 10);
    if (!ok) {
        Serial.println("[error] hub.set failed — verify PRODUCT_UID and Notecard "
                       "readiness; device is NOT commissioned");
    }

    // Suppress built-in accelerometer — motion tracking is not needed here and
    // it adds a small steady-state current drain to the Notecard.
    req = notecard.newRequest("card.motion.mode");
    JAddBoolToObject(req, "stop", true);
    notecard.sendRequest(req);

    return ok;
}

// ─────────────────────────────────────────────────────────────────────────────
// defineTemplates — compact templates required for satellite transmission.
// port: each compact Notefile gets a unique integer (1–100) so the Notecard
// can reference it by number over the NTN link rather than the full filename.
// delete:true on battery_status.qo prevents queued hourly summaries from
// being routed over the metered Skylo satellite link.
//
// Returns true only when BOTH note.template requests succeed.  The caller must
// not persist last_applied_report_m unless this function returns true; leaving
// it at its previous value ensures the next wake retries template definition.
// ─────────────────────────────────────────────────────────────────────────────
bool defineTemplates(void) {
    bool ok = true;

    // Hourly status summary — cellular-preferred, satellite-suppressed
    J *req = notecard.newRequest("note.template");
    JAddStringToObject(req, "file", "battery_status.qo");
    JAddNumberToObject(req, "port", STATUS_PORT);
    JAddStringToObject(req, "format", "compact");
    JAddBoolToObject(req, "delete", true);
    J *body = JAddObjectToObject(req, "body");
    JAddNumberToObject(body, "pack_v",  14.1);  // 4-byte float (TFLOAT32)
    JAddNumberToObject(body, "cur_a",   14.1);
    JAddNumberToObject(body, "temp_c",  14.1);
    JAddNumberToObject(body, "soc_pct", 21);    // 1-byte unsigned int (TUINT8)
    JAddNumberToObject(body, "soh_pct", 21);
    JAddNumberToObject(body, "throughput_ah", 14.1);
    JAddBoolToObject(body,   "can_ok",  true);
    if (!notecard.sendRequestWithRetry(req, 5)) {
        Serial.println("[error] note.template battery_status.qo failed");
        ok = false;
    }

    // Immediate alert — no delete flag: critical alerts must reach the fleet
    // via cellular or Skylo satellite, whichever link is available
    req = notecard.newRequest("note.template");
    JAddStringToObject(req, "file", "battery_alert.qo");
    JAddNumberToObject(req, "port", ALERT_PORT);
    JAddStringToObject(req, "format", "compact");
    body = JAddObjectToObject(req, "body");
    // Compact string fields require a fixed-length placeholder string whose
    // length equals the maximum field width to reserve.  The longest alert
    // value is "cell_imbalance" (14 chars); 16 chars gives two spare bytes.
    // Using a numeric string like "16" would encode a 2-char field — too short.
    JAddStringToObject(body, "alert",   "________________");  // 16-char placeholder
    JAddNumberToObject(body, "pack_v",  14.1);
    JAddNumberToObject(body, "soc_pct", 21);
    JAddNumberToObject(body, "temp_c",  14.1);
    JAddNumberToObject(body, "extra_v", 14.1);
    if (!notecard.sendRequestWithRetry(req, 5)) {
        Serial.println("[error] note.template battery_alert.qo failed");
        ok = false;
    }

    if (!ok) {
        Serial.println("[error] template definition incomplete — "
                       "will retry on next wake");
    }
    return ok;
}

// ─────────────────────────────────────────────────────────────────────────────
// fetchEnvOverrides — pull overrides from Notehub fleet/device environment.
// All variables are optional; firmware defaults are used if not set.
// ─────────────────────────────────────────────────────────────────────────────
void fetchEnvOverrides(PersistState &s) {
    (void)s;  // reserved for future per-wake state fixup if env changes

    // Retry env.get so this — the very first Notecard transaction on both cold
    // and warm boots — survives the I²C cold-boot race where the Notecard can
    // take up to ~10 s to become ready after power-on.
    J *rsp = nullptr;
    for (int attempt = 0; attempt < 5 && !rsp; attempt++) {
        if (attempt) delay(2000);
        rsp = notecard.requestAndResponse(notecard.newRequest("env.get"));
    }
    if (!rsp) return;
    J *body = JGetObject(rsp, "body");
    if (!body) { notecard.deleteResponse(rsp); return; }

    const char *chem = JGetString(body, "chemistry");
    if (chem && strlen(chem)) {
        if (strcmp(chem, "lithium") == 0) {
            cfg.is_lithium = true;
        } else if (strcmp(chem, "lead_acid") == 0) {
            cfg.is_lithium = false;
        } else {
            // Unknown value — keep the existing setting to avoid silently
            // selecting the wrong OCV table due to a typo or casing mismatch.
            Serial.print("[cfg] unknown chemistry '");
            Serial.print(chem);
            Serial.println("' — accepted values: lithium, lead_acid");
        }
    }

    auto getF = [&](const char *k, float def) -> float {
        const char *v = JGetString(body, k); return (v && strlen(v)) ? atof(v) : def;
    };
    auto getU = [&](const char *k, uint32_t def) -> uint32_t {
        const char *v = JGetString(body, k);
        return (v && strlen(v)) ? (uint32_t)atol(v) : def;
    };

    cfg.soc_alert_pct     = getF("soc_alert_pct",    cfg.soc_alert_pct);
    cfg.temp_high_c       = getF("temp_high_c",       cfg.temp_high_c);
    cfg.temp_low_c        = getF("temp_low_c",        cfg.temp_low_c);
    cfg.soh_alert_pct     = getF("soh_alert_pct",     cfg.soh_alert_pct);
    cfg.rated_cap_ah      = getF("rated_cap_ah",      cfg.rated_cap_ah);
    cfg.cell_delta_mv     = getF("cell_delta_mv",     cfg.cell_delta_mv);
    cfg.sample_interval_s = getU("sample_interval_s", cfg.sample_interval_s);
    cfg.report_interval_m = getU("report_interval_m", cfg.report_interval_m);
#if ENABLE_ACS758
    // ACS758 calibration: set acs758_zero_v at commissioning by measuring the
    // sensor output with zero traction current flowing (contactor open).
    cfg.acs758_zero_v     = getF("acs758_zero_v",     cfg.acs758_zero_v);
    cfg.acs758_mv_per_a   = getF("acs758_mv_per_a",   cfg.acs758_mv_per_a);
#endif

    notecard.deleteResponse(rsp);

    // ── Clamp to sane operating bounds ───────────────────────────────────────
    // Both a floor and a ceiling are enforced so a typo or malformed Notehub
    // env value cannot soft-brick reporting (e.g., sample_interval_s = 0 or
    // sample_interval_s = 999999 would stall the fleet silently).
    if (cfg.sample_interval_s < 30)       cfg.sample_interval_s = 30;
    if (cfg.sample_interval_s > 86400)    cfg.sample_interval_s = 86400;  // max 24 h
    if (cfg.report_interval_m < 5)        cfg.report_interval_m = 5;
    if (cfg.report_interval_m > 1440)     cfg.report_interval_m = 1440;   // max 24 h
    if (cfg.rated_cap_ah < 1.0f)          cfg.rated_cap_ah = 1.0f;
    if (cfg.rated_cap_ah > 2000.0f)       cfg.rated_cap_ah = 2000.0f;
    if (cfg.soc_alert_pct < 0.0f)         cfg.soc_alert_pct = 0.0f;
    if (cfg.soc_alert_pct > 99.0f)        cfg.soc_alert_pct = 99.0f;
    if (cfg.soh_alert_pct < 0.0f)         cfg.soh_alert_pct = 0.0f;
    if (cfg.soh_alert_pct > 99.0f)        cfg.soh_alert_pct = 99.0f;
    if (cfg.temp_high_c > 100.0f)         cfg.temp_high_c = 100.0f;
    if (cfg.temp_low_c  < -40.0f)         cfg.temp_low_c  = -40.0f;
    if (cfg.cell_delta_mv < 1.0f)         cfg.cell_delta_mv = 1.0f;
    if (cfg.cell_delta_mv > 5000.0f)      cfg.cell_delta_mv = 5000.0f;
#if ENABLE_ACS758
    // ACS758 zero-point: 1.0–4.0 V spans all practical VCC-ratiometric offsets.
    if (cfg.acs758_zero_v < 1.0f)         cfg.acs758_zero_v = 1.0f;
    if (cfg.acs758_zero_v > 4.0f)         cfg.acs758_zero_v = 4.0f;
    // Sensitivity: 1–100 mV/A covers the full ACS758 variant range (5–40 mV/A).
    if (cfg.acs758_mv_per_a < 1.0f)       cfg.acs758_mv_per_a = 1.0f;
    if (cfg.acs758_mv_per_a > 100.0f)     cfg.acs758_mv_per_a = 100.0f;
#endif
}

// ─────────────────────────────────────────────────────────────────────────────
// readPackVI — read pack bus voltage (V) and current (A).
//
// Bus voltage (all builds): INA228 readBusVoltage() returns voltage at VIN+
// relative to GND — i.e., the full pack bus voltage (nominally 48 V).
//
// Current (ENABLE_ACS758 0, BENCH_ONLY 0 — primary field path):
//   INA228 readCurrent() through an external precision shunt wired to
//   VIN+/VIN−.  setShunt() is called with DEFAULT_SHUNT_MOHM and
//   DEFAULT_SHUNT_MAX_A at startup; update those constants to match the
//   installed shunt before building.  The external shunt is inline on the
//   traction conductor; VIN+ connects to the shunt high side and VIN− to the
//   shunt low side.
//
// Current (ENABLE_ACS758 0, BENCH_ONLY 1 — bench path):
//   INA228 readCurrent() through the onboard 15 mΩ shunt — ≤ 10 A continuous.
//   VIN− connects to the bench-load positive terminal so current flows through
//   the shunt.  Do NOT use at real traction-pack currents (50–200 A).
//
// Current (ENABLE_ACS758 1, BENCH_ONLY 0 — alternative field path):
//   ACS758LCB-200B-PFF-T on A1.  The sensor is powered at 5 V from the Pololu
//   supply rail; its ratiometric output at VCC = 5 V is cfg.acs758_zero_v
//   (nominal 2.5 V) at 0 A with cfg.acs758_mv_per_a (nominal 10 mV/A)
//   sensitivity, reaching ~4.5 V at +200 A (above the 3.3 V ADC rail).
//   A 10 kΩ / 20 kΩ voltage divider on VOUT scales by 2/3 so the maximum
//   ADC input is ~3.0 V.
//   Conversion: curA = (VADC × 1.5 − acs758_zero_v) / (acs758_mv_per_a × 0.001)
//   Resolution at nominal sensitivity: one ADC count ≈ 0.121 A.
//   Calibrate acs758_zero_v via Notehub after installation with zero current
//   flowing to eliminate Hall-sensor offset error from throughput / SoH accounting.
//   Both INA228 VIN+ and VIN− connect to pack positive in this path so the
//   onboard shunt carries no traction current.
// ─────────────────────────────────────────────────────────────────────────────
bool readPackVI(float &packV, float &curA) {
    packV = ina228.readBusVoltage();

#if ENABLE_ACS758
    // Alternative field path: ACS758LCB-200B-PFF-T on A1.
    // VCC = 5 V; VIOUT = cfg.acs758_zero_v + (I × cfg.acs758_mv_per_a mV/A)
    // 10 kΩ / 20 kΩ divider scales VIOUT by 2/3 → VADC = VIOUT × (2/3)
    // Undo divider: VIOUT = VADC × 1.5
    // curA = (VADC × 1.5 − acs758_zero_v) / (acs758_mv_per_a × 0.001)
    // Calibrated values from Notehub env vars correct for Hall-sensor zero-offset
    // error that would otherwise bias the Ah accumulator and corrupt throughput / SoH.
    float vadc_v = analogRead(A1) * (3.3f / 4095.0f);
    curA = (vadc_v * 1.5f - cfg.acs758_zero_v) / (cfg.acs758_mv_per_a * 0.001f);
#else
    // Primary field path (BENCH_ONLY 0): INA228 readCurrent() through external shunt.
    // Bench path (BENCH_ONLY 1): INA228 onboard 15 mΩ shunt (≤ 10 A only).
    // setShunt() was called in setup() with the appropriate calibration values.
    curA = ina228.readCurrent();
#endif

    // Reject out-of-range values that signal a wiring fault or ADC saturation.
    // Upper ceiling is set at the INA228's 85 V common-mode input limit; any
    // reading above 85 V either indicates a wiring fault or a pack that exceeds
    // the hardware's safe operating range and must not be used.
    if (isnan(packV) || packV < 10.0f || packV > 85.0f) return false;
    if (isnan(curA)  || fabsf(curA) > 200.0f)           return false;
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// readPackTempC — NTC thermistor on A0 via the β (Steinhart-Hart simplified)
// equation.  Returns NAN if the ADC reading indicates an open or shorted probe.
// ─────────────────────────────────────────────────────────────────────────────
float readPackTempC(void) {
    float sum = 0.0f;
    for (int i = 0; i < 16; i++) sum += (float)analogRead(PIN_THERMISTOR);
    float raw = sum / 16.0f;
    if (raw < 10.0f || raw > ADC_COUNTS - 10.0f) return NAN;
    float r  = THERM_SERIES_R * raw / (ADC_COUNTS - raw);
    float tk = 1.0f / (1.0f / (THERM_NOMINAL_T + 273.15f) +
                       (1.0f / THERM_BETA) * logf(r / THERM_NOMINAL_R));
    return tk - 273.15f;
}

// ─────────────────────────────────────────────────────────────────────────────
// voltageToSoC — linear interpolation between OCV lookup table entries.
// OCV is only accurate when the pack is at rest; see README §9 for the
// production path that blends OCV with Coulomb counting.
// ─────────────────────────────────────────────────────────────────────────────
float voltageToSoC(float voltage, bool isLithium) {
    const float (*tbl)[2] = isLithium ? SOC_TABLE_LITHIUM : SOC_TABLE_LEAD_ACID;
    const int   n         = isLithium ? SOC_LITHIUM_ROWS  : SOC_LEAD_ACID_ROWS;
    if (voltage >= tbl[0][0])   return tbl[0][1];
    if (voltage <= tbl[n-1][0]) return tbl[n-1][1];
    for (int i = 0; i < n - 1; i++) {
        if (voltage >= tbl[i+1][0]) {
            float slope = (tbl[i][1] - tbl[i+1][1]) / (tbl[i][0] - tbl[i+1][0]);
            return tbl[i+1][1] + slope * (voltage - tbl[i+1][0]);
        }
    }
    return 0.0f;
}

// updateThroughput — sampled Ah accounting: adds |current| × Δt to the
// bidirectional throughput accumulator reported in summary notes, and adds
// discharge-only current × Δt to a separate accumulator used exclusively by
// the SoH estimator.  With the documented high-side wiring, discharge reads as
// positive current; the 0.5 A noise floor prevents idle creep.
// Note: current transients that begin and end within a single sample interval
// are not captured — this is a coarse heuristic accumulator, not a continuous
// coulomb counter.
void updateThroughput(PersistState &s, float curA) {
    const float dt_h = (float)cfg.sample_interval_s / 3600.0f;
    if (fabsf(curA) > 0.5f) s.throughput_ah      += fabsf(curA) * dt_h;
    if (curA         > 0.5f) s.cycle_discharge_ah += curA        * dt_h;
}

// updateSoH — update rolling SoH when a full charge cycle completes.
// A cycle is defined as: SoC dips below 30%, then recovers above 90%.
// Only the discharge portion of the cycle (cycle_discharge_ah) is used as the
// measured pack capacity; counting regenerated charge Ah would overstate
// capacity and inflate SoH.  EWMA α = 0.3 smooths over noisy partial cycles.
void updateSoH(PersistState &s, float socPct) {
    if (socPct < 30.0f && !s.soh_cycle_started) s.soh_cycle_started = true;
    if (!s.soh_cycle_started || socPct < 90.0f) return;

    s.soh_cycle_started = false;
    // Use discharge-only Ah for capacity; fall back to the full throughput
    // accumulator only if the discharge accumulator is implausibly small (e.g. legacy state).
    float measuredAh = (s.cycle_discharge_ah > 0.1f)
                       ? s.cycle_discharge_ah : s.throughput_ah;
    if (measuredAh < cfg.rated_cap_ah * 0.3f) {
        s.throughput_ah = s.cycle_discharge_ah = 0.0f;
        return;  // not a meaningful full cycle; skip update
    }
    s.measured_cap_ah = (s.measured_cap_ah < 1.0f)
        ? measuredAh
        : 0.7f * s.measured_cap_ah + 0.3f * measuredAh;
    float newSoH = (s.measured_cap_ah / cfg.rated_cap_ah) * 100.0f;
    s.soh_pct    = (newSoH > 100.0f) ? 100.0f : newSoH;
    Serial.print("[soh] cycle complete, discharge="); Serial.print(measuredAh, 1);
    Serial.print(" Ah, soh="); Serial.println(s.soh_pct, 1);
    s.throughput_ah = s.cycle_discharge_ah = 0.0f;
}

// ─────────────────────────────────────────────────────────────────────────────
// sendAlert — compact alert note with sync:true for immediate transmission.
// sync:true causes the Notecard to establish a cellular session (or Skylo NTN
// if cellular is unavailable) immediately rather than waiting for the next
// outbound window.
//
// extra_v is always emitted so every note matches the fixed compact template
// shape exactly.  Pass 0.0f when there is no meaningful auxiliary value.
// ─────────────────────────────────────────────────────────────────────────────
void sendAlert(const char *alert, float packV, float socPct, float tempC,
               float extraV) {
    J *req = notecard.newRequest("note.add");
    JAddStringToObject(req, "file", "battery_alert.qo");
    // port is bound in note.template (ALERT_PORT); it is not a valid note.add field.
    JAddBoolToObject(req, "sync", true);
    J *body = JAddObjectToObject(req, "body");
    JAddStringToObject(body, "alert",   alert);
    JAddNumberToObject(body, "pack_v",  packV);
    JAddNumberToObject(body, "soc_pct", (int)socPct);
    // -9999.0 is the machine-readable missing-data sentinel for an invalid or
    // absent thermistor reading — unambiguously distinct from a real 0°C pack.
    JAddNumberToObject(body, "temp_c",  isnan(tempC) ? -9999.0f : tempC);
    JAddNumberToObject(body, "extra_v", extraV);
    if (!notecard.sendRequestWithRetry(req, 10)) {
        Serial.print("[warn] alert note failed: "); Serial.println(alert);
    } else {
        Serial.print("[alert] "); Serial.println(alert);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// checkAlerts — evaluate all threshold rules with per-alert 30-min cooldown.
// Each alert type has its own independent cooldown field so one condition
// cannot suppress another (e.g. temp_high no longer blocks temp_low).
//
// When the Notecard clock is not yet set (now == 0), unsigned epoch arithmetic
// would underflow and either spam every wake or suppress everything.  Instead,
// each alert is allowed to fire once before time sync (when the epoch field is
// still 0 / "never sent"), then uses normal time-based cooldown thereafter.
// This ensures critical pre-sync alerts (e.g. a dead-flat battery on first
// power-on) reach the fleet rather than being silently dropped.
// ─────────────────────────────────────────────────────────────────────────────
void checkAlerts(PersistState &s, float packV, float socPct,
                 float tempC, uint32_t now) {
    // ALERT_DUE: returns true when an alert is eligible to fire.
    //   now > 0: eligible if never fired (ep == 0) OR the cooldown has elapsed
    //            since the last fire.  The explicit ep == 0 check ensures the
    //            very first alert after time-sync fires immediately and is never
    //            held back by the 30-minute cooldown window.
    //   now == 0 (no time sync yet): eligible only if never fired (ep == 0).
    // MARK_ALERT: stores now when epoch is known; stores sentinel 1 when not (1 is
    //   effectively epoch 1970-01-01 00:00:01 — distinct from 0 so a later
    //   ep == 0 check does not re-open the gate for a pre-sync alert that
    //   already fired, while also correctly re-entering the cooldown comparison
    //   once a real epoch is available).
#define ALERT_DUE(ep)  (now > 0 ? ((ep) == 0 || now - (ep) > ALERT_COOLDOWN_SEC) \
                                 : ((ep) == 0))
#define MARK_ALERT(ep) do { (ep) = (now > 0 ? now : 1u); } while (0)

    if (socPct < cfg.soc_alert_pct && ALERT_DUE(s.alert_epoch_soc)) {
        MARK_ALERT(s.alert_epoch_soc);
        sendAlert("soc_low", packV, socPct, tempC, 0.0f);
    }
    if (!isnan(tempC)) {
        if (tempC > cfg.temp_high_c && ALERT_DUE(s.alert_epoch_temp_hi)) {
            MARK_ALERT(s.alert_epoch_temp_hi);
            sendAlert("temp_high", packV, socPct, tempC, tempC);
        }
        if (tempC < cfg.temp_low_c && ALERT_DUE(s.alert_epoch_temp_lo)) {
            MARK_ALERT(s.alert_epoch_temp_lo);
            sendAlert("temp_low", packV, socPct, tempC, tempC);
        }
    }
    if (s.soh_pct < cfg.soh_alert_pct && s.measured_cap_ah > 1.0f &&
        ALERT_DUE(s.alert_epoch_soh)) {
        MARK_ALERT(s.alert_epoch_soh);
        sendAlert("soh_low", packV, socPct, tempC, s.soh_pct);
    }

#undef ALERT_DUE
#undef MARK_ALERT
}

// ─────────────────────────────────────────────────────────────────────────────
// sendSummary — template-encoded hourly status note queued for normal outbound.
// Accumulators are cleared only after the note is successfully handed to the
// Notecard so a transient failure retries the same window on the next wake.
// After MAX_SUMM_RETRIES consecutive failures the window is discarded to
// prevent unbounded accumulator growth during a prolonged fault.
//
// Return value (SummaryResult):
//   SUMM_QUEUED    — note handed to Notecard; accumulators cleared.
//   SUMM_RETAINED  — transient failure; window kept; caller must NOT zero
//                    wakes_since_summ so the retry fires on the next wake.
//   SUMM_DISCARDED — retry ceiling reached; window discarded; caller may
//                    zero wakes_since_summ to open a fresh window.
// ─────────────────────────────────────────────────────────────────────────────
SummaryResult sendSummary(PersistState &s, uint32_t now) {
    if (s.summ_count == 0) {
        // No valid samples accumulated in this window (all wakes aborted due to
        // sensor fault or out-of-range reads).  Advance last_summ_epoch to open
        // a fresh full report interval rather than firing immediately on the
        // first recovered sample with only a partial window of data.
        s.last_summ_epoch = now;
        return SUMM_QUEUED;  // no note emitted; window reset
    }

    float avgV = s.summ_v_sum / s.summ_count;
    float avgI = s.summ_i_sum / s.summ_count;
    // -9999.0 is the machine-readable missing-data sentinel for "no valid
    // temperature samples in this window" — unambiguously distinct from 0°C.
    float avgT = (s.summ_t_count > 0) ? s.summ_t_sum / s.summ_t_count : -9999.0f;

#if ENABLE_CAN_BMS
    bool canOk = gCanOk;
#else
    bool canOk = false;
#endif

    J *req = notecard.newRequest("note.add");
    JAddStringToObject(req, "file", "battery_status.qo");
    // port is bound in note.template (STATUS_PORT); it is not a valid note.add field.
    J *body = JAddObjectToObject(req, "body");
    JAddNumberToObject(body, "pack_v",  avgV);
    JAddNumberToObject(body, "cur_a",   avgI);
    JAddNumberToObject(body, "temp_c",  avgT);
    JAddNumberToObject(body, "soc_pct", (int)s.soc_pct);
    JAddNumberToObject(body, "soh_pct", (int)s.soh_pct);
    JAddNumberToObject(body, "throughput_ah", s.throughput_ah);
    JAddBoolToObject(body,   "can_ok",  canOk);

    bool sent = notecard.sendRequestWithRetry(req, 5);
    if (!sent) {
        s.summ_fail_count++;
        if (s.summ_fail_count < MAX_SUMM_RETRIES) {
            Serial.print("[warn] summary note failed — retaining window for retry (attempt ");
            Serial.print(s.summ_fail_count);
            Serial.println(")");
            return SUMM_RETAINED;  // leave last_summ_epoch unchanged; window retries next wake
        }
        Serial.print("[warn] summary note failed ");
        Serial.print(s.summ_fail_count);
        Serial.println("x — discarding stale window to prevent accumulator overflow");
    } else {
        s.summ_fail_count = 0;
        Serial.print("[summary] soc="); Serial.print(s.soc_pct, 0);
        Serial.print("% soh="); Serial.print(s.soh_pct, 0);
        Serial.print("% thr="); Serial.print(s.throughput_ah, 1);
        Serial.print("Ah v="); Serial.println(avgV, 1);
    }

    // Clear accumulators: reached here on success (→ QUEUED) or after hitting
    // the retry ceiling (→ DISCARDED) — both cases open a fresh window.
    SummaryResult result = sent ? SUMM_QUEUED : SUMM_DISCARDED;
    s.summ_v_sum = s.summ_i_sum = s.summ_t_sum = 0.0f;
    s.summ_count = s.summ_t_count = 0;
    s.summ_fail_count = 0;
    s.last_summ_epoch = now;
    return result;
}

// ─────────────────────────────────────────────────────────────────────────────
// CAN BMS polling — compiled only when ENABLE_CAN_BMS == 1.
// Update BMS_CELL_GROUP_ID and parseCellGroupFrame() to match your BMS vendor's
// CAN message layout. Many rental-equipment BMS units publish cell-group
// voltages on SAE J1939 PGNs at 250 kbps; consult the machine's service manual.
// ─────────────────────────────────────────────────────────────────────────────
#if ENABLE_CAN_BMS
// parseCellGroupFrame — fill gCellMv[] from a CAN frame and return the number
// of cell-group values actually decoded.  A classic CAN frame carries at most
// 8 data bytes (DLC ≤ 8), so at most four 16-bit big-endian values fit in one
// frame — regardless of BMS_CELL_COUNT.  BMS_CELL_COUNT is the array capacity
// for the logical group count the application tracks; if the BMS uses a
// vendor-specific multi-frame or segmented protocol, this function must be
// rewritten to reassemble complete messages before decoding cell voltages.
// MUST be updated to match the actual BMS CAN protocol for your pack.
static int parseCellGroupFrame(const struct can_frame &f) {
    int count = 0;
    for (int i = 0; i < BMS_CELL_COUNT && (i * 2 + 1) < (int)f.can_dlc; i++) {
        gCellMv[i] = (float)((f.data[i * 2] << 8) | f.data[i * 2 + 1]);
        count++;
    }
    return count;
}

void pollCanBms(PersistState &s, uint32_t now) {
    // s.can_err_epoch (PersistState field) is used instead of a static local so
    // the once-per-hour rate limit survives the sleep/hard-reset cycle.
    struct can_frame frame;
    gCanOk = false;
    int parsedCells = 0;

    // Listen for at least one full BMS broadcast period.  Many rental-equipment
    // BMS units publish cell-group frames every 500 ms on a passive broadcast
    // bus; a 50 ms window frequently misses all frames in a wake cycle.
    // A 1000 ms window covers two typical broadcast periods.
    unsigned long deadline = millis() + 1000UL;
    while (millis() < deadline) {
        if (mcp2515.readMessage(&frame) != MCP2515::ERROR_OK) {
            delay(1);   // no frame yet — yield briefly and keep polling
            continue;
        }
        // Extended frames carry the CAN_EFF_FLAG bit in can_id; compare the
        // 29-bit ID separately rather than testing the raw value directly.
        if ((frame.can_id & CAN_EFF_FLAG) &&
            (frame.can_id & CAN_EFF_MASK) == BMS_CELL_GROUP_ID) {
            parsedCells = parseCellGroupFrame(frame);
            gCanOk = (parsedCells > 0);
        }
    }

    // Rate-limited CAN error event (once per hour) so a powered-off BMS
    // doesn't flood the Notefile with noise.  Guard on now > 0: when the
    // Notecard clock is unset, unsigned subtraction (0 - s.can_err_epoch) would
    // wrap and fire spuriously on every wake.
    if (!gCanOk && now > 0 && now - s.can_err_epoch > 3600) {
        s.can_err_epoch = now;
        J *req = notecard.newRequest("note.add");
        JAddStringToObject(req, "file", "battery_alert.qo");
        // port is bound in note.template (ALERT_PORT); it is not a valid note.add field.
        J *body = JAddObjectToObject(req, "body");
        JAddStringToObject(body, "alert",   "can_error");
        JAddNumberToObject(body, "pack_v",  0.0f);
        JAddNumberToObject(body, "soc_pct", (int)s.soc_pct);
        // -9999.0: thermistor is not read inside CAN poll — use missing-data sentinel.
        JAddNumberToObject(body, "temp_c",  -9999.0f);
        JAddNumberToObject(body, "extra_v", 0.0f);
        if (!notecard.sendRequestWithRetry(req, 5)) {
            Serial.println("[warn] can_error note failed");
        }
    }

    // Need at least 2 cells to compute a meaningful imbalance delta.
    // Also requires a valid epoch for cooldown arithmetic.
    if (!gCanOk || parsedCells < 2 || now == 0) return;

    // Cell imbalance check — only across cells that were actually received
    float minMv = gCellMv[0], maxMv = gCellMv[0];
    for (int i = 1; i < parsedCells; i++) {
        if (gCellMv[i] < minMv) minMv = gCellMv[i];
        if (gCellMv[i] > maxMv) maxMv = gCellMv[i];
    }
    float delta = maxMv - minMv;
    // First-fire logic matches checkAlerts(): ep == 0 means never sent; thereafter
    // apply the 30-minute cooldown.  (now > 0 is already guaranteed by the early
    // return above, so the ep == 0 branch handles the very first imbalance alert
    // without waiting a full cooldown period after the device first gets a clock.)
    if (delta > cfg.cell_delta_mv &&
        (s.alert_epoch_imb == 0 || now - s.alert_epoch_imb > ALERT_COOLDOWN_SEC)) {
        s.alert_epoch_imb = now;
        J *req = notecard.newRequest("note.add");
        JAddStringToObject(req, "file", "battery_alert.qo");
        // port is bound in note.template (ALERT_PORT); it is not a valid note.add field.
        JAddBoolToObject(req, "sync", true);
        J *body = JAddObjectToObject(req, "body");
        JAddStringToObject(body, "alert",   "cell_imbalance");
        JAddNumberToObject(body, "pack_v",  0.0f);
        JAddNumberToObject(body, "soc_pct", (int)s.soc_pct);
        // -9999.0: thermistor is not read inside CAN poll — use missing-data sentinel.
        JAddNumberToObject(body, "temp_c",  -9999.0f);
        JAddNumberToObject(body, "extra_v", delta);
        if (!notecard.sendRequestWithRetry(req, 10)) {
            Serial.println("[warn] cell_imbalance note failed");
        } else {
            Serial.print("[alert] cell_imbalance delta="); Serial.println(delta, 0);
        }
    }
}
#endif  // ENABLE_CAN_BMS
