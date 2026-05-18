/*
 * ev_charger_session_monitor_helpers.cpp
 *
 * Helper-function implementations for the EV Charger Session &
 * Utilization Monitor sketch.  Kept here to satisfy the sketch's
 * 500-line limit while preserving a single logical unit.
 *
 * All functions share the `notecard` and `state` globals defined in
 * ev_charger_session_monitor.ino and declared extern in the header.
 */

#include "ev_charger_session_monitor_helpers.h"
#include <ModbusMaster.h>

// ── Modbus (file-scope) ───────────────────────────────────────────────────────
static ModbusMaster node;

// RS-485 direction control — called by ModbusMaster immediately before and after
// each transmitted frame.  On the SparkFun BOB-10124 (SP3485), the DE and RE
// inputs are tied together internally and exposed as the RTS pad (PIN_RS485_DE).
static void rs485PreTransmit()  { digitalWrite(PIN_RS485_DE, HIGH); }
static void rs485PostTransmit() { digitalWrite(PIN_RS485_DE, LOW);  }

// Convert two consecutive 16-bit Modbus registers to an IEEE-754 float.
// The SDM120 stores floats in big-endian word order: the high 16 bits of the
// float arrive in the first (lower-addressed) register, the low 16 bits in the
// second.  Using a union avoids strict-aliasing UB while remaining valid C++.
static float regsToFloat(uint16_t hi, uint16_t lo) {
    union { float f; uint32_t u; } v;
    v.u = ((uint32_t)hi << 16) | (uint32_t)lo;
    return v.f;
}

// ─────────────────────────────────────────────────────────────────────────────
// initModbus — configure the RS-485 transceiver pin and initialise the
// ModbusMaster instance against Serial1.
//
// Must be called every wake because the STM32L4 UART peripheral registers
// are not retained through full host power-off (the Notecarrier CX ATTN
// power-gate cuts the MCU's supply entirely between samples).  Re-calling
// Serial1.begin() and node.begin() is safe and fast.
// ─────────────────────────────────────────────────────────────────────────────
void initModbus() {
    pinMode(PIN_RS485_DE, OUTPUT);
    digitalWrite(PIN_RS485_DE, LOW);          // start in receive mode

    Serial1.begin(state.modbus_baud);
    node.begin(state.modbus_slave_id, Serial1);
    node.preTransmission(rs485PreTransmit);
    node.postTransmission(rs485PostTransmit);
}

// ─────────────────────────────────────────────────────────────────────────────
// pollMeter — read voltage, active power, and cumulative import energy from the
// SDM120 via Modbus RTU (Function Code 0x04, input registers).
//
// Returns true and populates *out when all three reads succeed; returns false
// with out->valid = false if every attempt fails (e.g., meter offline due to no
// mains, wiring fault, or slave-ID mismatch).
//
// Three separate readInputRegisters transactions are issued because voltage
// (0x0000), active power (0x000C), and import kWh (0x0048) are not in a
// contiguous block in the SDM120 register map.  A 20 ms inter-transaction
// pause is sufficient for the SDM120 to finish one response and be ready for
// the next at 9600 baud (a 9-byte frame takes < 10 ms).
//
// The complete three-register sequence is retried up to 3 times with a 50 ms
// inter-attempt pause.  This absorbs single-frame bus collisions and RS-485
// turnaround-timing faults that most commonly occur at power-on or after a
// brief conductor disturbance.  If all attempts fail, the caller suppresses
// session-state transitions and window accumulation for this wake — see
// runSessionStateMachine() and README §6.3.
//
// Negative active power (export, e.g. from a solar inverter on the same feed)
// is clamped to zero — an EV charging circuit should never export, and a
// negative value would corrupt session kWh accounting.
// ─────────────────────────────────────────────────────────────────────────────
bool pollMeter(MeterReading *out) {
    out->voltage_v  = 0.0f;
    out->power_w    = 0.0f;
    out->import_kwh = 0.0f;
    out->valid      = false;

    const int kMaxAttempts = 3;
    for (int attempt = 1; attempt <= kMaxAttempts; attempt++) {
        if (attempt > 1) delay(50); // let the bus settle before retry

        uint8_t rc;

        // ── Voltage ──────────────────────────────────────────────────────────
        rc = node.readInputRegisters(SDM_REG_VOLTAGE, 2);
        if (rc != ModbusMaster::ku8MBSuccess) {
            Serial.print("[app] WARN: Modbus voltage read error 0x");
            Serial.print(rc, HEX);
            if (attempt < kMaxAttempts) { Serial.println(" — retrying"); continue; }
            Serial.println();
            return false;
        }
        float voltage_v = regsToFloat(node.getResponseBuffer(0), node.getResponseBuffer(1));
        delay(20);

        // ── Active power ─────────────────────────────────────────────────────
        rc = node.readInputRegisters(SDM_REG_POWER, 2);
        if (rc != ModbusMaster::ku8MBSuccess) {
            Serial.print("[app] WARN: Modbus power read error 0x");
            Serial.print(rc, HEX);
            if (attempt < kMaxAttempts) { Serial.println(" — retrying"); continue; }
            Serial.println();
            return false;
        }
        float pw = regsToFloat(node.getResponseBuffer(0), node.getResponseBuffer(1));
        delay(20);

        // ── Cumulative import energy ──────────────────────────────────────────
        rc = node.readInputRegisters(SDM_REG_IMPORT_KWH, 2);
        if (rc != ModbusMaster::ku8MBSuccess) {
            Serial.print("[app] WARN: Modbus kWh read error 0x");
            Serial.print(rc, HEX);
            if (attempt < kMaxAttempts) { Serial.println(" — retrying"); continue; }
            Serial.println();
            return false;
        }

        out->voltage_v  = voltage_v;
        out->power_w    = (pw > 0.0f) ? pw : 0.0f;  // clamp export to zero
        out->import_kwh = regsToFloat(node.getResponseBuffer(0), node.getResponseBuffer(1));
        out->valid      = true;
        return true;
    }
    return false; // unreachable — satisfies the compiler
}

// ─────────────────────────────────────────────────────────────────────────────
// initNotecard — configure hub sync and quiet the accelerometer.  Returns true
// when hub.set succeeds; the caller persists this result in
// state.notecard_configured so a failed attempt is retried on the next wake.
//
// sendRequestWithRetry on the very first I²C call addresses the documented
// race condition where the Cygnet comes up faster than the Notecard.
// ─────────────────────────────────────────────────────────────────────────────
bool initNotecard(const char *product_uid) {
    J *req = notecard.newRequest("hub.set");
    JAddStringToObject(req, "product",  product_uid);
    JAddStringToObject(req, "mode",     "periodic");
    JAddNumberToObject(req, "outbound", (int)state.report_interval_min);
    JAddNumberToObject(req, "inbound",  120);
    bool ok = notecard.sendRequestWithRetry(req, 5);
    if (!ok) Serial.println("[app] WARN: hub.set failed — will retry on next wake");

    // Disable the on-board accelerometer to eliminate interrupt-driven current
    // blips on a Mojo power trace during bench validation.  Best-effort.
    req = notecard.newRequest("card.motion.mode");
    JAddBoolToObject(req, "stop", true);
    if (!notecard.sendRequest(req)) Serial.println("[app] WARN: card.motion.mode failed");

    return ok;
}

// ─────────────────────────────────────────────────────────────────────────────
// defineTemplates — register a fixed-schema template for charger_summary.qo.
// Returns true when the note.template request succeeds; retried on failure.
//
// Template type codes:
//   14.1 = 4-byte IEEE-754 float
//   12   = 2-byte signed integer
//
// Templated Notes are stored as fixed-length binary records (~50 bytes each)
// rather than free-form JSON (~250 bytes), meaningful across a large fleet
// over a multi-year deployment.
//
// charger_session.qo and charger_alert.qo are intentionally untemplated:
// session events are infrequent, and the two Notefiles carry different body
// shapes — mixing them under a single template causes schema conflicts.
// ─────────────────────────────────────────────────────────────────────────────
bool defineTemplates() {
    J *req  = notecard.newRequest("note.template");
    JAddStringToObject(req, "file", FILE_SUMMARY);
    JAddNumberToObject(req, "port", 50);
    J *body = JAddObjectToObject(req, "body");
    JAddNumberToObject(body, "sessions",             12);    // completed sessions in window
    JAddNumberToObject(body, "total_kwh",            14.1);  // metered energy delivered
    JAddNumberToObject(body, "avg_session_kwh",      14.1);  // per-session average
    JAddNumberToObject(body, "peak_w",               14.1);  // peak active power in window
    JAddNumberToObject(body, "charging_min",         12);    // minutes delivering energy
    JAddNumberToObject(body, "idle_min",             12);    // minutes not delivering energy
    JAddNumberToObject(body, "utilization_pct",      14.1);  // charging_min / total_min × 100
    JAddNumberToObject(body, "available_min",        12);    // minutes with V_rms ≥ floor
    JAddNumberToObject(body, "availability_pct",     14.1);  // available_min / elapsed_min × 100
    JAddNumberToObject(body, "sample_coverage_pct",  14.1);  // total_min / elapsed_min × 100
    bool ok = notecard.sendRequest(req);
    if (!ok) Serial.println("[app] WARN: note.template failed — will retry on next wake");
    else     Serial.println("[app] Notefile template registered for " FILE_SUMMARY);
    return ok;
}

// ─────────────────────────────────────────────────────────────────────────────
// clampU32 / clampF — inclusive range clamps for environment-variable values.
// ─────────────────────────────────────────────────────────────────────────────
uint32_t clampU32(uint32_t v, uint32_t lo, uint32_t hi) {
    return (v < lo) ? lo : (v > hi) ? hi : v;
}
float clampF(float v, float lo, float hi) {
    return (v < lo) ? lo : (v > hi) ? hi : v;
}

// ─────────────────────────────────────────────────────────────────────────────
// applyHubCadence — issue a hub.set that (re-)applies state.report_interval_min
// as the Notecard's outbound sync cadence.  Called from fetchEnvOverrides()
// when the interval changes and from the hub_cadence_dirty retry path in
// setup().  Returns true on success; the caller clears hub_cadence_dirty.
// ─────────────────────────────────────────────────────────────────────────────
bool applyHubCadence() {
    J *h = notecard.newRequest("hub.set");
    JAddStringToObject(h, "mode",     "periodic");
    JAddNumberToObject(h, "outbound", (int)state.report_interval_min);
    JAddNumberToObject(h, "inbound",  120);
    bool ok = notecard.sendRequest(h);
    if (!ok) Serial.println("[app] WARN: hub.set cadence re-sync failed — will retry on next wake");
    return ok;
}

// ─────────────────────────────────────────────────────────────────────────────
// fetchEnvOverrides — pull environment variables from the Notecard.
//
// The "time" filter only returns variables that changed after env_last_modified,
// so this costs essentially nothing on wakes where operators have not changed
// anything in Notehub.  When report_interval_min changes, hub_cadence_dirty is
// set true before the hub.set attempt and cleared only on confirmed success —
// preventing permanent local/Notecard cadence drift from a transient failure.
// ─────────────────────────────────────────────────────────────────────────────
void fetchEnvOverrides() {
    J *req = notecard.newRequest("env.get");
    JAddNumberToObject(req, "time", (double)state.env_last_modified);
    J *rsp = notecard.requestAndResponse(req);
    if (!rsp) { Serial.println("[app] WARN: env.get returned NULL"); return; }

    const char *err = JGetString(rsp, "err");
    if (err && err[0]) {
        if (!strstr(err, "nothing to return")) {
            Serial.print("[app] WARN: env.get error: ");
            Serial.println(err);
        }
        notecard.deleteResponse(rsp);
        return;
    }

    uint32_t mod = (uint32_t)JGetNumber(rsp, "time");
    if (mod == 0) { notecard.deleteResponse(rsp); return; }
    state.env_last_modified = mod;

    J *b = JGetObject(rsp, "body");
    if (b) {
        double v;

        if ((v = JGetNumber(b, "sample_interval_sec")) > 0)
            state.sample_interval_sec = clampU32((uint32_t)v, 5, 3600);

        if ((v = JGetNumber(b, "report_interval_min")) > 0) {
            state.report_interval_min = clampU32((uint32_t)v, 5, 1440);
            // Set the dirty flag before the attempt so that a transient failure
            // cannot leave the Notecard permanently on the old outbound cadence.
            state.hub_cadence_dirty = true;
            if (applyHubCadence()) {
                state.hub_cadence_dirty = false;
                Serial.println("[app] hub outbound cadence updated");
            }
        }

        // session_threshold_w: minimum active power (W) to open a session.
        // Level 2 EVSE minimum pilot signalling draws ~1250 W (208 V, 6 A);
        // 500 W default sits safely below any real session while above charger
        // standby draw.  Allow values from 10 W (bench-test range) to 20 kW.
        if ((v = JGetNumber(b, "session_threshold_w")) > 0)
            state.session_threshold_w = clampF((float)v, 10.0f, 20000.0f);

        if ((v = JGetNumber(b, "session_end_count")) > 0)
            state.session_end_count = (uint8_t)clampU32((uint32_t)v, 1, 20);

        // voltage_present_v: V_rms floor for mains-present classification.
        // 85 V is below the sag floor for both 120 V and 208/240 V feeds,
        // ensuring the flag is not triggered by normal voltage variation.
        if ((v = JGetNumber(b, "voltage_present_v")) > 0)
            state.voltage_present_v = clampF((float)v, 10.0f, 300.0f);

        // alert_offline_min accepts 0 (disable) or any positive value.
        // Check for key presence so an absent key does not zero-out the default.
        if (JGetObjectItem(b, "alert_offline_min")) {
            v = JGetNumber(b, "alert_offline_min");
            if (v >= 0)
                state.alert_offline_min = clampU32((uint32_t)v, 0, 10080); // max 7 days
        }

        if ((v = JGetNumber(b, "modbus_slave_id")) > 0)
            state.modbus_slave_id = (uint8_t)clampU32((uint32_t)v, 1, 247);

        if ((v = JGetNumber(b, "modbus_baud")) > 0) {
            uint32_t new_baud = clampU32((uint32_t)v, 2400, 115200);
            if (new_baud != state.modbus_baud) {
                state.modbus_baud = new_baud;
                initModbus(); // re-initialise Serial1 at new baud immediately
            }
        }
    }
    notecard.deleteResponse(rsp);
    Serial.println("[app] env vars refreshed from Notehub");
}

// ─────────────────────────────────────────────────────────────────────────────
uint32_t getEpoch() {
    J *rsp = notecard.requestAndResponse(notecard.newRequest("card.time"));
    if (!rsp) return 0;
    const char *err = JGetString(rsp, "err");
    uint32_t t = 0;
    if (!err || !err[0]) t = (uint32_t)JGetNumber(rsp, "time");
    notecard.deleteResponse(rsp);
    return t;
}

// ─────────────────────────────────────────────────────────────────────────────
// runSessionStateMachine — IDLE ↔ CHARGING state machine driven by metered data.
//
// Session opens when meter.power_w ≥ session_threshold_w.
// Session closes after session_end_count consecutive below-threshold wakes,
// providing a grace period for brief dips during EV charge-phase transitions.
//
// Session energy is derived from the meter's cumulative import-kWh register:
//   session_kwh = (import_kwh at close) − (import_kwh at open)
// This is actual metered energy — no voltage or power-factor assumptions.
// A guard against negative values handles the unlikely case of a meter reset.
//
// Charger availability is accumulated each wake in two counters:
//   window_elapsed_sec  — advances unconditionally; wall-clock denominator
//   window_available_sec — advances only when meter.valid and V_rms ≥ floor
// When the meter is unreachable (mains absent or Modbus fault), meter.valid
// is false: window_elapsed_sec still grows but window_available_sec does not,
// so those wakes correctly reduce the reported availability_pct.
//
// When meter.valid is false (all Modbus retries failed this wake):
//   - session-state transitions (open and close) are suppressed
//   - below_threshold_count is NOT incremented
//   - neither charging_sec nor idle_sec is accumulated
// The session is held open until valid below-threshold readings arrive.
// The mains-absent offline timer advances independently in setup() — fault
// wakes still count toward the mains-absence duration.
//
// All window accumulators are gated on window_start_epoch > 0 so no data
// enters a summary before the Notecard has a valid time sync.
//
// Session-close retry: when emitSessionNote() fails, the completed payload is
// stored in the pending_session_* fields and session_active is cleared
// immediately — decoupling new-session detection from Note-queue retries.
// The pending note is retried at the top of this function on every subsequent
// wake, then falls through to normal state-machine processing regardless of
// whether the retry succeeded.
// ─────────────────────────────────────────────────────────────────────────────
void runSessionStateMachine(const MeterReading &meter, uint32_t now) {
    bool above       = meter.valid && (meter.power_w >= state.session_threshold_w);
    bool window_open = (state.window_start_epoch > 0);

    // ── Wall-clock elapsed window time ───────────────────────────────────────
    // Advances every wake regardless of meter validity.  Used as the
    // availability_pct denominator so wakes where the meter is unreachable
    // (mains absent, wiring fault, Modbus failure) correctly count as
    // unavailable rather than being excluded from the denominator and silently
    // inflating the reported availability.
    if (window_open) {
        state.window_elapsed_sec += state.sample_interval_sec;
    }

    // ── Availability tracking ────────────────────────────────────────────────
    // Only accumulate when the meter returned a valid reading and V_rms is at
    // or above the mains-present floor.  When the meter is offline, only
    // window_elapsed_sec grows — so that wake contributes to the denominator
    // but not the numerator, correctly reducing availability_pct.
    if (window_open && meter.valid && meter.voltage_v >= state.voltage_present_v) {
        state.window_available_sec += state.sample_interval_sec;
    }

    // ── Retry pending completed-session note ─────────────────────────────────
    // Window-attribution note: window accumulators are incremented only on a
    // confirmed success.  If emitSummaryNote() resets accumulators between
    // when the session ended and when this retry finally succeeds, the session
    // count and kWh are attributed to the newer window — see README §6.6 / §9.
    if (state.pending_session_note) {
        if (emitSessionNote(state.pending_session_kwh,
                            state.pending_session_peak_w,
                            state.pending_session_start_epoch,
                            state.pending_session_end_epoch)) {
            if (window_open) {
                state.window_sessions++;
                state.window_completed_session_kwh += state.pending_session_kwh;
            }
            state.pending_session_note = false;
            Serial.println("[app] pending session Note queued (retry succeeded)");
        }
        // Fall through regardless so live power is always monitored.
    }

    if (!state.session_active) {
        // ── IDLE state ──────────────────────────────────────────────────────
        if (!above) {
            // Only count idle time when the meter returned a valid reading.
            // A Modbus fault this wake must not be attributed as idle time —
            // see README §6.3 / §6.6.
            if (window_open && meter.valid) state.idle_sec += state.sample_interval_sec;
        } else {
            // ── IDLE → CHARGING transition ─────────────────────────────────
            state.session_active        = true;
            state.session_start_epoch   = now;
            state.session_start_kwh     = meter.import_kwh;
            state.session_peak_w        = meter.power_w;
            state.below_threshold_count = 0;
            Serial.println("[app] session STARTED");
            if (window_open) {
                if (meter.power_w > state.window_peak_w) state.window_peak_w = meter.power_w;
                state.charging_sec += state.sample_interval_sec;
            }
        }
    } else {
        // ── CHARGING state ──────────────────────────────────────────────────
        if (above) {
            state.below_threshold_count = 0;
            if (meter.power_w > state.session_peak_w) state.session_peak_w = meter.power_w;
            if (window_open) {
                if (meter.power_w > state.window_peak_w) state.window_peak_w = meter.power_w;
                state.charging_sec += state.sample_interval_sec;
            }
        } else {
            if (!meter.valid) {
                // Modbus fault this wake — do not increment below_threshold_count
                // and do not attribute window time.  The session stays open until
                // valid below-threshold readings arrive.  See README §6.3 / §6.6.
            } else {
                state.below_threshold_count++;
                if (state.below_threshold_count >= state.session_end_count) {
                    // ── Session end ─────────────────────────────────────────
                    // Compute metered session energy from the import-kWh register
                    // delta.  Guard against a negative result caused by a meter
                    // register reset between session open and close.
                    float session_kwh = 0.0f;
                    if (meter.import_kwh >= state.session_start_kwh) {
                        session_kwh = meter.import_kwh - state.session_start_kwh;
                    }

                    // Copy payload into the pending-note slot.  If another session's
                    // Note is still unconfirmed, warn and overwrite — see README §9.
                    if (state.pending_session_note) {
                        Serial.println("[app] WARN: overwriting unconfirmed pending session Note");
                    }
                    state.pending_session_kwh         = session_kwh;
                    state.pending_session_peak_w      = state.session_peak_w;
                    state.pending_session_start_epoch = state.session_start_epoch;
                    state.pending_session_end_epoch   = now;
                    state.pending_session_note        = true;

                    state.session_active        = false;
                    state.session_peak_w        = 0.0f;
                    state.below_threshold_count = 0;
                    Serial.println("[app] session end condition met — attempting close");

                    if (emitSessionNote(state.pending_session_kwh,
                                        state.pending_session_peak_w,
                                        state.pending_session_start_epoch,
                                        state.pending_session_end_epoch)) {
                        if (window_open) {
                            state.window_sessions++;
                            state.window_completed_session_kwh += state.pending_session_kwh;
                        }
                        state.pending_session_note = false;
                        if (window_open) state.idle_sec += state.sample_interval_sec;
                        Serial.println("[app] session ENDED — charger_session.qo queued");
                    } else {
                        // Note not queued; pending fields remain set for retry next wake.
                        if (window_open) state.idle_sec += state.sample_interval_sec;
                    }
                } else {
                    // Grace period: still counts as charging time.
                    if (window_open) state.charging_sec += state.sample_interval_sec;
                }
            }
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// emitSessionNote — send a charger_session.qo Note with sync:true.
//
// All payload values are passed as parameters rather than read from global
// state so the function works correctly both at session-close time and during
// pending-note retries (when session_* fields may be overwritten by a new
// session).
//
// sync:true bypasses the hourly outbound timer so the record lands in Notehub
// within a cellular session-establishment window (~15–60 s).
//
// peak_w is the highest active-power reading seen during the session; kwh is
// the metered energy from the import-kWh delta — both are always valid.
//
// timing_valid is false when start_epoch == 0 (session opened before the
// Notecard had a valid time sync). duration_min and start_epoch are emitted
// as 0 in that case so downstream consumers can suppress or flag those records.
// ─────────────────────────────────────────────────────────────────────────────
bool emitSessionNote(float kwh, float peak_w,
                     uint32_t start_epoch, uint32_t end_epoch) {
    bool     timing_valid = (start_epoch > 0 && end_epoch >= start_epoch);
    uint32_t dur_sec      = timing_valid ? (end_epoch - start_epoch) : 0;

    J *req  = notecard.newRequest("note.add");
    JAddStringToObject(req, "file", FILE_SESSION);
    JAddBoolToObject(req,   "sync", true);
    J *body = JAddObjectToObject(req, "body");
    JAddNumberToObject(body, "session_kwh",  kwh);
    JAddNumberToObject(body, "duration_min", (float)dur_sec / 60.0f);
    JAddNumberToObject(body, "peak_w",       peak_w);
    JAddNumberToObject(body, "start_epoch",  timing_valid ? (double)start_epoch : 0.0);
    JAddBoolToObject(body,   "timing_valid", timing_valid);

    J *rsp = notecard.requestAndResponse(req);
    if (!rsp) {
        Serial.println("[app] WARN: charger_session.qo note.add returned NULL");
        return false;
    }
    const char *err = JGetString(rsp, "err");
    bool ok = (!err || !err[0]);
    if (!ok) {
        Serial.print("[app] WARN: charger_session.qo error: ");
        Serial.println(err);
    }
    notecard.deleteResponse(rsp);
    return ok;
}

// ─────────────────────────────────────────────────────────────────────────────
// emitSummaryNote — send a templated hourly summary and reset window state.
//
// Window total kWh is computed from state.last_valid_import_kwh (the most
// recently successfully-read meter value) rather than from the current wake's
// meter reading.  This ensures that a failed poll on the wake where the summary
// fires does not force total_kwh to zero or reset window_start_kwh to a
// synthetic baseline that would inflate the next window's total.
//
// Window accumulators are reset only on a confirmed successful send so that
// a transient Notecard failure cannot silently discard an hour of data; the
// window grows slightly until the send succeeds.
//
// Derived metrics and their denominators:
//   charging_min + idle_min == total_min    (valid-sample wakes only)
//   utilization_pct  == charging_min / total_min × 100
//   availability_pct == available_min / elapsed_min × 100
//   sample_coverage_pct == total_min / elapsed_min × 100
//
// total_min counts only wakes where meter.valid was true.  elapsed_min is
// derived from window_elapsed_sec, which advances unconditionally every wake —
// so invalid-meter wakes correctly reduce availability_pct rather than being
// excluded from its denominator.
// ─────────────────────────────────────────────────────────────────────────────
bool emitSummaryNote(uint32_t now) {
    uint32_t total_sec    = state.charging_sec + state.idle_sec;
    int      charging_min = (int)(state.charging_sec / 60);
    int      total_min    = (int)(total_sec / 60);
    int      idle_min     = total_min - charging_min;   // ensures sum == total_min
    float    util_pct     = (total_min > 0)
                            ? (100.0f * (float)charging_min / (float)total_min)
                            : 0.0f;
    float    avg_kwh      = (state.window_sessions > 0)
                            ? (state.window_completed_session_kwh / (float)state.window_sessions)
                            : 0.0f;

    // Total kWh from the last-valid meter reading — avoids a bogus zero when the
    // poll fails on the summary wake and prevents window_start_kwh from being
    // reset to a synthetic fallback that would inflate the following window.
    // The baseline_set guard suppresses total_kwh for windows that never had a
    // valid meter read — without it, a window opened during a meter fault would
    // report (last_valid_import_kwh − 0) = the meter's lifetime energy.
    float total_kwh = 0.0f;
    if (state.window_kwh_baseline_set &&
        state.last_valid_import_kwh >= state.window_start_kwh)
        total_kwh = state.last_valid_import_kwh - state.window_start_kwh;

    // Availability uses elapsed wall-clock time as the denominator so that
    // wakes where the meter was unreachable count as unavailable.
    int   elapsed_min   = (int)(state.window_elapsed_sec / 60);
    int   available_min = (int)(state.window_available_sec / 60);
    float avail_pct     = (elapsed_min > 0)
                          ? (100.0f * (float)available_min / (float)elapsed_min)
                          : 0.0f;
    // sample_coverage_pct: fraction of the wall-clock window with valid meter data.
    // 100.0 means every poll succeeded; a lower value flags meter downtime.
    float coverage_pct  = (elapsed_min > 0)
                          ? (100.0f * (float)total_min / (float)elapsed_min)
                          : 0.0f;

    J *req  = notecard.newRequest("note.add");
    JAddStringToObject(req, "file", FILE_SUMMARY);
    J *body = JAddObjectToObject(req, "body");
    JAddNumberToObject(body, "sessions",             state.window_sessions);
    JAddNumberToObject(body, "total_kwh",            total_kwh);
    JAddNumberToObject(body, "avg_session_kwh",      avg_kwh);
    JAddNumberToObject(body, "peak_w",               state.window_peak_w);
    JAddNumberToObject(body, "charging_min",         charging_min);
    JAddNumberToObject(body, "idle_min",             idle_min);
    JAddNumberToObject(body, "utilization_pct",      util_pct);
    JAddNumberToObject(body, "available_min",        available_min);
    JAddNumberToObject(body, "availability_pct",     avail_pct);
    JAddNumberToObject(body, "sample_coverage_pct",  coverage_pct);

    J *rsp = notecard.requestAndResponse(req);
    if (!rsp) {
        Serial.println("[app] WARN: charger_summary.qo note.add returned NULL");
        return false;
    }
    const char *err = JGetString(rsp, "err");
    bool ok = (!err || !err[0]);
    if (!ok) {
        Serial.print("[app] WARN: charger_summary.qo error: ");
        Serial.println(err);
    }
    notecard.deleteResponse(rsp);

    if (ok) {
        // Reset window accumulators only after the Note is confirmed queued.
        state.window_sessions              = 0;
        state.window_completed_session_kwh = 0.0f;
        state.window_peak_w                = 0.0f;
        state.charging_sec                 = 0;
        state.idle_sec                     = 0;
        state.window_available_sec         = 0;
        state.window_elapsed_sec           = 0;
        state.window_start_epoch           = now;
        // Anchor the new window's kWh baseline to the last confirmed reading
        // — but only if the window we just summarised had at least one valid
        // poll.  If it did not, last_valid_import_kwh is stale (or 0), so we
        // defer baseline anchoring to the lazy-set in the .ino, which fires
        // on the next valid poll and prevents a stale baseline from inflating
        // the following window's total_kwh.
        if (state.window_kwh_baseline_set) {
            state.window_start_kwh = state.last_valid_import_kwh;
        } else {
            state.window_start_kwh = 0.0f;
        }
        Serial.println("[app] summary Note emitted — window reset");
    }
    return ok;
}

// ─────────────────────────────────────────────────────────────────────────────
// emitOfflineAlert — fire once when the charger circuit has had no mains
// voltage for more than alert_offline_min minutes.
//
// "mains_absent" accurately describes what the firmware has inferred: the
// SDM120 (which requires mains power to respond on Modbus) has been
// unreachable, or V_rms has been consistently below voltage_present_v, for
// the configured duration.  This indicates a mains outage, tripped breaker,
// or sustained meter fault.  Downstream routes should correlate with other
// site signals before dispatching maintenance.
//
// The offline_min payload field is measured from the last observed mains-
// present epoch, or from window_start_epoch (commissioning time) if mains
// has never been confirmed present — so the alert can fire even for a circuit
// that has been dead since initial installation.
// ─────────────────────────────────────────────────────────────────────────────
bool emitOfflineAlert(uint32_t now) {
    uint32_t ref = (state.last_mains_epoch > 0)
                   ? state.last_mains_epoch
                   : state.window_start_epoch;
    uint32_t offline_min = (ref > 0 && now > ref) ? ((now - ref) / 60) : 0;

    J *req  = notecard.newRequest("note.add");
    JAddStringToObject(req, "file", FILE_ALERT);
    JAddBoolToObject(req,   "sync", true);
    J *body = JAddObjectToObject(req, "body");
    JAddStringToObject(body, "alert",       "mains_absent");
    JAddNumberToObject(body, "offline_min", (double)offline_min);

    J *rsp = notecard.requestAndResponse(req);
    if (!rsp) {
        Serial.println("[app] WARN: charger_alert.qo note.add returned NULL");
        return false;
    }
    const char *err = JGetString(rsp, "err");
    bool ok = (!err || !err[0]);
    if (ok)  Serial.println("[app] mains_absent alert emitted");
    else   { Serial.print("[app] WARN: charger_alert.qo error: "); Serial.println(err); }
    notecard.deleteResponse(rsp);
    return ok;
}

// ─────────────────────────────────────────────────────────────────────────────
// sleepHost — serialise state to Notecard flash, then cut host power.
//
// NotePayloadSaveAndSleep() issues a card.attn "sleep" request that pulls
// the ATTN pin LOW for sample_interval_sec seconds, which the Notecarrier CX
// uses to cut the host MCU's +VBAT rail entirely. On wake, the host enters
// setup() from cold and NotePayloadRetrieveAfterSleep() rehydrates the struct.
// ─────────────────────────────────────────────────────────────────────────────
void sleepHost() {
    NotePayloadDesc payload = {0, 0, 0};
    NotePayloadAddSegment(&payload, STATE_SEG_ID, &state, sizeof(state));
    NotePayloadSaveAndSleep(&payload, state.sample_interval_sec, NULL);
    // Should not reach here — ATTN is expected to cut host power.
    // If it does (e.g., bare breakout bench test), loop() handles the spin.
}
