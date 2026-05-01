/*
 * creek_flood_gauge_helpers.cpp
 *
 * Notecard configuration, sensor drivers, scheduler utilities, and note-
 * emission helpers for the Creek / Culvert Flood-Warning Stream Gauge.
 * Kept in a companion file so creek_flood_gauge.ino stays within the
 * project's 500-line limit and focuses on setup/loop orchestration.
 */
#include "creek_flood_gauge_helpers.h"

// ---------------------------------------------------------------------------
// Implementation-private constants (not needed outside this translation unit)
// ---------------------------------------------------------------------------
#define MB7389_READ_TIMEOUT_MS  220   // > 1 sensor cycle (~150 ms)
#define RAIN_DEBOUNCE_MS         50   // reed-switch debounce window (ms)

// ===========================================================================
// clampU32 / clampF — guard against pathological env-var values.
// Returns fallback when v is outside [lo, hi].
// ===========================================================================
uint32_t clampU32(float v, uint32_t lo, uint32_t hi, uint32_t fallback) {
    if (v < (float)lo || v > (float)hi) return fallback;
    return (uint32_t)v;
}

float clampF(float v, float lo, float hi, float fallback) {
    if (v < lo || v > hi) return fallback;
    return v;
}

// ===========================================================================
// hubConfigure — called on first boot (retried if a prior boot's setup failed)
// and re-called by fetchEnvOverrides() when summary_interval_min changes.
//
// Pass PRODUCT_UID on first boot so the Notecard registers the project.
// Pass NULL on subsequent re-calls (interval updates only); the Notecard
// retains the product UID in flash and does not need it re-sent.
// Returns true if the Notecard accepted the request.
// ===========================================================================
bool hubConfigure(const char *productUid) {
    J *req = notecard.newRequest("hub.set");
    if (!req) return false;
    if (productUid && productUid[0]) JAddStringToObject(req, "product", productUid);
    // outbound controls the regular summary-note cadence; alert notes with
    // sync:true bypass this timer immediately.
    // inbound is intentionally set to g_inboundIntervalMin (default 1440 min /
    // 24 h) rather than matching outbound: each inbound session is a radio
    // transaction that consumes NTN budget when cellular is unavailable.
    // Decoupling inbound preserves the 10 KB Skylo budget for alert delivery
    // rather than hourly env-var polling.
    JAddStringToObject(req, "mode",     HUB_MODE);
    JAddNumberToObject(req, "outbound", g_summaryIntervalMin);
    JAddNumberToObject(req, "inbound",  g_inboundIntervalMin);
    // sendRequestWithRetry works around the cold-boot I2C enumeration race;
    // 10 s is deliberately generous to tolerate worst-case cold-start delays.
    bool ok = notecard.sendRequestWithRetry(req, 10);
    if (ok) {
        g_state.lastAppliedOutboundMin = g_summaryIntervalMin;
        g_state.lastAppliedInboundMin  = g_inboundIntervalMin;
    }
    return ok;
}

// ===========================================================================
// configureSkyloTransport — one-time Skylo NTN prerequisites, called on
// first boot as part of the hubConfigure/defineTemplates/transport sequence.
//
// 1. card.transport "cell-ntn": cellular (LTE-M/NB-IoT) is the primary
//    transport; the Notecard automatically falls back to Skylo satellite when
//    terrestrial coverage is unavailable.  Alert notes with sync:true trigger
//    an immediate connection attempt on whichever transport is live.
// 2. card.location.mode "continuous": aggressive GNSS during commissioning
//    so the first valid fix is stored in Notecard flash as quickly as
//    possible.  A persisted lat/lon is a hard prerequisite for Skylo NTN
//    satellite sessions.  Once checkLocationAcquired() confirms the fix, it
//    automatically switches to a 24-hour periodic cadence so the GPS radio
//    stays idle between infrequent re-checks on this static installation.
// Returns true if both requests were accepted by the Notecard.
// ===========================================================================
bool configureSkyloTransport() {
    // Cellular primary → Skylo satellite fallback
    {
        J *req = notecard.newRequest("card.transport");
        if (!req) return false;
        JAddStringToObject(req, "method", "cell-ntn");
        if (!notecard.sendRequestWithRetry(req, 5)) return false;
    }
    // Phase 1 — commissioning: continuous GNSS until the first valid fix is
    // stored.  checkLocationAcquired() detects the fix and transitions to the
    // low-power daily periodic cadence (Phase 2) automatically.
    {
        J *req = notecard.newRequest("card.location.mode");
        if (!req) return false;
        JAddStringToObject(req, "mode", "continuous");
        return notecard.sendRequestWithRetry(req, 5);
    }
}

// ===========================================================================
// checkLocationAcquired — verify the Notecard holds a valid GNSS fix and,
// on the first confirmation, transition to the low-power daily cadence.
//
// Three-phase GNSS strategy:
//   Phase 1 (commissioning, ≤ GNSS_COMMISSION_TIMEOUT_SEC = 30 min):
//     configureSkyloTransport() sets continuous mode so the Notecard acquires
//     and stores the first fix as quickly as possible.
//     checkLocationAcquired() is called each wake while locationOk is false;
//     it polls card.location until a non-zero "time" field appears.
//     The 30-minute commissioning window is enforced by two parallel mechanisms
//     to guarantee the cap fires regardless of Notecard time-sync state:
//       • Epoch-based (gnssCommissionStartSec): latched on the first wake that
//         has a valid Notecard time; provides an accurate wall-clock bound.
//       • Wake-count-based (gnssCommissionStartWake): latched on the first
//         no-fix wake regardless of time-sync; uses wakeCount × sampleInterval
//         to guarantee the cap fires even before the Notecard has contacted
//         Notehub for its first timestamp.
//     The window expires when either mechanism reaches GNSS_COMMISSION_TIMEOUT_SEC.
//
//   Phase 1b (commissioning timeout / degraded-power fallback):
//     If no fix is confirmed within GNSS_COMMISSION_TIMEOUT_SEC, the firmware
//     falls back from continuous to GNSS_RETRY_PERIOD_SEC (5-min periodic)
//     mode to reduce the elevated GPS-active power draw on the solar budget.
//     A "gnss_timeout" fault note is emitted once to gauge_fault.qo with
//     sync:true so the issue surfaces in Notehub without a site visit.
//     gauge_fault.qo is a dedicated commissioning-diagnostic Notefile, separate
//     from the four threshold-alert kinds in gauge_alert.qo, and is not subject
//     to the alert cooldown.  The device continues sampling water level and rain
//     in this degraded state; only Skylo NTN satellite fallback is unavailable
//     until a fix is obtained.
//
//   Phase 2 (steady-state, after fix confirmed):
//     On the first confirmed fix this function issues
//     card.location.mode periodic/86400 s, dialling back to a once-per-day
//     re-check cadence appropriate for a static installation.  The Notecard
//     retains the fix in flash across power cycles, so the phase-2 mode change
//     persists without re-sending it on every subsequent boot.
//
// A persisted lat/lon is a hard prerequisite for Skylo NTN satellite sessions.
// Returns true when a fix is confirmed (locationOk can then be set to true).
// ===========================================================================
bool checkLocationAcquired() {
    J *rsp = notecard.requestAndResponse(notecard.newRequest("card.location"));
    if (rsp == NULL) return false;
    if (notecard.responseError(rsp)) { notecard.deleteResponse(rsp); return false; }
    uint32_t gpsTime = (uint32_t)JGetNumber(rsp, "time");
    notecard.deleteResponse(rsp);

    if (gpsTime == 0) {
        // No fix yet — enforce the bounded commissioning window.
        uint32_t nowSec = notecardTimeSec();

        // ── Latch commissioning start markers ────────────────────────────
        // Epoch-based: latched on the first wake that has a valid Notecard time.
        if (g_state.gnssCommissionStartSec == 0 && nowSec > 0) {
            g_state.gnssCommissionStartSec = nowSec;
        }
        // Wake-count-based: latched on the first no-fix wake regardless of
        // time-sync so the 30-minute cap is guaranteed even before the Notecard
        // has contacted Notehub.  We store wakeCount+1 so that 0 remains the
        // unambiguous "not yet latched" sentinel — wakeCount is 0 on first boot
        // because loop() has not yet incremented it when setup() calls us here.
        if (g_state.gnssCommissionStartWake == 0) {
            g_state.gnssCommissionStartWake = g_state.wakeCount + 1;
        }

        // ── Determine whether the continuous-GNSS window has expired ─────
        // Primary check: epoch-based (accurate, unaffected by interval changes).
        bool epochExpired =
            (!g_state.gnssCommissionTimedOut &&
             g_state.gnssCommissionStartSec > 0 &&
             nowSec > g_state.gnssCommissionStartSec &&
             (nowSec - g_state.gnssCommissionStartSec) >= GNSS_COMMISSION_TIMEOUT_SEC);

        // Secondary check: wake-count × sample interval.
        // Adding 1 to wakeCount mirrors the anchor's +1 offset so the elapsed
        // count is 0 on the first call (not 1), and increments by 1 each
        // subsequent boot as loop() advances wakeCount.
        uint32_t wakesElapsed =
            (g_state.wakeCount + 1 >= g_state.gnssCommissionStartWake)
            ? (g_state.wakeCount + 1 - g_state.gnssCommissionStartWake)
            : 0u;
        bool wakeExpired =
            (!g_state.gnssCommissionTimedOut &&
             g_state.gnssCommissionStartWake > 0 &&
             (wakesElapsed * g_sampleIntervalSec) >= GNSS_COMMISSION_TIMEOUT_SEC);

        bool windowExpired = epochExpired || wakeExpired;

        if (windowExpired) {
            // Phase 1 timed out.  Fall back to 5-minute periodic retry to
            // conserve solar reserve while continuing to try for a fix.
            {
                J *req = notecard.newRequest("card.location.mode");
                if (req) {
                    JAddStringToObject(req, "mode",    "periodic");
                    JAddNumberToObject(req, "seconds", GNSS_RETRY_PERIOD_SEC);
                    notecard.sendRequestWithRetry(req, 5);
                }
            }
            g_state.gnssCommissionTimedOut = true;

            // Emit a one-time commissioning-fault note to gauge_fault.qo.
            // This is a separate Notefile from the four threshold alert kinds
            // in gauge_alert.qo and is not subject to the alert cooldown.
            // Configure a dedicated Notehub route for gauge_fault.qo to direct
            // commissioning faults to an installer notification endpoint.
            // Field-specific sentinels: level_mm, depth_mm, and rate_mm_per_min
            // carry -9999 (no sensor data during setup-time commissioning).
            // tips_window carries 0 — its unsigned compact encoding (type 22)
            // cannot hold a negative value, and no rain polling has occurred yet.
            {
                J *fault = notecard.newRequest("note.add");
                if (fault) {
                    JAddStringToObject(fault, "file", NOTEFILE_FAULT);
                    JAddBoolToObject(fault,   "sync", true);
                    J *body = JAddObjectToObject(fault, "body");
                    JAddStringToObject(body, "kind",            "gnss_timeout");
                    JAddNumberToObject(body, "level_mm",        -9999.0);
                    JAddNumberToObject(body, "depth_mm",        -9999.0);
                    JAddNumberToObject(body, "rate_mm_per_min", -9999.0);
                    JAddNumberToObject(body, "tips_window",     0);
                    notecard.sendRequestWithRetry(fault, 5);
                }
            }
            DBG_PRINTLN("[SKYLO] ERROR: GNSS commissioning timed out (30 min) — "
                        "fell back to 5-min periodic retry; gnss_timeout fault emitted "
                        "to gauge_fault.qo. Ensure the GNSS antenna has an unobstructed "
                        "equatorial sky view.");
        } else if (g_state.gnssCommissionTimedOut) {
            DBG_PRINTLN("[SKYLO] WARNING: still no GNSS fix — retrying every 5 min; "
                        "NTN satellite fallback remains unavailable.");
        } else {
            DBG_PRINTLN("[SKYLO] WARNING: no GNSS fix yet — NTN satellite fallback "
                        "unavailable until location is acquired outdoors.");
        }
        return false;
    }

    // Phase 2: first valid fix confirmed — switch to daily periodic cadence.
    // The fix is now stored in Notecard flash and persists across power cycles,
    // so the GPS radio can stay idle between infrequent re-checks.
    {
        J *req = notecard.newRequest("card.location.mode");
        if (req) {
            JAddStringToObject(req, "mode",    "periodic");
            JAddNumberToObject(req, "seconds", 86400);   // once per day maximum
            notecard.sendRequestWithRetry(req, 5);
        }
    }
    DBG_PRINTLN("[SKYLO] GNSS fix confirmed; switched to daily periodic cadence; "
                "NTN satellite fallback ready.");
    return true;
}

// ===========================================================================
// defineTemplates — compact format works with both cellular and satellite.
// Returns true if both templates were accepted by the Notecard.
// ===========================================================================
bool defineTemplates() {
    // Summary notefile
    {
        J *req = notecard.newRequest("note.template");
        if (!req) return false;
        JAddStringToObject(req, "file",   NOTEFILE_SUMMARY);
        JAddStringToObject(req, "format", "compact");  // satellite-safe binary encoding
        JAddNumberToObject(req, "port",   50);
        J *body = JAddObjectToObject(req, "body");
        JAddNumberToObject(body, "level_mm",        14.1);  // 4-byte float
        JAddNumberToObject(body, "depth_mm",        14.1);
        JAddNumberToObject(body, "rate_mm_per_min", 14.1);
        JAddNumberToObject(body, "tips_window",     22);    // 2-byte uint
        JAddNumberToObject(body, "tips_total",      24);    // 4-byte uint
        JAddStringToObject(body, "trend",           "8");   // up to 8-char string
        if (!notecard.sendRequestWithRetry(req, 5)) return false;
    }

    // Alert notefile — the four threshold-based flood alert kinds
    {
        J *req = notecard.newRequest("note.template");
        if (!req) return false;
        JAddStringToObject(req, "file",   NOTEFILE_ALERT);
        JAddStringToObject(req, "format", "compact");
        JAddNumberToObject(req, "port",   51);
        J *body = JAddObjectToObject(req, "body");
        JAddStringToObject(body, "kind",            "16");  // covers "level_critical" at 14 chars
        JAddNumberToObject(body, "level_mm",        14.1);
        JAddNumberToObject(body, "depth_mm",        14.1);
        JAddNumberToObject(body, "rate_mm_per_min", 14.1);
        JAddNumberToObject(body, "tips_window",     22);
        if (!notecard.sendRequestWithRetry(req, 5)) return false;
    }

    // Fault notefile — one-time commissioning diagnostics (e.g., gnss_timeout)
    // routed separately from threshold alerts so downstream consumers never
    // have to special-case a setup-time diagnostic mixed in with flood events.
    // Shares the same field schema as gauge_alert.qo for consistency.
    // level_mm, depth_mm, and rate_mm_per_min carry -9999 (no sensor data
    // during setup-time commissioning); tips_window carries 0 (unsigned compact
    // encoding, type 22; no rain polling has occurred at fault-emission time).
    {
        J *req = notecard.newRequest("note.template");
        if (!req) return false;
        JAddStringToObject(req, "file",   NOTEFILE_FAULT);
        JAddStringToObject(req, "format", "compact");
        JAddNumberToObject(req, "port",   52);
        J *body = JAddObjectToObject(req, "body");
        JAddStringToObject(body, "kind",            "16");  // "gnss_timeout" = 12 chars; 16 for headroom
        JAddNumberToObject(body, "level_mm",        14.1);
        JAddNumberToObject(body, "depth_mm",        14.1);
        JAddNumberToObject(body, "rate_mm_per_min", 14.1);
        JAddNumberToObject(body, "tips_window",     22);
        if (!notecard.sendRequestWithRetry(req, 5)) return false;
    }
    return true;
}

// ===========================================================================
// fetchEnvOverrides — read per-device / fleet env vars each wake.
// All values are clamped to documented sane bounds before being applied so a
// bad fleet value cannot create a tight loop, overflow timing math, or produce
// nonsensical thresholds.  Re-issues hub.set (without product UID) if
// summary_interval_min changed to keep the radio schedule aligned.
// ===========================================================================
void fetchEnvOverrides() {
    J *req = notecard.newRequest("env.get");
    if (!req) return;
    J *rsp = notecard.requestAndResponse(req);
    if (rsp == NULL) return;
    if (notecard.responseError(rsp)) { notecard.deleteResponse(rsp); return; }

    // Save pre-parse effective values so cross-parameter validation below can
    // revert to the current in-memory state on rejection rather than silently
    // leaving thresholds in an inconsistent condition.  These reflect either
    // compile-time defaults (first boot) or the last successfully validated
    // values (subsequent boots, pre-loaded from cfgValid state in setup()).
    float prevLevelWarningMm  = g_levelWarningMm;
    float prevLevelCriticalMm = g_levelCriticalMm;

    J *body = JGetObject(rsp, "body");
    if (body != NULL) {
        float v;

        v = JGetNumber(body, "sample_interval_sec");
        if (v > 0) g_sampleIntervalSec =
            clampU32(v, 30, 86400, g_sampleIntervalSec);       // 30 s – 24 h

        v = JGetNumber(body, "summary_interval_min");
        if (v > 0) g_summaryIntervalMin =
            clampU32(v, 1, 1440, g_summaryIntervalMin);         // 1 min – 24 h

        v = JGetNumber(body, "level_warning_mm");
        if (v > 0) g_levelWarningMm =
            clampF(v, 10.0f, 9999.0f, g_levelWarningMm);

        v = JGetNumber(body, "level_critical_mm");
        if (v > 0) g_levelCriticalMm =
            clampF(v, 10.0f, 9999.0f, g_levelCriticalMm);

        v = JGetNumber(body, "rate_warning_mm_per_min");
        if (v > 0) g_rateWarningMmPerMin =
            clampF(v, 0.1f, 9999.0f, g_rateWarningMmPerMin);

        v = JGetNumber(body, "rain_intense_tips");
        if (v > 0) g_rainIntenseTips =
            clampU32(v, 1, 1000, g_rainIntenseTips);

        v = JGetNumber(body, "sensor_height_mm");
        if (v > 0) g_sensorHeightMm =
            clampF(v, 100.0f, 9999.0f, g_sensorHeightMm);

        v = JGetNumber(body, "alert_cooldown_sec");
        if (v > 0) g_alertCooldownSec =
            clampU32(v, 60, 86400, g_alertCooldownSec);         // 1 min – 24 h

        v = JGetNumber(body, "inbound_interval_min");
        if (v > 0) g_inboundIntervalMin =
            clampU32(v, 60, 10080, g_inboundIntervalMin);       // 1 h – 7 days
    }

    // -----------------------------------------------------------------------
    // Cross-parameter validation.  Individual clamps above guard absolute
    // ranges; here we enforce required relationships between related settings
    // so a bad fleet value cannot silently invert alert severity semantics.
    // -----------------------------------------------------------------------

    // level_critical_mm must be strictly less than level_warning_mm.
    // If inverted (e.g., critical=500 mm, warning=400 mm) the critical branch
    // fires over a broader sensor-distance range than the warning branch —
    // because critical is evaluated first in loop() — so the device raises
    // "level_critical" alerts where it should raise "level_warning" and may
    // never fire a warning at all.  Reject the inverted pair atomically and
    // revert both to the values that were in effect before this env.get, so
    // the device continues on the last known-good thresholds.
    if (g_levelCriticalMm >= g_levelWarningMm) {
        DBG_PRINTLN("[ENV] REJECTED: level_critical_mm >= level_warning_mm "
                    "(inverted thresholds) — reverting both to previous values.");
        g_levelCriticalMm = prevLevelCriticalMm;
        g_levelWarningMm  = prevLevelWarningMm;
    }

    // sensor_height_mm should exceed level_warning_mm so that
    // depth_mm = (sensor_height_mm - level_mm) is positive when a warning
    // fires.  A sensor height at or below the warning threshold implies the
    // sensor is already submerged at the warning level, making depth_mm
    // zero or negative.  Warn but do not reject: the operator may be
    // intentionally using a conservative mounting height and the alert logic
    // still functions correctly; the depth field simply loses meaning.
    if (g_sensorHeightMm <= g_levelWarningMm) {
        DBG_PRINTLN("[ENV] WARNING: sensor_height_mm <= level_warning_mm "
                    "— depth_mm will be zero or negative when a warning fires; "
                    "verify sensor mounting height.");
    }

    // Persist the effective config so the next wake cycle starts from
    // these values even if env.get fails on that cycle.
    g_state.cfgSampleIntervalSec   = g_sampleIntervalSec;
    g_state.cfgSummaryIntervalMin  = g_summaryIntervalMin;
    g_state.cfgInboundIntervalMin  = g_inboundIntervalMin;
    g_state.cfgLevelWarningMm      = g_levelWarningMm;
    g_state.cfgLevelCriticalMm     = g_levelCriticalMm;
    g_state.cfgRateWarningMmPerMin = g_rateWarningMmPerMin;
    g_state.cfgRainIntenseTips     = g_rainIntenseTips;
    g_state.cfgSensorHeightMm      = g_sensorHeightMm;
    g_state.cfgAlertCooldownSec    = g_alertCooldownSec;
    g_state.cfgValid               = true;

    notecard.deleteResponse(rsp);

    // Re-apply hub.set whenever the operator changes summary_interval_min or
    // inbound_interval_min so the Notecard's outbound and inbound cadences
    // track the local scheduler.  Product UID is not re-sent here: the
    // Notecard retains it in flash from first boot.
    if (g_summaryIntervalMin != g_state.lastAppliedOutboundMin ||
        g_inboundIntervalMin != g_state.lastAppliedInboundMin) {
        hubConfigure(NULL);
    }
}

// ===========================================================================
// readWaterLevelMm — parse one 'Rxxxx\r' frame from the MB7389 (UART).
// Returns distance in mm; -1.0 on timeout or parse failure.
// ===========================================================================
float readWaterLevelMm() {
    // Flush any stale bytes from the buffer
    while (Serial1.available()) { Serial1.read(); }

    char    buf[8]  = {0};
    uint8_t idx     = 0;
    bool    inFrame = false;
    unsigned long deadline = millis() + MB7389_READ_TIMEOUT_MS;

    while (millis() < deadline) {
        if (!Serial1.available()) continue;
        char c = (char)Serial1.read();
        if (c == 'R') {
            // Start-of-frame marker
            inFrame = true;
            idx     = 0;
            memset(buf, 0, sizeof(buf));
        } else if (inFrame && c == '\r') {
            // End-of-frame: validate digit count then convert.
            // MB7389 zero-pads to 4 digits (e.g., R0300\r..R5000\r);
            // accept 3–4 to tolerate units that omit the leading zero.
            if (idx >= 3 && idx <= 4) return (float)atoi(buf);
            inFrame = false;  // wrong digit count — discard and restart
        } else if (inFrame) {
            if (c < '0' || c > '9') {
                // Non-digit between 'R' and '\r': malformed frame, discard.
                inFrame = false;
            } else if (idx < (sizeof(buf) - 1)) {
                buf[idx++] = c;
            } else {
                inFrame = false;  // overflow: frame longer than expected
            }
        }
    }
    return -1.0f;
}

// ===========================================================================
// countRainTips — count reed-switch closures (HIGH→LOW edges) for windowMs
// ===========================================================================
uint32_t countRainTips(uint32_t windowMs) {
    uint32_t count   = 0;
    bool     lastPin = digitalRead(PIN_RAIN_GAUGE);
    unsigned long deadline = millis() + windowMs;

    while (millis() < deadline) {
        bool pin = digitalRead(PIN_RAIN_GAUGE);
        if (lastPin == HIGH && pin == LOW) {
            // Falling edge: bucket just tipped
            count++;
            delay(RAIN_DEBOUNCE_MS);   // debounce; skip glitches
        }
        lastPin = pin;
        delayMicroseconds(500);        // ~2 kHz poll rate
    }
    return count;
}

// ===========================================================================
// updateHistory — insert a level reading into the circular buffer.
// Stores the Notecard epoch alongside the reading so calcRisingRateMmPerMin()
// can use actual elapsed time.  Resets the buffer if g_sampleIntervalSec has
// changed since the last insert so stale samples at a different cadence don't
// corrupt the rate calculation.
// ===========================================================================
void updateHistory(float levelMm, uint32_t timeSec) {
    if (g_state.historyIntervalSec != g_sampleIntervalSec) {
        // Interval changed: samples taken at different cadences aren't
        // comparable, so start fresh with the new interval.
        g_state.historyCount       = 0;
        g_state.historyIdx         = 0;
        g_state.historyIntervalSec = g_sampleIntervalSec;
    }
    g_state.levelHistoryMm[g_state.historyIdx]      = levelMm;
    g_state.levelHistoryTimeSec[g_state.historyIdx] = timeSec;
    g_state.historyIdx = (g_state.historyIdx + 1) % HISTORY_LEN;
    if (g_state.historyCount < HISTORY_LEN) g_state.historyCount++;
}

// ===========================================================================
// calcRisingRateMmPerMin — positive = water rising (sensor distance decreasing)
//
// Computes rate from the oldest and newest valid samples in the history buffer.
// Uses actual epoch timestamps when available (tolerates missed samples and
// mid-deployment cadence changes); falls back to sample count × interval when
// timestamps are zero (Notecard not yet time-synced).
// ===========================================================================
float calcRisingRateMmPerMin() {
    if (g_state.historyCount < 2) return 0.0f;

    uint8_t oldestIdx = (g_state.historyIdx + HISTORY_LEN - g_state.historyCount) % HISTORY_LEN;
    uint8_t newestIdx = (g_state.historyIdx + HISTORY_LEN - 1) % HISTORY_LEN;

    float    oldest = g_state.levelHistoryMm[oldestIdx];
    float    newest = g_state.levelHistoryMm[newestIdx];
    uint32_t tOld   = g_state.levelHistoryTimeSec[oldestIdx];
    uint32_t tNew   = g_state.levelHistoryTimeSec[newestIdx];

    float windowMin;
    if (tOld > 0 && tNew > tOld) {
        // Use actual elapsed time: correct even after missed samples or
        // a sample_interval_sec change that already reset the buffer.
        windowMin = (float)(tNew - tOld) / 60.0f;
    } else {
        // Timestamps unavailable: fall back to count × interval estimate.
        windowMin = (float)(g_state.historyCount - 1)
                    * (float)g_state.historyIntervalSec / 60.0f;
    }

    if (windowMin < 0.01f) return 0.0f;
    // Decrease in sensor distance → water is rising → positive rate
    return (oldest - newest) / windowMin;
}

// ===========================================================================
// notecardTimeSec — return Notecard's epoch time; 0 if not yet synced or error
// ===========================================================================
uint32_t notecardTimeSec() {
    J *rsp = notecard.requestAndResponse(notecard.newRequest("card.time"));
    if (rsp == NULL) return 0;
    if (notecard.responseError(rsp)) { notecard.deleteResponse(rsp); return 0; }
    uint32_t t = (uint32_t)JGetNumber(rsp, "time");
    notecard.deleteResponse(rsp);
    return t;
}

// ===========================================================================
// trendLabel — convert rate to a human-readable string for the summary
// ===========================================================================
const char *trendLabel(float rateMmPerMin) {
    if (rateMmPerMin <= -9000.0f) return "unknown";  // sentinel: sensor offline or insufficient history
    if (rateMmPerMin >=  20.0f)   return "surging";
    if (rateMmPerMin >=   5.0f)   return "rising";
    if (rateMmPerMin >=  -5.0f)   return "stable";
    return "falling";
}

// ===========================================================================
// sendAlert — bypasses the periodic outbound timer via sync:true so the
// Notecard syncs immediately using whatever transport is available.
// Retries up to 3 times so a transient I2C error cannot silently drop a
// flood warning.  Each attempt rebuilds req because requestAndResponse frees
// it on return.  Returns true if the note was successfully queued.
// Callers must only advance cooldown state on a true return.
//
// Sentinel normalization: the MB7389 driver returns -1.0 on failure; the
// rate calculator returns -9999.0 when the sensor is offline or fewer than
// two history samples exist.  rain_intense can fire while the ultrasonic
// sensor is offline (levelMm == -1.0); without normalization the alert
// payload would contain -1.0, inconsistent with sendSummary()'s convention.
// level_mm, depth_mm, and rate_mm_per_min are normalised to -9999.0 here
// using the same threshold checks as sendSummary() for a consistent contract.
// tips_window carries the raw count (minimum 0) and is never set to -9999 —
// its unsigned compact encoding (type 22) cannot represent a negative value.
// ===========================================================================
bool sendAlert(const char *kind, float levelMm, float rateMmPerMin,
               uint32_t tips, float waterDepthMm) {
    // Normalize invalid-sensor values to -9999.0 before writing the note.
    // Uses the same thresholds as sendSummary() for a consistent data contract.
    float normLevel = (levelMm      >  0.0f)    ? levelMm      : -9999.0f;
    float normDepth = (levelMm      >  0.0f)    ? waterDepthMm : -9999.0f;
    float normRate  = (rateMmPerMin > -9000.0f) ? rateMmPerMin : -9999.0f;

    for (int attempt = 0; attempt < 3; attempt++) {
        J *req = notecard.newRequest("note.add");
        if (!req) continue;
        JAddStringToObject(req, "file", NOTEFILE_ALERT);
        JAddBoolToObject(req, "sync", true);   // bypass periodic timer; sync immediately
        J *body = JAddObjectToObject(req, "body");
        JAddStringToObject(body, "kind",            kind);
        JAddNumberToObject(body, "level_mm",        normLevel);
        JAddNumberToObject(body, "depth_mm",        normDepth);
        JAddNumberToObject(body, "rate_mm_per_min", normRate);
        JAddNumberToObject(body, "tips_window",     (int)tips);
        J *rsp = notecard.requestAndResponse(req);
        if (rsp == NULL) continue;
        bool ok = !notecard.responseError(rsp);
        notecard.deleteResponse(rsp);
        if (ok) { DBG_PRINT("[ALERT] "); DBG_PRINTLN(kind); return true; }
    }
    return false;
}

// ===========================================================================
// sendSummary — queued for the next periodic outbound sync (no sync:true).
// -9999.0 is reported for level/depth when the sensor is offline, and for
// rate when the sensor is offline or fewer than two history samples exist;
// trend is "unknown" in both cases.  Retries up to 2 times to handle
// transient I2C errors.  Returns true if the note was successfully queued.
// ===========================================================================
bool sendSummary(float levelMm, float rateMmPerMin,
                 uint32_t windowTips, float waterDepthMm) {
    // Map internal sentinels (-9999 covers both sensor-offline and insufficient
    // history) to on-wire fields.
    float summLevelMm   = (levelMm      >  0.0f)    ? levelMm      : -9999.0f;
    float summDepthMm   = (levelMm      >  0.0f)    ? waterDepthMm : -9999.0f;
    float summRateMmMin = (rateMmPerMin > -9000.0f) ? rateMmPerMin : -9999.0f;

    // No sync:true — periodic mode queues this note for the next scheduled
    // outbound session; only alert notes bypass the timer immediately.
    for (int attempt = 0; attempt < 2; attempt++) {
        J *req = notecard.newRequest("note.add");
        if (!req) continue;
        JAddStringToObject(req, "file", NOTEFILE_SUMMARY);
        J *body = JAddObjectToObject(req, "body");
        JAddNumberToObject(body, "level_mm",        summLevelMm);
        JAddNumberToObject(body, "depth_mm",        summDepthMm);
        JAddNumberToObject(body, "rate_mm_per_min", summRateMmMin);
        JAddNumberToObject(body, "tips_window",     (int)windowTips);
        JAddNumberToObject(body, "tips_total",      (double)g_state.totalTips);
        JAddStringToObject(body, "trend",           trendLabel(rateMmPerMin));
        J *rsp = notecard.requestAndResponse(req);
        if (rsp == NULL) continue;
        bool ok = !notecard.responseError(rsp);
        notecard.deleteResponse(rsp);
        if (ok) { DBG_PRINTLN("[SUMMARY] sent"); return true; }
    }
    return false;
}

// ===========================================================================
// sleepUntilNextSample — persist state to Notecard flash, then cut host power
// ===========================================================================
void sleepUntilNextSample() {
    NotePayloadDesc payload = {0, 0, 0};
    NotePayloadAddSegment(&payload, STATE_SEG_ID, &g_state, sizeof(g_state));
    // NotePayloadSaveAndSleep writes state to Notecard flash, then issues
    // card.attn sleep — the Notecard pulls ATTN HIGH after the interval
    // and the Notecarrier CX re-powers the Cygnet, re-entering setup().
    NotePayloadSaveAndSleep(&payload, g_sampleIntervalSec, NULL);

    // If we reach here the carrier does not cut Cygnet power via ATTN;
    // fall back to a blocking delay so we don't spin-poll the Notecard.
    delay(g_sampleIntervalSec * 1000UL);
}
