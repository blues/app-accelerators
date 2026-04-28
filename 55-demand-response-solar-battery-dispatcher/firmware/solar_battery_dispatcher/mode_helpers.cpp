// mode_helpers.cpp
//
// UTC time utilities, operating-mode resolution with TOU schedule and SOC
// guards, relay output control, and outbound note emission for the
// solar_battery_dispatcher sketch.

#include "dispatcher.h"

// -------- Module-private SOC hysteresis latches -----------------------------
// Both latches are isolated to this module because they are read and written
// exclusively by resolveMode() and applyRelays().
static bool s_in_soc_protect    = false; // lower-SOC hysteresis latch
static bool s_in_charge_inhibit = false; // upper-SOC hysteresis latch

// -------- UTC time utilities ------------------------------------------------
// Extract the hour-of-day (0–23) from a Unix epoch.
static uint8_t utcHour(uint32_t epoch) {
    return (uint8_t)((epoch % 86400UL) / 3600UL);
}

// Returns true when `hour` falls inside a TOU window.
// start == end → always false (window disabled — the shipped default).
// start < end  → contiguous window (e.g. 08:00–18:00).
// start > end  → wraps midnight (e.g. 21:00–02:00).
static bool isInPeakWindow(uint8_t hour, uint8_t start_utc, uint8_t end_utc) {
    if (start_utc == end_utc) return false;   // disabled
    if (start_utc < end_utc)  return (hour >= start_utc && hour < end_utc);
    return (hour >= start_utc || hour < end_utc);  // wraps midnight
}

// -------- Mode name ---------------------------------------------------------
const char *modeName(DispatchMode m) {
    switch (m) {
        case MODE_NORMAL:           return "normal";
        case MODE_PEAK_DISCHARGE:   return "peak_discharge";
        case MODE_OVERNIGHT_CHARGE: return "overnight_charge";
        case MODE_DR_CURTAIL:       return "dr_curtail";
        case MODE_LOW_SOC_PROTECT:  return "low_soc_protect";
        case MODE_FORCED_NORMAL:    return "normal";   // internal; always "normal" on wire
        default:                    return "unknown";
    }
}

// -------- UTC time ----------------------------------------------------------
uint32_t currentUtcEpoch() {
    static uint32_t s_cached_epoch    = 0;   // last epoch returned by card.time
    static uint32_t s_cached_epoch_ms = 0;   // millis() at which it was latched

    J *req = notecard.newRequest("card.time");
    if (req) {
        J *rsp = notecard.requestAndResponse(req);
        if (rsp) {
            const bool     err = notecard.responseError(rsp);
            const uint32_t t   = err ? 0 : (uint32_t)JGetNumber(rsp, "time");
            notecard.deleteResponse(rsp);
            if (t > 0) {
                s_cached_epoch    = t;
                s_cached_epoch_ms = millis();
                return t;
            }
        }
    }

    // Failure path: once a valid time has been obtained at least once,
    // extrapolate rather than returning 0. A transient I²C fault or
    // connectivity gap must not disable expires_epoch tracking or TOU window
    // evaluation. The ±1 s extrapolation error over any realistic outage
    // window is negligible for hour-granularity TOU scheduling.
    if (s_cached_epoch > 0) {
        return s_cached_epoch + (millis() - s_cached_epoch_ms) / 1000UL;
    }
    return 0;   // never acquired a valid time (pre-first-sync)
}

// -------- Mode resolution ---------------------------------------------------
// Priority: cloud dispatch > TOU peak window > TOU off-peak charge > NORMAL.
//
// Cloud dispatch commands are stored in g_commanded_mode. The "normal" command
// maps to MODE_FORCED_NORMAL — a distinct state that is != MODE_NORMAL so it
// actively suppresses TOU evaluation for the duration of any expires_epoch
// window. When the window expires, g_commanded_mode reverts to MODE_NORMAL and
// TOU evaluation resumes.
//
// Lower SOC guard with hysteresis: MODE_LOW_SOC_PROTECT overrides discharge
// modes when SOC < soc_min_pct or the BMS is unreachable. The guard latches
// and releases only after SOC recovers to soc_min_pct + soc_hyst_pct to
// prevent relay chatter when readings oscillate near the threshold.
// DR_CURTAIL restricts export only and does not enable discharge, so it is
// excluded from the would_discharge check.
DispatchMode resolveMode(uint32_t utc_epoch, bool bms_valid, float soc_pct) {
    // Expire a timed dispatch; device falls back to the TOU schedule.
    if (g_dr_expires_epoch > 0 && utc_epoch >= g_dr_expires_epoch) {
        g_commanded_mode   = MODE_NORMAL;
        g_dr_expires_epoch = 0;
        usbSerial.println("[dispatch] timed dispatch expired -> scheduled");
    }

    // Determine the candidate mode. Any g_commanded_mode != MODE_NORMAL
    // (including MODE_FORCED_NORMAL) suppresses TOU evaluation. TOU windows
    // default to disabled (start == end == 0) and engage only after an
    // operator sets them via env vars.
    DispatchMode candidate = MODE_NORMAL;
    if (g_commanded_mode != MODE_NORMAL) {
        candidate = g_commanded_mode;
    } else if (utc_epoch > 0 && isInPeakWindow(utcHour(utc_epoch), g_peak_start_utc, g_peak_end_utc)) {
        candidate = MODE_PEAK_DISCHARGE;
    } else if (utc_epoch > 0 && isInPeakWindow(utcHour(utc_epoch), g_charge_start_utc, g_charge_end_utc)) {
        candidate = MODE_OVERNIGHT_CHARGE;
    }

    bool would_discharge = (candidate == MODE_PEAK_DISCHARGE);
    if (would_discharge) {
        if (!bms_valid || soc_pct < g_soc_min_pct) {
            s_in_soc_protect = true;   // enter or hold protect
        } else if (s_in_soc_protect && soc_pct >= (g_soc_min_pct + g_soc_hyst_pct)) {
            s_in_soc_protect = false;  // SOC recovered past hysteresis band
        }
        if (s_in_soc_protect) return MODE_LOW_SOC_PROTECT;
    } else {
        // Not a discharge mode — clear latch so the next discharge-window entry
        // starts with a fresh SOC check.
        s_in_soc_protect = false;
    }

    return candidate;
}

// -------- Relay control -----------------------------------------------------
// Maps the resolved operating mode onto the four relay outputs.
//
// The primary lower-SOC guard lives in resolveMode() (which returns
// MODE_LOW_SOC_PROTECT when needed); applyRelays() retains a belt-and-
// suspenders discharge check for the peak_discharge case.
//
// Upper-SOC charge-inhibit latch: s_in_charge_inhibit uses a deadband that
// engages when SOC reaches soc_max_pct and releases only after SOC drops to
// soc_max_pct - soc_max_hyst_pct (default 92 %). This prevents RELAY3 chatter
// when BMS readings oscillate near the ceiling. Because the latch operates
// entirely inside applyRelays() without changing the resolved mode returned by
// resolveMode(), it does not affect the wire-name mode-change comparison in
// loop() and does not generate dr_event.qo events; its effect is relay-only.
//
// Fail-safe on BMS comm loss: when bms_valid is false, s_in_charge_inhibit is
// forced active — we cannot confirm the battery has cleared the charge ceiling,
// so charging is inhibited. This mirrors the discharge path (BMS comm loss →
// resolveMode returns LOW_SOC_PROTECT). Both guards now fail safe symmetrically.
void applyRelays(DispatchMode mode, float soc_pct, bool bms_valid) {
    bool low_soc = (soc_pct < g_soc_min_pct);

    if (!bms_valid) {
        // BMS unreachable: cannot confirm SOC has cleared the charge ceiling.
        // Engage inhibit (or hold it if already active). The latch releases
        // only once BMS comms are restored and SOC confirms it has dropped
        // through the soc_max_hyst_pct deadband on the same poll cycle.
        s_in_charge_inhibit = true;
    } else if (soc_pct >= g_soc_max_pct) {
        s_in_charge_inhibit = true;    // engage: SOC at or above ceiling
    } else if (s_in_charge_inhibit && soc_pct <= (g_soc_max_pct - g_soc_max_hyst_pct)) {
        s_in_charge_inhibit = false;   // release: SOC dropped through deadband
    }
    bool full_soc = s_in_charge_inhibit;

    bool export_en    = true;
    bool discharge_en = false;
    bool charge_en    = false;
    bool dr_active    = false;

    switch (mode) {
        case MODE_FORCED_NORMAL:  // cloud-commanded normal; identical relay map to MODE_NORMAL
        case MODE_NORMAL:
            export_en = true;  discharge_en = false;     charge_en = !full_soc; break;
        case MODE_PEAK_DISCHARGE:
            export_en = true;  discharge_en = !low_soc;  charge_en = false;     break;
        case MODE_OVERNIGHT_CHARGE:
            export_en = true;  discharge_en = false;     charge_en = !full_soc; break;
        case MODE_DR_CURTAIL:
            export_en = false; discharge_en = false;     charge_en = false; dr_active = true; break;
        case MODE_LOW_SOC_PROTECT:
        default:
            export_en = true;  discharge_en = false;     charge_en = !full_soc; break;
    }

    if (low_soc) discharge_en = false;   // belt-and-suspenders SOC hard-guard

    digitalWrite(RELAY_GRID_EXPORT,    export_en    ? HIGH : LOW);
    digitalWrite(RELAY_BATT_DISCHARGE, discharge_en ? HIGH : LOW);
    digitalWrite(RELAY_BATT_CHARGE,    charge_en    ? HIGH : LOW);
    digitalWrite(RELAY_DR_INDICATOR,   dr_active    ? HIGH : LOW);
}

// -------- Outbound note emission --------------------------------------------
void sendTelemetry() {
    // Emit -9999 sentinel when a device was unreachable so downstream
    // analytics can distinguish "sensor failed" from a true zero or negative.
    static uint8_t s_fail_count = 0;
    J *req = notecard.newRequest("note.add");
    if (!req) return;
    JAddStringToObject(req, "file", "solar_telemetry.qo");
    J *body = JAddObjectToObject(req, "body");
    JAddNumberToObject(body, "pv_w",         g_inv.valid ? g_inv.pv_w     : -9999);
    JAddNumberToObject(body, "ac_out_w",     g_inv.valid ? g_inv.ac_out_w : -9999);
    JAddNumberToObject(body, "grid_w",       g_inv.valid ? g_inv.grid_w   : -9999);
    JAddNumberToObject(body, "batt_soc_pct", g_bms.valid ? g_bms.soc_pct  : -9999);
    JAddNumberToObject(body, "batt_v",       g_bms.valid ? g_bms.batt_v   : -9999);
    JAddNumberToObject(body, "batt_a",       g_bms.valid ? g_bms.batt_a   : -9999);
    JAddStringToObject(body, "mode",         modeName(g_active_mode));
    // No sync:true — the periodic outbound cadence batches these into one session.
    if (!notecard.sendRequest(req)) {
        usbSerial.print("[notecard] solar_telemetry.qo send failed (count: ");
        usbSerial.print(++s_fail_count);
        usbSerial.println(")");
    } else {
        s_fail_count = 0;
    }
}

// Mode-change events ship immediately (sync:true) so operators and cloud
// systems see transitions within one Notecard session-establishment window.
void sendModeEvent(DispatchMode new_mode, DispatchMode old_mode) {
    J *req = notecard.newRequest("note.add");
    if (!req) return;
    JAddStringToObject(req, "file", "dr_event.qo");
    JAddBoolToObject  (req, "sync", true);
    J *body = JAddObjectToObject(req, "body");
    JAddStringToObject(body, "new_mode",     modeName(new_mode));
    JAddStringToObject(body, "prev_mode",    modeName(old_mode));
    JAddNumberToObject(body, "batt_soc_pct", g_bms.valid ? g_bms.soc_pct : -9999);
    JAddNumberToObject(body, "pv_w",         g_inv.valid ? g_inv.pv_w    : -9999);
    JAddNumberToObject(body, "grid_w",       g_inv.valid ? g_inv.grid_w  : -9999);
    if (!notecard.sendRequest(req)) {
        usbSerial.print("[notecard] dr_event.qo send failed (mode: ");
        usbSerial.print(modeName(new_mode));
        usbSerial.println(")");
    }
}
