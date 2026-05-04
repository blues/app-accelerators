/***************************************************************************
  cargo_cold_chain_monitor.ino

  Shipper-Owned Cargo-Level Cold Chain Monitor
  Blues Notecarrier CX (Cygnet host) + Notecard for Skylo (NOTE-NBGLWX)

  Condition monitor for pallet-attached cold chain logging with NIST-
  traceable temperature measurement, a tamper-evident per-sample record,
  and a shipment-state model that drives adaptive summary batching and
  immediate state-change notifications.

  What this sketch does
    - Wakes every sample_interval_sec (default 300 s), reads temperature
      from a NIST-traceable PT100 probe (MAX31865 SPI amplifier), relative
      humidity from an SHT41 (I2C), interior cargo-bay light from an
      inward-facing VEML7700 (I2C), and accumulated motion/orientation from
      the Notecard's built-in accelerometer (card.motion).
    - Dispatches cargo_alert.qo immediately (sync:true) on any threshold
      trip: temp_low, temp_high, humidity_high, shock_detected,
      light_exposure (cargo bay opened), or tilt_detected.  The first alert
      of each type fires immediately regardless of whether valid time has
      been obtained from Notehub; on the first post-sync wake, any pre-sync
      sentinel is promoted to the real epoch so the 30-minute per-type
      cooldown begins from time-sync rather than from epoch 0.
    - Queues one compact-templated cargo_data.qo per effective summary
      interval (summary_interval_min, extended by dwell_batch_factor during
      confirmed warehouse dwell) for hourly or coarsened condition records.
      When a summary interval elapses the completed window is frozen into a
      persistent snapshot and the live accumulator resets immediately; new
      samples continue collecting while the snapshot is retried.  A stale
      snapshot that cannot be delivered before the next window closes is
      discarded; fixed-window boundaries are never stretched by a failed send.
    - Appends one compact-templated cargo_log.db entry per sample cycle for
      a tamper-evident per-sample log.  Each entry carries a monotonic
      sequence number (seq), a rolling integrity chain hash (chain_crc)
      computed over all sensor readings, and a boot_seg counter that
      increments on every cold boot.  seq and chain_crc reset at the start
      of each boot_seg; downstream systems replay the chain within each
      boot_seg from seq=1 (seed=0) to detect insertions, deletions, or
      modifications.  Entries are queued for the regular outbound window
      (no extra satellite session per sample).
    - Runs a shipment-state model (UNKNOWN → DWELL / IN_TRANSIT / HANDLING)
      derived from consecutive motion counts and interior light level.
      Emits cargo_state.qo (sync:true) on every state transition.  During
      confirmed DWELL, both the summary interval and the hub.set outbound
      cadence are extended to their respective defaults * dwell_batch_factor,
      reducing both summary volume and outbound satellite session frequency
      during long warehouse stays.  applyDynamicOutbound() re-issues hub.set
      on every DWELL entry/exit transition.
    - Gates the host fully off between samples via NotePayloadSaveAndSleep /
      card.attn — requires Notecarrier CX host power gating.
    - Re-issues hub.set on every warm boot so a reflash with a new
      PRODUCT_UID takes effect immediately.  card.motion.mode and both
      note.template registrations each set a persistent flag in
      ColdChainState on success and are only reapplied when the flag is
      false — a transient I2C race on cold boot leaves no step permanently
      missed.  All three steps are reapplied unconditionally when
      CONFIG_VERSION changes.
    - Alert-cooldown timestamps advance only when the corresponding note.add
      is confirmed by the Notecard; a failed transmission does not consume
      the cooldown window.

  Sensors
    MAX31865 RTD Amplifier + PT100 Class A probe (SPI) — NIST-traceable temp
    Adafruit SHT41 breakout (#5776)                   — relative humidity (I2C)
    Adafruit VEML7700 breakout (#4162)                — interior cargo-bay light (I2C)
    Notecard built-in accelerometer (card.motion)      — shock / motion / orientation

  Notefiles
    cargo_data.qo   — compact-templated summary (adaptive interval)
    cargo_alert.qo  — immediate sync on any threshold trip
    cargo_log.db    — compact-templated per-sample tamper-evident log
    cargo_state.qo  — immediate sync on shipment-state transitions

  Alert types
    temp_low        — below temp_min_c
    temp_high       — above temp_max_c
    humidity_high   — above humidity_max_pct
    shock_detected  — accumulated motion-event count >= shock_events
    light_exposure  — interior lux >= light_open_lux (cargo bay opened)
    tilt_detected   — orientation changed from baseline set at activation

  Power strategy
    Host is fully cut between samples via NotePayloadSaveAndSleep / card.attn.
    Requires Notecarrier CX host power gating for correct operation.
    Notecard idles at ~8-18 uA between radio sessions (see NOTE-NBGLWX datasheet).
    Full-system estimate: ~2.6-3.5 mAh/hour at default cadence (cellular-dominant).

  See README.md for full wiring, Notehub setup, and validation details.
***************************************************************************/

#include <Wire.h>
#include <SPI.h>
#include <Notecard.h>
#include <Adafruit_MAX31865.h>
#include <Adafruit_SHT4x.h>
#include <Adafruit_VEML7700.h>
#include "cargo_cold_chain_monitor_helpers.h"

// ---------------------------------------------------------------------------
// Object instances — declared here, accessed from helpers.cpp via extern
// ---------------------------------------------------------------------------
Notecard          notecard;
Adafruit_MAX31865 rtd(MAX31865_CS_PIN);   // PT100 RTD amplifier, hardware SPI, CS=D10
Adafruit_SHT4x    sht4x;                  // humidity sensor (I2C, used for RH only)
Adafruit_VEML7700 veml7700;               // interior cargo-bay light sensor (I2C)

// ---------------------------------------------------------------------------
// Persistent state — serialized to Notecard flash across sleep cycles
// ---------------------------------------------------------------------------
ColdChainState gState;

// ---------------------------------------------------------------------------
// Runtime configuration (populated from firmware defaults + env-var overrides)
// ---------------------------------------------------------------------------
float    gTempMinC         = DEFAULT_TEMP_MIN_C;
float    gTempMaxC         = DEFAULT_TEMP_MAX_C;
float    gHumidityMax      = DEFAULT_HUMIDITY_MAX;
float    gLightLux         = DEFAULT_LIGHT_LUX;
uint32_t gShockCount       = DEFAULT_SHOCK_COUNT;
uint32_t gSampleSec        = SAMPLE_INTERVAL_SEC;
uint32_t gSummaryMin       = SUMMARY_INTERVAL_MIN;
uint32_t gTransitMotion    = DEFAULT_TRANSIT_MOTION;
uint32_t gDwellConfirm     = DEFAULT_DWELL_CONFIRM;
uint32_t gTransitConfirm   = DEFAULT_TRANSIT_CONFIRM;
uint32_t gDwellBatchFactor = DEFAULT_DWELL_BATCH_FACTOR;

// ===========================================================================
// setup() — re-entered on every wake from NotePayloadSaveAndSleep
// ===========================================================================
void setup() {
    Serial.begin(115200);
    delay(250);   // Brief wait for Serial on cold boot; safe to miss after sleep

    Wire.begin();
    notecard.begin();  // I2C at default 100 kHz

    // ── Restore or initialize persistent state ──────────────────────────────
    NotePayloadDesc payload;
    bool warmBoot = NotePayloadRetrieveAfterSleep(&payload);
    bool stateOk  = false;

    if (warmBoot) {
        stateOk = NotePayloadGetSegment(&payload, STATE_SEG,
                                        &gState, sizeof(gState));
        NotePayloadFree(&payload);
        if (stateOk) {
            Serial.println("[cargo] warm boot — state restored");
        } else {
            Serial.println("[cargo] warm boot — state segment missing, re-initializing");
        }
    }

    if (!warmBoot || !stateOk) {
        Serial.println("[cargo] cold boot — initializing");
        memset(&gState, 0, sizeof(gState));
        // Seed min/max sentinels so the first real reading always wins
        gState.temp_min = 999.0f;  gState.temp_max = -999.0f;
        gState.rh_min   = 999.0f;  gState.rh_max   = -999.0f;
        gState.lux_max  = 0.0f;
        gState.config_version = CONFIG_VERSION;
        gState.shipment_state = SHIP_STATE_UNKNOWN;
        gState.seq            = 0;
        gState.chain_crc      = 0;
        // Increment and persist the boot-segment counter in Notecard local flash
        // (chain_boot.dbx) so every uncontrolled cold boot produces a distinct,
        // traceable chain-segment boundary in cargo_log.db.  Must run after
        // notecard.begin() and before the first sendLogEntry().
        loadOrIncrementBootSeg();
        notecardConfigure();
        defineTemplates();
    }

    // ── On warm boot, check for config version mismatch ─────────────────────
    if (warmBoot && stateOk) {
        if (gState.config_version != CONFIG_VERSION) {
            Serial.println("[cargo] config version mismatch — forcing Notecard reconfiguration");
            gState.hub_configured       = false;
            gState.motion_configured    = false;
            gState.templates_registered = false;
            gState.config_version       = CONFIG_VERSION;
        }
        notecardConfigure();
        if (!gState.templates_registered) {
            Serial.println("[cargo] retrying incomplete template registration");
            defineTemplates();
        }
    }

    // ── Fetch env-var overrides (every wake) ─────────────────────────────────
    uint32_t prevSampleSec  = gSampleSec;
    uint32_t prevSummaryMin = gSummaryMin;
    fetchEnvOverrides();

    // ── Read sensors ─────────────────────────────────────────────────────────
    float    temp_c = INVALID_F, rh_pct = INVALID_F, lux = INVALID_F;
    bool     sensorsOk = readSensors(&temp_c, &rh_pct, &lux);
    if (!sensorsOk) {
        Serial.println("[cargo] one or more sensor reads failed this cycle");
    }

    char     currentOrientation[ORIENT_MAX] = {0};
    uint32_t motion   = 0;
    bool     motionOk = readMotionCount(&motion, currentOrientation,
                                        sizeof(currentOrientation));
    if (!motionOk) {
        Serial.println("[cargo] card.motion unavailable — shock evaluation skipped");
    }

    // ── One-time epoch read ──────────────────────────────────────────────────
    uint32_t now = currentEpoch();

    // ── Detect cadence changes ───────────────────────────────────────────────
    bool cadenceChanged = (gSampleSec != prevSampleSec ||
                           gSummaryMin != prevSummaryMin);
    if (cadenceChanged) {
        Serial.println("[cargo] cadence changed — resetting summary window");
        resetAccumulators();
        if (now > 0) gState.last_summary_epoch = now;
    }

    // ── Anchor summary window to first wake with valid time ──────────────────
    bool anchorJustSet = false;
    if (now > 0 && gState.last_summary_epoch == 0) {
        gState.last_summary_epoch = now;
        anchorJustSet = true;
        resetAccumulators();
        Serial.println("[cargo] summary window anchored — pre-anchor samples cleared");
    }

    bool skipThisCycle = anchorJustSet || cadenceChanged;

    // ── Set orientation baseline on first successful read ────────────────────
    // On a cold boot after power loss, loadOrIncrementBootSeg() has already
    // restored baseline_orientation from chain_boot.dbx (if previously saved),
    // so this block only fires on true first activation or when chain_boot.dbx
    // was unreachable.  persistBaselineOrientation() saves the new baseline to
    // chain_boot.dbx so subsequent uncontrolled cold boots can restore it,
    // preventing a post-reboot orientation from silently replacing the
    // activation-time baseline and suppressing a real tilt event.
    if (currentOrientation[0] && gState.baseline_orientation[0] == '\0') {
        strncpy(gState.baseline_orientation, currentOrientation,
                sizeof(gState.baseline_orientation) - 1);
        gState.baseline_orientation[sizeof(gState.baseline_orientation) - 1] = '\0';
        persistBaselineOrientation();
        Serial.print("[cargo] orientation baseline set: ");
        Serial.println(gState.baseline_orientation);
    }

    // ── Retry pending state-change note from a prior failed send ─────────────
    // Attempted before new state detection so transitions are delivered in
    // order.  A transient Notecard failure on the original send does not
    // permanently lose the chain-of-custody record — pending_state_change
    // remains set in ColdChainState until the Notecard confirms note.add.
    if (gState.pending_state_change) {
        if (sendStateChange(gState.pending_state_from, gState.pending_state_to,
                            gState.pending_state_epoch)) {
            gState.pending_state_change = false;
        } else {
            Serial.println("[cargo] state-change note retry failed — will retry next wake");
        }
    }

    // ── Shipment-state detection ─────────────────────────────────────────────
    // Run before evaluateAlerts so light_exposure alerts and HANDLING state
    // transitions are evaluated on the same sensor reading.
    // On a new transition: store the pending record first, then attempt the
    // send.  If a prior pending transition was not yet confirmed it is
    // overwritten — the newer transition supersedes it in the physical record.
    uint8_t prevShipState = gState.shipment_state;
    bool stateChanged = detectShipmentState(motion, motionOk, lux, now);
    if (stateChanged) {
        gState.pending_state_change = true;
        gState.pending_state_from   = prevShipState;
        gState.pending_state_to     = gState.shipment_state;
        gState.pending_state_epoch  = now;
        if (sendStateChange(prevShipState, gState.shipment_state, now)) {
            gState.pending_state_change = false;
        }
    }

    // ── Apply dynamic outbound cadence (DWELL extends; transit/handling restores)
    // Re-issues hub.set only when the required cadence differs from the last
    // applied value.  Alerts and state-change notes (sync:true) always bypass
    // the outbound queue and are unaffected by this cadence.
    applyDynamicOutbound();

    // ── Evaluate alerts ──────────────────────────────────────────────────────
    evaluateAlerts(temp_c, rh_pct, lux, motion, motionOk, currentOrientation, now);

    // ── Accumulate sample ────────────────────────────────────────────────────
    if (!skipThisCycle && gState.last_summary_epoch > 0) {
        accumulateSample(temp_c, rh_pct, lux, motion, motionOk);
    }

    // ── Per-sample tamper-evident log entry ──────────────────────────────────
    // Written every sample cycle (including anchor/cadence-change skips) for
    // a complete chain-of-custody record.  Gated on templates_registered:
    // cargo_log.db is compact-templated; a note sent before template
    // registration would be rejected by the Notecard.
    // motionOk is forwarded so sendLogEntry can set motion_valid correctly —
    // distinguishing "card.motion unavailable" from "no motion occurred."
    if (gState.templates_registered) {
        sendLogEntry(now, temp_c, rh_pct, lux, motion, motionOk, gState.shipment_state);
    }

    // ── Retry pending summary from a prior failed send ───────────────────────
    if (gState.pending_epoch > 0 && gState.templates_registered) {
        if (sendPendingSummary()) {
            gState.pending_epoch = 0;
        }
    }

    // ── Freeze completed summary window when interval has elapsed ────────────
    // During confirmed DWELL the effective interval is extended by
    // dwell_batch_factor to reduce NTN session frequency during warehouse stays.
    // effectiveSummaryMin is a local — not stored — so dwell/transit transitions
    // between wakes don't create false cadence-change resets.
    uint32_t effectiveSummaryMin = gSummaryMin;
    if (gState.shipment_state == SHIP_STATE_DWELL) {
        uint32_t extended = gSummaryMin * gDwellBatchFactor;
        effectiveSummaryMin = (extended > 1440U) ? 1440U : extended;
    }

    if (!skipThisCycle && gState.last_summary_epoch > 0 && gState.summary_n > 0 &&
            gState.templates_registered) {
        bool intervalElapsed = (now > 0) &&
            (now >= gState.last_summary_epoch) &&
            ((now - gState.last_summary_epoch) >= effectiveSummaryMin * 60UL);
        if (intervalElapsed) {
            if (gState.pending_epoch > 0) {
                Serial.println("[cargo] WARNING: stale pending summary discarded — "
                               "Notecard unreachable for full summary window");
                gState.pending_epoch = 0;
            }
            snapshotSummary(now);
            resetAccumulators();
            gState.last_summary_epoch = now;
            if (sendPendingSummary()) {
                gState.pending_epoch = 0;
            }
        }
    }

    // ── Persist state and cut host power until next sample ───────────────────
    NotePayloadDesc save = {0, 0, 0};
    NotePayloadAddSegment(&save, STATE_SEG, &gState, sizeof(gState));
    NotePayloadSaveAndSleep(&save, gSampleSec, NULL);

    // Reached only if card.attn did not cut host power.  State has already
    // been persisted.  Reset so setup() re-enters as a warm boot.
    Serial.println("[cargo] card.attn did not cut host power — issuing software reset");
    delay(100);
    NVIC_SystemReset();
}

// loop() is intentionally empty — all logic runs in setup() on each wake.
void loop() {}
