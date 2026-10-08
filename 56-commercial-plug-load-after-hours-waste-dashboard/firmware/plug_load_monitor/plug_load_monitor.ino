// plug_load_monitor.ino
//
// Host:     Blues Notecarrier CX (onboard STM32L433 host MCU)
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
// Power strategy:
//   Between samples the host sleeps in STM32 STOP2 and is woken by the
//   Notecard's ATTN pin (card.attn "sleep"; see cx_sleep.h).  RAM is retained,
//   so application state simply lives in memory.
//   Wiring: jumper the Notecarrier CX ATTN pin to D5 (same 16-pin header).
//   Leave EN unconnected — on the CX it enables the shared 3.3 V VIO rail,
//   so ATTN -> EN would brown out the whole board.
//   Build:  Tools > USB support (if available) > None (usb=none).  With the
//   USB CDC stack enabled the USB wakeup interrupt exits STOP2 immediately.
//   Debug output (PLUG_LOAD_DEBUG) goes to the LPUART on the CX debug jack,
//   which an ST-LINK V3 exposes as a virtual COM port.
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
//   cx_sleep.h                     — host STOP2 sleep / ATTN wake

#include <Notecard.h>
#include "plug_load_monitor_helpers.h"
#include "cx_sleep.h"
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

// ── Application state — lives in RAM across STOP2 sleep cycles ───────────────
AppState   state;

// Template-application confirmation.
// note.template is re-issued unconditionally on every wake (idempotent on an
// intact Notecard; auto-recovers after a factory reset or card replacement).
// Tracked per Notefile so a transient failure registering one template does
// not gate emission on the other.  A host-side boolean cannot reliably reflect
// Notecard state across a card reset or swap, hence the per-wake re-issue.
bool g_summary_template_applied = false;
#ifdef PLUG_LOAD_ALERTS
bool g_alert_template_applied   = false;
#endif

Notecard notecard;
#ifdef PLUG_LOAD_DEBUG
Uart     dbgSerial(PIN_VCP_RX, PIN_VCP_TX);
#endif

// ── Arduino entry points ──────────────────────────────────────────────────────
//
// setup() runs once at power-up: it configures the Notecard and arms the ATTN
// wake.  loop() performs one sample cycle and then asks the Notecard to hold
// ATTN low for CFG_SAMPLE_INTERVAL_SEC seconds while the host sleeps in STOP2.
// The ATTN rising edge on D5 wakes the host and execution resumes in place, so
// per-wake work (env.get, template re-issue, hub.set re-apply) lives in loop().
// The Notecard itself idles at ~8–18 µA between cellular sessions.

void setup() {
#ifdef PLUG_LOAD_DEBUG
    // Debug output is gated entirely on PLUG_LOAD_DEBUG.  In production builds
    // this block is compiled out entirely.
    dbgSerial.begin(115200);
#endif

    analogReadResolution(12);  // STM32L4: enable 12-bit ADC (default is 10-bit)
    notecard.begin();
#ifdef PLUG_LOAD_DEBUG
    notecard.setDebugOutputStream(dbgSerial);
#endif

    // Zero-initialise so accumulators, timestamps, and
    // last_applied_outbound_min all start from known values.
    memset(&state, 0, sizeof(state));

    // ── First-boot sequencing ─────────────────────────────────────────────────
    // Configure the Notecard before fetching env vars.  On a cold boot the
    // Notecard's local env cache is empty: it cannot receive pre-provisioned
    // Notehub values until after the first cellular sync.  Calling
    // hubConfigure() first establishes the product UID and inbound cadence
    // so the Notecard can begin that first sync promptly; fetchEnvOverrides()
    // on the first pass through loop() will typically return an empty body
    // (cache not yet populated), but that is safe because all CFG_* globals
    // start at their compile-time defaults (e.g. CFG_CIRCUIT_COUNT = 4) and
    // the firmware samples all configured channels immediately.
    hubConfigure();

    // Quiesce the onboard accelerometer so its interrupt activity doesn't add
    // noise to a Mojo power trace during bench validation (see README §8).
    {
        J *req = notecard.newRequest("card.motion.mode");
        JAddBoolToObject(req, "stop", true);
        notecard.sendRequest(req);
    }

    // Wake from STOP2 on the ATTN rising edge.
    cxSleepBegin();
}

void loop() {
    // ── Per-wake sequencing ───────────────────────────────────────────────────
    // Re-read env vars on every wake so operator changes take effect within
    // one inbound cycle.  The CFG_* globals keep their last values in RAM, so
    // a transient I²C error leaves the previous configuration active; only
    // replace saved_cfg on confirmed success.
    if (fetchEnvOverrides()) {
        captureCfg(state.saved_cfg);
        state.cfg_valid = true;
    }
    if (CFG_REPORT_INTERVAL_MIN != state.last_applied_outbound_min) {
        // Env var delivered a different cadence; re-apply hub.set so the
        // Notecard's outbound period matches the operator value.
        hubConfigure();
    }

    // ── Template application (unconditional, every wake) ─────────────────────
    // note.template is idempotent: re-issuing it on an intact Notecard is a
    // no-op, and re-issuing after a Notecard factory reset or card replacement
    // restores the fixed-schema binary encoding before any note.add calls reach
    // the Notecard.  defineTemplates() updates the per-Notefile g_*_template_applied
    // flags; sendSummary() and sendAlert() each gate on their own flag so a
    // failure on one template does not suppress the other Notefile.
    defineTemplates();

    runSampleCycle();
    state.cycles++;

    // ── Sleep until the Notecard raises ATTN ─────────────────────────────────
#ifdef PLUG_LOAD_DEBUG
    Stream *log = &dbgSerial;
#else
    Stream *log = NULL;
#endif
    if (!cxSleepUntilAttn(notecard, CFG_SAMPLE_INTERVAL_SEC, NULL, log)) {
        // The Notecard didn't take the sleep request, or ATTN never went low
        // (check the ATTN -> D5 jumper).  Keep the sample cadence and try again.
#ifdef PLUG_LOAD_DEBUG
        dbgSerial.println("[sleep] ATTN sleep failed; waiting out the interval awake");
#endif
        delay(CFG_SAMPLE_INTERVAL_SEC * 1000UL);
    }
}
