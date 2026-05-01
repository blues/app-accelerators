/*******************************************************************************
  trailer_sensors.h — Sensor types, window-state layout, and sensor-helper
  prototypes for the Blues Connected Trailer Platform.

  Included by both connected_trailer_platform.ino and trailer_sensors.cpp.
  Keeping constants, structs, and enums here eliminates duplication and lets
  the Arduino build system compile the two translation units independently.

  Power model: the host (Cygnet STM32L433 on Notecarrier CX) is powered off
  between sample cycles via the Notecard's card.attn ATTN signal. setup() runs
  on every wakeup; loop() serialises PersistState via NotePayloadSaveAndSleep()
  and hands control back to the Notecard until the next scheduled wake.
  PersistState carries all inter-sample context (accumulators, alert cooldowns,
  door state, TPMS last-known pressures, summary window epoch) across the
  host-off sleep interval.
*******************************************************************************/
#pragma once
#include <Arduino.h>
#include <SoftwareSerial.h>

// ---- Product UID -------------------------------------------------------
#ifndef PRODUCT_UID
#define PRODUCT_UID ""  // "com.your-company.your-name:connected_trailer"
#pragma message "PRODUCT_UID is not defined. Set it to your Notehub project UID."
#endif

// ---- Sleep state segment identifier ------------------------------------
// Identifies the PersistState segment inside the NotePayload stored by the
// Notecard between wakes. Must be unique within this firmware image. Four
// ASCII characters, matching the pattern used by other Blues reference apps.
#define STATE_SEG_ID "TRLR"

// ---- Post-wakeup UART drain window (ms) --------------------------------
// After hardware re-initialisation each wake, both UART channels are drained
// for this many milliseconds before the sample cycle begins. At 9600 baud an
// 8-byte J2497 frame takes ~8 ms; 250 ms accommodates ~3 complete frames and
// several TPMS packets if a gateway is transmitting near the wake boundary.
#define WAKE_UART_DRAIN_MS  250u

// ---- Pin assignments (Notecarrier CX dual 16-pin header) ---------------
#define PIN_THERM_1   A0   // front cargo-air thermistor divider wiper
#define PIN_THERM_2   A1   // rear cargo-air thermistor divider wiper
#define PIN_DOOR      D9   // N.C. reed switch; INPUT_PULLUP — LOW = door open
#define PIN_TPMS_RX   D6   // SoftwareSerial RX ← TPMS receiver TX
#define PIN_TPMS_TX   D5   // SoftwareSerial TX → TPMS receiver RX

// ---- Thermistor parameters ---------------------------------------------
#define THERM_SERIES_OHM  10000.0f  // divider series resistor (Ω)
#define THERM_NOM_OHM     10000.0f  // NTC nominal resistance at 25 °C
#define THERM_NOM_C       25.0f     // nominal temperature (°C)
#define THERM_BETA        3950.0f   // beta coefficient (K)
#define ADC_MAX           4095.0f   // 12-bit STM32 ADC

// ---- J2497 PLC modem UART (hardware Serial1) ---------------------------
// Simplified POC frame: [0xAA][0x55][setHi][setLo][actHi][actLo][status][xorCk]
// Temperatures: signed 16-bit, 0.1 °F LSB.
//
// drainReeferUart() is called during the WAKE_UART_DRAIN_MS window after each
// wakeup and between blocking Notecard I2C calls within the sample cycle. In
// the sleep architecture the host is powered off between samples; frames that
// arrive while the host is off are lost (the UART peripheral has no power).
// At wakeup the drain window catches frames arriving in the first 250 ms;
// subsequent drains within the sample cycle recover any bytes arriving during
// Notecard I2C transactions.
//
// The reefer_sensor_loss alert is gated by g_ps.j2497Commissioned: it fires
// only once the firmware has received at least one valid J2497 frame, so a
// bench build with no modem connected produces zero spurious loss alerts.
// Frame reception within a wake is tracked by g_ps.reeferFrameSeen (persisted
// in PersistState) rather than an ephemeral RAM boolean, so a frame arriving
// late in a wake (during evaluateAlerts() drains or the final loop() drain)
// is credited correctly on the next wake's updateReeferMissCount() call.
//
// Production requires a full J2497 application-layer stack, reefer-OEM message
// mapping, and field validation. See README §9 Limitations.
#define REEFER_BAUD      9600
#define REEFER_HDR_A     0xAAu
#define REEFER_HDR_B     0x55u
#define REEFER_PKT_LEN   8        // bytes per frame (including 2-byte header)
#define REEFER_MISS_MAX  3        // consecutive wake cycles with no frame → alert

// ---- TPMS receiver (SoftwareSerial) ------------------------------------
// Simplified POC frame: [0xCC][posID][pressHi][pressLo][tempC][xorCk]
// Pressure: unsigned 16-bit, 0.1 PSI LSB. posID 0–3.
//
// drainTpmsUart() is called during the WAKE_UART_DRAIN_MS window and between
// Notecard I2C calls within each sample cycle. SoftwareSerial requires the CPU
// to be active (bit-banging); it cannot receive while the host is powered off
// during the sleep interval. Frames arriving between wakes are lost. The drain
// window catches frames that happen to arrive within the first 250 ms after
// wakeup; subsequent drains within the sample cycle capture any late arrivals.
//
// The most recent valid pressure per position is latched in g_sensors.tpmsPsi[]
// AND persisted in g_ps.tpmsPsiLast[] so the correct last-known value is
// available at sendSummary() time even if no frame arrived in the current wake.
// Positions that stop reporting are aged out to −9999 after TPMS_STALE_COUNT
// consecutive summary windows without data.
// Production firmware must replace with the vendor's decode library.
#define TPMS_BAUD         9600
#define NUM_TPMS_POS      4
#define TPMS_FRAME_LEN    6
#define TPMS_STALE_COUNT  2     // summary windows without data → pressure sentinel

// ---- Notefile names and template port ----------------------------------
#define NOTE_SUMMARY   "trailer_summary.qo"
#define NOTE_ALERT     "trailer_alert.qo"
#define TEMPLATE_PORT  50

// ---- Compile-time defaults (all overridable via Notehub env vars) ------
#define DEF_REEFER_MAX_F         40.0f
#define DEF_REEFER_MIN_F         28.0f
#define DEF_TPMS_MIN_PSI         95.0f
#define DEF_TPMS_MAX_PSI        130.0f
#define DEF_DOOR_OPEN_SEC        300u
#define DEF_SAMPLE_SEC           300u
#define DEF_SUMMARY_MIN           60u
#define DEF_OUTBOUND_TRANSIT_MIN  60u
#define DEF_OUTBOUND_PARKED_MIN  240u
#define DEF_ALERT_COOLDOWN_SEC  1800u

// ---- Trailer state machine ---------------------------------------------
enum TrailerState : uint8_t {
    STATE_PARKED = 0, STATE_LOADING = 1, STATE_IN_TRANSIT = 2
};

// ---- Alert indices -----------------------------------------------------
enum AlertIdx : uint8_t {
    A_REEFER_HIGH = 0, A_REEFER_LOW,
    A_TPMS_LOW,        A_TPMS_HIGH,
    A_DOOR_TRANSIT,    A_REEFER_LOSS,
    ALERT_COUNT
};

// =========================================================================
// Runtime configuration — reset to compiled defaults at the start of each
// fetchEnvOverrides() call, then env-var overrides are applied on top.
// Not persisted; the reset+apply pattern ensures removed env vars revert
// to their compiled defaults without an MCU power cycle.
// =========================================================================
struct Config {
    float    reeferMaxF         = DEF_REEFER_MAX_F;
    float    reeferMinF         = DEF_REEFER_MIN_F;
    float    tpmsMinPsi         = DEF_TPMS_MIN_PSI;
    float    tpmsMaxPsi         = DEF_TPMS_MAX_PSI;
    uint32_t doorOpenTransitSec = DEF_DOOR_OPEN_SEC;
    uint32_t sampleIntervalSec  = DEF_SAMPLE_SEC;
    uint32_t summaryIntervalMin = DEF_SUMMARY_MIN;
    uint32_t outboundTransitMin = DEF_OUTBOUND_TRANSIT_MIN;
    uint32_t outboundParkedMin  = DEF_OUTBOUND_PARKED_MIN;
    uint32_t alertCooldownSec   = DEF_ALERT_COOLDOWN_SEC;
};

// =========================================================================
// Current-cycle sensor snapshot — populated once per sample cycle by
// drainReeferUart() (via continuous loop() drain), readThermistors(), and
// door-state resolution in runSampleCycle(). tpmsPsi[] is updated
// continuously by drainTpmsUart() and reset to the −9999 sentinel by
// sendSummary() when a position ages past TPMS_STALE_COUNT windows.
// =========================================================================
struct Sensors {
    float reeferSetF            = -9999.0f;
    float reeferActualF         = -9999.0f;
    float airT1F                = -9999.0f;
    float airT2F                = -9999.0f;
    float tpmsPsi[NUM_TPMS_POS] = {-9999.0f, -9999.0f, -9999.0f, -9999.0f};
    bool  doorOpen              = false;
};

// =========================================================================
// Temperature window accumulator — lives in PersistState so window
// statistics span multiple sample cycles within a summary window.
// No constructor: callers must call reset() to initialise after memset.
// =========================================================================
struct TempAccum {
    float    vmin, vmax, vsum;
    uint16_t n;

    void reset() { vmin = 9999.0f; vmax = -9999.0f; vsum = 0.0f; n = 0; }

    void add(float v) {
        if (v < -1000.0f) return;   // reject sentinel / invalid reading
        if (v < vmin) vmin = v;
        if (v > vmax) vmax = v;
        vsum += v;
        ++n;
    }

    float winMin()  const { return n ? vmin            : -9999.0f; }
    float winMax()  const { return n ? vmax            : -9999.0f; }
    float winMean() const { return n ? vsum / (float)n : -9999.0f; }
};

// =========================================================================
// Window state — shared across sample cycles within a summary window.
//
// Timing strategy: door-event epoch fields (doorOpenSinceEpoch,
// doorOpenTransitStartEpoch, lastAlertEpoch[]) use Notecard epoch seconds
// for accurate multi-cycle duration measurements when time is synced.
// The inter-sample interval, summary-window boundary, alert cooldown fallback,
// and door-transit start fallback all use millis() and are kept as statics
// in connected_trailer_platform.ino (g_lastAlertMs[], g_doorOpenTransitStartMs)
// so epoch is never required for cadence or alert correctness within a single
// wake session. Cross-wake timing uses summaryWindowStartEpoch and
// doorOpenTransitStartEpoch (epoch-based fields in PersistState).
//
// Initialisation: setup() calls memset then explicitly resets non-zero
// fields (TempAccum instances, reeferSetLast).
// =========================================================================
struct PersistState {
    // ---- Trailer state machine -----------------------------------------
    TrailerState trailerState;
    uint32_t     currentOutboundMin; // outbound cadence last confirmed by Notecard

    // ---- GPS delta for motion detection --------------------------------
    float        lastLat;
    float        lastLon;

    // ---- Door transit-alert timer (epoch; 0 = not being timed) --------
    // Set when the door first opens during transit and epoch is available.
    // The millis()-based g_doorOpenTransitStartMs (connected_trailer_platform.ino)
    // is the fallback before time sync. Both are cleared when the door closes
    // or the trailer leaves transit; alert fires when elapsed > doorOpenTransitSec.
    uint32_t     doorOpenTransitStartEpoch;

    // ---- Door open-duration tracking (epoch of most recent open edge) --
    // Stamped at the sample cycle that first observes a closed→open transition;
    // 0 means the door is currently closed or no valid epoch was available
    // at open time. Duration is computed from epoch deltas at close time and
    // at sendSummary() for a door still open at window boundary.
    uint32_t     doorOpenSinceEpoch;

    // ---- Reefer sensor health ------------------------------------------
    uint8_t      reeferMissCount;

    // ---- TPMS stale counters and per-window seen flags -----------------
    // tpmsStaleCounts: consecutive summary windows with no fresh frame for
    // that position. Reset to 0 by drainTpmsUart() on receipt of a valid
    // frame; advanced by sendSummary() only for positions NOT seen in the
    // window being closed. Positions that reach TPMS_STALE_COUNT are
    // invalidated to −9999 and skipped by evaluateAlerts().
    // tpmsSeenThisWindow: set true by drainTpmsUart() when a fresh frame
    // arrives; cleared by sendSummary() after the note is accepted. Positions
    // seen in the current window report age=0 and do not advance the stale
    // counter, preventing the off-by-one that aged fresh data to age=1.
    uint8_t      tpmsStaleCounts[NUM_TPMS_POS];
    bool         tpmsSeenThisWindow[NUM_TPMS_POS];

    // ---- Summary-window temperature accumulators -----------------------
    TempAccum    reeferAccum;
    TempAccum    airT1Accum;
    TempAccum    airT2Accum;
    float        reeferSetLast;     // last valid reefer setpoint seen

    // ---- Summary-window door accumulators ------------------------------
    // Door open-duration and event count are sampled at sampleIntervalSec
    // granularity: only state transitions observed at sample-cycle boundaries
    // are counted. An open/close event fully within one interval is missed;
    // open-duration measures sample-time epoch deltas, not exact edge times.
    float        doorOpenMinAccum;
    uint16_t     doorEventCount;    // distinct open events observed in the window
    bool         prevDoorOpen;      // door state at end of last sample cycle
    uint16_t     sampleCount;       // sample cycles completed in current window

    // ---- Alert cooldowns (epoch; 0 = never sent → first fire allowed) --
    // When nowEpoch == 0, the millis()-based g_lastAlertMs[] array in
    // connected_trailer_platform.ino provides cooldown enforcement so alerts
    // are not silently dropped before the Notecard acquires a time lock.
    uint32_t     lastAlertEpoch[ALERT_COUNT];

    // ---- J2497 commissioning gate ------------------------------------------
    // Set to true the first time drainReeferUart() accepts a valid frame. Until
    // true, reefer_sensor_loss is suppressed so a bench build with no J2497
    // modem connected does not produce a continuous stream of loss alerts.
    // Persisted across wakes so the gate survives the sleep interval.
    bool         j2497Commissioned;

    // ---- Reefer frame freshness flag (persisted across host-off sleep) ------
    // Set by drainReeferUart() whenever a valid J2497 frame is accepted during
    // any drain call in the current wake — including drains that occur after
    // updateReeferMissCount() has already run (e.g., inside evaluateAlerts()
    // or the final loop() drain). Because this flag lives in PersistState it
    // survives the host-off sleep interval, so a frame received late in one
    // wake is correctly credited when updateReeferMissCount() runs on the next
    // wake. updateReeferMissCount() consumes (clears) the flag after evaluation;
    // it may be re-set by a subsequent drain within the same wake.
    bool         reeferFrameSeen;

    // ---- Epoch-based summary window ----------------------------------------
    // Epoch (seconds) when the current summary window opened. Persisted across
    // wakes so the window boundary survives host-off sleep intervals. Seeded on
    // the first wake that returns a valid Notecard epoch; summary fires when
    // nowEpoch - summaryWindowStartEpoch >= summaryIntervalMin * 60. A
    // sample-count fallback is used when epoch is unavailable (pre-time-sync).
    uint32_t     summaryWindowStartEpoch;

    // ---- Persisted TPMS last-known pressures -------------------------------
    // g_sensors.tpmsPsi[] is freshly initialised to −9999 on each wakeup (the
    // global is re-constructed after host power-on). tpmsPsiLast[] carries the
    // most recent valid pressure per position across the sleep interval so that
    // sendSummary() can report the correct value for positions that did not send
    // a fresh frame in the current wake's drain window.
    // Updated by drainTpmsUart() in parallel with g_sensors.tpmsPsi[].
    float        tpmsPsiLast[NUM_TPMS_POS];
};

// =========================================================================
// Globals defined in connected_trailer_platform.ino
// =========================================================================
extern Config         g_cfg;
extern Sensors        g_sensors;
extern PersistState   g_ps;
extern SoftwareSerial tpmsSerial;
// g_reeferFrameReceived removed: reefer frame freshness is now tracked by
// the persisted g_ps.reeferFrameSeen field (see PersistState above).

// Door-edge ISR state (defined in trailer_sensors.cpp)
// g_doorIsrFired: set in the ISR on any PIN_DOOR edge; cleared after each read.
// g_doorIsrState: the pin state captured in the ISR (true = door open).
extern volatile bool  g_doorIsrFired;
extern volatile bool  g_doorIsrState;

// =========================================================================
// Sensor helper function prototypes (implemented in trailer_sensors.cpp)
// =========================================================================

// Attach the hardware interrupt on PIN_DOOR. Call once in setup().
void setupDoorInterrupt();

// Drain the J2497 reefer UART and latch the most recent valid frame.
// Call on every loop() iteration to prevent hardware-buffer overflow.
// Sets g_ps.reeferFrameSeen and g_ps.j2497Commissioned when a valid frame
// is parsed.
void drainReeferUart();

// Drain the TPMS SoftwareSerial and update g_sensors.tpmsPsi[] / stale counts.
// Call on every loop() iteration to collect packets as they arrive.
void drainTpmsUart();

// Assess reefer miss count based on g_ps.reeferFrameSeen; invalidate stale
// readings when the miss count reaches REEFER_MISS_MAX. Call once per wake
// cycle, after the pre-evaluateAlerts() UART drains in runSampleCycle(). Any
// valid frame received after this call (during evaluateAlerts() internal drains
// or the final loop() drain) sets g_ps.reeferFrameSeen and is credited on the
// next wake's call to updateReeferMissCount().
void updateReeferMissCount();

// Read both cargo-air thermistors (16-sample average) into g_sensors.
void readThermistors();

// Accumulate temperature and door statistics for the current summary window.
// nowEpoch: Notecard epoch at the start of this sample cycle. Door-open
// duration is computed from epoch deltas; transitions are detected at
// sample-cycle granularity (sampleIntervalSec).
void accumulateSampleStats(uint32_t nowEpoch);
