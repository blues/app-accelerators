/***************************************************************************
  lift_station_monitor.ino — Municipal Wastewater Lift Station Monitor

  Reads a submersible wet-well level sensor (4-20 mA pressure transducer),
  two split-core current transformers (one per pump), and a high-water float
  switch. Runs edge logic every 60 s to detect three fault conditions:
    • pump_fail_to_start  — level above threshold, no pump drawing current
    • pump_clog           — pump running but wet-well level still rising
    • high_water_alarm    — hardwired float switch triggered

  Detected faults → note.add(sync:true) for immediate delivery to Notehub.
  Hourly aggregates → templated lift_summary.qo note (batched cellular sync).

  All thresholds are tunable via Notehub environment variables without a
  firmware reflash. Between 60-second samples the host sleeps in STM32 STOP2
  and is woken by the Notecard's ATTN pin (card.attn "sleep"); RAM is
  retained, so application state simply lives in memory (see cx_sleep.h).

  Hardware:
    - Notecarrier CX (onboard STM32L433 host)
    - Notecard for Skylo NOTE-NBGLWX (M.2 slot) — cellular + WiFi + Skylo
      satellite (NTN) on one board; card.transport selects cellular-primary
      with automatic satellite fallback (see notecardConfigure()).
    - 4-20 mA submersible level transducer  → A0 (150 Ω shunt to GND)
    - SCT-013-030 split-core CT, pump 1     → A1 (bias circuit)
    - SCT-013-030 split-core CT, pump 2     → A2 (bias circuit)
    - SPST float switch (N.O., active-low)  → D2 (INPUT_PULLUP)
    - Jumper: Notecarrier CX ATTN → D5 (same 16-pin header). Leave EN
      unconnected — on the CX it enables the shared 3.3 V VIO rail.

  Build: Tools > USB support (if available) > None (usb=none). With the USB
  CDC stack enabled the USB wakeup interrupt exits STOP2 immediately. Debug
  output goes to the LPUART on the CX debug jack (ST-LINK virtual COM port).
***************************************************************************/

#include <Notecard.h>
#include "lift_station_monitor_helpers.h"
#include "cx_sleep.h"

// ---------------------------------------------------------------------------
// Global variable definitions — externed in lift_station_monitor_helpers.h
// ---------------------------------------------------------------------------
AppState  g_state;
Notecard  notecard;
Uart      debugSerial(PIN_VCP_RX, PIN_VCP_TX);

// Env-var overridable thresholds (defaults match Notehub env var defaults).
// Mirrored in g_state.cfg_* as last-known-good values so a failed env.get
// retains the last operator-configured value rather than reverting to
// compile-time defaults.
float    g_pump_on_amps         =  3.0f;  // A below which pump is "off"
float    g_high_level_pct       = 85.0f;  // % full → fail-to-start check
float    g_rising_rate_pct      =  2.0f;  // % per cycle = "rising"
uint32_t g_summary_interval_min = 60;     // minutes between summary notes
uint32_t g_inbound_interval_min = INBOUND_INTERVAL_MIN;

// ---------------------------------------------------------------------------
// Forward declarations for sketch-local functions
// ---------------------------------------------------------------------------
void runSampleCycle(void);
void runDetectionCycle(float level_pct, bool level_valid,
                       float p1_a, bool ct1_valid,
                       float p2_a, bool ct2_valid,
                       bool float_sw);
bool sendAlert(const char *type, float level_pct,
               float p1_a, float p2_a, bool float_sw);
bool sendSummary(float level_pct, float p1_a, float p2_a, bool float_sw);

// ---------------------------------------------------------------------------
// setup() — runs once at power-up. The host resumes in place after each
// STOP2 sleep, so one-time Notecard configuration lives here and every
// per-wake step lives in loop().
// ---------------------------------------------------------------------------
void setup() {
    debugSerial.begin(115200);

    // Use 12-bit ADC resolution for maximum level-sensor precision
    analogReadResolution(12);

    // Float switch: pull-up, active-low (switch closing pulls pin to GND)
    pinMode(PIN_FLOAT_SWITCH, INPUT_PULLUP);

    // Zero state and seed the last-known-good config with compile-time
    // defaults so the first wake after a failed env.get uses sane values
    // rather than zeroed garbage.
    memset(&g_state, 0, sizeof(g_state));
    g_state.cfg_pump_on_amps         = g_pump_on_amps;
    g_state.cfg_high_level_pct       = g_high_level_pct;
    g_state.cfg_rising_rate_pct      = g_rising_rate_pct;
    g_state.cfg_summary_interval_min = g_summary_interval_min;
    g_state.cfg_inbound_interval_min = g_inbound_interval_min;

    // Initialize I²C channel to Notecard
    notecard.begin();

    // One-time Notecard configuration (hub.set, card.transport, accelerometer).
    notecardConfigure();

    // Arm the ATTN wake: the Notecard raises ATTN (D5) to end each sleep.
    cxSleepBegin();
}

// ---------------------------------------------------------------------------
// loop() — one wake cycle, then sleep in STOP2 until the Notecard raises ATTN.
//
// Each pass retries template registration until it succeeds, pulls env-var
// changes from Notehub, re-applies hub.set if a cadence changed, runs a full
// sample cycle, and then asks the Notecard to hold ATTN low for
// SAMPLE_INTERVAL_SEC. Execution resumes on the line after the sleep call
// with g_state intact, so env-var edits made during a bench run (e.g. setting
// high_level_pct = 1.0 to trigger pump_fail_to_start) take effect on the next
// cycle without a power-cycle.
// ---------------------------------------------------------------------------
void loop() {
    // Attempt template registration on every wake until both templates succeed.
    if (!g_state.templates_registered) {
        if (defineTemplates()) {
            g_state.templates_registered = true;
        }
    }

    // Pull environment variables from Notehub on every wake so threshold
    // changes deployed via the Notehub UI take effect within one cycle.
    fetchEnvOverrides();

    // Re-apply hub.set whenever summary_interval_min or inbound_interval_min
    // changes, or on the first wake when applied_* are still 0. Includes
    // PRODUCT_UID so any successful hub.set fully binds the device.
    applyHubSetIfChanged();

    // Run one complete sensor-read and evaluation cycle.
    runSampleCycle();

    // Sleep until the Notecard raises ATTN SAMPLE_INTERVAL_SEC from now.
    if (!cxSleepUntilAttn(notecard, SAMPLE_INTERVAL_SEC, NULL, &debugSerial)) {
        // The Notecard didn't take the sleep request, or ATTN never went low
        // (check the ATTN -> D5 jumper). Keep the sample cadence and try again.
        debugSerial.println("[SLEEP] ATTN sleep failed — waiting out the interval awake.");
        delay(SAMPLE_INTERVAL_SEC * 1000UL);
    }
}

// ---------------------------------------------------------------------------
// runSampleCycle — one complete sensor-read, accumulation, and evaluation pass.
// Called once per wake from loop(), before the host goes back to sleep.
// ---------------------------------------------------------------------------
void runSampleCycle(void) {
    // ---- Read all sensors ------------------------------------------------
    bool  level_valid, ct1_valid, ct2_valid;
    float level_pct = readLevelPct(&level_valid);
    float p1_amps   = readPumpAmps(PIN_PUMP1_CT, &ct1_valid);
    float p2_amps   = readPumpAmps(PIN_PUMP2_CT, &ct2_valid);
    bool  float_sw  = readFloatSwitch();

#ifdef BENCH_FORCE_LEVEL_PCT
    // Bench-test override: bypass ADC and inject a synthetic level reading.
    // Pair with high_level_pct = 1.0 in Notehub to guarantee pump_fail_to_start.
    // Remove before flashing to a deployed station (see lift_station_monitor_helpers.h).
    level_pct   = (float)(BENCH_FORCE_LEVEL_PCT);
    level_valid = true;
    debugSerial.print("[BENCH] level overridden to "); debugSerial.print(level_pct); debugSerial.println("%");
#endif

    if (!level_valid) {
        debugSerial.print("[SENSOR] level=FAULT (ADC out of range) ");
        g_state.level_fault_count++;
    } else {
        debugSerial.print("[SENSOR] level="); debugSerial.print(level_pct); debugSerial.print("% ");
    }
    if (!ct1_valid) { debugSerial.print("p1=FAULT "); g_state.ct1_fault_count++; }
    else            { debugSerial.print("p1="); debugSerial.print(p1_amps); debugSerial.print("A "); }
    if (!ct2_valid) { debugSerial.print("p2=FAULT "); g_state.ct2_fault_count++; }
    else            { debugSerial.print("p2="); debugSerial.print(p2_amps); debugSerial.print("A "); }
    debugSerial.print("float="); debugSerial.println(float_sw ? "ALARM" : "OK");

    // ---- Accumulate for hourly summary -----------------------------------
    // Only valid readings are added to their respective sums so window averages
    // are not skewed by fault-sentinel values. pump*_run_samples counts only
    // samples where the CT confirmed the pump was above the current threshold;
    // invalid CT samples are excluded so run-time minutes are not understated
    // by CT faults.
    if (level_valid) {
        g_state.sum_level_pct += level_pct;
    }
    if (ct1_valid) {
        g_state.sum_pump1_amps += p1_amps;
        if (p1_amps >= g_pump_on_amps) g_state.pump1_run_samples++;
    }
    if (ct2_valid) {
        g_state.sum_pump2_amps += p2_amps;
        if (p2_amps >= g_pump_on_amps) g_state.pump2_run_samples++;
    }
    g_state.sample_count++;

    // ---- Fault detection -------------------------------------------------
    runDetectionCycle(level_pct, level_valid,
                      p1_amps, ct1_valid,
                      p2_amps, ct2_valid,
                      float_sw);

    // ---- Hourly summary --------------------------------------------------
    uint32_t summary_cycle_target =
        (g_summary_interval_min * 60UL) / SAMPLE_INTERVAL_SEC;
    if (g_state.sample_count >= summary_cycle_target) {
        // Accumulators are cleared only after a successful note.add. If the
        // Notecard is temporarily unreachable, the accumulated window data is
        // preserved and the send is retried on the next cycle.
        if (sendSummary(level_pct, p1_amps, p2_amps, float_sw)) {
            g_state.sum_level_pct     = 0.0f;
            g_state.sum_pump1_amps    = 0.0f;
            g_state.sum_pump2_amps    = 0.0f;
            g_state.sample_count      = 0;
            g_state.pump1_run_samples = 0;
            g_state.pump2_run_samples = 0;
            g_state.alert_count       = 0;
            g_state.level_fault_count = 0;
            g_state.ct1_fault_count   = 0;
            g_state.ct2_fault_count   = 0;
        }
    }

    // Record the level for next cycle's clog-trend comparison only when the
    // reading was valid. An invalid reading must not overwrite a good baseline,
    // and have_prev_level is cleared so the clog rule waits for the next valid
    // sample pair rather than comparing against stale data.
    if (level_valid) {
        g_state.prev_level_pct  = level_pct;
        g_state.have_prev_level = true;
    } else {
        g_state.have_prev_level = false;
    }

    // Persist whether at least one pump was confirmed running this cycle.
    // The clog rule uses this on the next wake to require two consecutive
    // running samples before evaluating the rising-level condition — a pump
    // that only just started cannot reasonably be expected to have reduced
    // the wet-well level yet.
    // A CT-invalid reading is treated as "not confirmed running": if CT1 is
    // faulted, pump1 is unknown and is not counted as running here.
    bool p1_on = ct1_valid && (p1_amps >= g_pump_on_amps);
    bool p2_on = ct2_valid && (p2_amps >= g_pump_on_amps);
    g_state.prev_any_on = p1_on || p2_on;
}

// ---------------------------------------------------------------------------
// runDetectionCycle — three independent fault rules, each with cooldown.
//
// level_valid must be true before evaluating rules that depend on wet-well
// level (Rules 2 and 3). An invalid level reading is already counted in
// level_fault_count; it is not surfaced as a fault detection because the
// true fill state is unknown.
//
// ct1_valid / ct2_valid must be checked before trusting p1_a / p2_a. A
// faulted CT returns CT_INVALID_SENTINEL (~0 A or −9999 A) which, if
// consumed without validity gating, would cause "pump off" to be asserted
// falsely and could trigger pump_fail_to_start even when a pump is running.
//
// Alert cooldowns are armed only after a successful sendAlert() so that a
// transient I²C fault or Notecard error does not suppress a real alarm for
// 30 minutes. A failed send leaves the cooldown at zero; the alert is retried
// on the next 60-second cycle.
// ---------------------------------------------------------------------------
void runDetectionCycle(float level_pct, bool level_valid,
                       float p1_a, bool ct1_valid,
                       float p2_a, bool ct2_valid,
                       bool float_sw) {
    // Decrement cooldown counters each cycle
    if (g_state.cooldown_fail_to_start > 0) g_state.cooldown_fail_to_start--;
    if (g_state.cooldown_clog > 0)          g_state.cooldown_clog--;
    if (g_state.cooldown_high_water > 0)    g_state.cooldown_high_water--;

    // A pump is only considered "on" when its CT returned a valid reading and
    // that reading meets the current threshold. A faulted CT cannot confirm
    // pump state; treating it as "off" prevents a broken sensor from masking
    // real pump activity in the clog rule or from suppressing a genuine
    // fail-to-start alert by falsely asserting "pump is on".
    bool p1_on  = ct1_valid && (p1_a >= g_pump_on_amps);
    bool p2_on  = ct2_valid && (p2_a >= g_pump_on_amps);
    bool any_on = p1_on || p2_on;

    // Rule 1: High-water alarm — float switch triggered.
    // This rule is independent of the level sensor and CT channels; the
    // hardwired float switch is authoritative and fires regardless of ADC
    // validity. level_pct may be LEVEL_INVALID_SENTINEL when the sensor is
    // faulted; p*_a may be CT_INVALID_SENTINEL when a CT is faulted.
    // Downstream analytics interpret sentinels as "value unknown at alarm time".
    if (float_sw && g_state.cooldown_high_water == 0) {
        if (sendAlert("high_water_alarm", level_pct, p1_a, p2_a, float_sw)) {
            g_state.alert_count++;
            g_state.cooldown_high_water = ALERT_COOLDOWN_CYCLES;
        }
        // No cooldown on failure: allow retry next cycle so a transient send
        // error does not suppress a real high-water alarm for 30 minutes.
    }

    // Rule 2: Pump fail-to-start — wet well is deep but no pump is drawing
    // current.
    //
    // Guards:
    //   level_valid         — true fill level must be known.
    //   ct1_valid && ct2_valid — both CTs must return valid readings before
    //     asserting "no pump is on". A single faulted CT reads CT_INVALID_SENTINEL
    //     which the any_on logic treats as "off"; if that pump is actually running,
    //     requiring both CTs to be valid prevents a broken sensor from generating a
    //     false fail-to-start alert.
    if (level_valid && ct1_valid && ct2_valid &&
        !any_on && level_pct >= g_high_level_pct &&
        g_state.cooldown_fail_to_start == 0) {
        if (sendAlert("pump_fail_to_start", level_pct, p1_a, p2_a, float_sw)) {
            g_state.alert_count++;
            g_state.cooldown_fail_to_start = ALERT_COOLDOWN_CYCLES;
        }
    }

    // Rule 3: Pump clog — pump running but level is still climbing.
    // A rising delta between consecutive samples while a pump draws current
    // indicates discharge rate < inflow rate — consistent with a clogged
    // discharge line, a stuck check valve, or a worn impeller.
    //
    // Guards:
    //   level_valid      — current reading must not be a sensor fault.
    //   have_prev_level  — false until the second valid sample; prevents a
    //     spurious alert when prev_level_pct has not yet been established.
    //   prev_any_on      — a pump must have been confirmed running on the
    //     previous sample as well. On a pump-off → pump-on transition the wet
    //     well can still be rising from inflow inertia; the pump has not had
    //     time to overcome that inflow and lower the level. Without this guard
    //     the rule would fire on the very first sample after pump start whenever
    //     the prior level reading happened to be lower.
    //   any_on           — a pump must still be confirmed running now.
    if (level_valid && g_state.have_prev_level &&
        g_state.prev_any_on && any_on) {
#ifdef BENCH_CLOG_DELTA
        // Bench-test override: inject a synthetic level rise so the clog rule
        // fires when a CT reads above pump_on_amps for two consecutive cycles.
        // Remove before flashing to a deployed station.
        float delta = (float)(BENCH_CLOG_DELTA);
        debugSerial.print("[BENCH] clog delta overridden to "); debugSerial.println(delta);
#else
        float delta = level_pct - g_state.prev_level_pct;
#endif
        if (delta >= g_rising_rate_pct && g_state.cooldown_clog == 0) {
            if (sendAlert("pump_clog", level_pct, p1_a, p2_a, float_sw)) {
                g_state.alert_count++;
                g_state.cooldown_clog = ALERT_COOLDOWN_CYCLES;
            }
        }
    }
}

// ---------------------------------------------------------------------------
// sendAlert — note.add to lift_alert.qo with sync:true for immediate delivery.
// Returns true when the note was successfully queued. The caller increments
// alert_count and arms the dedup cooldown only on success so a transient
// I²C fault or Notecard error does not suppress real alarms for 30 minutes.
//
// For high_water_alarm (Rule 1), level_pct may be LEVEL_INVALID_SENTINEL and
// p1_a / p2_a may be CT_INVALID_SENTINEL when the float switch fires while the
// corresponding sensor is also faulted. Rules 2 and 3 gate on sensor validity
// before calling this function, so their calls always carry real readings.
// ---------------------------------------------------------------------------
bool sendAlert(const char *type, float level_pct,
               float p1_a, float p2_a, bool float_sw) {
    J *req = notecard.newRequest("note.add");
    JAddStringToObject(req, "file", NOTEFILE_ALERT);
    JAddBoolToObject(req,   "sync", true);   // bypass outbound interval; wake radio now
    J *body = JAddObjectToObject(req, "body");
    JAddStringToObject(body, "alert",      type);
    JAddNumberToObject(body, "level_pct",  level_pct);  // may be LEVEL_INVALID_SENTINEL
    JAddNumberToObject(body, "pump1_amps", p1_a);
    JAddNumberToObject(body, "pump2_amps", p2_a);
    JAddBoolToObject(body,   "float_sw",   float_sw);
    // Alerts are safety-critical; retry briefly so a transient I²C hiccup
    // does not silently drop a fault notification.
    bool ok = notecard.sendRequestWithRetry(req, 5);
    if (!ok) {
        debugSerial.print("[ALERT] note.add failed for: "); debugSerial.println(type);
    } else {
        debugSerial.print("[ALERT] "); debugSerial.println(type);
    }
    return ok;
}

// ---------------------------------------------------------------------------
// sendSummary — hourly aggregated note to lift_summary.qo (batched sync).
// Returns true when the note was successfully queued. The caller must not
// reset accumulators on false so the window data is preserved and the send
// is retried on the next cycle.
//
// level_avg_pct is computed over valid level samples only (sample_count minus
// level_fault_count). LEVEL_INVALID_SENTINEL is emitted when the entire window
// had no valid level readings. The same pattern applies to CT channels:
// pump*_amps_avg is computed over valid CT samples only; CT_INVALID_SENTINEL
// is emitted when every sample in the window was a CT fault. ct*_faults
// gives operators direct visibility into how many samples were excluded.
// ---------------------------------------------------------------------------
bool sendSummary(float level_pct, float p1_a, float p2_a, bool float_sw) {
    // Compute average level over valid samples only.
    uint32_t valid_level_n = (g_state.sample_count > g_state.level_fault_count)
                             ? (g_state.sample_count - g_state.level_fault_count)
                             : 0;
    float avg_level = (valid_level_n > 0)
                      ? (g_state.sum_level_pct / (float)valid_level_n)
                      : LEVEL_INVALID_SENTINEL;  // entire window was faulted

    // Compute average pump current over valid CT samples only.
    // sum_pump*_amps already excludes invalid CT readings (see runSampleCycle).
    uint32_t valid_ct1_n = (g_state.sample_count > g_state.ct1_fault_count)
                           ? (g_state.sample_count - g_state.ct1_fault_count)
                           : 0;
    uint32_t valid_ct2_n = (g_state.sample_count > g_state.ct2_fault_count)
                           ? (g_state.sample_count - g_state.ct2_fault_count)
                           : 0;
    float avg_p1 = (valid_ct1_n > 0)
                   ? (g_state.sum_pump1_amps / (float)valid_ct1_n)
                   : CT_INVALID_SENTINEL;  // all CT1 samples faulted this window
    float avg_p2 = (valid_ct2_n > 0)
                   ? (g_state.sum_pump2_amps / (float)valid_ct2_n)
                   : CT_INVALID_SENTINEL;  // all CT2 samples faulted this window

    // Convert run-sample counts to minutes
    float p1_run_min = g_state.pump1_run_samples * (SAMPLE_INTERVAL_SEC / 60.0f);
    float p2_run_min = g_state.pump2_run_samples * (SAMPLE_INTERVAL_SEC / 60.0f);

    J *req = notecard.newRequest("note.add");
    JAddStringToObject(req, "file", NOTEFILE_SUMMARY);
    J *body = JAddObjectToObject(req, "body");
    JAddNumberToObject(body, "level_pct",      level_pct);   // may be LEVEL_INVALID_SENTINEL
    JAddNumberToObject(body, "level_avg_pct",  avg_level);   // sentinel when no valid samples
    JAddNumberToObject(body, "pump1_amps_avg", avg_p1);      // may be CT_INVALID_SENTINEL
    JAddNumberToObject(body, "pump2_amps_avg", avg_p2);      // may be CT_INVALID_SENTINEL
    JAddNumberToObject(body, "pump1_run_min",  p1_run_min);
    JAddNumberToObject(body, "pump2_run_min",  p2_run_min);
    JAddBoolToObject(body,   "float_sw",       float_sw);
    JAddNumberToObject(body, "alert_count",    (int)g_state.alert_count);
    JAddNumberToObject(body, "level_faults",   (int)g_state.level_fault_count);
    JAddNumberToObject(body, "ct1_faults",     (int)g_state.ct1_fault_count);
    JAddNumberToObject(body, "ct2_faults",     (int)g_state.ct2_fault_count);
    bool ok = notecard.sendRequest(req);
    if (!ok) {
        debugSerial.println("[SUMMARY] note.add failed; accumulators preserved for retry.");
    } else {
        debugSerial.println("[SUMMARY] Hourly summary queued.");
    }
    return ok;
}
