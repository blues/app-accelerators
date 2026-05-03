/*
 * tenant_sub_meter.ino — Commercial Tenant Energy Monitoring Bridge
 *
 * Blues Notecard + Notecarrier CX (Cygnet STM32L433 host)
 *
 * Up to four Rogowski coil sensors (A0–A3, each through an active Miller
 * integrator) sample each tenant's single-phase branch-circuit current.
 * A voltage reference on A4 — ZMPT101B module for bench/prototype builds, or a
 * production-grade isolated AC voltage transducer for commercial panel
 * installations — provides the building line-voltage waveform for
 * sequential-interleaved V×I power measurement.  Estimated interval energy
 * (Wh, derived from periodic active-power snapshots), peak demand power
 * (W, highest DEMAND_INTERVAL_SEC blocked average), and a per-channel fault
 * bitmask are accumulated per tenant and reported as a template-backed
 * hourly summary note (meter_summary.qo).
 *
 * IMPORTANT — readings are ESTIMATED, not metered: each interval's energy is
 * derived from a single ~200 ms active-power snapshot (mean V×I over 2000
 * pairs) multiplied by the full sample_interval_sec.  Accurate for constant or
 * slowly-varying loads; may over- or under-state energy on bursting or cycling
 * loads.  For certified metering accuracy, replace the measurement front end
 * with a dedicated simultaneous-sampling energy-metering IC.  See README §6.3.
 *
 * The hourly meter_summary.qo notes stored in Notehub are the canonical
 * record.  A downstream billing system can sum t*_wh across any date range
 * by querying the Notehub Event Query API — all monthly aggregation is
 * performed on the Notehub side; no device-side monthly rollup is generated.
 *
 * Cellular is the only viable data channel: landlord energy telemetry must
 * not traverse tenant-owned WiFi or any building network the tenants can reach.
 *
 * Hardware:
 *   • Blues Notecarrier CX (Cygnet STM32L433 host)
 *   • Blues Notecard Cell+WiFi MBGLW in M.2 slot
 *   • 4× Rogowski coil sensor (e.g. Magnelab RCT-1800-000 or equivalent) on A0–A3
 *     Each channel through an active Miller integrator:
 *       – R_in  10 kΩ (input series resistor)
 *       – C_f   10 nF (feedback integration capacitor)
 *       – R_f    1 MΩ (feedback DC-stabilisation resistor)
 *       – op-amp section of MCP6004 (single-supply quad, 2.5 V–5.5 V)
 *     Followed by a half-rail bias divider: 2× 100 kΩ + 1× 10 µF → 1.65 V
 *   • Voltage reference on A4:
 *     – Bench/prototype: ZMPT101B module (VCC/GND/VOUT, 5 V supply) with
 *       half-rail bias: 2× 100 kΩ + 1× 10 µF → 1.65 V
 *     – Production: UL/IEC 61010-1 listed isolated AC voltage transducer with
 *       analog output (see README §3 and §4)
 *   • Blues Mojo inline on +VBAT (commissioning and bench validation only)
 *
 * Calibration notes:
 *   rogowski_amps_per_volt — A/V at the integrator output.  Derivation:
 *     primary_amps_rated ÷ integrator_output_V_rms_at_rated_amps.
 *     The default of 400 is a nominal starting point only; it does NOT
 *     correspond to any specific coil model or installation.  Always
 *     calibrate against a reference clamp meter at a known load after
 *     installation and push the measured value via the Notehub env var
 *     before using readings for tenant billing.
 *   volt_scale — line-voltage V RMS ÷ ADC-side V RMS (voltage transducer
 *     calibration).  For the ZMPT101B at 120 V with factory burden, output
 *     ≈ 0.1 V RMS; default is 120 ÷ 0.1 = 1200.  Trim per module and burden,
 *     or per the production transducer's rated output.
 *
 * Helper functions (measureChannel, notecardReady, fetchEnvOverrides,
 * initNotecard, reissueHubSet, defineTemplates, sendSummary, getEpochSec) are
 * in tenant_sub_meter_helpers.cpp.  Shared types, constants, and extern
 * declarations live in tenant_sub_meter_helpers.h.
 *
 * SPDX-License-Identifier: MIT
 */

#include "tenant_sub_meter_helpers.h"

// ─── Persist-state segment ID ─────────────────────────────────────────────────
// "SMT5": bumped from "SMT4" to force a clean zero-initialised state when
// upgrading from the previous sketch (PersistState shed all on-device monthly
// energy tracking fields — monthly_mwh[], unanchored_mwh[], monthly_start_epoch,
// pending_bill_valid, pending_bill_from_epoch, pending_bill_to_epoch, _pad2, and
// pending_bill_mwh[] — significantly reducing sizeof(PersistState); monthly
// aggregation is now handled purely Notehub-side; see PersistState ABI note in
// tenant_sub_meter_helpers.h).
static const char STATE_SEG_ID[] = "SMT5";

// ─── Current-sensor pin assignments (voltage pin is in helpers.h) ─────────────
static const uint8_t CURRENT_PINS[4] = {A0, A1, A2, A3};

// ─── Globals ──────────────────────────────────────────────────────────────────
Notecard      notecard;
PersistState  state;
RuntimeConfig cfg;

// ─── Forward declaration ──────────────────────────────────────────────────────
static void runCycle(void);

// =============================================================================
void setup() {
    // Serial is useful during bench bring-up; remove the begin() call for
    // production builds to save a few milliseconds of wake time.
    Serial.begin(115200);

    Wire.begin();
    notecard.begin();

    // Explicitly configure 12-bit ADC resolution.  Arduino-compatible STM32
    // cores may default to 10-bit compatibility mode; this call ensures the
    // ADC_FULL_SCALE = 4095 assumption in measureChannel() is always correct.
    analogReadResolution(12);

    runCycle();
}

// loop() is entered only when card.attn does NOT cut host power (e.g. bench
// rigs where the ATTN→EN path is not wired).  On a production Notecarrier CX,
// NotePayloadSaveAndSleep() in runCycle() cuts Cygnet power before loop() is
// ever reached; the next wake is a cold reset back into setup().  Calling
// runCycle() from loop() ensures the no-ATTN path executes the full
// sample → report → sleep cycle on every interval, not just on first boot.
void loop() {
    runCycle();
}

// =============================================================================
// Main cycle: recover state → sample → report → persist → sleep.
// Called from setup() on every power-on wake.  On no-ATTN bench rigs, loop()
// calls it again after the fallback delay at the end of each cycle.
// =============================================================================
static void runCycle(void) {
    // ── Notecard cold-boot readiness handshake — must be the first I2C transaction
    // sendRequestWithRetry (inside notecardReady) MUST be the first Notecard
    // transaction on every cold boot.  It blocks up to NOTECARD_READY_TIMEOUT_SEC
    // to resolve the I2C race where the STM32L433 host comes up before the
    // Notecard is ready.  On warm wakes it returns almost immediately.
    //
    // This call must precede NotePayloadRetrieveAfterSleep and every other
    // Notecard transaction.  If the Notecard is unresponsive, do not attempt any
    // further I2C access — including NotePayloadSaveAndSleep — and fall back to a
    // plain delay so loop() retries on bench rigs where the ATTN→EN path is not
    // wired.  On a production Notecarrier CX the Notecard will restore host power
    // when it recovers.  cfg is not yet populated at this point, so fall back to
    // DEFAULT_SAMPLE_INTERVAL_SEC rather than cfg.sample_interval_sec.
    if (!notecardReady(NOTECARD_READY_TIMEOUT_SEC)) {
        Serial.println("[notecard] readiness timed out — aborting cycle");
        delay(DEFAULT_SAMPLE_INTERVAL_SEC * 1000UL);
        return;
    }

    // ── Recover state saved before the previous sleep ──────────────────────────
    // Notecard is ready: retrieve the persisted state payload.  On first boot
    // (no saved payload present) NotePayloadGetSegment zero-initialises the
    // struct — the correct initial state for all accumulators and flags.
    NotePayloadDesc payload = {0};
    NotePayloadRetrieveAfterSleep(&payload);
    NotePayloadGetSegment(&payload, STATE_SEG_ID, &state, sizeof(state));

    // ── Pull environment variable overrides from Notehub ────────────────────────
    // cfg.summary_interval_min is valid after this call and is used both by
    // hub.set (outbound cadence) and the hourly-summary trigger below.
    fetchEnvOverrides();

    // ── Detect num_tenants decrease; clear stale accumulators ──────────────────
    // If the operator reduces num_tenants, dormant channels may hold partial
    // data from when they were active.  Clear all per-cycle accumulators for
    // deactivated channels before this cycle's energy is added so stale totals
    // never appear in meter_summary.qo.
    uint8_t active = (cfg.num_tenants <= 4) ? cfg.num_tenants : 4;
    if (state.prev_num_tenants > 0 && active < state.prev_num_tenants) {
        for (uint8_t t = active; t < 4; t++) {
            state.tenant[t].accum_wh_milli    = 0;  // clear stale hourly energy
            state.tenant[t].demand_window_mwh = 0;  // clear stale demand window
            state.tenant[t].demand_window_sec = 0;
            state.tenant[t].peak_demand_cw    = 0;  // clear stale peak demand
            state.tenant[t].fault_accum       = 0;  // clear stale fault history
        }
    }
    state.prev_num_tenants = active;

    // ── First-boot: configure Notecard and register note templates ─────────────
    // initNotecard() uses cfg.summary_interval_min (populated above) as the
    // hub.set outbound cadence, so syncs match the configured report frequency.
    if (!state.notecard_configured) {
        Serial.println("[notecard] first-boot: hub.set + template registration");
        bool hub_ok = initNotecard();

        // Disable the onboard accelerometer — unnecessary for this application
        // and its background sampling adds avoidable quiescent current draw.
        J *req = notecard.newRequest("card.motion.mode");
        if (req) {
            JAddBoolToObject(req, "stop", true);
            notecard.sendRequest(req);
        }

        // Both hub.set and template registration must succeed before latching
        // notecard_configured.  If either fails the flag stays clear and the
        // full init sequence retries automatically on the next wake.
        bool tmpl_ok = hub_ok && defineTemplates();
        if (hub_ok && tmpl_ok) {
            state.notecard_configured = 1;
            state.last_outbound_min   = cfg.summary_interval_min;
        } else {
            Serial.println("[notecard] init incomplete — will retry next wake");
        }
    } else if (cfg.summary_interval_min != state.last_outbound_min) {
        // ── Re-issue hub.set when the summary cadence changes ────────────────────
        // Matches the Notecard's outbound flush frequency to the configured
        // reporting interval, avoiding both unnecessary syncs and delivery lag.
        if (reissueHubSet()) {
            state.last_outbound_min = cfg.summary_interval_min;
        }
    }

    // ── Get current UTC time from Notecard ──────────────────────────────────────
    uint32_t now = getEpochSec();

    // ── Elapsed seconds since the last completed sample ─────────────────────────
    // Used in demand-window accumulation below.  When time is not yet valid
    // (now == 0) or this is the first boot (last_sample_epoch == 0), fall back to
    // cfg.sample_interval_sec so the window still advances predictably.
    uint32_t elapsed = (now > 0 && state.last_sample_epoch > 0
                        && now > state.last_sample_epoch)
                       ? (now - state.last_sample_epoch)
                       : cfg.sample_interval_sec;

    // ── Recovery: first summary sent before time was valid ──────────────────────
    // If sendSummary() succeeded on a pre-sync wake (now == 0 at the time),
    // first_summary_sent was latched but last_summary_epoch was left at 0.
    // Seed it now so the normal elapsed-time gate takes over from this wake.
    if (now > 0 && state.first_summary_sent && state.last_summary_epoch == 0) {
        state.last_summary_epoch = now;
    }

    // ── Sample every active tenant channel ──────────────────────────────────────
    // ACCURACY NOTE: each delta_mwh is one ~200 ms active-power snapshot (mean
    // V×I over 2000 pairs) multiplied by the full sample_interval_sec.  This is
    // estimated interval energy — accurate for constant or slowly-varying loads
    // but may under- or over-state energy on bursting or cycling loads.  See
    // README §6.3.
    //
    // RELIABILITY NOTE: accumulators are only reset after a confirmed successful
    // note.add.  If sendSummary() returns false the data is preserved intact; the
    // next wake cycle retries automatically.  This prevents silent data loss on
    // transient I2C faults or Notecard busy conditions.
    uint64_t delta_mwh[4] = {0};  // estimated milli-Wh this interval, per tenant
    for (uint8_t t = 0; t < active; t++) {
        ChannelMeasurement m = measureChannel(CURRENT_PINS[t]);

        Serial.print("[sample] T"); Serial.print(t + 1);
        Serial.print(": ");         Serial.print(m.rms_amps, 2);
        Serial.print(" A  ");       Serial.print(m.watts, 1);
        Serial.print(" W");
        if (m.fault) {
            Serial.print("  fault=0x"); Serial.print(m.fault, HEX);
        }
        Serial.println();

        // Accumulate per-channel fault flags across the summary period so any
        // hardware anomaly detected on any wake is visible in fault_mask.
        state.tenant[t].fault_accum |= m.fault;

        // Clamp negative watts to zero — a negative result indicates a reversed
        // Rogowski coil lead; see rogowski_amps_per_volt note in the file header.
        float w = (m.watts > 0.0f) ? m.watts : 0.0f;
        // Use double arithmetic for the energy conversion to avoid float32
        // truncation at high-wattage × long-interval combinations.
        delta_mwh[t] = (uint64_t)(
            ((double)w * cfg.sample_interval_sec * 1000.0) / 3600.0 + 0.5
        );
    }

    // ── Update hourly accumulators and demand window ────────────────────────────
    // The hourly summary spans sample wakes without interruption.
    //
    // Demand window: energy and elapsed time accumulate across wakes.  When the
    // window reaches DEMAND_INTERVAL_SEC, average watts are computed and
    // peak_demand_cw is updated if higher; then the window resets.  The window
    // deliberately straddles summary-period boundaries: peak_demand_cw is cleared
    // on summary emit but demand_window_mwh/sec continue, so no demand interval
    // is ever silently truncated at a summary boundary.
    for (uint8_t t = 0; t < active; t++) {
        state.tenant[t].accum_wh_milli    += delta_mwh[t];
        state.tenant[t].demand_window_mwh += delta_mwh[t];
        state.tenant[t].demand_window_sec += elapsed;

        if (state.tenant[t].demand_window_sec >= DEMAND_INTERVAL_SEC) {
            // Average watts over the completed demand window.
            // demand_window_mwh [mWh] × 3600 / (demand_window_sec [s] × 1000) = W
            double avg_w = (state.tenant[t].demand_window_sec > 0)
                ? ((double)state.tenant[t].demand_window_mwh * 3600.0
                   / ((double)state.tenant[t].demand_window_sec * 1000.0))
                : 0.0;
            uint32_t avg_cw = (uint32_t)(avg_w * 100.0 + 0.5);
            if (avg_cw > state.tenant[t].peak_demand_cw) {
                state.tenant[t].peak_demand_cw = avg_cw;
            }
            // Reset; the next sample opens a new demand interval.
            state.tenant[t].demand_window_mwh = 0;
            state.tenant[t].demand_window_sec = 0;
        }
    }

    // ── Hourly summary emission ─────────────────────────────────────────────────
    // Emit on first boot or once the configured interval has elapsed.
    bool first_boot = (state.last_summary_epoch == 0) && !state.first_summary_sent;
    uint32_t since  = (now > 0 && state.last_summary_epoch > 0)
                      ? (now - state.last_summary_epoch) : 0;

    if (state.notecard_configured && (first_boot || since >= cfg.summary_interval_min * 60UL)) {
        bool sent = sendSummary();
        if (sent) {
            state.first_summary_sent = 1;
            for (uint8_t t = 0; t < 4; t++) {
                state.tenant[t].accum_wh_milli  = 0;
                state.tenant[t].peak_demand_cw  = 0;
                // demand_window_mwh and demand_window_sec are intentionally
                // preserved: the in-progress demand interval continues across
                // summary-period boundaries.
                state.tenant[t].fault_accum     = 0;
            }
            if (now > 0) {
                state.last_summary_epoch = now;
            }
        } else {
            Serial.println("[summary] note.add failed — accumulators preserved for retry");
        }
    }

    // ── Record epoch of this completed sample ───────────────────────────────────
    // Used on the next wake to compute elapsed time for demand-window
    // accumulation.  Only updated when time is valid.
    if (now > 0) {
        state.last_sample_epoch = now;
    }

    // ── Persist state and cut host power until next sample ──────────────────────
    NotePayloadDesc out = {0};
    NotePayloadAddSegment(&out, STATE_SEG_ID, &state, sizeof(state));
    NotePayloadSaveAndSleep(&out, cfg.sample_interval_sec, NULL);

    // card.attn cuts Cygnet power on the Notecarrier CX.  This fallback fires
    // only on bench rigs where the ATTN→EN path is not wired.  After this delay,
    // loop() calls runCycle() again, executing the full sample/report/sleep cycle
    // rather than spinning in a sleep-only loop.
    delay(cfg.sample_interval_sec * 1000UL);
}
