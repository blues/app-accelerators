/*
 * cargo_cold_chain_monitor_helpers.h
 *
 * Shared constants, ColdChainState definition, extern declarations, and
 * helper-function prototypes for cargo_cold_chain_monitor.ino.
 *
 * Include this header from both the .ino and the helpers .cpp; all
 * application-global state is defined in the .ino and accessed here via
 * extern declarations.
 */
#pragma once

#include <Wire.h>
#include <SPI.h>
#include <Notecard.h>
#include <Adafruit_MAX31865.h>
#include <Adafruit_SHT4x.h>
#include <Adafruit_VEML7700.h>

// ---------------------------------------------------------------------------
// Product UID — set this to your Notehub project's ProductUID
// ---------------------------------------------------------------------------
#ifndef PRODUCT_UID
#define PRODUCT_UID ""
#pragma message "PRODUCT_UID is not defined. Set it before flashing."
#endif

// ---------------------------------------------------------------------------
// Timing
// ---------------------------------------------------------------------------
#define SAMPLE_INTERVAL_SEC    300    // 5 min between samples; override via env var
#define SUMMARY_INTERVAL_MIN   60     // 1 hr between summary notes; override via env var
#define OUTBOUND_INTERVAL_MIN  60     // Notecard outbound sync cadence (matches summary)
// Satellite best-practice: keep inbound polls infrequent to conserve NTN data budget.
// Each inbound poll costs ~50 bytes over satellite; 12-hour cadence is a safe default.
#define INBOUND_INTERVAL_MIN   720    // 12 hr; reduce to 60 for bench testing

// ---------------------------------------------------------------------------
// Alert cooldown — prevents repeated paging during a sustained excursion
// ---------------------------------------------------------------------------
#define ALERT_COOLDOWN_SEC     1800   // 30 min per alert type

// ---------------------------------------------------------------------------
// MAX31865 RTD amplifier — SPI interface for PT100 NIST-traceable probe
// ---------------------------------------------------------------------------
#define MAX31865_CS_PIN    10       // Chip-select GPIO (Cygnet D10)
#define MAX31865_RREF      430.0f  // Reference resistor on Adafruit #3328 for PT100 (ohms)
#define MAX31865_RNOMINAL  100.0f  // PT100 nominal resistance at 0 °C (ohms)

// ---------------------------------------------------------------------------
// Configuration version — encodes hub cadence and note.template schema.
// Automatically changes when OUTBOUND_INTERVAL_MIN, INBOUND_INTERVAL_MIN,
// or SCHEMA_VERSION changes.
//
// CONFIG_VERSION is stored in gState.config_version at configuration time
// and compared against the compiled value on every warm boot.  A mismatch
// clears hub_configured, motion_configured, and templates_registered so
// hub.set, card.motion.mode, and note.template are fully reapplied.
//
// Note: CONFIG_VERSION does NOT encode PRODUCT_UID.  hub.set is re-issued
// on every warm boot (see setup()) so a reflash with a new PRODUCT_UID
// targets the correct Notehub project immediately, without requiring a
// SCHEMA_VERSION bump.
//
// Bump SCHEMA_VERSION manually whenever the note.template field list or
// field types change.  SCHEMA_VERSION must remain < 100.
//
// Encoding: OUTBOUND * 100 000 + INBOUND * 100 + SCHEMA_VERSION.
//
// Schema history:
//   5 — added motion_valid (TUINT16) to cargo_log.db template so unavailable
//         card.motion is distinguishable from genuine zero motion.
//   4 — added boot_seg (TUINT16) to cargo_log.db template.
// ---------------------------------------------------------------------------
#define SCHEMA_VERSION   5U
#define CONFIG_VERSION   ((uint32_t)(OUTBOUND_INTERVAL_MIN) * 100000UL + \
                          (uint32_t)(INBOUND_INTERVAL_MIN)  * 100UL    + \
                          (uint32_t)(SCHEMA_VERSION))

// ---------------------------------------------------------------------------
// Default thresholds — all overridable via Notehub environment variables
// ---------------------------------------------------------------------------
#define DEFAULT_TEMP_MIN_C          2.0f   // Standard pharma cold chain lower limit (°C)
#define DEFAULT_TEMP_MAX_C          8.0f   // Standard pharma cold chain upper limit (°C)
#define DEFAULT_HUMIDITY_MAX        75.0f  // % RH above which condensation risk fires
#define DEFAULT_LIGHT_LUX           50.0f  // Lux above which light_exposure fires
#define DEFAULT_SHOCK_COUNT         5U     // Accumulated motion-event count = shock event

// --- Shipment-state model thresholds ---
// transit_motion_min: motion events per sample interval at or above which the
//   sample is counted as "moving."  Consecutive moving samples advance
//   transit_count toward the IN_TRANSIT threshold.
// dwell_confirm_samples: consecutive low-motion samples before state → DWELL.
// transit_confirm_samples: consecutive high-motion samples before state → IN_TRANSIT.
// dwell_batch_factor: multiplier applied to both summary_interval_min AND the
//   hub.set outbound cadence during confirmed DWELL.  applyDynamicOutbound()
//   re-issues hub.set on every DWELL entry/exit, so both summary volume and
//   outbound satellite session frequency are reduced during long warehouse / DC
//   stays.  Alerts and state-change notes (sync:true) always bypass the outbound
//   queue and trigger an immediate session regardless of cadence.
#define DEFAULT_TRANSIT_MOTION      3U
#define DEFAULT_DWELL_CONFIRM       3U
#define DEFAULT_TRANSIT_CONFIRM     2U
#define DEFAULT_DWELL_BATCH_FACTOR  4U

// ---------------------------------------------------------------------------
// Shipment state codes (stored in ColdChainState.shipment_state)
// ---------------------------------------------------------------------------
#define SHIP_STATE_UNKNOWN   0   // initial — not enough data to classify
#define SHIP_STATE_DWELL     1   // confirmed stationary (warehouse / DC dwell)
#define SHIP_STATE_TRANSIT   2   // confirmed in motion (over-road / rail / vessel)
#define SHIP_STATE_HANDLING  3   // cargo bay opened or sudden motion burst

// ---------------------------------------------------------------------------
// Notefiles and sentinel values
// ---------------------------------------------------------------------------
#define NOTE_SUMMARY   "cargo_data.qo"    // compact-templated hourly summary
#define NOTE_ALERT     "cargo_alert.qo"   // immediate-sync threshold alerts
#define NOTE_LOG       "cargo_log.db"     // compact-templated per-sample tamper-evident log
#define NOTE_STATE     "cargo_state.qo"   // immediate-sync shipment-state transitions

// INVALID_F is non-zero so it survives compact-template omitempty and signals
// "no valid data for this metric" to downstream consumers.
#define INVALID_F      -9999.0f
// MOTION_INVALID is the uint32_t sentinel stored in pending_motion when
// card.motion is unavailable for the window.
#define MOTION_INVALID  UINT32_MAX

// ---------------------------------------------------------------------------
// Orientation buffer size
// ---------------------------------------------------------------------------
#define ORIENT_MAX  32    // max chars for orientation string (e.g. "face-up")

// ---------------------------------------------------------------------------
// Persistent state — serialized to Notecard flash across sleep cycles.
// Segment ID "COL5" — bump this string whenever the struct layout changes
// so old payloads force a clean cold-boot re-initialization rather than
// being deserialized into a mismatched struct.
// ---------------------------------------------------------------------------
#define STATE_SEG  "COL6"

struct ColdChainState {
    // ── Live rolling aggregates — reset after each summary window closes ──────
    float    temp_sum;
    float    temp_min;
    float    temp_max;
    float    rh_sum;
    float    rh_min;
    float    rh_max;
    float    lux_max;
    uint32_t temp_n;          // valid temperature readings this window
    uint32_t rh_n;            // valid humidity readings this window
    uint32_t lux_n;           // valid lux readings this window
    uint32_t motion_total;    // accumulated motion-event count this window
    uint32_t motion_n;        // valid card.motion reads this window (0 = no data)
    uint16_t summary_n;       // total sample cycles in current summary window

    // ── Alert cooldown anchors (Unix epoch of last emission per alert type) ───
    // 0  = never fired — immediately eligible regardless of epoch state.
    // 1  = fired before valid time (pre-sync sentinel) — promoted to the real
    //      epoch on the first post-sync wake so the 30-minute cooldown starts
    //      from time-sync, not from epoch 0.
    // >1 = normal Unix timestamp — eligible after ALERT_COOLDOWN_SEC elapses.
    uint32_t last_temp_low_alert;
    uint32_t last_temp_high_alert;
    uint32_t last_rh_alert;
    uint32_t last_shock_alert;
    uint32_t last_light_alert;
    uint32_t last_tilt_alert;

    // ── Summary-interval anchor ──────────────────────────────────────────────
    uint32_t last_summary_epoch;

    // ── Frozen window pending send ───────────────────────────────────────────
    uint32_t pending_epoch;         // 0 = no pending summary
    float    pending_temp_mean;
    float    pending_temp_min;
    float    pending_temp_max;
    float    pending_rh_mean;
    float    pending_rh_min;
    float    pending_rh_max;
    float    pending_lux_max;
    uint32_t pending_motion;        // MOTION_INVALID = no valid motion data in window
    uint16_t pending_samples;

    // ── Orientation baseline ─────────────────────────────────────────────────
    char     baseline_orientation[ORIENT_MAX];

    // ── Configuration-retry flags ────────────────────────────────────────────
    bool     hub_configured;
    bool     motion_configured;
    bool     templates_registered;
    uint32_t config_version;

    // ── Tamper-evident local log ─────────────────────────────────────────────
    // seq: monotonically incremented before each cargo_log.db entry.  A gap
    //   in Notehub's seq sequence indicates a missed or dropped entry.
    // chain_crc: rolling 32-bit hash that chains seq + sensor readings for
    //   every sample cycle.  Downstream consumers can replay the chain from
    //   the seed value (0) to verify that no entries have been inserted,
    //   deleted, or modified in the remote log.  The hash advances even when
    //   the note.add fails so the chain represents the physical event sequence,
    //   not just the transmitted subset.
    uint32_t seq;
    uint32_t chain_crc;

    // ── Shipment-state model ─────────────────────────────────────────────────
    // shipment_state: current state (SHIP_STATE_* codes above).
    // state_since_epoch: epoch when the current state was entered.
    // dwell_count: consecutive sample cycles with motion below transit threshold;
    //   resets to zero on any high-motion sample.
    // transit_count: consecutive sample cycles with motion at or above transit
    //   threshold; resets to zero on any low-motion sample.
    uint8_t  shipment_state;
    uint32_t state_since_epoch;
    uint16_t dwell_count;
    uint16_t transit_count;

    // ── Dynamic outbound cadence ─────────────────────────────────────────────
    // last_outbound_min: the hub.set outbound cadence (minutes) most recently
    //   applied via notecardConfigure() or applyDynamicOutbound().
    //   0 = not yet set; notecardConfigure() falls back to OUTBOUND_INTERVAL_MIN
    //   and stores the result here.  applyDynamicOutbound() extends this to
    //   OUTBOUND_INTERVAL_MIN * dwell_batch_factor during confirmed DWELL and
    //   restores it to OUTBOUND_INTERVAL_MIN when the device leaves DWELL.
    uint32_t last_outbound_min;

    // ── Boot segment counter ─────────────────────────────────────────────────
    // boot_seg: monotonically incremented on every cold boot (planned reset or
    //   uncontrolled power loss).  Persisted to the Notecard sleep payload for
    //   planned-sleep resilience AND to the local Notecard notefile
    //   chain_boot.dbx for cold-boot resilience (power-loss safe).
    //   Included in every cargo_log.db entry so downstream verifiers can split
    //   chain verification at boot-segment boundaries.  seq and chain_crc reset
    //   to 0 at the start of each new boot_seg; each segment's chain is
    //   independent and replayed from seq=1, seed=0.
    uint16_t boot_seg;

    // ── Pending state-change note ─────────────────────────────────────────────
    // pending_state_change: true when a cargo_state.qo note.add has not yet
    //   been confirmed by the Notecard.  On every subsequent wake the note is
    //   retried before new state detection runs so transitions are delivered in
    //   order.  pending_state_from/to hold the previous and new states at the
    //   time of the original transition; pending_state_epoch is the epoch at
    //   that time.  If a new transition occurs while a prior one is still
    //   pending, the pending record is overwritten with the newer transition
    //   (the prior transition is superseded in the physical record).
    bool     pending_state_change;
    uint8_t  pending_state_from;
    uint8_t  pending_state_to;
    uint32_t pending_state_epoch;
};

// ---------------------------------------------------------------------------
// Extern declarations for globals defined in the .ino
// ---------------------------------------------------------------------------
extern Notecard           notecard;
extern Adafruit_MAX31865  rtd;
extern Adafruit_SHT4x     sht4x;
extern Adafruit_VEML7700  veml7700;
extern ColdChainState     gState;
extern float              gTempMinC;
extern float              gTempMaxC;
extern float              gHumidityMax;
extern float              gLightLux;
extern uint32_t           gShockCount;
extern uint32_t           gSampleSec;
extern uint32_t           gSummaryMin;
extern uint32_t           gTransitMotion;
extern uint32_t           gDwellConfirm;
extern uint32_t           gTransitConfirm;
extern uint32_t           gDwellBatchFactor;

// ---------------------------------------------------------------------------
// Helper function prototypes
// ---------------------------------------------------------------------------
bool     ncSend(J *req);
J       *ncQuery(J *req);
bool     alertCooldownOk(uint32_t last, uint32_t now);
void     notecardConfigure(void);
void     defineTemplates(void);
float    envFloat(J *body, const char *key, float fallback);
void     fetchEnvOverrides(void);
bool     readSensors(float *temp_c, float *rh_pct, float *lux);
bool     readMotionCount(uint32_t *count_out, char *orient_out, size_t orient_max);
void     evaluateAlerts(float temp_c, float rh_pct, float lux,
                        uint32_t motion, bool motionOk,
                        const char *orientation, uint32_t now);
bool     sendAlert(const char *type, float temp_c, float rh_pct,
                   float lux, uint32_t motion);
bool     sendTiltAlert(const char *prev_orient, const char *cur_orient,
                       float temp_c, float rh_pct, float lux, uint32_t motion);
void     accumulateSample(float temp_c, float rh_pct,
                          float lux, uint32_t motion, bool motionOk);
void     snapshotSummary(uint32_t epoch);
bool     sendPendingSummary(void);
void     resetAccumulators(void);
uint32_t currentEpoch(void);
// Tamper-evident per-sample log
bool     sendLogEntry(uint32_t now, float temp_c, float rh_pct,
                      float lux, uint32_t motion, bool motionOk, uint8_t state);
// Shipment-state model
bool     detectShipmentState(uint32_t motion, bool motionOk,
                              float lux, uint32_t now);
bool     sendStateChange(uint8_t prev_state, uint8_t new_state, uint32_t now);
// Dynamic outbound cadence — re-issues hub.set when DWELL state changes
void     applyDynamicOutbound(void);
// Cold-boot-resilient boot segment counter (persisted to chain_boot.dbx)
uint16_t loadOrIncrementBootSeg(void);
// Persist orientation baseline to chain_boot.dbx for cold-boot resilience
void     persistBaselineOrientation(void);
