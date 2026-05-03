/***************************************************************************
  transformer_load_monitor_helpers.cpp — Notecard, sensor, alert, and
  summary helper implementations for the Utility Distribution Transformer
  Load Monitor sketch.

  All functions declared in transformer_load_monitor_helpers.h are
  implemented here, keeping transformer_load_monitor.ino focused on
  setup(), loop(), and high-level orchestration.
***************************************************************************/

#include "transformer_load_monitor_helpers.h"

// ---------------------------------------------------------------------------
// Notecard hub configuration — called on cold boot and on any subsequent
// wake where a previous attempt was not confirmed.  Returns true when hub.set
// is acknowledged by the Notecard.
// ---------------------------------------------------------------------------
bool hubConfigure(const char *product_uid) {
    // An empty ProductUID means the device cannot associate with any Notehub
    // project.  Sending hub.set without a product would mark hub_configured=true
    // while leaving the Notecard stranded — treat it as a fatal configuration
    // error and refuse to proceed so the operator sees a clear serial message
    // rather than a silently misconfigured deployment.
    if (!product_uid || product_uid[0] == '\0') {
        Serial.println("[init] ERROR: PRODUCT_UID is empty — hub.set skipped. "
                       "Set PRODUCT_UID in the sketch and reflash before deploying.");
        return false;
    }
    J *req = notecard.newRequest("hub.set");
    JAddStringToObject(req, "product", product_uid);
    // periodic mode: outbound syncs every summary_interval_min, inbound
    // every 2 hours to pull updated env vars.  Alert notes with sync:true
    // bypass this timer and transmit immediately.
    JAddStringToObject(req, "mode",     "periodic");
    JAddNumberToObject(req, "outbound", DEFAULT_SUMMARY_INTERVAL_MIN);
    JAddNumberToObject(req, "inbound",  120);
    bool ok = notecard.sendRequestWithRetry(req, 5);
    if (!ok) {
        Serial.println("[init] hub.set failed; will retry on next wake");
    }
    return ok;
}

// ---------------------------------------------------------------------------
// Note templates — fixed-schema records save ~3–5× on-wire bytes versus
// free-form JSON.  Called on cold boot and on any subsequent wake where
// template registration was not previously confirmed.  Returns true on success.
// ---------------------------------------------------------------------------
bool defineTemplates() {
    J *req = notecard.newRequest("note.template");
    JAddStringToObject(req, "file", NOTEFILE_SUMMARY);
    JAddNumberToObject(req, "port", 50);
    J *body = JAddObjectToObject(req, "body");
    JAddNumberToObject(body, "i_a_rms",       14.1);  // Phase-A RMS (A)
    JAddNumberToObject(body, "i_b_rms",       14.1);  // Phase-B RMS (A)
    JAddNumberToObject(body, "i_c_rms",       14.1);  // Phase-C RMS (A)
    JAddNumberToObject(body, "i_total",       14.1);  // Sum of phase currents (A)
    JAddNumberToObject(body, "loading_pct",   14.1);  // % of transformer nameplate
    JAddNumberToObject(body, "imbalance_pct", 14.1);  // Phase imbalance %
    JAddNumberToObject(body, "temp_c",        14.1);  // Enclosure temperature (°C)
    JAddNumberToObject(body, "overloads",     12);    // Overload intervals in window
    JAddNumberToObject(body, "samples",       12);    // Loaded sample intervals
    JAddNumberToObject(body, "total_wakes",   12);    // All host wakes in window
    bool ok = notecard.sendRequest(req);
    if (!ok) {
        Serial.println("[template] note.template (summary) failed; will retry on next wake");
    }
    return ok;
}

// ---------------------------------------------------------------------------
// Environment variable overrides — fetches all config keys in one batch
// env.get request so the env.modified timestamp is only committed to
// persistent state after a complete, successful read.
//
// cache_valid sentinel: sample_interval_sec == 0 is impossible after a
// successful parse (the clamp in Step 4 raises it to at least 30), so it
// serves as a reliable "no successful fetch has ever completed" flag.
//
// On entry, c is seeded from state.cached_cfg when a valid cache exists, or
// left at compile-time defaults otherwise.  This means every early-return
// failure path automatically retains the last good configuration without
// needing an explicit assignment at each return site.
//
// If env.modified matches the last successful fetch the batch env.get is
// skipped entirely to reduce I²C traffic.
//
// If the batch request fails transiently, state.last_env_modified is NOT
// updated, so the next wake retries the full fetch rather than silently
// locking in stale or default values.
// ---------------------------------------------------------------------------
void fetchEnvOverrides(EnvConfig &c) {
    // Determine whether a prior successful fetch produced a usable cache.
    // sample_interval_sec == 0 is the sentinel for "never fetched" because
    // Step 4 clamps the minimum to 30; zero can only occur on a cold-boot
    // PersistState that has never been written.
    const bool cache_valid = (state.cached_cfg.sample_interval_sec != 0);

    // Seed c up-front so that every early-return failure path below
    // automatically retains the last good config.  Only fall back to
    // compile-time defaults when no cache has ever been populated.
    if (cache_valid) {
        c = state.cached_cfg;
    }

    // Step 1: Check env.modified. Save the new timestamp locally; do NOT
    // write it to state.last_env_modified until all env vars are read
    // successfully (see Step 5).
    uint32_t new_ts = 0;
    {
        J *req = notecard.newRequest("env.modified");
        J *rsp = notecard.requestAndResponse(req);
        if (rsp) {
            new_ts = (uint32_t)JGetNumber(rsp, "time");
            notecard.deleteResponse(rsp);
        }
    }

    // If the timestamp is unchanged and the cache is valid, reuse cached
    // values and skip the batch request entirely to reduce I²C traffic.
    // c is already set to state.cached_cfg above, so just return.
    if (new_ts && new_ts == state.last_env_modified && cache_valid) {
        Serial.println("[config] env vars unchanged; using cached config");
        return;
    }

    // Step 2: Fetch all env vars in one batch request.
    J *req = notecard.newRequest("env.get");
    J *names = JAddArrayToObject(req, "names");
    JAddItemToArray(names, JCreateString("sample_interval_sec"));
    JAddItemToArray(names, JCreateString("summary_interval_min"));
    JAddItemToArray(names, JCreateString("rated_amps"));
    JAddItemToArray(names, JCreateString("overload_pct"));
    JAddItemToArray(names, JCreateString("imbalance_pct_thresh"));
    JAddItemToArray(names, JCreateString("temp_alert_c"));
    JAddItemToArray(names, JCreateString("alert_cooldown_sec"));
    JAddItemToArray(names, JCreateString("phase_count"));

    J *rsp = notecard.requestAndResponse(req);
    if (!rsp) {
        // Notecard communication failed. c already holds the cached config (or
        // compile-time defaults on first boot); leave state.last_env_modified
        // untouched so the next wake retries the full fetch.
        if (cache_valid) {
            Serial.println("[config] env.get failed; retaining cached config");
        } else {
            Serial.println("[config] env.get failed; no cache available — using compile-time defaults");
        }
        return;
    }
    if (notecard.responseError(rsp)) {
        notecard.deleteResponse(rsp);
        if (cache_valid) {
            Serial.println("[config] env.get error; retaining cached config");
        } else {
            Serial.println("[config] env.get error; no cache available — using compile-time defaults");
        }
        return;
    }

    // Step 3: Parse response body. Start from firmware defaults so that any
    // key absent from Notehub reverts to its default rather than persisting a
    // stale cached value (e.g. an operator deleting a var resets it to default).
    EnvConfig nc;
    nc.sample_interval_sec  = DEFAULT_SAMPLE_INTERVAL_SEC;
    nc.summary_interval_min = DEFAULT_SUMMARY_INTERVAL_MIN;
    nc.rated_amps           = DEFAULT_RATED_AMPS;
    nc.overload_pct         = DEFAULT_OVERLOAD_PCT;
    nc.imbalance_pct_thresh = DEFAULT_IMBALANCE_PCT_THRESH;
    nc.temp_alert_c         = DEFAULT_TEMP_ALERT_C;
    nc.alert_cooldown_sec   = DEFAULT_ALERT_COOLDOWN_SEC;
    nc.phase_count          = DEFAULT_PHASE_COUNT;

    J *body = JGetObjectItem(rsp, "body");
    if (body) {
        const char *v;
        v = JGetString(body, "sample_interval_sec");
        if (v && *v) nc.sample_interval_sec  = (uint32_t)atol(v);
        v = JGetString(body, "summary_interval_min");
        if (v && *v) nc.summary_interval_min = (uint32_t)atol(v);
        v = JGetString(body, "rated_amps");
        if (v && *v) nc.rated_amps           = (float)atof(v);
        v = JGetString(body, "overload_pct");
        if (v && *v) nc.overload_pct         = (float)atof(v);
        v = JGetString(body, "imbalance_pct_thresh");
        if (v && *v) nc.imbalance_pct_thresh = (float)atof(v);
        v = JGetString(body, "temp_alert_c");
        if (v && *v) nc.temp_alert_c         = (float)atof(v);
        v = JGetString(body, "alert_cooldown_sec");
        if (v && *v) nc.alert_cooldown_sec   = (uint32_t)atol(v);
        v = JGetString(body, "phase_count");
        if (v && *v) nc.phase_count          = (int)atoi(v);
    }
    notecard.deleteResponse(rsp);

    // Step 4: Clamp all thresholds to sane engineering ranges.
    // Lower bounds prevent degenerate thresholds; upper bounds prevent overflow
    // in uint16_t cooldown counters, elapsed_sec, and the delay() fallback.
    // (sample_interval_sec ≤ 3600 keeps ×1000UL safe on 32-bit targets;
    //  alert_cooldown_sec ≤ 86400 keeps cooldown_cycles within uint16_t at
    //  the minimum 30-second sample interval: 86400/30 = 2880.)
    if (nc.sample_interval_sec  < 30)       nc.sample_interval_sec  = 30;
    if (nc.sample_interval_sec  > 3600U)    nc.sample_interval_sec  = 3600U;
    if (nc.summary_interval_min < 5)        nc.summary_interval_min = 5;
    if (nc.summary_interval_min > 1440U)    nc.summary_interval_min = 1440U;
    if (nc.rated_amps           < 1.0f)     nc.rated_amps           = 1.0f;
    if (nc.overload_pct         < 50.0f)    nc.overload_pct         = 50.0f;
    if (nc.overload_pct         > 125.0f)   nc.overload_pct         = 125.0f;
    if (nc.imbalance_pct_thresh < 1.0f)     nc.imbalance_pct_thresh = 1.0f;
    if (nc.imbalance_pct_thresh > 100.0f)   nc.imbalance_pct_thresh = 100.0f;
    if (nc.temp_alert_c         < 0.0f)     nc.temp_alert_c         = 0.0f;
    if (nc.temp_alert_c         > 150.0f)   nc.temp_alert_c         = 150.0f;
    if (nc.alert_cooldown_sec   > 86400U)   nc.alert_cooldown_sec   = 86400U;
    if (nc.phase_count < 1 || nc.phase_count > 3) nc.phase_count    = DEFAULT_PHASE_COUNT;

    // Step 5: All reads succeeded — now commit the new timestamp and cache.
    // Doing this only after successful parsing prevents a transient I²C glitch
    // from locking in stale/default values and blocking future refetches.
    c = nc;
    state.cached_cfg = nc;
    if (new_ts) state.last_env_modified = new_ts;

    // Re-apply hub.set only when the outbound cadence changed AND the initial
    // hub.set (which includes the product association) has already been confirmed.
    // Skipping when hub_configured is false prevents a product-less hub.set from
    // making the device look partly configured while it is still unassociated.
    // Persisting lastAppliedSummaryMin in PersistState (rather than a
    // function-static) means this check survives card.attn power cycles correctly.
    if (state.hub_configured && nc.summary_interval_min != state.lastAppliedSummaryMin) {
        J *hreq = notecard.newRequest("hub.set");
        JAddStringToObject(hreq, "mode",     "periodic");
        JAddNumberToObject(hreq, "outbound", (int)nc.summary_interval_min);
        JAddNumberToObject(hreq, "inbound",  120);
        if (notecard.sendRequest(hreq)) {
            state.lastAppliedSummaryMin = nc.summary_interval_min;
        } else {
            Serial.println("[config] hub.set cadence update failed; will retry next wake");
        }
    }
}

// ---------------------------------------------------------------------------
// CT RMS measurement — two-pass approach.
// Pass 1: estimate DC bias offset (~2048 counts for a centred bias network).
// Pass 2: compute AC RMS relative to that offset.
// ---------------------------------------------------------------------------
float readCtRms(uint8_t pin) {
    // Pass 1: DC offset
    long sum = 0;
    for (int i = 0; i < CT_DC_SAMPLES; i++) {
        sum += analogRead(pin);
    }
    int offset = (int)(sum / CT_DC_SAMPLES);

    // Pass 2: AC RMS
    float sum_sq = 0.0f;
    for (int i = 0; i < CT_RMS_SAMPLES; i++) {
        float s = (float)(analogRead(pin) - offset);
        sum_sq += s * s;
    }
    float rms_counts = sqrtf(sum_sq / CT_RMS_SAMPLES);

    // Convert ADC RMS counts → primary amps and suppress noise floor
    float i_rms = rms_counts * CT_SCALE;
    return (i_rms < CT_NOISE_FLOOR_A) ? 0.0f : i_rms;
}

// ---------------------------------------------------------------------------
// Temperature reading via Adafruit MCP9808 (I2C, default address 0x18).
// Returns -999.0f if the sensor is absent or unresponsive.
// ---------------------------------------------------------------------------
float readTemperatureC() {
    if (!mcp9808.begin(0x18)) {
        Serial.println("[temp] MCP9808 not found at 0x18");
        return -999.0f;
    }
    mcp9808.setResolution(3);   // 0.0625 °C, ~250 ms conversion
    mcp9808.wake();
    float t = mcp9808.readTempC();
    mcp9808.shutdown_wake(1);   // power down between reads
    return t;
}

// ---------------------------------------------------------------------------
// Alert evaluation — three independent checks, each with its own cooldown.
// Each sendAlert() call is keyed to a fixed slot so simultaneous triggers
// are stored independently and neither overwrites the other.
// ---------------------------------------------------------------------------
void checkAlerts(float i_a, float i_b, float i_c, float temp_c) {
    const float threshold = cfg.rated_amps * (cfg.overload_pct / 100.0f);

    // Compute cooldown_cycles with a saturating cast.
    // alert_cooldown_sec ≤ 86400 and sample_interval_sec ≥ 30 are guaranteed
    // by fetchEnvOverrides() clamping; the saturation below is belt-and-
    // suspenders for any path that bypasses that function.
    const uint32_t interval = (cfg.sample_interval_sec > 0)
                              ? cfg.sample_interval_sec : 1U;
    uint32_t raw_cycles = (cfg.alert_cooldown_sec > 0)
        ? max(1U, (cfg.alert_cooldown_sec + interval - 1U) / interval)
        : 0U;
    if (raw_cycles > 65535U) raw_cycles = 65535U;
    const uint16_t cooldown_cycles = (uint16_t)raw_cycles;

    // 1. Overload: any single phase exceeds the threshold
    bool overloaded = (i_a > threshold ||
                       (cfg.phase_count >= 2 && i_b > threshold) ||
                       (cfg.phase_count >= 3 && i_c > threshold));
    if (overloaded) {
        state.overload_count++;
        if (state.cd_overload == 0) {
            float worst = max(i_a, max(i_b, i_c));
            float pct   = (cfg.rated_amps > 0.0f)
                          ? (worst / cfg.rated_amps * 100.0f) : 0.0f;
            sendAlert(ALERT_SLOT_OVERLOAD, "overload",
                      i_a, i_b, i_c, temp_c, pct);
            state.cd_overload = cooldown_cycles;
        }
    }

    // 2. Phase imbalance: (max − min) / max > threshold.
    // Only compared for active phases so an unconnected phase (always 0 A)
    // does not produce a spurious 100% imbalance on a 2-phase installation.
    if (cfg.phase_count > 1) {
        float i_max = (cfg.phase_count >= 3)
                      ? max(i_a, max(i_b, i_c)) : max(i_a, i_b);
        float i_min = (cfg.phase_count >= 3)
                      ? min(i_a, min(i_b, i_c)) : min(i_a, i_b);
        float imb   = (i_max > 2.0f)
                      ? ((i_max - i_min) / i_max * 100.0f) : 0.0f;
        if (imb > cfg.imbalance_pct_thresh && state.cd_imbalance == 0) {
            sendAlert(ALERT_SLOT_IMBALANCE, "phase_imbalance",
                      i_a, i_b, i_c, temp_c, imb);
            state.cd_imbalance = cooldown_cycles;
        }
    }

    // 3. High temperature: enclosure above thermal threshold
    if (temp_c >= -40.0f && temp_c > cfg.temp_alert_c && state.cd_temp == 0) {
        float i_total = i_a + i_b + i_c;
        sendAlert(ALERT_SLOT_TEMP, "high_temp",
                  i_a, i_b, i_c, temp_c, i_total);
        state.cd_temp = cooldown_cycles;
    }
}

// ---------------------------------------------------------------------------
// Alert note — sent with sync:true to bypass the outbound queue and wake
// the Notecard radio immediately.
//
// The alert is written to pending_alerts[slot] before the first send attempt
// so it survives an unexpected reset mid-retry.  Delivery is attempted twice;
// on both failures the slot stays active so the next ATTN wake re-queues it.
// Each alert type has its own dedicated slot (ALERT_SLOT_OVERLOAD,
// ALERT_SLOT_IMBALANCE, ALERT_SLOT_TEMP) so simultaneous multi-type alerts
// do not overwrite each other.
// ---------------------------------------------------------------------------
void sendAlert(uint8_t slot, const char *type, float i_a, float i_b,
               float i_c, float temp_c, float extra) {
    // Persist intent before the first attempt.
    PendingAlert &pa = state.pending_alerts[slot];
    pa.active = true;
    strncpy(pa.type, type, sizeof(pa.type) - 1);
    pa.type[sizeof(pa.type) - 1] = '\0';
    pa.i_a    = i_a;
    pa.i_b    = i_b;
    pa.i_c    = i_c;
    pa.temp_c = temp_c;
    pa.extra  = extra;

    bool queued = false;
    for (int attempt = 0; attempt < 2 && !queued; attempt++) {
        J *req = notecard.newRequest("note.add");
        JAddStringToObject(req, "file", NOTEFILE_ALERT);
        JAddBoolToObject(req,   "sync", true);

        J *body = JAddObjectToObject(req, "body");
        JAddStringToObject(body, "alert",   type);
        JAddNumberToObject(body, "i_a_rms", i_a);
        JAddNumberToObject(body, "i_b_rms", i_b);
        JAddNumberToObject(body, "i_c_rms", i_c);
        JAddNumberToObject(body, "temp_c",  temp_c);
        JAddNumberToObject(body, "extra",   extra);  // loading_pct, imbalance_pct, or i_total

        J *rsp = notecard.requestAndResponse(req);
        if (rsp) {
            const char *err = JGetString(rsp, "err");
            queued = !(err && *err);
            notecard.deleteResponse(rsp);
        }
        if (!queued && attempt == 0) {
            Serial.print("[alert] note.add attempt 1 failed for: ");
            Serial.println(type);
        }
    }

    if (queued) {
        pa.active = false;
        Serial.print("[alert] queued: "); Serial.println(type);
    } else {
        Serial.print("[alert] note.add failed (will retry next wake): ");
        Serial.println(type);
    }
}

// ---------------------------------------------------------------------------
// Hourly summary note — template-encoded for data-efficient cellular.
// Emitted at the end of every summary window even when no valid CT samples
// were collected (bench bring-up / CT disconnected) so the record still
// reaches Notehub as a liveness heartbeat.
// ---------------------------------------------------------------------------
bool sendSummary() {
    // Guard against division-by-zero: zero-sample windows produce 0.0 averages,
    // not NaN.
    float n       = (state.valid_samples > 0) ? (float)state.valid_samples : 1.0f;
    float i_a_avg = state.sum_i_a / n;
    float i_b_avg = state.sum_i_b / n;
    float i_c_avg = state.sum_i_c / n;
    float i_total = i_a_avg + i_b_avg + i_c_avg;

    float rated_tot = cfg.rated_amps * cfg.phase_count;
    float loading   = (rated_tot > 0.0f) ? (i_total / rated_tot * 100.0f) : 0.0f;

    // Imbalance is only meaningful with two or more active phases.
    float imbalance = 0.0f;
    if (cfg.phase_count >= 2) {
        float i_max = (cfg.phase_count >= 3)
                      ? max(i_a_avg, max(i_b_avg, i_c_avg))
                      : max(i_a_avg, i_b_avg);
        float i_min = (cfg.phase_count >= 3)
                      ? min(i_a_avg, min(i_b_avg, i_c_avg))
                      : min(i_a_avg, i_b_avg);
        imbalance = (i_max > 2.0f) ? ((i_max - i_min) / i_max * 100.0f) : 0.0f;
    }

    // Temperature average uses only cycles that produced a valid reading.
    float temp_avg = (state.valid_temp_samples > 0)
                     ? (state.sum_temp_c / (float)state.valid_temp_samples)
                     : -999.0f;

    J *req = notecard.newRequest("note.add");
    JAddStringToObject(req, "file", NOTEFILE_SUMMARY);
    J *body = JAddObjectToObject(req, "body");
    JAddNumberToObject(body, "i_a_rms",       i_a_avg);
    JAddNumberToObject(body, "i_b_rms",       i_b_avg);
    JAddNumberToObject(body, "i_c_rms",       i_c_avg);
    JAddNumberToObject(body, "i_total",       i_total);
    JAddNumberToObject(body, "loading_pct",   loading);
    JAddNumberToObject(body, "imbalance_pct", imbalance);
    JAddNumberToObject(body, "temp_c",        temp_avg);
    JAddNumberToObject(body, "overloads",     state.overload_count);
    JAddNumberToObject(body, "samples",       state.valid_samples);
    JAddNumberToObject(body, "total_wakes",   state.total_cycles);

    bool ok = notecard.sendRequest(req);
    if (ok) {
        Serial.print("[summary] queued (");
        Serial.print(state.valid_samples);
        Serial.print(" loaded / ");
        Serial.print(state.total_cycles);
        Serial.println(" total wakes)");
    } else {
        Serial.println("[summary] note.add failed; accumulators retained for next wake");
    }
    return ok;
}
