// diesel_gen_monitor.ino
//
// Legacy Diesel Generator Fleet Performance Uplift
// Active-Alarm Monitoring, Firmware-Observed Alarm Chronology, and Run-Data Telemetry
// Target hardware: Arduino OPTA RS485 + Blues Wireless for OPTA
// Notecard:        Cell+WiFi (NOTE-WBNAW), I2C via expansion's AUX connector
//
// Polls seven Modbus holding registers from a DeepSea-, Woodward-, or compatible
// generator controller on a configurable cadence, accumulates rolling statistics,
// evaluates alert rules locally, and emits summary + event Notes via the Notecard.
//
// Register addresses default to a contiguous illustrative map. Real controller
// families (DeepSea 7000, Woodward EasyGen, Kohler, Caterpillar, Cummins) each
// have their own Modbus map — override with reg_* environment variables on Notehub.
//
// Runtime cadence (defaults):
//   - Modbus poll:  every 1 minute   (sample_minutes)
//   - Summary note: every 60 minutes (report_minutes)  <- batched, periodic sync
//   - Alert note:   immediate on rule trigger           <- sync:true, bypasses interval
//
// Helper functions (Modbus polling, env-var overrides, rule evaluation) live in
// diesel_gen_monitor_helpers.h / .cpp to keep this file readable.
//
// Dependencies:
//   - Blues Wireless Notecard (note-arduino)  — install via Arduino Library Manager
//   - ArduinoModbus, ArduinoRS485             — install via Arduino Library Manager
//   - Arduino Mbed OS Opta Boards (core)      — install via Arduino Boards Manager

#include <Arduino.h>
#include <Notecard.h>
#include <ArduinoRS485.h>
#include <ArduinoModbus.h>
#include "diesel_gen_monitor_helpers.h"

// -------- Project configuration ---------------------------------------------------------
// Replace with the ProductUID from your Notehub project. See:
//   https://dev.blues.io/notehub/notehub-walkthrough/#finding-a-productuid
#ifndef PRODUCT_UID
#define PRODUCT_UID  "com.your-company.your-name:diesel_gen_monitor"
#pragma message "PRODUCT_UID not set. Define it here or set it in Notehub before deploying."
#endif

// Expose PRODUCT_UID as a runtime global so helpers.cpp can reference it in
// applyHubSetIfChanged() without requiring the macro to be in scope there.
const char *g_product_uid = PRODUCT_UID;

#define usbSerial   Serial
#define DEBUG_BAUD  115200

// -------- Default env-var values --------------------------------------------------------
// Every value below is overridable via a Notehub environment variable of the same name.
// Register-address defaults are illustrative; see README §5 and §9 for the production
// path with real controller Modbus maps.

uint32_t g_sample_minutes         = 1;   // minutes between Modbus polls; adopted at window close
uint32_t g_pending_sample_minutes = 1;   // fetched from Notehub; promoted to g_sample_minutes at next window close
uint32_t g_report_minutes         = 60;  // minutes between gen_summary.qo notes
uint8_t  g_modbus_slave_id   = 1;       // Modbus server (slave) address
uint32_t g_modbus_baud       = 19200;   // bus baud rate
char     g_modbus_parity[8]  = "none";  // "none" | "even" | "odd"
uint8_t  g_modbus_stop_bits  = 1;       // 1 or 2

// Holding-register addresses (0-based wire-level Modbus addressing).
// Illustrative contiguous defaults — real controllers differ, use reg_* env vars.
uint16_t g_reg_engine_rpm    = 768;  // Engine speed in RPM
uint16_t g_reg_fuel_pct      = 769;  // Fuel level 0–100%
uint16_t g_reg_load_pct      = 770;  // Generator load 0–100%
uint16_t g_reg_oil_kpa       = 771;  // Oil pressure in kPa
uint16_t g_reg_coolant_c     = 772;  // Coolant temperature in °C (signed)
uint16_t g_reg_run_hours     = 773;  // Cumulative engine hours (16-bit demo; see §9 for 32-bit)
uint16_t g_reg_alarm_word    = 774;  // Active alarm bitmask (0 = no alarms)

// Alert thresholds — all tunable via Notehub env vars without reflashing.
float    g_fuel_low_pct      = 25.0f;   // % below which fuel_low fires
float    g_coolant_alarm_c   = 95.0f;   // °C above which coolant_overtemp fires
float    g_oil_low_kpa       = 138.0f;  // kPa (~20 psi) below which oil_low_pressure fires while running
uint16_t g_alarm_mask_fts    = 0;       // bitmask applied to alarm_word to detect failure-to-start;
                                        //   0 disables (default). Example: 0x0001 if bit 0 = FTS on your controller.
uint16_t g_rpm_running       = 100;     // RPM above which engine is considered running

// -------- Shared state ------------------------------------------------------------------
Notecard notecard;
RollingStats stats;

// Persistent alarm state. Survives stats.reset() at the report boundary so a fault
// active across two windows isn't re-counted as a new event.
uint16_t g_current_alarm_word  = 0;

// Engine-running state for start-event rising-edge detection.
bool g_engine_was_running      = false;

// Edge-trigger flags: each alert fires once on the rising edge and rearms when
// the condition clears. Prevents a slow drift from re-alerting every report interval.
bool g_active_fuel_low         = false;
bool g_active_coolant_overtemp = false;
bool g_active_oil_low          = false;
bool g_active_controller_alarm = false;
bool g_active_fts              = false;

// History-logged flags: track whether a logAlarmHistory() entry has been written for
// the current assertion period, independently of whether sendEvent() has succeeded.
// Decoupling history-logged state from event-queued state prevents duplicate ring-buffer
// entries when sendEvent() fails across multiple samples — the assertion is recorded once
// on first detection and the matching clearance is recorded once when the alarm word
// returns to zero.
bool g_alarm_logged_controller = false;
bool g_alarm_logged_fts        = false;

// Most-recent valid sample — carried into modbus_unreachable events for context.
GenSample g_last_known_sample  = {};

// Modbus and hub.set change-detection.
uint32_t g_last_modbus_baud    = 0;
uint16_t g_last_serial_cfg     = 0;
uint32_t g_last_hubset_outbound = 0;

// True once a full hub.set (product + mode + outbound + inbound) has been confirmed
// by the Notecard. applyHubSetIfChanged() re-sends the complete request until this
// is set, ensuring a factory-fresh Notecard that missed notecardConfigure() at boot
// eventually enters periodic mode without requiring a power cycle.
bool     g_hub_provisioned     = false;

// Tracks whether ModbusRTUClient.begin() has succeeded at least once since boot
// (or since the last serial-config change). False drives sample-cadence retries
// in applyModbusSerialIfChanged() until bring-up succeeds.
bool     g_modbus_ready        = false;

// Millis-based next-deadline scheduler state.
// Storing the absolute next fire time (rather than the last fire time) prevents
// long-term drift: each deadline advances by exactly one interval so accumulated
// loop jitter is not folded into every subsequent period.
// Both values are initialised in setup() once g_sample_minutes and g_report_minutes
// are known; the static-zero default would cause them to fire on the first loop() call.
static uint32_t next_sample_ms       = 0;  // initialised in setup()
static uint32_t next_report_ms       = 0;  // initialised in setup()
static uint32_t last_modbus_alert_ms = 0;
static bool     g_modbus_alert_sent  = false;
// Guards engine-start rising-edge detection on the first valid poll after boot or
// watchdog reset. The engine may already be running when the monitor powers up, so
// the first sample seeds g_engine_was_running without incrementing engine_starts.
static bool     g_first_sample_seen  = false;

// -------- Alarm history -----------------------------------------------------------------
// Firmware-observed alarm chronology: a ring buffer of the last ALARM_HISTORY_DEPTH
// alarm-word transitions (assertions and clearances) observed during normal polling.
// Flushed as gen_alarm_log.qo at each report boundary (after evaluateRules, before
// sendSummary), then cleared on confirmed note.add.
//
// This is firmware-observed history — the record of every alarm event the firmware
// has detected since the last flush. It is distinct from the controller's own internal
// latched alarm log (a vendor-specific multi-register operation; see README §9), which
// can contain events that pre-date the monitor's installation.
#define ALARM_HISTORY_DEPTH 8
struct AlarmHistoryEntry {
    uint32_t elapsed_s;   // seconds since boot (millis()/1000) at time of event
    uint16_t alarm_word;  // alarm_word bits asserted at event; 0 for clearance entries
    char     alert[24];   // "controller_alarm", "alarm_clear",
                          //   "failure_to_start", or "fts_clear"
};
static AlarmHistoryEntry g_alarm_history[ALARM_HISTORY_DEPTH];
static uint8_t g_alarm_history_count = 0;  // valid entries currently stored (0..DEPTH)
static uint8_t g_alarm_history_head  = 0;  // next-write index (circular)

// -------- Notecard setup helpers --------------------------------------------------------

static void notecardConfigure() {
    // Periodic mode: queue notes locally and ship them in a single cellular session
    // once per report window. Alert notes set sync:true and ship within the session-
    // establishment window (~15–60 s) after the trigger condition is detected.
    J *req = notecard.newRequest("hub.set");
    if (req) {
        JAddStringToObject(req, "product",  PRODUCT_UID);
        JAddStringToObject(req, "mode",     "periodic");
        JAddNumberToObject(req, "outbound", g_report_minutes);
        JAddNumberToObject(req, "inbound",  g_report_minutes * 2);
        // sendRequestWithRetry handles the cold-boot I2C race where the host MCU
        // comes up before the Notecard is ready to receive its first transaction.
        // Both flags are updated only on confirmed success. If this call fails,
        // g_hub_provisioned stays false and applyHubSetIfChanged() will retry the
        // full hub.set (including product + mode) at the next report boundary,
        // ensuring a factory-fresh Notecard eventually enters periodic mode.
        if (notecard.sendRequestWithRetry(req, 5)) {
            g_hub_provisioned      = true;
            g_last_hubset_outbound = g_report_minutes;
        }
    }
}

static void defineTemplates() {
    // Note templates store records as fixed-length binary rather than free-form JSON,
    // shrinking on-wire payload 3–5x. A fleet of 50 generators sending hourly summaries
    // accumulates ~435,000 notes per year — template compression keeps that well inside
    // the included 500 MB prepaid data allowance.
    //
    // Each template is retried up to MAX_TRIES times using sendRequestWithRetry(), which
    // internally handles the cold-boot I2C race where the MCU comes up before the
    // Notecard's I2C stack is ready. If the template definition still fails after all
    // retries, note.add calls will be silently dropped by the Notecard until setup()
    // is re-run (power cycle / watchdog reset). The Serial log records the failure.
    const uint8_t MAX_TRIES = 5;

    for (uint8_t attempt = 0; attempt < MAX_TRIES; attempt++) {
        J *req = notecard.newRequest("note.template");
        if (!req) { delay(500); continue; }
        JAddStringToObject(req, "file", "gen_summary.qo");
        JAddNumberToObject(req, "port", 50);
        J *body = JAddObjectToObject(req, "body");
        JAddNumberToObject(body, "fuel_pct",       14.1);  // float: mean fuel (%)
        JAddNumberToObject(body, "load_pct",       14.1);  // float: mean load while running (%)
        JAddNumberToObject(body, "load_pct_peak",  14.1);  // float: peak load (%)
        JAddNumberToObject(body, "oil_kpa_mean",   14.1);  // float: mean oil pressure while running (kPa)
        JAddNumberToObject(body, "oil_kpa_peak",   14.1);  // float: peak oil pressure (kPa)
        JAddNumberToObject(body, "coolant_c_mean", 12);    // int16: mean coolant temp (°C)
        JAddNumberToObject(body, "coolant_c_peak", 12);    // int16: peak coolant temp (°C)
        JAddNumberToObject(body, "run_hours",      24);    // uint32: cumulative engine hours
        JAddNumberToObject(body, "run_min",        12);    // int16: minutes engine ran this window
        JAddNumberToObject(body, "stop_min",       12);    // int16: minutes engine was stopped
        JAddNumberToObject(body, "engine_starts",  21);    // uint8: start events this window
        JAddNumberToObject(body, "alarm_word",       22);    // uint16: latest alarm-word polled this window;
                                                           //   carries last-known pre-blackout value when data_ok=0
        JAddNumberToObject(body, "alarm_word_stale", 21);  // uint8: 1 when alarm_word carries a last-known value
                                                           //   from before a complete telemetry blackout (data_ok=0);
                                                           //   0 when alarm_word is from this window's polls
        JAddNumberToObject(body, "samples_ok",     22);    // uint16: successful Modbus polls this window
        JAddNumberToObject(body, "samples_failed", 22);    // uint16: failed polls — enables downstream
                                                           //   analytics to distinguish 'stopped' from
                                                           //   'no telemetry' in run_min / stop_min
        JAddNumberToObject(body, "data_ok",        21);    // uint8: 1 = at least one valid sample this window,
                                                           //   0 = complete telemetry blackout; consumers should
                                                           //   treat all measurement fields as undefined when 0
        if (notecard.sendRequestWithRetry(req, 5)) break;
        Serial.println("[notecard] note.template gen_summary.qo failed; retrying");
        delay(1000);
    }

    for (uint8_t attempt = 0; attempt < MAX_TRIES; attempt++) {
        J *req = notecard.newRequest("note.template");
        if (!req) { delay(500); continue; }
        JAddStringToObject(req, "file", "gen_event.qo");
        JAddNumberToObject(req, "port", 51);
        J *body = JAddObjectToObject(req, "body");
        JAddStringToObject(body, "alert",             "modbus_unreachable");  // string: exemplar = longest alert name (18 chars)
        JAddNumberToObject(body, "engine_rpm",        22);    // uint16: RPM (last-known sample)
        JAddNumberToObject(body, "fuel_pct",          14.1);  // float: fuel % (last-known sample)
        JAddNumberToObject(body, "load_pct",          14.1);  // float: load % (last-known sample)
        JAddNumberToObject(body, "oil_kpa",           14.1);  // float: oil kPa (last-known sample)
        JAddNumberToObject(body, "coolant_c",         12);    // int16: coolant °C (last-known sample)
        JAddNumberToObject(body, "alarm_word",        22);    // uint16: alarm bitmask at trigger
        JAddNumberToObject(body, "run_hours",         24);    // uint32: cumulative hours at trigger
        // trigger_val / trigger_threshold carry the window aggregate or peak that fired a
        // report-window rule (fuel_low, coolant_overtemp, oil_low_pressure) and its configured
        // limit. For per-poll alerts (controller_alarm, failure_to_start, modbus_unreachable)
        // both fields are -1.0 — the sample fields already explain why the alert fired.
        JAddNumberToObject(body, "trigger_val",       14.1);  // float: triggering aggregate/peak; -1.0 for per-poll alerts
        JAddNumberToObject(body, "trigger_threshold", 14.1);  // float: configured threshold; -1.0 for per-poll alerts
        if (notecard.sendRequestWithRetry(req, 5)) break;
        Serial.println("[notecard] note.template gen_event.qo failed; retrying");
        delay(1000);
    }
}

// -------- Alarm history helpers ---------------------------------------------------------

// Appends one alarm transition entry to the ring buffer. When the buffer is full
// the oldest entry is silently overwritten — in practice, 8 slots cover an entire
// report window even under repeated intermittent faults.
static void logAlarmHistory(const char *alert, uint16_t alarm_word) {
    AlarmHistoryEntry &e = g_alarm_history[g_alarm_history_head];
    e.elapsed_s  = millis() / 1000UL;
    e.alarm_word = alarm_word;
    strncpy(e.alert, alert, sizeof(e.alert) - 1);
    e.alert[sizeof(e.alert) - 1] = '\0';
    g_alarm_history_head = (g_alarm_history_head + 1) % ALARM_HISTORY_DEPTH;
    if (g_alarm_history_count < ALARM_HISTORY_DEPTH) g_alarm_history_count++;
}

// Emits accumulated alarm-history entries as gen_alarm_log.qo and clears the local
// buffer. Called at each report boundary after evaluateRules() and before sendSummary().
//
// gen_alarm_log.qo is NOT template-backed: the events array is variable-length and
// cannot be expressed as a fixed Notecard template. Full JSON is used instead;
// this is acceptable because alarm events are rare relative to the hourly flush cadence.
//
// A failed note.add leaves the buffer intact so its entries are included in the next
// window — no alarm history is silently dropped on a transient I²C or Notecard failure.
static void flushAlarmHistory() {
    if (g_alarm_history_count == 0) return;

    J *req = notecard.newRequest("note.add");
    if (!req) return;
    JAddStringToObject(req, "file", "gen_alarm_log.qo");
    J *body = JAddObjectToObject(req, "body");
    JAddNumberToObject(body, "count", g_alarm_history_count);
    J *events = JAddArrayToObject(body, "events");

    // Emit in chronological order (oldest first).
    // When the buffer is full, g_alarm_history_head is the oldest slot (the
    // next-write position overwrote it). When not full, index 0 is oldest.
    const uint8_t start = (g_alarm_history_count == ALARM_HISTORY_DEPTH)
                          ? g_alarm_history_head : 0;
    for (uint8_t i = 0; i < g_alarm_history_count; i++) {
        const uint8_t idx = (start + i) % ALARM_HISTORY_DEPTH;
        const AlarmHistoryEntry &e = g_alarm_history[idx];
        J *entry = JCreateObject();
        if (entry) {
            JAddStringToObject(entry, "alert",      e.alert);
            JAddNumberToObject(entry, "alarm_word", e.alarm_word);
            JAddNumberToObject(entry, "elapsed_s",  e.elapsed_s);
            JAddItemToArray(events, entry);
        }
    }

    J *rsp = notecard.requestAndResponse(req);
    bool ok = rsp && !notecard.responseError(rsp);
    notecard.deleteResponse(rsp);
    if (ok) {
        g_alarm_history_count = 0;
        g_alarm_history_head  = 0;
    } else {
        Serial.println("[notecard] flushAlarmHistory: note.add failed; history retained for next window");
    }
}

// -------- Note emission -----------------------------------------------------------------

bool sendSummary() {
    // Always close the reporting window, even during a complete telemetry blackout
    // (total == 0). Emitting with samples_ok=0 and samples_failed>0 lets downstream
    // analytics distinguish 'generator stopped' from 'no telemetry'. An early return
    // on total==0 would leave samples_failed un-reset and bleeding into the next window.
    // Returns true on confirmed note.add (stats reset); false on any failure (stats
    // preserved so the caller can retry rather than silently losing the window data).
    const uint32_t total = stats.run_samples + stats.stop_samples;

    J *req = notecard.newRequest("note.add");
    if (!req) {
        // Allocation failure: preserve stats so data is not silently dropped.
        Serial.println("[notecard] sendSummary: alloc failed; stats preserved for next window");
        return false;
    }
    JAddStringToObject(req, "file", "gen_summary.qo");
    J *body = JAddObjectToObject(req, "body");

    // has_data / has_run_data guard all division and drive sentinel vs. real values.
    // -1.0f / -1 are used as explicit no-data sentinels so downstream consumers can
    // distinguish "measured zero" from "no samples taken this window" without having
    // to cross-reference samples_ok. data_ok is the primary validity signal; the -1
    // sentinel makes a blackout window visible at a glance in raw Note payloads.
    const bool  has_data     = (total > 0);
    const bool  has_run_data = (stats.run_samples > 0);

    const float fuel_mean    = has_data     ? stats.fuel_sum / (float)total                  : -1.0f;
    const float load_mean    = has_run_data ? stats.load_sum / (float)stats.run_samples      : -1.0f;
    const float oil_mean     = has_run_data ? stats.oil_sum  / (float)stats.run_samples      : -1.0f;
    const float coolant_mean = has_data     ? (float)stats.coolant_sum / (float)total        : -1.0f;
    // coolant_peak is initialised to INT16_MIN as a comparison sentinel in reset().
    // Substitute -1 when no samples were taken (same sentinel as other measurement
    // fields; INT16_MIN is only used internally as an accumulator guard).
    const int   coolant_peak = has_data     ? (int)stats.coolant_peak                        : -1;

    JAddNumberToObject(body, "data_ok",        has_data ? 1 : 0);
    JAddNumberToObject(body, "fuel_pct",       fuel_mean);
    JAddNumberToObject(body, "load_pct",       load_mean);
    JAddNumberToObject(body, "load_pct_peak",  has_run_data ? stats.load_peak : -1.0f);
    JAddNumberToObject(body, "oil_kpa_mean",   oil_mean);
    JAddNumberToObject(body, "oil_kpa_peak",   has_run_data ? stats.oil_peak  : -1.0f);
    JAddNumberToObject(body, "coolant_c_mean", (int)coolant_mean);
    JAddNumberToObject(body, "coolant_c_peak", coolant_peak);
    // During a complete blackout (has_data=false), stats.run_hours_last and
    // stats.last_alarm_word are zeroed by reset() and never updated this window.
    // Carry forward last-known values instead of emitting zeros that look like
    // genuine readings to downstream consumers.
    // alarm_word_stale=1 flags the alarm_word as pre-window data so consumers can
    // treat it as "last seen before blackout" rather than "currently 0 alarms".
    // run_hours falls back to g_last_known_sample.run_hours (persists across resets);
    // emits 0 only on the very first window before any successful poll ever completed.
    const bool alarm_stale = !has_data;
    JAddNumberToObject(body, "run_hours",        has_data ? stats.run_hours_last
                                                          : (g_last_known_sample.valid
                                                             ? (int)g_last_known_sample.run_hours : 0));
    // run_min / stop_min use stats.sample_minutes_active (the cadence active when this
    // window opened) rather than g_sample_minutes, which is promoted from
    // g_pending_sample_minutes only after the note is confirmed below. This ensures
    // the minute accounting always reflects a single consistent cadence per window.
    JAddNumberToObject(body, "run_min",        (int)(stats.run_samples  * stats.sample_minutes_active));
    JAddNumberToObject(body, "stop_min",       (int)(stats.stop_samples * stats.sample_minutes_active));
    JAddNumberToObject(body, "engine_starts",  stats.engine_starts);
    JAddNumberToObject(body, "alarm_word",       alarm_stale ? (int)g_current_alarm_word
                                                             : (int)stats.last_alarm_word);
    JAddNumberToObject(body, "alarm_word_stale", alarm_stale ? 1 : 0);
    // Coverage fields: let downstream consumers distinguish 'generator stopped'
    // from 'telemetry missing'. samples_ok = run_samples + stop_samples.
    JAddNumberToObject(body, "samples_ok",     (int)total);
    JAddNumberToObject(body, "samples_failed", stats.samples_failed);

    // Confirm acceptance before clearing the window. A failed note.add (transient I2C
    // hiccup, Notecard not ready) keeps stats intact so the data is not silently dropped.
    // No sync:true — this note is batched by the periodic outbound interval.
    J *rsp = notecard.requestAndResponse(req);
    bool added = rsp && !notecard.responseError(rsp);
    notecard.deleteResponse(rsp);
    if (!added) {
        Serial.println("[notecard] sendSummary: note.add failed; stats preserved for next window");
        return false;
    }

    // Promote any pending cadence change and open a fresh accumulation window.
    // A mid-window sample_minutes update deferred by fetchEnvOverrides() takes effect
    // here so run_min / stop_min always reflect one consistent cadence per window.
    g_sample_minutes = g_pending_sample_minutes;
    stats.reset();
    stats.sample_minutes_active = g_sample_minutes;
    return true;
}

// sendEvent emits a gen_event.qo Note with sync:true, which instructs the Notecard
// to bypass the outbound interval and open a cellular session immediately.
//
// Caller determines the sample pointer and optional trigger fields:
//   - Alarm-transition events (controller_alarm, failure_to_start) pass the
//     freshly-polled sample directly — closest available reading to the trigger.
//     trigger_val and trigger_threshold default to -1.0 (not applicable; the sample
//     fields already explain why the alert fired).
//   - modbus_unreachable passes &g_last_known_sample when a valid prior sample
//     exists, or nullptr (fields serialise as zero/default) if no successful poll
//     has been completed yet.
//   - Report-boundary threshold rules (fuel_low, coolant_overtemp, oil_low_pressure)
//     pass &g_last_known_sample as machine-state context PLUS the actual window
//     aggregate or peak that fired the rule (trigger_val) and its configured limit
//     (trigger_threshold). This makes the alert self-explaining: even when the most-
//     recent sample reading has returned within bounds, trigger_val preserves the
//     worst-case value that crossed the threshold. See evaluateRules() in helpers.cpp.
//
// A bounded retry loop defends against transient I2C or Notecard-side failures.
// requestAndResponse() is used (rather than sendRequest()) so the response error
// field can be inspected before the attempt is counted as successful.
bool sendEvent(const char *alert, const GenSample *s,
               float trigger_val, float trigger_threshold) {
    const uint8_t MAX_TRIES = 3;
    for (uint8_t attempt = 0; attempt < MAX_TRIES; attempt++) {
        J *req = notecard.newRequest("note.add");
        if (!req) { delay(100); continue; }
        JAddStringToObject(req, "file", "gen_event.qo");
        JAddBoolToObject  (req, "sync", true);
        J *body = JAddObjectToObject(req, "body");
        JAddStringToObject(body, "alert",             alert);
        JAddNumberToObject(body, "engine_rpm",        s ? s->engine_rpm : 0);
        JAddNumberToObject(body, "fuel_pct",          s ? s->fuel_pct   : 0.0f);
        JAddNumberToObject(body, "load_pct",          s ? s->load_pct   : 0.0f);
        JAddNumberToObject(body, "oil_kpa",           s ? s->oil_kpa    : 0.0f);
        JAddNumberToObject(body, "coolant_c",         s ? (int)s->coolant_c : 0);
        JAddNumberToObject(body, "alarm_word",        s ? s->alarm_word : g_current_alarm_word);
        JAddNumberToObject(body, "run_hours",         s ? s->run_hours  : stats.run_hours_last);
        JAddNumberToObject(body, "trigger_val",       trigger_val);
        JAddNumberToObject(body, "trigger_threshold", trigger_threshold);
        J *rsp = notecard.requestAndResponse(req);
        bool ok = rsp && !notecard.responseError(rsp);
        notecard.deleteResponse(rsp);
        if (ok) return true;
        Serial.print("[notecard] sendEvent attempt ");
        Serial.print(attempt + 1);
        Serial.println(" failed; retrying");
        delay(200);
    }
    Serial.print("[notecard] sendEvent failed after retries: ");
    Serial.println(alert);
    return false;
}

// -------- Arduino entry points ----------------------------------------------------------

void setup() {
    usbSerial.begin(DEBUG_BAUD);
    for (uint32_t t0 = millis(); !usbSerial && millis() - t0 < 3000; ) {}

    notecard.begin();
    notecard.setDebugOutputStream(usbSerial);

    // Order matters: defineTemplates first (before any note.add can be queued),
    // fetchEnvOverrides before notecardConfigure so the first cellular session
    // uses operator-tuned cadences, then bring up Modbus with the tuned serial config.
    // Note: fetchEnvOverrides() + applyModbusSerialIfChanged() are also called on
    // every sample interval so that register-map / baud-rate changes pushed via
    // Notehub inbound sync take effect within one sample period, not at the next
    // report boundary. The boot-time calls here prime the local Notecard cache.
    defineTemplates();
    fetchEnvOverrides();
    // Apply the pending sample_minutes immediately at boot — it is promoted from
    // g_pending_sample_minutes to g_sample_minutes only at sendSummary() thereafter,
    // but at startup there is no open window to protect so we adopt it right away.
    g_sample_minutes = g_pending_sample_minutes;
    notecardConfigure();
    applyModbusSerialIfChanged();

    stats.reset();
    stats.sample_minutes_active = g_sample_minutes;  // record the boot-time polling cadence
    const uint32_t t0_ms = millis();
    next_sample_ms = t0_ms + (uint32_t)g_sample_minutes * 60UL * 1000UL;
    next_report_ms = t0_ms + (uint32_t)g_report_minutes * 60UL * 1000UL;
}

void loop() {
    const uint32_t now = millis();

    // Determine which deadlines have elapsed. Casting to signed int32_t makes the
    // subtraction wrap correctly through the ~49-day millis() rollover.
    const bool sample_due = ((int32_t)(now - next_sample_ms) >= 0);
    const bool report_due = ((int32_t)(now - next_report_ms) >= 0);

    // ── Sample block ─────────────────────────────────────────────────────────
    // Processed BEFORE the report block so that a boundary sample — one whose
    // scheduled time coincides with the report deadline — is included in the
    // closing window's statistics and is visible to evaluateRules() before the
    // window resets. With a 1-minute sample cadence and 60-minute reports, this
    // guarantees all 60 samples contribute to each window's run/stop totals and
    // averages.
    if (sample_due) {
        // Re-read env vars and re-apply Modbus config on every sample interval so
        // that register maps, baud rate, and other controller-specific settings
        // pushed via a Notehub inbound sync take effect within one sample period
        // rather than waiting until the next report boundary. This also retries
        // ModbusRTUClient.begin() whenever g_modbus_ready is false, recovering
        // from a failed initial bring-up without requiring a serial-config change.
        fetchEnvOverrides();
        applyModbusSerialIfChanged();
        // Retry the full hub.set (product + mode) on every sample interval until
        // provisioning is confirmed. A factory-fresh Notecard that missed the
        // boot-time notecardConfigure() call must be provisioned as quickly as
        // possible — a failure-to-start alert during a power outage could fire
        // before the first report boundary otherwise.
        if (!g_hub_provisioned) { applyHubSetIfChanged(); }

        GenSample s = {};
        bool ok = pollGenerator(s);

        if (ok) {
            // Rearm the modbus_unreachable edge trigger so that a new outage after
            // recovery fires an immediate alert rather than waiting up to an hour
            // for the hourly-suppression window to expire.
            g_modbus_alert_sent = false;

            bool engine_running = (s.engine_rpm >= g_rpm_running);

            if (!g_first_sample_seen) {
                // Seed engine-running state without counting a start. The engine may
                // already be running when the monitor boots or resets (e.g. mid-outage
                // watchdog kick); we have no rising edge to detect.
                g_engine_was_running = engine_running;
                // Seed the last-known alarm word for the modbus_unreachable fallback
                // and stats.last_alarm_word reporting.
                g_current_alarm_word = s.alarm_word;

                // Boot-time alarm seeding: if a controller alarm is already asserted
                // at boot or watchdog reset, emit controller_alarm immediately and
                // latch g_active_controller_alarm so the normal detection block below
                // does not re-fire while the fault persists. Using the documented alert
                // name (not a separate startup_* variant) keeps downstream routing
                // consistent — the alarm_word payload field identifies the specific
                // fault regardless of when it was first detected.
                // logAlarmHistory is called unconditionally before sendEvent so the
                // firmware-observed alarm history captures the event even if the
                // Notecard note.add fails on this attempt (the latch stays false
                // and the detection block retries on the next sample).
                if (s.alarm_word != 0) {
                    logAlarmHistory("controller_alarm", s.alarm_word);
                    g_alarm_logged_controller = true;  // mark history written for this assertion
                    if (sendEvent("controller_alarm", &s)) {
                        g_active_controller_alarm = true;
                    }
                } else {
                    g_active_controller_alarm = false;
                }

                if (g_alarm_mask_fts != 0) {
                    bool fts_at_boot = ((s.alarm_word & g_alarm_mask_fts) != 0);
                    if (fts_at_boot) {
                        // FTS bits already asserted at boot: log to history and emit
                        // failure_to_start (the documented alert name), then latch.
                        // g_alarm_logged_fts is set so the main detection block below
                        // does not add a second history entry if sendEvent() fails here
                        // and the latch stays false — retries re-send the event but
                        // do not re-log.
                        logAlarmHistory("failure_to_start", s.alarm_word);
                        g_alarm_logged_fts = true;  // mark history written for this assertion
                        if (sendEvent("failure_to_start", &s)) {
                            g_active_fts = true;
                        }
                    } else {
                        g_active_fts = false;
                    }
                } else {
                    g_active_fts = false;
                }

                g_first_sample_seen = true;
            } else {
                // Rising-edge detection: count each engine start event this window.
                if (engine_running && !g_engine_was_running) {
                    if (stats.engine_starts < 255) stats.engine_starts++;
                }
                g_engine_was_running = engine_running;
            }

            accumulate(s);
            g_last_known_sample       = s;
            g_last_known_sample.valid = true;

            // ── Alarm word detection ─────────────────────────────────────────
            // Three-state detection: 0→nonzero (assertion), nonzero→different-nonzero
            // (fault-set change while still alarming), and nonzero→0 (clearance).
            //
            // Event sending is latch-based: gated on per-alert flags, not on a change in
            // g_current_alarm_word. If sendEvent() fails on one sample, the latch
            // stays false and the alert is retried on every subsequent sample while
            // the condition remains asserted — no event is permanently lost due to a
            // transient I2C or Notecard-side failure. A sustained fault still only
            // emits one event per assertion period because the latch blocks re-firing
            // once a queue operation succeeds.
            //
            // History logging captures every distinct alarm_word value: the initial
            // assertion (g_alarm_logged_controller gate ensures one entry), any mid-
            // nonzero transitions where the fault set evolves while at least one bit
            // remains set (logged unconditionally — no gate needed since each is a
            // genuinely new word), and the final clearance. This gives downstream
            // consumers a complete per-window fault chronology without duplicate entries
            // from failed-send retries. No new sendEvent() is fired for mid-nonzero
            // changes — one alert per assertion period avoids alarm fatigue on
            // gradually-evolving fault sets.
            if (s.alarm_word != 0 && !g_active_controller_alarm) {
                // 0→nonzero: first detection of this assertion period, OR a retry
                // of a previously-failed send while the assertion is still active.
                // Log every distinct alarm_word value: the initial entry on first
                // observation, and any new value seen during the failed-send retry
                // window (e.g. a second fault asserts before the first event ever
                // queued). Repeated polls with the same alarm_word skip logging so
                // the ring buffer never accumulates duplicate entries for one
                // unchanged assertion.
                if (!g_alarm_logged_controller || s.alarm_word != g_current_alarm_word) {
                    logAlarmHistory("controller_alarm", s.alarm_word);
                    g_alarm_logged_controller = true;
                }
                if (sendEvent("controller_alarm", &s)) {
                    g_active_controller_alarm = true;
                }
            } else if (s.alarm_word != 0 && g_active_controller_alarm &&
                       s.alarm_word != g_current_alarm_word) {
                // nonzero→different-nonzero: fault bits changed while at least one
                // remains asserted (e.g. a second fault added, or one of several
                // cleared while others persist). Log the updated alarm_word to preserve
                // the fault-set chronology. No sendEvent() — one alert per assertion
                // period. g_alarm_logged_controller stays true so the final clearance
                // entry is still written when the word eventually reaches 0.
                logAlarmHistory("controller_alarm", s.alarm_word);
            } else if (s.alarm_word == 0) {
                // nonzero→0: all alarms cleared. Log the clearance once, gated on
                // g_alarm_logged_controller so the clear entry is written even when
                // sendEvent() never succeeded for this assertion period.
                if (g_alarm_logged_controller) {
                    logAlarmHistory("alarm_clear", 0);
                }
                g_active_controller_alarm = false;  // all alarms cleared; rearm
                g_alarm_logged_controller = false;  // rearm history flag for next assertion
            }

            // Failure-to-start: if the operator has mapped the controller's FTS
            // alarm bit(s) into alarm_mask_fts, detect that specific condition as
            // a distinct high-priority event separate from the generic controller_alarm.
            // DeepSea 7000-series controllers assert FTS bits within seconds of a
            // failed crank attempt; Woodward EasyGen uses a different bit position.
            if (g_alarm_mask_fts != 0) {
                bool fts_active = ((s.alarm_word & g_alarm_mask_fts) != 0);
                if (fts_active && !g_active_fts) {
                    // Log once on first detection; subsequent send retries do not re-log.
                    if (!g_alarm_logged_fts) {
                        logAlarmHistory("failure_to_start", s.alarm_word);
                        g_alarm_logged_fts = true;
                    }
                    if (sendEvent("failure_to_start", &s)) {
                        g_active_fts = true;
                    }
                } else if (!fts_active) {
                    // Log the clearance once; gated on g_alarm_logged_fts so it fires
                    // even when sendEvent() never succeeded for this assertion period.
                    if (g_alarm_logged_fts) {
                        logAlarmHistory("fts_clear", 0);
                    }
                    g_active_fts = false;   // rearm when FTS bits clear
                    g_alarm_logged_fts = false;
                }
            }

            // Always update g_current_alarm_word and stats.last_alarm_word so a
            // fault sustained across a report boundary is never summarised as 0,
            // and the modbus_unreachable fallback always carries the freshest reading.
            g_current_alarm_word  = s.alarm_word;
            stats.last_alarm_word = s.alarm_word;

        } else {
            // Count failed polls for downstream coverage analysis.
            // samples_ok + samples_failed lets analytics distinguish
            // 'generator stopped' from 'telemetry missing' in the summary.
            stats.samples_failed++;

            // Fire immediately on the first miss in a new outage period (catches
            // commissioning wiring problems at first light without waiting an hour).
            // Rate-limit to once per hour for a sustained outage — a generator
            // powered down for scheduled service should not flood the event log.
            // g_modbus_alert_sent is cleared on any successful poll above, so
            // a recovery followed by a new failure restarts the immediate-alert logic.
            if (!g_modbus_alert_sent ||
                now - last_modbus_alert_ms >= 60UL * 60UL * 1000UL) {
                const GenSample *trig = g_last_known_sample.valid
                                        ? &g_last_known_sample : nullptr;
                if (sendEvent("modbus_unreachable", trig)) {
                    last_modbus_alert_ms = now;
                    g_modbus_alert_sent  = true;
                }
            }
        }

        // Advance by one interval using the cadence active for this window.
        // g_sample_minutes is not promoted until sendSummary() confirms the outgoing
        // summary, so this is always consistent with the samples being accumulated.
        // Advancing by fixed interval (not resetting to now) prevents long-term drift.
        next_sample_ms += (uint32_t)g_sample_minutes * 60UL * 1000UL;
    }

    // ── Report block ─────────────────────────────────────────────────────────
    // Report boundary: refresh env, re-apply any changed configs, evaluate rules,
    // emit summary, and reset rolling stats.
    // CRITICAL: evaluateRules() must run BEFORE sendSummary() — sendSummary() calls
    // stats.reset(), erasing the data the rules need to read.
    // Runs after the sample block so a boundary sample is included in the
    // closing window's statistics.
    if (report_due) {
        // Snapshot cadences before fetchEnvOverrides() may update g_report_minutes
        // and before sendSummary() may promote g_sample_minutes from the pending value.
        // These snapshots drive the cadence-change detection below.
        const uint32_t prev_report_minutes = g_report_minutes;
        const uint32_t prev_sample_minutes = g_sample_minutes;

        fetchEnvOverrides();
        applyModbusSerialIfChanged();
        applyHubSetIfChanged();
        evaluateRules();
        // Flush the firmware-observed alarm history before clearing the stats window.
        // flushAlarmHistory() emits gen_alarm_log.qo (full JSON, not template-backed)
        // for any alarm assertions/clearances logged since the last flush. A failed
        // note.add leaves the buffer intact so no entries are silently dropped.
        flushAlarmHistory();
        bool summary_sent = sendSummary();

        // Only advance the report deadline by a full report interval after a
        // confirmed successful note.add. On failure (transient I²C / Notecard
        // hiccup), defer the next attempt by REPORT_RETRY_MS rather than letting
        // the report block fire every loop() iteration. Without this defer, a
        // sustained Notecard fault triggers fetchEnvOverrides() + evaluateRules()
        // + flushAlarmHistory() + sendSummary() at the loop's 50 ms cadence,
        // generating continuous I²C traffic against an already-struggling bus.
        // Stats remain preserved across the retry so no window data is dropped.
        static const uint32_t REPORT_RETRY_MS = 60UL * 1000UL;
        if (summary_sent) {
            if (g_report_minutes != prev_report_minutes) {
                // Cadence changed via env override: reanchor the next deadline from
                // now using the new interval so the first new-cadence window is the
                // correct length rather than adding the now-stale old interval.
                next_report_ms = now + (uint32_t)g_report_minutes * 60UL * 1000UL;
            } else {
                // Stable cadence: advance by exactly one interval to prevent drift.
                next_report_ms += (uint32_t)prev_report_minutes * 60UL * 1000UL;
            }

            // If sendSummary() promoted g_sample_minutes, reanchor next_sample_ms
            // from now on the new cadence. Without this, if sample_due fired on the
            // same tick the preceding next_sample_ms advance used the old interval,
            // scheduling the first new-cadence sample at the wrong time.
            if (g_sample_minutes != prev_sample_minutes) {
                next_sample_ms = now + (uint32_t)g_sample_minutes * 60UL * 1000UL;
            }
        } else {
            next_report_ms = now + REPORT_RETRY_MS;
        }
    }

    // Yield between iterations. The sample cadence is minute-scale; a 50 ms pause
    // brings the loop() poll rate from ~MHz to ~20 Hz, saving meaningful power on a
    // battery-backed control bus without adding measurable latency to any scheduled event.
    delay(50);
}
