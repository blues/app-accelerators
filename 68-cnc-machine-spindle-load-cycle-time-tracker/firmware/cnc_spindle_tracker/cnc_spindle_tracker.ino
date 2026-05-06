// cnc_spindle_tracker.ino — CNC Machine Spindle Load & Cycle Time Tracker
//
// Reads CNC controller telemetry via Modbus TCP (OPTA Ethernet), accumulates
// hourly OEE-component statistics, and routes events to Notehub via Blues
// Wireless for OPTA (cellular).
//
// Hardware: Arduino OPTA RS485 + Blues Wireless for OPTA
// Blues docs: https://dev.blues.io
// Wireless for OPTA quickstart: https://dev.blues.io/quickstart/wireless-for-opta-quickstart/
//
// Dependencies (Arduino Library Manager):
//   Blues Wireless Notecard (note-arduino), ArduinoModbus, ArduinoRS485
//   Arduino Mbed OS Opta Boards (board package — includes Ethernet.h)

#include "cnc_spindle_tracker_helpers.h"

// Paste your Notehub ProductUID here: https://dev.blues.io/tools-and-sdks/samples/product-uid
#ifndef PRODUCT_UID
#define PRODUCT_UID ""
#pragma message "PRODUCT_UID is not defined. Set it to your Notehub ProductUID."
#endif

// Define DEBUG to enable Notecard wire-level logs on usbSerial.
// Leave undefined for production builds; logs expose internal API traffic
// and consume serial bandwidth that can disrupt timing-sensitive sketches.
// #define DEBUG

// ---------------------------------------------------------------------------
// Hardware objects
// ---------------------------------------------------------------------------
Notecard notecard;

// MAC note: safe for point-to-point deployments where a single OPTA and the
// CNC switch share a dedicated subnet. If multiple OPTAs are ever co-located
// on the same Ethernet segment, assign each unit a unique MAC by changing the
// last octet (0x68) to avoid duplicate-address collisions.
static byte            MAC_OPTA[] = { 0xDE, 0xAD, 0xBE, 0xEF, 0xFE, 0x68 };
// OPTA static IP on the private CNC subnet. Adjust to match the installation;
// see README §4 for network-configuration guidance.
static const IPAddress LOCAL_IP(192, 168, 250, 10);

// ---------------------------------------------------------------------------
// Globals accessible to cnc_spindle_tracker_helpers.cpp (extern in .h)
// ---------------------------------------------------------------------------
Config      cfg;
WindowStats g_window;
uint8_t     g_lastCycleState    = 0xFF;   // force-init on first sample
uint32_t    g_lastCycleStartMs  = 0;
bool        g_modbusConnected   = false;
uint32_t    g_envLastModTime    = 0;      // incremental env.get

// ---------------------------------------------------------------------------
// Globals local to this translation unit
// ---------------------------------------------------------------------------
static uint16_t g_lastAlarmCode      = 0;
static uint32_t g_lastSpindleAlertMs = 0;
// g_spindleAlertArmed starts false so the very first qualifying overload fires
// immediately rather than being blocked until millis() has counted past
// SPINDLE_ALERT_COOLDOWN_MS from epoch (which would suppress alerts for the
// first 30 minutes after every boot).
static bool     g_spindleAlertArmed  = false;

// Alarm-event ring buffer: replaces the old single-slot g_alarmPending so that
// multiple distinct alarm-code transitions observed during a comms outage are
// queued rather than overwritten. One event is drained per evaluateAlerts()
// call; the head entry stays in place until sendAlarm() succeeds. On overflow
// the oldest slot is silently evicted and logged to Serial.
static PendingAlarm g_alarmFifo[ALARM_FIFO_SIZE] = {};
static uint8_t      g_alarmFifoHead              = 0;
static uint8_t      g_alarmFifoTail              = 0;

// Cycle-count register baseline for per-window delta computation.
// On the first valid sample we record the baseline without adding a delta;
// the explicit `…Initialized` flag avoids any collision with a real register
// value of 0xFFFF (which a high-throughput controller will eventually reach
// at counter wrap — using a magic sentinel would silently drop the wrap delta).
// Persists across report windows — do not reset in resetWindow().
static bool         g_cycleCountInitialized      = false;
static uint16_t     g_lastCycleCount             = 0;

// Operator-ID change tracking. First sample establishes the baseline without
// emitting an event (no prior state to report as "previous"); the explicit
// flag means a real operator_id of 0xFFFF is not mistaken for "uninitialized".
// Persists across report windows.
static bool         g_operatorIdInitialized      = false;
static uint16_t     g_lastOperatorId             = 0;

static uint32_t g_lastSampleMs       = 0;
static uint32_t g_lastReportMs       = 0;

// ---------------------------------------------------------------------------
// setup
// ---------------------------------------------------------------------------
void setup() {
    usbSerial.begin(115200);
    const uint32_t t0 = millis();
    while (!usbSerial && (millis() - t0) < 3000) {}

    // Notecard wire-level debug logs: define DEBUG at compile time to enable.
#if defined(DEBUG) && !defined(NOTE_C_LOW_MEM)
    notecard.setDebugOutputStream(usbSerial);
#endif

    // Load compile-time defaults into config.
    cfg.sampleMs           = (uint32_t)DEFAULT_SAMPLE_MINUTES * 60000UL;
    cfg.reportMs           = (uint32_t)DEFAULT_REPORT_MINUTES  * 60000UL;
    cfg.modbusPort         = DEFAULT_MODBUS_PORT;
    cfg.modbusUnitId       = DEFAULT_MODBUS_UNIT_ID;
    cfg.regSpindleLoad     = DEFAULT_REG_SPINDLE_LOAD;
    cfg.spindleOverloadPct = DEFAULT_SPINDLE_OVERLOAD_PCT;
    cfg.expectedCycleSec   = DEFAULT_EXPECTED_CYCLE_SEC;

    // Bring up OPTA Ethernet with static IP (default subnet 255.255.255.0).
    // Allow 1 s for PHY auto-negotiation before attempting any connections.
    Ethernet.begin(MAC_OPTA, LOCAL_IP);
    delay(1000);
    usbSerial.print("[ETH] Local IP: ");
    usbSerial.println(Ethernet.localIP());

    // Initialize Notecard over I²C (Wireless for OPTA AUX connector).
    notecard.begin();

    // Configure hub — blocks until hub.set succeeds so the device never enters
    // the main loop in an unconfigured state. Then pull env-var overrides
    // (which may adjust cadence), then register templates.
    notecardConfigure(PRODUCT_UID);
    fetchEnvOverrides();
    defineTemplates();

    // Attempt initial Modbus TCP connection to CNC controller.
    modbusConnect();

    memset(&g_window, 0, sizeof(g_window));
    g_lastSampleMs = millis();
    g_lastReportMs = millis();

    usbSerial.println("[APP] CNC Spindle Tracker started.");
}

// ---------------------------------------------------------------------------
// loop
// ---------------------------------------------------------------------------
void loop() {
    const uint32_t now = millis();

    // Poll CNC holding registers on sample cadence.
    if (now - g_lastSampleMs >= cfg.sampleMs) {
        g_lastSampleMs = now;

        if (!g_modbusConnected) {
            modbusConnect();
        }

        Sample s;
        if (pollCnc(s)) {
            accumulateSample(s);
            evaluateAlerts(s);
        }
    }

    // Emit hourly summary Note and reset rolling window.
    if (now - g_lastReportMs >= cfg.reportMs) {
        g_lastReportMs = now;
        sendSummary();

        // Re-fetch env vars each report window; pick up threshold or cadence changes.
        fetchEnvOverrides();
    }

    // Yield to RTOS scheduler and Ethernet stack between deadlines.
    delay(50);
}

// ---------------------------------------------------------------------------
// Alarm FIFO helpers — static to this translation unit
// ---------------------------------------------------------------------------
static inline bool alarmFifoEmpty(void) {
    return g_alarmFifoHead == g_alarmFifoTail;
}
static inline bool alarmFifoFull(void) {
    return ((g_alarmFifoTail + 1u) % ALARM_FIFO_SIZE) == g_alarmFifoHead;
}
// Push an alarm event. Evicts the oldest slot on overflow so late alarms are
// never silently dropped without at least a Serial warning.
static void alarmFifoPush(const char *type, const Sample &s) {
    if (alarmFifoFull()) {
        g_alarmFifoHead = (g_alarmFifoHead + 1u) % ALARM_FIFO_SIZE;
        usbSerial.println("[ALARM] FIFO full — oldest event evicted.");
    }
    PendingAlarm &slot = g_alarmFifo[g_alarmFifoTail];
    strncpy(slot.alertType, type, sizeof(slot.alertType) - 1);
    slot.alertType[sizeof(slot.alertType) - 1] = '\0';
    slot.sample = s;
    g_alarmFifoTail = (g_alarmFifoTail + 1u) % ALARM_FIFO_SIZE;
}

// ---------------------------------------------------------------------------
// accumulateSample
// ---------------------------------------------------------------------------
static void accumulateSample(const Sample &s) {
    const uint32_t now     = millis();
    const bool     running = (s.cycleState == 1);

    g_window.validSamples++;
    g_window.operatorId = s.operatorId;

    if (running) {
        g_window.spindleSum      += s.spindleLoadPct;
        g_window.feedOverrideSum += s.feedOverridePct;
        if (s.spindleLoadPct > g_window.spindlePeak) {
            g_window.spindlePeak = s.spindleLoadPct;
        }
        g_window.runSamples++;
        g_window.runMinutes  += (cfg.sampleMs / 60000UL);
    } else {
        g_window.idleMinutes += (cfg.sampleMs / 60000UL);
    }

    // --- Controller-authoritative cycle count (primary for cycle_count field) ---
    // Accumulate a per-window delta directly from the cycleCount holding register.
    // Unsigned 16-bit subtraction handles counter wrap (65535→0) correctly;
    // a controller reset that drops the counter by more than 32767 cannot be
    // distinguished from a natural wrap and would inflate one window's delta —
    // an accepted corner case given the rarity of mid-session resets on CNC
    // controllers.
    if (!g_cycleCountInitialized) {
        // First valid sample: record the baseline without adding a delta yet.
        g_lastCycleCount        = s.cycleCount;
        g_cycleCountInitialized = true;
    } else {
        const uint16_t delta = (uint16_t)(s.cycleCount - g_lastCycleCount);
        g_lastCycleCount = s.cycleCount;
        g_window.windowCycleCountDelta += (uint32_t)delta;
    }

    // --- Edge-timing cycle tracking (heuristic — avg_cycle_sec only) ---
    // Detect cycle-start/end via cycleState transitions. This count is NOT
    // used as the authoritative cycle_count (the register delta above is
    // published for that); it exists solely to measure average cycle wall-clock
    // duration. Short cycles that complete within one sample interval are missed
    // here but are still captured by the register delta above.
    if (g_lastCycleState != s.cycleState) {
        if (!running && g_lastCycleState == 1) {
            // running → idle: a cycle just finished.
            if (g_lastCycleStartMs > 0) {
                g_window.totalCycleMs += (now - g_lastCycleStartMs);
                g_window.cyclesCompleted++;
            }
        } else if (running) {
            // idle → running: a new cycle just started.
            g_lastCycleStartMs = now;
        }
        g_lastCycleState = s.cycleState;
    }
}

// ---------------------------------------------------------------------------
// evaluateAlerts
// ---------------------------------------------------------------------------
static void evaluateAlerts(const Sample &s) {
    const uint32_t now     = millis();
    const bool     running = (s.cycleState == 1);

    // Drain the alarm FIFO: attempt one delivery per poll to clear the backlog
    // without stalling the Modbus read loop with back-to-back I²C transactions.
    // The head entry stays put until sendAlarm() succeeds.
    if (!alarmFifoEmpty()) {
        const PendingAlarm &front = g_alarmFifo[g_alarmFifoHead];
        if (sendAlarm(front.alertType, front.sample)) {
            g_alarmFifoHead = (g_alarmFifoHead + 1u) % ALARM_FIFO_SIZE;
        }
    }

    // Spindle overload: only meaningful while actively cutting.
    // The cooldown timestamp is always advanced before the send so that a
    // sustained overload during a comms outage adds at most one entry to the
    // FIFO per cooldown window rather than flooding it.
    // g_spindleAlertArmed bypasses the timestamp check on the very first
    // qualifying overload, avoiding a 30-minute blind spot after boot.
    if (running &&
        s.spindleLoadPct > cfg.spindleOverloadPct &&
        (!g_spindleAlertArmed || (now - g_lastSpindleAlertMs) >= SPINDLE_ALERT_COOLDOWN_MS))
    {
        g_spindleAlertArmed  = true;
        g_lastSpindleAlertMs = now;   // advance first — prevents FIFO flood
        if (!sendAlarm("spindle_overload", s)) {
            alarmFifoPush("spindle_overload", s);
        }
    }

    // CNC alarm: fire on any transition to a nonzero value (0 → nonzero, or
    // one nonzero code changing to another). Avoids paging every sample for
    // the duration of a latched alarm.
    //
    // State tracking (g_lastAlarmCode, g_window.alarmCount) is updated
    // immediately — independent of whether the Note queues — so a comm outage
    // cannot cause the summary to undercount real alarm transitions. Failed
    // deliveries are buffered in the FIFO and retried on subsequent polls.
    if (s.alarmCode != g_lastAlarmCode) {
        if (s.alarmCode != 0) {
            g_window.alarmCount++;
            g_lastAlarmCode = s.alarmCode;
            if (!sendAlarm("cnc_alarm", s)) {
                alarmFifoPush("cnc_alarm", s);
            }
        } else {
            // Alarm cleared — advance tracker; no alert to send.
            g_lastAlarmCode = s.alarmCode;
        }
    }

    // Operator-ID change: emit a cnc_operator.qo event on any login/logout
    // transition (operator_id == 0 conventionally means no operator logged in).
    // g_lastOperatorId is updated immediately so future changes are always
    // detected regardless of whether the event Note is delivered. Events are
    // best-effort and sampled at the poll interval: a comms outage can drop a
    // transition Note, and any change that occurs and reverts between two
    // consecutive polls is invisible to the firmware. The hourly summary
    // snapshot captures only the most recently observed operator ID at window
    // close — not a complete record of all transitions within the window.
    if (!g_operatorIdInitialized) {
        // First observation: record baseline without emitting an event
        // (there is no prior state to report as "previous ID").
        g_lastOperatorId        = s.operatorId;
        g_operatorIdInitialized = true;
    } else if (s.operatorId != g_lastOperatorId) {
        const uint16_t prevId = g_lastOperatorId;
        g_lastOperatorId = s.operatorId;
        sendOperatorChange(prevId, s.operatorId);
    }
}
