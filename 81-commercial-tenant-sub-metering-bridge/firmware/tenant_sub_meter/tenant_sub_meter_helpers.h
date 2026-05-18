/*
 * tenant_sub_meter_helpers.h — Shared types, constants, and declarations
 *                              for Commercial Tenant Energy Monitoring Bridge
 *
 * Included by both tenant_sub_meter.ino (main sketch, defines the globals
 * declared extern below) and tenant_sub_meter_helpers.cpp (implementations).
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <Arduino.h>
#include <Notecard.h>

// ─── Product UID ──────────────────────────────────────────────────────────────
#ifndef PRODUCT_UID
#define PRODUCT_UID ""   // set to your Notehub ProductUID before flashing
#pragma message "PRODUCT_UID is not defined — replace the empty string"
#endif

// ─── Firmware defaults (all overridable via Notehub environment variables) ───
static const uint32_t DEFAULT_SAMPLE_INTERVAL_SEC   = 300;     // 5 min
static const uint32_t DEFAULT_SUMMARY_INTERVAL_MIN  = 60;      // 1 hr
static const float    DEFAULT_ROGOWSKI_AMPS_PER_VOLT = 400.0f;
static const float    DEFAULT_VOLT_SCALE             = 1200.0f;
static const uint8_t  DEFAULT_NUM_TENANTS            = 4;

// ─── Env var bounds ───────────────────────────────────────────────────────────
static const uint32_t MIN_SAMPLE_INTERVAL_SEC  = 60;
static const uint32_t MAX_SAMPLE_INTERVAL_SEC  = 86400;  // 24 hr ceiling
// MIN_SUMMARY_INTERVAL_MIN is set to DEMAND_INTERVAL_SEC / 60 = 15 min.
// A summary period shorter than one full demand window would always report
// t*_demand_w = 0 because no 15-minute window could complete within the period.
// Clamping the minimum here prevents that correctness gap at the source.
static const uint32_t MIN_SUMMARY_INTERVAL_MIN = 15;     // floor = one demand window
static const uint32_t MAX_SUMMARY_INTERVAL_MIN = 1440;   // 24 hr ceiling

// ─── Notecard sync parameters ─────────────────────────────────────────────────
// INBOUND_MINUTES is fixed; OUTBOUND_MINUTES tracks cfg.summary_interval_min
// and is written to hub.set dynamically (see reissueHubSet()).
static const uint32_t INBOUND_MINUTES = 120;  // pull env var changes every 2 hr

// ─── Demand-window interval ───────────────────────────────────────────────────
// Length of each fixed blocked-average demand window.  Energy accumulated over
// DEMAND_INTERVAL_SEC is divided by the elapsed window time to yield an average-
// watts demand reading; the peak of all such windows in a summary period is
// reported as t*_demand_w.  900 s = 15 minutes matches the most common North
// American utility demand-charge billing window.  Change to 1800 for a 30-minute
// tariff by editing this constant (no env-var override needed; re-flash required).
static const uint32_t DEMAND_INTERVAL_SEC = 900;  // 15-minute demand window

// ─── Cold-boot readiness timeout ─────────────────────────────────────────────
// Passed to notecardReady(); limits the I2C handshake on power-on.
static const uint32_t NOTECARD_READY_TIMEOUT_SEC = 10;

// ─── Notefile names ──────────────────────────────────────────────────────────
#define SUMMARY_NOTEFILE  "meter_summary.qo"

// ─── ADC sampling ────────────────────────────────────────────────────────────
// 2000 V+I pairs × 2 reads per pair × ~50 µs per read ≈ 200 ms, covering
// ~12 full 60 Hz cycles.  analogReadResolution(12) in setup() guarantees
// 12-bit output; ADC_FULL_SCALE must match that setting.
static const int   ADC_SAMPLES    = 2000;
static const float ADC_VREF       = 3.3f;
static const float ADC_FULL_SCALE = 4095.0f;  // 12-bit; enforced by analogReadResolution(12)

// ─── Voltage-sensor pin (used by measureChannel in helpers.cpp) ──────────────
static const uint8_t VOLTAGE_PIN = A4;  // voltage transducer output (ZMPT101B or production)

// ─── Per-channel fault flags (ChannelMeasurement.fault, OR'd into TenantState.fault_accum) ─
// Returned by measureChannel() and accumulated over the summary period.  Any non-zero
// fault_accum value is packed into meter_summary.qo's fault_mask field so downstream
// billing systems can reject or flag suspect readings.
//
// FAULT_BIAS_RANGE  — current-channel DC bias outside the expected 1.40–1.90 V window;
//   indicates a disconnected bias resistor, shorted decoupling capacitor, or absent
//   supply voltage on one half of the half-rail divider.
// FAULT_SATURATED   — current-channel RMS exceeds 85 % of the 1.65 V half-swing;
//   indicates ADC rail clipping and likely distortion of both power and RMS results.
// FAULT_VOLTAGE_REF — shared voltage-reference fault: voltage transducer bias out of
//   range, voltage signal saturated, or line voltage implausibly low
//   (< VOLTAGE_MIN_V_RMS).  Set simultaneously in every active channel's fault nibble
//   so downstream billing can identify periods where all tenant watt calculations are
//   compromised.  Occupies bit 3 of each 4-bit channel nibble in fault_mask.
//
// Bit 2 of each nibble (0x04) is reserved.  FAULT_NO_SIGNAL (formerly 0x04) has been
// retired from fault_mask: low RMS current is indistinguishable from a legitimately
// unloaded tenant, and treating it as a hard fault quarantines valid zero-usage
// intervals.  The threshold check is kept as a commissioning-only Serial diagnostic
// in measureChannel(); it never sets m.fault.
static const uint8_t FAULT_BIAS_RANGE  = 0x01;
static const uint8_t FAULT_SATURATED   = 0x02;
// 0x04 reserved — formerly FAULT_NO_SIGNAL, retired from fault_mask; see above.
static const uint8_t FAULT_VOLTAGE_REF = 0x08;  // shared voltage-path fault (bit 3 of nibble)

// Bias validation thresholds (ADC counts, 12-bit / 3.3 V reference).
//   1.40 V / 3.30 V × 4095 ≈ 1738 counts (lower bound)
//   1.90 V / 3.30 V × 4095 ≈ 2359 counts (upper bound)
static const float BIAS_MIN_COUNTS       = 1738.0f;
static const float BIAS_MAX_COUNTS       = 2359.0f;
// Saturation guard: trips when the centered signal's RMS exceeds the RMS-equivalent
// of 85 % of the 1.65 V half-rail peak amplitude.  For a sine wave, peak amplitude
// ≤ ADC_FULL_SCALE/2 (≈ 2047 counts at 12-bit resolution) and RMS ≤ peak/√2 ≈
// 1448 counts; trying to compare an RMS value against a peak threshold (e.g. ~1740
// counts) would never trip on an unclipped sine wave.  Convert peak → RMS with /√2:
//   0.85 × (4095 / 2) / √2 ≈ 1230 counts RMS  →  ≈ 0.99 V RMS at the ADC pin
// This trips before the signal actually rails, providing margin to flag waveforms
// approaching distortion.
static const float SATURATION_RMS_COUNTS = 1230.0f;
// Commissioning-only low-current threshold: NOT propagated to fault_mask.
// See FAULT_VOLTAGE_REF note above and measureChannel() implementation.
static const float MIN_SIGNAL_AMPS       = 0.05f;
// Minimum plausible line RMS voltage (V) for the FAULT_VOLTAGE_REF plausibility check.
// 60 V (0.5 pu on a 120 V nominal line) clearly distinguishes a disconnected voltage
// transducer from a deep brownout (which still exceeds 90 V = 0.75 pu).
static const float VOLTAGE_MIN_V_RMS     = 60.0f;

// ─── Per-tenant hourly accumulator ───────────────────────────────────────────
struct TenantState {
    uint64_t accum_wh_milli;      // milli-Wh estimated interval energy this summary period.
                                  // Derived from periodic ~200 ms active-power snapshots ×
                                  // sample_interval_sec; not a continuous integral.  Accurate
                                  // for constant or slowly-varying loads; may over- or under-
                                  // state energy on bursting or cycling loads.  See README §6.3.
                                  // uint64_t supports >50 years of continuous 200 A / 120 V load.
    uint64_t demand_window_mwh;   // milli-Wh accumulated in the current demand interval window.
                                  // Paired with demand_window_sec.  NOT reset on summary emit —
                                  // the demand window straddles summary-period boundaries so no
                                  // incomplete window is silently discarded.
    uint32_t demand_window_sec;   // elapsed seconds in the current demand interval window.
                                  // When this reaches DEMAND_INTERVAL_SEC, avg kW is computed,
                                  // peak_demand_cw is updated if higher, and both window fields
                                  // are reset to begin the next interval.
    uint32_t peak_demand_cw;      // centi-watts: highest DEMAND_INTERVAL_SEC-average active
                                  // power seen this summary period.  This is a true interval-
                                  // average demand reading, not a short transient snapshot.
                                  // Reset to 0 after a confirmed sendSummary().
    uint8_t  fault_accum;         // OR of all per-wake FAULT_* flags across this summary period.
                                  // Cleared after a confirmed sendSummary().  Packed (4-bit nibble
                                  // per channel) into the summary note's fault_mask field so
                                  // downstream billing systems can reject suspect readings.
    uint8_t  _pad[7];             // explicit padding; keeps sizeof(TenantState) == 32 and
                                  // maintains 8-byte alignment of both uint64_t fields in array.
};

// ─── Full persistent state (survives card.attn sleep via Notecard payload) ───
//
// ABI / layout note
// -----------------
// Field order must not change between firmware versions.  New fields appended
// at the END are backward-compatible: NotePayloadGetSegment zero-fills tail
// bytes when restoring a smaller (older) payload, which is the correct initial
// state for any new field.
//
// SMT2 → SMT3: TenantState gained fault_accum (uint8_t) and _pad[3] (explicit
// padding), growing sizeof(TenantState) from 12 to 16 bytes.  The
// meter_summary.qo template was updated to rename t*_peak_w → t*_peak_snapshot_w
// and add a fault_mask field.
//
// SMT3 → SMT4: TenantState replaced peak_snapshot_cw (uint32_t) with
// demand_window_mwh (uint64_t), demand_window_sec (uint32_t), and
// peak_demand_cw (uint32_t), growing sizeof(TenantState) from 16 to 32 bytes.
// The meter_summary.qo template was updated to rename t*_peak_snapshot_w →
// t*_demand_w (now a true DEMAND_INTERVAL_SEC blocked-average demand reading).
// FAULT_NO_SIGNAL was retired from fault_mask (bit 2 reserved; bit 3 is now
// FAULT_VOLTAGE_REF).
//
// SMT4 → SMT5 (this revision): PersistState shed all on-device monthly energy
// tracking — monthly_mwh[], unanchored_mwh[], monthly_start_epoch,
// pending_bill_valid, pending_bill_from_epoch, pending_bill_to_epoch, _pad2, and
// pending_bill_mwh[] — significantly reducing sizeof(PersistState).  Monthly
// energy aggregation is now handled purely Notehub-side by summing hourly
// meter_summary.qo events via the Notehub Event Query API.  The segment ID bump
// forces a clean zero-initialised struct on upgrade (handled automatically by
// NotePayloadGetSegment when the stored segment ID does not match).
//
// SMT1 → SMT2: TenantState and monthly_mwh widened from uint32_t to uint64_t;
// pending_bill_* fields added.
//
// SMT1 was the initial release ("SMTR" in the very first sketch; renamed SMT1
// for clarity).
struct PersistState {
    TenantState tenant[4];
    uint32_t    last_summary_epoch;  // epoch when the last hourly summary note was sent
    uint32_t    last_sample_epoch;   // epoch of the most recent completed sample cycle;
                                     // used to compute elapsed demand-window time on wake
    uint32_t    last_outbound_min;   // outbound cadence (min) last pushed to hub.set;
                                     // triggers a re-issue when summary_interval_min changes
    uint8_t     notecard_configured; // latched after hub.set + templates both succeed
    uint8_t     first_summary_sent;  // latched after first sendSummary() succeeds
    uint8_t     prev_num_tenants;    // num_tenants at previous wake; detects decreases
    uint8_t     _pad;                // explicit padding; aligns struct to 4-byte boundary
};

// ─── Runtime configuration (reloaded from env vars every wake cycle) ─────────
struct RuntimeConfig {
    uint32_t sample_interval_sec;
    uint32_t summary_interval_min;
    float    rogowski_amps_per_volt;  // primary A ÷ integrator output V RMS at rated amps
    float    volt_scale;              // line-V RMS ÷ ADC-side V RMS (voltage transducer calibration)
    uint8_t  num_tenants;
};

// ─── V×I measurement result ──────────────────────────────────────────────────
struct ChannelMeasurement {
    float   rms_amps;  // RMS current (A)
    float   watts;     // active power (W) from sequential-interleaved V×I cross-product
    uint8_t fault;     // OR of FAULT_* flags; 0 = all sanity checks passed
};

// ─── Globals defined in tenant_sub_meter.ino ─────────────────────────────────
extern Notecard      notecard;
extern PersistState  state;
extern RuntimeConfig cfg;

// ─── Helper function declarations ────────────────────────────────────────────
ChannelMeasurement measureChannel(uint8_t current_pin);
bool     notecardReady(uint32_t timeout_sec); // cold-boot I2C handshake (sendRequestWithRetry)
void     fetchEnvOverrides(void);
bool     initNotecard(void);     // first-boot hub.set; returns true on acknowledged success
bool     reissueHubSet(void);    // re-sends hub.set when outbound cadence changes
bool     defineTemplates(void);
bool     sendSummary(void);
uint32_t getEpochSec(void);
