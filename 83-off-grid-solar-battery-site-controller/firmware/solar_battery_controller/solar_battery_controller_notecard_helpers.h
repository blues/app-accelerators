/***************************************************************************
  solar_battery_controller_notecard_helpers - Notecard & Configuration
  for Off-Grid Solar Battery Site Controller

  Encapsulates all Notecard I/O (hub.set, note.template, env.get,
  note.add), the PersistState layout, and every configuration constant
  so the main sketch can focus purely on orchestration.

  The globals notecard, state, desired_outbound_min, and desired_inbound_min
  are defined in the main .ino and referenced from the companion .cpp via
  extern declarations.
***************************************************************************/

#pragma once
#include <Notecard.h>

// ---------------------------------------------------------------------------
// Default thresholds (all overridable via Notehub environment variables)
// ---------------------------------------------------------------------------
#define DEFAULT_SOC_ALERT_PCT        20.0f   // % — fire soc_low below this
#define DEFAULT_BAT_TEMP_MAX_C       45.0f   // °C — fire temp_high above this
#define DEFAULT_LOAD_ALERT_W        1000.0f  // W — fire load_high above this
#define DEFAULT_SAMPLE_INTERVAL_SEC  900UL   // 15 min between wakes

// DEFAULT_REPORT_INTERVAL_MIN: compile with -DSKYLO_BUILD for NOTE-NBGLWX
// satellite deployments.  This sets a satellite-safe 24-hour default cadence
// so the very first wake, before any Notehub inbound sync delivers Fleet env
// vars, does not burn satellite data budget at the 4-hour cellular rate.
// Fleet environment variables still tune cadence after the first inbound sync.
#ifdef SKYLO_BUILD
#  define DEFAULT_REPORT_INTERVAL_MIN  1440UL  // Skylo NTN: once per day (satellite-safe default)
#else
#  define DEFAULT_REPORT_INTERVAL_MIN  240UL   // LTE-M default: every 4 hours
#endif

// Per-alert repeat cooldown — number of sample wakes between re-fires while
// a fault persists.  At the default 15-min sample interval, 2 wakes ≈ 30 min.
// Persisted in PersistState so the cooldown survives sleep cycles.
#define ALERT_COOLDOWN_SAMPLES  2U

// Summary sentinel values — emitted for every template field even when no
// valid samples were collected in the window.  Using explicit out-of-physical-
// range constants keeps the fixed-schema template fully populated so downstream
// analytics can rely on column presence and distinguish 'no data' from a real
// zero measurement.  Aligns with the standard sentinel pattern used across
// Blues reference designs (SUMMARY_INVALID_SENTINEL = -9999).
//
//   SUMMARY_SENTINEL_F   — float fields: bat_v, bat_a, bat_w, soc_pct,
//                          bat_temp_c, pv_v, pv_w, yield_kwh, load_w
//   SUMMARY_SENTINEL_TTG — ttg_min (int): no SmartShunt data in window;
//                          use −1 when shunt present but not discharging
//   SUMMARY_SENTINEL_CS  — cs (int): no MPPT data in window; all valid
//                          VE.Direct CS codes are non-negative (0, 2–5, 7,
//                          245, 247, 252), so −1 unambiguously signals
//                          "no MPPT data" without colliding with any real state
#define SUMMARY_SENTINEL_F    -9999.0f
#define SUMMARY_SENTINEL_TTG  -9999
#define SUMMARY_SENTINEL_CS      -1

// PersistState layout guard — bump STATE_VERSION whenever the struct changes
// so stale flash content from a prior layout is detected and discarded cleanly.
#define STATE_MAGIC    0x534F4C52UL  // ASCII "SOLR"
#define STATE_VERSION  5

// NotePayload state segment ID (must be exactly 4 characters)
#define STATE_SEG_ID  "SOLR"

// ---------------------------------------------------------------------------
// PersistState — binary-serialised to Notecard flash across sleep cycles
// ---------------------------------------------------------------------------
struct PersistState {
    // Layout guard — validated after restore; mismatch forces a clean init.
    uint32_t magic;
    uint8_t  version;

    uint32_t boot_count;
    uint16_t samples_until_summary;   // countdown to next summary note

    // Accumulators for the current summary window
    float bat_v_sum;  float bat_v_cnt;
    float bat_a_sum;  float bat_a_cnt;
    float bat_w_sum;  float bat_w_cnt;
    float soc_sum;    float soc_cnt;
    float temp_sum;   float temp_cnt;
    float pv_v_sum;   float pv_v_cnt;
    float pv_w_sum;   float pv_w_cnt;
    float load_w_sum; float load_w_cnt;

    // Latest (not averaged) fields
    float   yield_kwh;
    int16_t cs;     // int16 to hold VE.Direct codes up to 252 without truncation
    int32_t ttg_min;

    // Per-alert state: active flag + repeat-cooldown counter.
    //   active  — alert was queued for this excursion; cleared when condition resolves.
    //   _cd     — wakes remaining before the alert may re-fire while the fault persists;
    //             loaded with ALERT_COOLDOWN_SAMPLES after each successful send.
    // Active flags are only set after note.add is confirmed queued so a transient
    // failure cannot arm suppression.  Both fields survive sleep cycles in flash.
    bool     soc_alert_active;    uint16_t soc_alert_cd;
    bool     temp_alert_active;   uint16_t temp_alert_cd;
    bool     load_alert_active;   uint16_t load_alert_cd;

    // Harvest-deficit tracking (recharge failure detection)
    bool     saw_full_charge;         // set if MPPT reached Float, Absorption, Equalize,
                                      // or Auto Equalize this window — any of these means
                                      // the battery completed a charge cycle
    uint16_t no_charge_window_count;  // consecutive summary windows without a full-charge state
    uint16_t harvest_cd;              // cooldown (windows) before harvest_deficit re-fires

    // Cached environment variables — refreshed every boot from Notehub
    float    soc_alert_pct;
    float    bat_temp_max_c;
    float    load_alert_w;
    uint32_t sample_interval_sec;
    uint32_t report_interval_min;
    float    harvest_deficit_days;    // 0 = disabled; N = alert after N days without a full-charge state

    // Last sync cadence successfully applied to the Notecard via hub.set.
    // Initialised to 0 so applyHubSetIfChanged() always fires on first boot.
    uint32_t last_outbound_min;
    uint32_t last_inbound_min;

    // Indicates that note.template for solar_summary.qo was confirmed registered
    // by the Notecard.  Cleared on a clean init (magic/version mismatch) so that
    // a firmware update that bumps STATE_VERSION re-registers on the next boot.
    bool     templates_confirmed;
};

// ---------------------------------------------------------------------------
// Function declarations
// ---------------------------------------------------------------------------

// One-time hardware init on a clean (non-restored) boot: disables the onboard
// accelerometer to reduce idle draw.
void notecardFirstBoot();

// Register fixed-width Note templates to minimise on-wire payload size over
// the lifetime of the deployment.  Returns true when the template is confirmed
// registered; callers should persist templates_confirmed and retry on the next
// boot if this returns false.
bool defineTemplates();

// Issue hub.set on every boot when PRODUCT_UID is configured so that a
// firmware reflash with a different UID immediately corrects the Notecard's
// Notehub association.  When PRODUCT_UID is empty, only re-issues hub.set if
// the cadence changed.  product_uid is passed in so this module stays
// decoupled from the PRODUCT_UID build-time macro defined in the main sketch.
void applyHubSetIfChanged(const char *product_uid);

// Pull Notehub environment variables and update cached thresholds and sync
// cadence.  Returns true when sample_interval_sec or report_interval_min
// changed so the caller can flush the current accumulation window before the
// new cadence takes effect.
bool fetchEnvOverrides();

// Emit an immediate, sync:true Note for operator notification.  Uses
// requestAndResponse to inspect the Notecard response err field.  Retries
// up to 5 times with 1 s backoff so a transient I2C or Notecard-readiness
// hiccup does not silently drop a time-sensitive event.  Returns true only
// after the Note is confirmed queued; callers must not arm suppression state
// (active flags, cooldowns) on a false return.
bool sendAlert(const char *alert, float v1, float v2, float v3);

// Compute window averages and push a solar_summary.qo Note.  Every template
// field is always emitted; fields with no valid samples receive a sentinel
// value (SUMMARY_SENTINEL_F / _TTG / _CS) so the fixed schema stays fully
// populated.  Returns true when the Note is confirmed queued OR when the
// window had no valid samples (skip with new-window open).  Returns false
// only on a Notecard I/O failure; callers must not reset accumulators on a
// false return so the window data is preserved for the next attempt.
bool sendSummary();
