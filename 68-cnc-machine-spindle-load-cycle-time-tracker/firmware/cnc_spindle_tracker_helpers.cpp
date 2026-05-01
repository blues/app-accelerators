// cnc_spindle_tracker_helpers.cpp
// Notecard and Modbus helper implementations for the CNC Machine Spindle Load
// & Cycle Time Tracker.
//
// All functions declared in cnc_spindle_tracker_helpers.h are implemented here.
// Globals that must be shared with the .ino are declared extern in the shared
// header; objects used exclusively within this file (Modbus TCP client and CNC
// server IP) are kept static to avoid polluting the sketch namespace.
//
// Hardware: Arduino OPTA RS485 + Blues Wireless for OPTA
// Blues docs: https://dev.blues.io

#include "cnc_spindle_tracker_helpers.h"

// ---------------------------------------------------------------------------
// Modbus TCP objects — private to this translation unit.
// EthernetClient must outlive ModbusTCPClient; both are file-scope so they
// share the same program lifetime.
// ---------------------------------------------------------------------------
static EthernetClient  _ethClient;
static ModbusTCPClient _modbusTCPClient(_ethClient);

// Static IP of the CNC Modbus TCP server on the private point-to-point subnet.
// Adjust to match the CNC controller's network configuration before flashing.
// The default (192.168.250.1) pairs with the OPTA local IP (192.168.250.10)
// defined in the .ino. See README §4 for network-configuration guidance.
static const IPAddress _DEFAULT_CNC_IP(192, 168, 250, 1);

// ---------------------------------------------------------------------------
// notecardConfigure
// ---------------------------------------------------------------------------
// Configure the Notecard for periodic cellular sync.
//
// Notecard configuration is mandatory: without a successful hub.set the
// ProductUID is not registered on the Notecard, the outbound/inbound cadence
// is wrong, and subsequent Notes are un-routable. The function blocks —
// retrying hub.set every 30 s — until configuration succeeds, so setup()
// never returns with the Notecard in an unconfigured state.
//
// Swap "periodic" -> "continuous" during development for live streaming.
void notecardConfigure(const char *productUid) {
    if (strlen(productUid) == 0) {
        usbSerial.println("[NOTECARD] PRODUCT_UID is empty — define it before flashing. Halting.");
        while (true) {}
    }

    while (true) {
        J *req = notecard.newRequest("hub.set");
        if (req == NULL) {
            usbSerial.println("[NOTECARD] hub.set allocation failed — retrying in 5 s.");
            delay(5000);
            continue;
        }
        JAddStringToObject(req, "product", productUid);
        JAddStringToObject(req, "mode", "periodic");
        JAddNumberToObject(req, "outbound", DEFAULT_REPORT_MINUTES);   // minutes
        JAddNumberToObject(req, "inbound",  DEFAULT_REPORT_MINUTES * 2);
        // sendRequestWithRetry handles the cold-boot I²C race where the host
        // MCU comes up before the Notecard is ready.
        if (notecard.sendRequestWithRetry(req, 5)) {
            usbSerial.println("[NOTECARD] hub.set OK.");
            return;
        }
        usbSerial.println("[NOTECARD] hub.set failed — check wiring and PRODUCT_UID. Retrying in 30 s.");
        delay(30000);
    }
}

// ---------------------------------------------------------------------------
// defineTemplates
// ---------------------------------------------------------------------------
// Register fixed-length templates for all three Notefiles.
// Type hints: 14.1 = 4-byte float; 14 = 4-byte signed int; 12 = 2-byte signed int;
// string fields: the exemplar string's character count sets the allocated record
// width (the Notecard truncates notes.add values to that length). Max 255 chars.
void defineTemplates(void) {
    // cnc_summary.qo — hourly OEE-component roll-up
    {
        J *req = notecard.newRequest("note.template");
        if (req == NULL) {
            usbSerial.println("[NOTECARD] cnc_summary.qo template allocation failed.");
        } else {
            JAddStringToObject(req, "file", "cnc_summary.qo");
            JAddNumberToObject(req, "port", 50);
            J *body = JAddObjectToObject(req, "body");
            JAddNumberToObject(body, "spindle_pct_mean",       14.1);
            JAddNumberToObject(body, "spindle_pct_peak",       14.1);
            // feed-rate override mean (0–150 % of programmed rate), not
            // engineering-unit feed rate — see README §9 for explanation.
            JAddNumberToObject(body, "feed_override_pct_mean", 14.1);
            JAddNumberToObject(body, "run_min",          12);
            JAddNumberToObject(body, "idle_min",         12);
            // 14 = 4-byte signed int; the per-window delta is a uint32_t that
            // can exceed 32767 (the 2-byte signed limit of type 12) on high-
            // throughput machines, so use the 4-byte type for safe encoding.
            JAddNumberToObject(body, "cycle_count",      14);
            // 14.1 = 4-byte float; supports avg cycle times above 32767 s (~9 h)
            JAddNumberToObject(body, "avg_cycle_sec",    14.1);
            // 14 = 4-byte signed int; safely represents the full uint16 range (0–65535)
            // without sign-extension corruption for values above 32767.
            JAddNumberToObject(body, "operator_id",      14);
            JAddNumberToObject(body, "alarm_count",      12);
            // valid_samples == 0 signals a total comm outage for the window
            JAddNumberToObject(body, "valid_samples",    12);
            if (!notecard.sendRequest(req)) {
                usbSerial.println("[NOTECARD] cnc_summary.qo template registration failed.");
            }
        }
    }

    // cnc_alarm.qo — immediate spindle-overload / Modbus / CNC-alarm events
    {
        J *req = notecard.newRequest("note.template");
        if (req == NULL) {
            usbSerial.println("[NOTECARD] cnc_alarm.qo template allocation failed.");
        } else {
            JAddStringToObject(req, "file", "cnc_alarm.qo");
            JAddNumberToObject(req, "port", 51);
            J *body = JAddObjectToObject(req, "body");
            // 32-character exemplar string: the Notecard allocates storage equal
            // to the exemplar's character count. "32" is only 2 chars and would
            // truncate all alert_type values beyond 2 characters. The longest
            // alert type ("modbus_unreachable") is 18 characters; 32 gives headroom.
            JAddStringToObject(body, "alert_type",  "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx");
            // 14 = 4-byte signed int; safely represents alarm codes and operator IDs
            // in the full uint16 range (0–65535) without overflow above 32767.
            JAddNumberToObject(body, "alarm_code",  14);
            JAddNumberToObject(body, "spindle_pct", 14.1);
            JAddNumberToObject(body, "operator_id", 14);
            if (!notecard.sendRequest(req)) {
                usbSerial.println("[NOTECARD] cnc_alarm.qo template registration failed.");
            }
        }
    }

    // cnc_operator.qo — operator login/logout / ID-change events.
    // Fired immediately (sync) whenever the operator_id register transitions.
    // operator_id == 0 conventionally means no operator is logged in.
    {
        J *req = notecard.newRequest("note.template");
        if (req == NULL) {
            usbSerial.println("[NOTECARD] cnc_operator.qo template allocation failed.");
        } else {
            JAddStringToObject(req, "file", "cnc_operator.qo");
            JAddNumberToObject(req, "port", 52);
            J *body = JAddObjectToObject(req, "body");
            // 14 = 4-byte signed int; safely represents operator IDs across the
            // full uint16 range (0–65535) without sign-extension above 32767.
            JAddNumberToObject(body, "operator_id",      14);
            JAddNumberToObject(body, "prev_operator_id", 14);
            if (!notecard.sendRequest(req)) {
                usbSerial.println("[NOTECARD] cnc_operator.qo template registration failed.");
            }
        }
    }
}

// ---------------------------------------------------------------------------
// fetchEnvOverrides
// ---------------------------------------------------------------------------
// Fetch env vars from Notehub. All values arrive as strings; parse with
// strtoul / strtof and validate each against a safe operating range before
// applying to cfg. The `time` argument makes fetches incremental — the
// Notecard returns a body only when variables have changed since
// g_envLastModTime.
void fetchEnvOverrides(void) {
    J *req = notecard.newRequest("env.get");
    if (req == NULL) {
        usbSerial.println("[ENV] env.get allocation failed.");
        return;
    }
    if (g_envLastModTime > 0) {
        JAddNumberToObject(req, "time", (double)g_envLastModTime);
    }
    J *rsp = notecard.requestAndResponse(req);
    if (rsp == NULL) return;

    // Check for an API-level error before reading any response fields.
    const char *errStr = JGetString(rsp, "err");
    if (errStr != NULL && errStr[0] != '\0') {
        usbSerial.print("[ENV] env.get error: ");
        usbSerial.println(errStr);
        notecard.deleteResponse(rsp);
        return;
    }

    // If body is absent, no variables have changed — nothing to do.
    J *env = JGetObject(rsp, "body");
    if (env == NULL) {
        notecard.deleteResponse(rsp);
        return;
    }

    // Capture the new modification timestamp but do NOT commit it to
    // g_envLastModTime yet. report_minutes requires a follow-on hub.set to
    // realign the Notecard outbound cadence; if that hub.set fails we must
    // leave g_envLastModTime unchanged so the next incremental env.get carries
    // the old timestamp and Notehub re-delivers this change set for retry.
    // g_envLastModTime is advanced at the bottom of this function only after
    // all side-effecting operations have succeeded.
    uint32_t newModTime  = (uint32_t)JGetInt(rsp, "time");
    bool     hubSetFailed = false;  // set true if a hub.set attempt fails below

    // Notehub environment variables always arrive as JSON strings. Parse each
    // key with strtoul() / strtof(), confirm full conversion via the endp
    // check, and clamp to a safe operating range before applying to cfg.
    const char *val;
    char       *endp;
    unsigned long ul;
    float         fv;

    val = JGetString(env, "sample_minutes");
    if (val && val[0] != '\0') {
        ul = strtoul(val, &endp, 10);
        if (endp != val && *endp == '\0' && ul >= 1 && ul <= 1440) {
            cfg.sampleMs = (uint32_t)ul * 60000UL;
        } else {
            usbSerial.println("[ENV] sample_minutes invalid (1-1440), ignored.");
        }
    }

    // report_minutes controls both the local summary window and the Notecard
    // outbound cadence. Both schedules must stay in lockstep, so cfg.reportMs
    // is updated only after a successful hub.set. On any hub.set failure,
    // hubSetFailed is set to true so that g_envLastModTime is NOT advanced at
    // the end of this function. The next incremental env.get will therefore
    // re-deliver this change set and retry hub.set automatically.
    val = JGetString(env, "report_minutes");
    if (val && val[0] != '\0') {
        ul = strtoul(val, &endp, 10);
        if (endp != val && *endp == '\0' && ul >= 1 && ul <= 1440) {
            uint32_t newReportMs = (uint32_t)ul * 60000UL;
            if (newReportMs != cfg.reportMs) {
                J *hreq = notecard.newRequest("hub.set");
                if (hreq == NULL) {
                    usbSerial.println("[ENV] hub.set allocation failed — cadence not updated.");
                    hubSetFailed = true;
                } else {
                    JAddNumberToObject(hreq, "outbound", (double)ul);
                    JAddNumberToObject(hreq, "inbound",  (double)(ul * 2));
                    if (notecard.sendRequest(hreq)) {
                        cfg.reportMs = newReportMs;
                    } else {
                        usbSerial.println("[ENV] hub.set cadence update failed — local cadence unchanged.");
                        hubSetFailed = true;
                    }
                }
            }
        } else {
            usbSerial.println("[ENV] report_minutes invalid (1-1440), ignored.");
        }
    }

    // modbus_port: changing port requires a reconnect so the live TCP session
    // uses the new address rather than continuing on the old port until failure.
    val = JGetString(env, "modbus_port");
    if (val && val[0] != '\0') {
        ul = strtoul(val, &endp, 10);
        if (endp != val && *endp == '\0' && ul >= 1 && ul <= 65535) {
            if ((uint16_t)ul != cfg.modbusPort) {
                cfg.modbusPort    = (uint16_t)ul;
                g_modbusConnected = false;   // force reconnect with new port on next poll
                usbSerial.println("[ENV] modbus_port changed — will reconnect.");
            }
        } else {
            usbSerial.println("[ENV] modbus_port invalid (1-65535), ignored.");
        }
    }

    // Modbus unit IDs 1-247 are valid per the specification; 0 and 248-255
    // are reserved and must not be used as server addresses.
    val = JGetString(env, "modbus_unit_id");
    if (val && val[0] != '\0') {
        ul = strtoul(val, &endp, 10);
        if (endp != val && *endp == '\0' && ul >= 1 && ul <= 247) {
            cfg.modbusUnitId = (uint8_t)ul;
        } else {
            usbSerial.println("[ENV] modbus_unit_id invalid (1-247), ignored.");
        }
    }

    // reg_spindle_load: base address of the contiguous six-register block.
    // Upper bound 65529 ensures the last register (base + 5) stays within the
    // 16-bit Modbus address space.
    val = JGetString(env, "reg_spindle_load");
    if (val && val[0] != '\0') {
        ul = strtoul(val, &endp, 10);
        if (endp != val && *endp == '\0' && ul <= 65529) {
            cfg.regSpindleLoad = (uint16_t)ul;
        } else {
            usbSerial.println("[ENV] reg_spindle_load invalid (0-65529), ignored.");
        }
    }

    val = JGetString(env, "spindle_overload_pct");
    if (val && val[0] != '\0') {
        fv = strtof(val, &endp);
        if (endp != val && *endp == '\0' && fv >= 0.0f && fv <= 150.0f) {
            cfg.spindleOverloadPct = fv;
        } else {
            usbSerial.println("[ENV] spindle_overload_pct invalid (0-150), ignored.");
        }
    }

    val = JGetString(env, "expected_cycle_sec");
    if (val && val[0] != '\0') {
        ul = strtoul(val, &endp, 10);
        if (endp != val && *endp == '\0' && ul >= 1 && ul <= 86400) {
            cfg.expectedCycleSec = (uint32_t)ul;
        } else {
            usbSerial.println("[ENV] expected_cycle_sec invalid (1-86400), ignored.");
        }
    }

    // Cross-validate sample/report cadence.
    //
    // run_min and idle_min are accumulated by adding cfg.sampleMs/60000 on
    // every poll. Two invariants must hold for the reported totals to be
    // meaningful:
    //   (a) sample_minutes <= report_minutes  — otherwise a single poll can
    //       add more minutes to the accumulator than the entire report window.
    //   (b) report_minutes % sample_minutes == 0 — otherwise the run+idle
    //       total will not equal the window duration even when the machine is
    //       continuously active, because the last partial sample never lands
    //       exactly on the report boundary.
    //
    // Invariant (a) is enforced by clamping sampleMs down to reportMs.
    // Invariant (b) is advisory; a warning is logged but the values are
    // accepted because the approximation is small and operator-chosen cadences
    // (e.g. 5/45) are still useful in practice.
    if (cfg.sampleMs > cfg.reportMs) {
        cfg.sampleMs = cfg.reportMs;
        usbSerial.println("[ENV] sample_minutes clamped to report_minutes — "
                          "a sample interval longer than the report window "
                          "would overcount run/idle minutes.");
    }
    if (cfg.reportMs % cfg.sampleMs != 0) {
        usbSerial.println("[ENV] WARNING: report_minutes is not a whole-number "
                          "multiple of sample_minutes — run/idle totals in "
                          "cnc_summary.qo will be approximate. Prefer cadences "
                          "where report_minutes divides sample_minutes evenly "
                          "(e.g. sample=5, report=60).");
    }

    notecard.deleteResponse(rsp);

    // Advance the applied timestamp only when every side-effecting operation
    // (hub.set) succeeded. If hubSetFailed is true, g_envLastModTime stays on
    // the previous value so the next incremental env.get re-delivers this
    // change set and retries the failed hub.set — matching the retry behavior
    // described in the README and code comments above.
    if (!hubSetFailed) {
        g_envLastModTime = newModTime;
        usbSerial.println("[ENV] Env vars applied.");
    } else {
        usbSerial.println("[ENV] Env vars partially applied — hub.set failed; will retry next fetch.");
    }
}

// ---------------------------------------------------------------------------
// modbusConnect
// ---------------------------------------------------------------------------
bool modbusConnect(void) {
    if (_modbusTCPClient.begin(_DEFAULT_CNC_IP, cfg.modbusPort)) {
        usbSerial.println("[MODBUS] Connected to CNC controller.");
        g_modbusConnected = true;
        return true;
    }
    usbSerial.println("[MODBUS] Connection failed — CNC unreachable.");
    g_modbusConnected = false;
    return false;
}

// ---------------------------------------------------------------------------
// pollCnc
// ---------------------------------------------------------------------------
// Read all 6 CNC registers in one Modbus TCP transaction.
bool pollCnc(Sample &s) {
    s.valid = false;

    // Rate-limit modbus_unreachable alarms to once per report window.
    // Declared static so the limit spans repeated pollCnc() calls.
    // lastModbusErrArmed starts false so the first failure fires immediately
    // rather than being gated for one full report window (default 60 min) while
    // millis() counts up from zero past cfg.reportMs.
    static uint32_t lastModbusErrMs    = 0;
    static bool     lastModbusErrArmed = false;

    // All 6 demo registers are contiguous starting at regSpindleLoad.
    // One requestFrom() is ~6× faster than six individual reads.
    const uint16_t regBase = cfg.regSpindleLoad;
    const int      qty     = 6;

    if (!_modbusTCPClient.requestFrom(cfg.modbusUnitId, HOLDING_REGISTERS, (int)regBase, qty)) {
        // Retry once: reconnect then re-poll.
        usbSerial.println("[MODBUS] Read error — reconnecting...");
        if (!modbusConnect() ||
            !_modbusTCPClient.requestFrom(cfg.modbusUnitId, HOLDING_REGISTERS, (int)regBase, qty)) {

            // Invalidate any in-progress cycle — the timing gap makes elapsed
            // time meaningless and would fabricate a spuriously long completion.
            g_lastCycleStartMs = 0;
            g_lastCycleState   = 0xFF;

            if (!lastModbusErrArmed || (millis() - lastModbusErrMs >= cfg.reportMs)) {
                Sample empty = {};
                if (sendAlarm("modbus_unreachable", empty)) {
                    lastModbusErrArmed = true;
                    lastModbusErrMs    = millis();
                }
            }
            return false;
        }
    }

    // Guard: verify the full register block is available before decoding.
    // A short or malformed TCP response must be discarded — reading beyond
    // what was received returns -1 values that silently corrupt telemetry.
    if (_modbusTCPClient.available() < qty) {
        usbSerial.println("[MODBUS] Short response — discarding.");
        g_lastCycleStartMs = 0;
        g_lastCycleState   = 0xFF;
        if (!lastModbusErrArmed || (millis() - lastModbusErrMs >= cfg.reportMs)) {
            Sample empty = {};
            if (sendAlarm("modbus_unreachable", empty)) {
                lastModbusErrArmed = true;
                lastModbusErrMs    = millis();
            }
        }
        return false;
    }

    // Registers use 0.1-unit scaling for percentages (demo map convention).
    int16_t rawSpindle      = (int16_t)_modbusTCPClient.read();
    // Register N+1 is the feed-rate override (0–150 % of the programmed feed
    // rate), NOT actual feed rate in engineering units (mm/min or in/min).
    // Engineering-unit feed rate is not routinely available over Modbus TCP on
    // most CNC controllers; see README §9 for the full explanation.
    int16_t rawFeedOverride = (int16_t)_modbusTCPClient.read();
    s.alarmCode  = (uint16_t)_modbusTCPClient.read();
    s.cycleState = (uint8_t)(_modbusTCPClient.read() & 0x00FF);
    s.cycleCount = (uint16_t)_modbusTCPClient.read();
    s.operatorId = (uint16_t)_modbusTCPClient.read();

    s.spindleLoadPct  = rawSpindle      / 10.0f;  // e.g. 714  → 71.4 %
    s.feedOverridePct = rawFeedOverride / 10.0f;  // e.g. 1000 → 100.0 % of programmed rate
    s.valid = true;
    return true;
}

// ---------------------------------------------------------------------------
// sendSummary
// ---------------------------------------------------------------------------
void sendSummary(void) {
    // cycle_count: controller-authoritative delta from the cycleCount register.
    // Captures short cycles that complete within a single sample interval,
    // and is not subject to the edge-detection gaps of the state-transition
    // heuristic below.
    const uint32_t cycles     = g_window.windowCycleCountDelta;
    const float spindleMean   = (g_window.runSamples > 0)
                                ? g_window.spindleSum  / g_window.runSamples
                                : 0.0f;
    const float feedOverrideMean = (g_window.runSamples > 0)
                                   ? g_window.feedOverrideSum / g_window.runSamples
                                   : 0.0f;
    // avg_cycle_sec: edge-timing heuristic derived from detected running→idle
    // cycleState transitions. Uses float so very long jobs (> 32767 s ≈ 9 h)
    // are represented correctly, matching the 14.1 (4-byte float) template
    // type. Short cycles completed within one sample interval are not captured
    // here, so this value may be 0 even when cycle_count > 0.
    const uint32_t edgeCycles = g_window.cyclesCompleted;
    const float avgCycleSec   = (edgeCycles > 0)
                                ? (float)(g_window.totalCycleMs / edgeCycles) / 1000.0f
                                : 0.0f;

    J *req = notecard.newRequest("note.add");
    if (req == NULL) {
        usbSerial.println("[APP] Summary note allocation failed — window retained.");
        return;
    }
    JAddStringToObject(req, "file", "cnc_summary.qo");
    J *body = JAddObjectToObject(req, "body");
    JAddNumberToObject(body, "spindle_pct_mean",       spindleMean);
    JAddNumberToObject(body, "spindle_pct_peak",       g_window.spindlePeak);
    JAddNumberToObject(body, "feed_override_pct_mean", feedOverrideMean);
    JAddNumberToObject(body, "run_min",          (int)g_window.runMinutes);
    JAddNumberToObject(body, "idle_min",         (int)g_window.idleMinutes);
    JAddNumberToObject(body, "cycle_count",      (int)cycles);
    JAddNumberToObject(body, "avg_cycle_sec",    avgCycleSec);
    JAddNumberToObject(body, "operator_id",      (int)g_window.operatorId);
    JAddNumberToObject(body, "alarm_count",      (int)g_window.alarmCount);
    // valid_samples == 0 means every Modbus poll failed this window;
    // downstream analytics use this to distinguish a comm outage from
    // true zero activity (CNC powered and idle).
    JAddNumberToObject(body, "valid_samples",    (int)g_window.validSamples);

    if (notecard.sendRequest(req)) {
        usbSerial.print("[APP] Summary queued: ");
        usbSerial.print(cycles);
        usbSerial.print(" cycles (reg), ");
        usbSerial.print(edgeCycles);
        usbSerial.print(" edge-timed, ");
        usbSerial.print(g_window.runMinutes);
        usbSerial.print(" run min, ");
        usbSerial.print(g_window.validSamples);
        usbSerial.println(" valid samples.");
        resetWindow();
    } else {
        // Leave window intact so accumulated data is not lost on a transient
        // I²C failure; the next report boundary will retry with combined stats.
        usbSerial.println("[APP] Summary send failed — window retained for next attempt.");
    }
}

// ---------------------------------------------------------------------------
// sendAlarm
// ---------------------------------------------------------------------------
// sync:true wakes the radio immediately rather than waiting for the outbound
// window. Returns true when the Note was successfully queued.
// A single automatic retry covers transient I²C / allocation errors without
// masking persistent failures — alarms are the highest-value events in the
// design.
bool sendAlarm(const char *alertType, const Sample &s) {
    for (int attempt = 0; attempt < 2; attempt++) {
        J *req = notecard.newRequest("note.add");
        if (req == NULL) {
            usbSerial.println("[ALARM] Request allocation failed.");
            return false;
        }
        JAddStringToObject(req, "file", "cnc_alarm.qo");
        JAddBoolToObject(req, "sync", true);
        J *body = JAddObjectToObject(req, "body");
        JAddStringToObject(body, "alert_type",  alertType);
        JAddNumberToObject(body, "alarm_code",  (int)s.alarmCode);
        JAddNumberToObject(body, "spindle_pct", s.spindleLoadPct);
        JAddNumberToObject(body, "operator_id", (int)s.operatorId);
        if (notecard.sendRequest(req)) {
            usbSerial.print("[ALARM] ");
            usbSerial.println(alertType);
            return true;
        }
        usbSerial.print("[ALARM] Send failed (attempt ");
        usbSerial.print(attempt + 1);
        usbSerial.print("): ");
        usbSerial.println(alertType);
    }
    return false;
}

// ---------------------------------------------------------------------------
// sendOperatorChange
// ---------------------------------------------------------------------------
// Emits a cnc_operator.qo Note immediately (sync) when the operator_id
// register transitions. Events are best-effort: a failed send is logged but
// not buffered for retry here because only current state is meaningful for a
// "who is logged in" signal. The hourly cnc_summary.qo captures the most
// recently observed operator ID as a window-close snapshot — it does not
// reconstruct the full sequence of transitions that occurred mid-window, and
// any change that occurs and reverts between consecutive polls is never
// recorded.
void sendOperatorChange(uint16_t prevId, uint16_t newId) {
    J *req = notecard.newRequest("note.add");
    if (req == NULL) {
        usbSerial.println("[OPERATOR] note.add allocation failed — event dropped.");
        return;
    }
    JAddStringToObject(req, "file", "cnc_operator.qo");
    JAddBoolToObject(req, "sync", true);
    J *body = JAddObjectToObject(req, "body");
    JAddNumberToObject(body, "operator_id",      (int)newId);
    JAddNumberToObject(body, "prev_operator_id", (int)prevId);
    if (notecard.sendRequest(req)) {
        usbSerial.print("[OPERATOR] ID change: ");
        usbSerial.print(prevId);
        usbSerial.print(" -> ");
        usbSerial.println(newId);
    } else {
        usbSerial.println("[OPERATOR] Operator-change Note failed — event dropped; summary still captures current ID.");
    }
}

// ---------------------------------------------------------------------------
// resetWindow
// ---------------------------------------------------------------------------
void resetWindow(void) {
    memset(&g_window, 0, sizeof(g_window));
    // g_lastCycleState, g_lastCycleCount, and g_lastOperatorId are intentionally
    // NOT reset here — they persist across report windows so that:
    //   - the first sample of a new window does not trigger spurious edge detections,
    //   - the register-delta baseline remains valid across the window boundary, and
    //   - operator-ID changes are tracked continuously across hourly summaries.
}
