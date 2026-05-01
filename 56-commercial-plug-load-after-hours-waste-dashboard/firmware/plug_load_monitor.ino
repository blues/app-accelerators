// plug_load_monitor.ino
//
// Host:     Blues Notecarrier CX (onboard STM32L433 Cygnet host MCU)
// Notecard: Blues Notecard Cell+WiFi (MBGLW / NBGLW)
// Sensors:  Up to 4x SCT-013-030 split-core CT clamps (0–30 A → 0–1 V AC)
//           connected to analog inputs A0–A3 on the Notecarrier CX headers.
//
// Purpose:
//   Monitors per-circuit RMS current on sub-panel branch circuits, once per
//   minute, so an energy consultant can identify after-hours plug-load waste
//   without requiring any access to the building's corporate WiFi or LAN.
//
//   Each wake the firmware:
//     1. Reads RMS amps on up to 4 CT channels.
//     2. Accumulates per-circuit mean, peak, and active-minute stats.
//     3. Once per hour, queues a circuit_summary.qo note with per-circuit
//        stats for downstream load-profile classification.
//
//   The Notecard transmits queued notes on the hub.set outbound cadence.
//
// Cadence defaults (all overridable via Notehub environment variables):
//   - Sample every 60 s  (sample_interval_sec)
//   - Summary every 60 min  (report_interval_min)
//
// Optional extension — real-time after-hours alerts:
//   Define PLUG_LOAD_ALERTS in plug_load_monitor_helpers.h to enable the
//   circuit_alert.qo Notefile.  When enabled the firmware also calls
//   card.time each wake, evaluates business hours, and fires an immediate
//   sync:true alert note when a circuit exceeds the threshold outside of
//   business hours.  Disabled by default: the baseline build produces only
//   circuit_summary.qo with no card.time calls and no extra sessions.
//
// Files in this directory:
//   plug_load_monitor.ino          — this file (setup / loop)
//   plug_load_monitor_helpers.h    — shared declarations, feature flags
//   plug_load_monitor_helpers.cpp  — helper implementations

#include <Notecard.h>
#include "plug_load_monitor_helpers.h"
// PRODUCT_UID is defined in plug_load_monitor_helpers.h so it is visible to
// both this file and helpers.cpp.  Edit the define there, not here.

// ── Default runtime config (all overridable from Notehub env vars) ────────────
// Helpers in plug_load_monitor_helpers.cpp access these via the extern
// declarations in plug_load_monitor_helpers.h.
uint32_t CFG_SAMPLE_INTERVAL_SEC  = 60;    // seconds between wakes
uint32_t CFG_REPORT_INTERVAL_MIN  = 60;    // minutes between summary notes
uint8_t  CFG_CIRCUIT_COUNT        = 4;     // channels to read (1–4); matches documented default
float    CFG_IDLE_THRESHOLD_AMPS  = 0.50f; // below this = circuit is off
float    CFG_AFTER_HOURS_AMPS     = 2.0f;  // PLUG_LOAD_ALERTS: threshold above which alert fires
int8_t   CFG_BIZ_HOURS_START      = 8;     // local hour, 24-h, inclusive
int8_t   CFG_BIZ_HOURS_END        = 18;    // local hour, 24-h, exclusive
int8_t   CFG_TZ_OFFSET_HRS        = 0;     // hours offset from UTC
uint32_t CFG_ALERT_COOLDOWN_SEC   = 3600;  // PLUG_LOAD_ALERTS: min seconds between repeat alerts
float    CFG_CT_FULL_SCALE_AMPS   = CT_FULL_SCALE_DEFAULT;

// ── State preserved across sleep cycles via NotePayloadSaveAndSleep ──────────
const char STATE_SEG_ID[] = "PLUG";
AppState   state;

// Template-application confirmation for the current boot only.
// note.template is re-issued unconditionally on every wake (idempotent on an
// intact Notecard; auto-recovers after a factory reset or card replacement).
// Tracked per Notefile so a transient failure registering one template does
// not gate emission on the other.  Not persisted — a host-side boolean cannot
// reliably reflect Notecard state across a card reset or swap.
bool g_summary_template_applied = false;
#ifdef PLUG_LOAD_ALERTS
bool g_alert_template_applied   = false;
#endif

Notecard notecard;

// ── Arduino entry points ──────────────────────────────────────────────────────
//
// This sketch uses the "host-is-off-when-idle" pattern. On the Notecarrier CX
// the ATTN pin is wired to the host power gate: when the Notecard asserts ATTN
// after CFG_SAMPLE_INTERVAL_SEC seconds, the host rail comes up, setup() runs,
// does one sample cycle, and loop() immediately calls NotePayloadSaveAndSleep
// to serialize state into Notecard flash and cut the host rail again.
// The Notecard itself idles at ~8–18 µA between cellular sessions.

void setup() {
#ifdef PLUG_LOAD_DEBUG
    // Serial init is gated entirely on PLUG_LOAD_DEBUG.  In production builds
    // this block is compiled out entirely — the host never spends a single
    // millisecond waiting for a CDC connection before sampling and sleeping.
    dbgSerial.begin(115200);
    for (uint32_t t0 = millis(); !dbgSerial && (millis() - t0) < 3000; ) {}
#endif

    analogReadResolution(12);  // STM32L4: enable 12-bit ADC (default is 10-bit)
    notecard.begin();
#ifdef PLUG_LOAD_DEBUG
    notecard.setDebugOutputStream(dbgSerial);
#endif

    // ── First call on every wake: retry-protected I2C warm-up ─────────────────
    // The Notecard's I2C peripheral can take a few hundred milliseconds after a
    // power-on before it ACKs requests.  Sending the first request via
    // sendRequestWithRetry() absorbs that race on every wake — including the
    // first boot — without a hard delay.  card.version is read-only and
    // idempotent, so it is safe to issue unconditionally each wake.
    {
        J *req = notecard.newRequest("card.version");
        notecard.sendRequestWithRetry(req, 10);
    }

    // Attempt to restore state from the payload saved before the last sleep.
    NotePayloadDesc payload;
    bool restored = NotePayloadRetrieveAfterSleep(&payload);
    if (restored) {
        restored &= NotePayloadGetSegment(&payload, STATE_SEG_ID,
                                          &state, sizeof(state));
        NotePayloadFree(&payload);
    }
    if (!restored) {
        // First boot (or payload corrupt): zero-initialise so accumulators,
        // timestamps, and last_applied_outbound_min all start from known values.
        memset(&state, 0, sizeof(state));
    }

    if (!restored) {
        // ── First-boot sequencing ─────────────────────────────────────────────
        // Configure the Notecard before fetching env vars.  On a cold boot the
        // Notecard's local env cache is empty: it cannot receive pre-provisioned
        // Notehub values until after the first cellular sync.  Calling
        // hubConfigure() first establishes the product UID and inbound cadence
        // so the Notecard can begin that first sync promptly; fetchEnvOverrides()
        // on this same wake will return an empty body (cache not yet populated),
        // but that is safe because all CFG_* globals start at their compile-time
        // defaults (e.g. CFG_CIRCUIT_COUNT = 4) and the firmware samples all
        // configured channels immediately.
        hubConfigure();
        if (fetchEnvOverrides()) {
            // On a cold boot env.get typically returns an empty body (the
            // Notecard's local cache is empty until the first cellular sync).
            // Snapshot whatever was parsed so subsequent wakes do not fall back
            // to compile-time defaults on a transient I²C error.
            captureCfg(state.saved_cfg);
            state.cfg_valid = true;
        }
        if (CFG_REPORT_INTERVAL_MIN != state.last_applied_outbound_min) {
            // Env var delivered a different cadence; re-apply hub.set immediately
            // so the Notecard's outbound period matches the operator value from
            // the very first session.
            hubConfigure();
        }
    } else {
        // ── Normal-wake sequencing ────────────────────────────────────────────
        // Restore the last successfully fetched config before calling
        // fetchEnvOverrides() so that a transient I²C error does not silently
        // revert to compile-time defaults for this wake cycle.
        if (state.cfg_valid) {
            applyCfg(state.saved_cfg);
        }
        // Re-read env vars on every wake so operator changes take effect within
        // one inbound cycle.  Only replace saved_cfg on confirmed success so a
        // transient failure does not discard a previously valid configuration.
        if (fetchEnvOverrides()) {
            captureCfg(state.saved_cfg);
            state.cfg_valid = true;
        }
        if (CFG_REPORT_INTERVAL_MIN != state.last_applied_outbound_min) {
            hubConfigure();
        }
    }

    // ── Template application (unconditional, every boot) ─────────────────────
    // note.template is idempotent: re-issuing it on an intact Notecard is a
    // no-op, and re-issuing after a Notecard factory reset or card replacement
    // restores the fixed-schema binary encoding before any note.add calls reach
    // the Notecard.  defineTemplates() updates the per-Notefile g_*_template_applied
    // flags; sendSummary() and sendAlert() each gate on their own flag so a
    // failure on one template does not suppress the other Notefile.
    defineTemplates();

    if (!restored) {
        // First boot only: quiesce the onboard accelerometer so its interrupt
        // activity doesn't add noise to a Mojo power trace during bench
        // validation (see README §8).
        J *req = notecard.newRequest("card.motion.mode");
        JAddBoolToObject(req, "stop", true);
        notecard.sendRequest(req);
    }

    runSampleCycle();
    state.cycles++;
}

void loop() {
    // Serialize state to Notecard flash and sleep the host for the next
    // sample interval.  NotePayloadSaveAndSleep issues card.attn internally
    // and the Notecard cuts the host power rail.
    NotePayloadDesc payload = {0, 0, 0};
    NotePayloadAddSegment(&payload, STATE_SEG_ID, &state, sizeof(state));
    NotePayloadSaveAndSleep(&payload, CFG_SAMPLE_INTERVAL_SEC, NULL);

    // ── No-ATTN fallback path ─────────────────────────────────────────────────
    // NotePayloadSaveAndSleep returned instead of cutting the host rail, which
    // means ATTN is not gating host power (typical on USB-only bench setups
    // without the full Notecarrier CX power path active).  Drive the sample
    // cadence here so the device keeps accumulating and emitting data even
    // without the power gate.  loop() is re-entered by the Arduino runtime
    // after each iteration, re-attempting NotePayloadSaveAndSleep with the
    // freshly updated state.
    delay(CFG_SAMPLE_INTERVAL_SEC * 1000UL);

    // Re-read env vars each iteration, mirroring the per-wake behaviour on
    // real hardware.  Only replace saved_cfg on confirmed success.
    if (fetchEnvOverrides()) {
        captureCfg(state.saved_cfg);
        state.cfg_valid = true;
    }
    // Mirror the post-env-fetch outbound-cadence check from setup(): if
    // report_interval_min changed, re-apply hub.set so summary cadence and
    // sync cadence stay in sync on the bench path as well as the sleep/wake path.
    if (CFG_REPORT_INTERVAL_MIN != state.last_applied_outbound_min) {
        hubConfigure();
    }

    // On the bench path, note.template is only issued once during setup().
    // If a previous attempt failed (a template flag is still false), retry
    // here so a transient I²C failure at boot does not suppress emission for
    // the entire bench session.  defineTemplates() rewrites both flags from
    // the fresh send-request results, so calling it unconditionally when any
    // Notefile is unconfirmed is safe — note.template is idempotent.
    if (!g_summary_template_applied
#ifdef PLUG_LOAD_ALERTS
        || !g_alert_template_applied
#endif
       ) {
        defineTemplates();
    }

    runSampleCycle();
    state.cycles++;
}
