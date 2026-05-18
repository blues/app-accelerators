/*******************************************************************************
  trailer_sensors.cpp — interrupt-backed UART drain, door-edge ISR, sensor
  reads, and summary-window accumulation for the Blues Connected Trailer
  Platform.

  All functions operate on globals declared extern in trailer_sensors.h and
  defined in connected_trailer_platform.ino.

  Power model: the host is powered off between sample cycles via the Notecard's
  ATTN signal. drainReeferUart() and drainTpmsUart() are called during the
  WAKE_UART_DRAIN_MS window at the start of each wakeup and between blocking
  Notecard I2C calls within the sample cycle. Bytes that arrive while the host
  is off are lost; the drain window and inter-call drains capture bytes arriving
  during the host-on period.

  J2497 state:
    drainReeferUart() sets g_ps.j2497Commissioned = true on the first accepted
    frame. Until that flag is set, reefer_sensor_loss is not fired, so a build
    with no J2497 modem produces zero spurious loss alerts.

  TPMS pressure persistence:
    drainTpmsUart() updates both g_sensors.tpmsPsi[] and g_ps.tpmsPsiLast[].
    setup() restores tpmsPsiLast[] into g_sensors.tpmsPsi[] on each wakeup so
    sendSummary() can report the correct last-known pressure for positions that
    did not transmit during the current drain window.
*******************************************************************************/
#include "trailer_sensors.h"
#include <math.h>   // logf

// =========================================================================
// Door-edge interrupt state
// =========================================================================

// Volatile flags written by doorISR() (interrupt context) and read in setup().
// g_doorIsrFired is set on any CHANGE edge on PIN_DOOR; g_doorIsrState captures
// the pin level at interrupt time so setup() can resolve rapid toggles to the
// final state without another digitalRead() race.
volatile bool g_doorIsrFired = false;
volatile bool g_doorIsrState = false;

static void doorISR() {
    g_doorIsrState = (digitalRead(PIN_DOOR) == LOW); // LOW = door open (N.C. reed)
    g_doorIsrFired = true;
}

// Attach the hardware interrupt on PIN_DOOR. Called once in setup() after
// pinMode(). Attaching on CHANGE detects both open and close transitions; the
// ISR latches the final pin level so rapid bounce sequences within a sample
// interval resolve to the last stable state rather than whichever edge the
// sample-time read would have caught.
void setupDoorInterrupt() {
    attachInterrupt(digitalPinToInterrupt(PIN_DOOR), doorISR, CHANGE);
}

// =========================================================================
// J2497 PLC modem UART drain
// =========================================================================

// Drain the J2497 reefer UART and latch the most recent valid frame.
// Called on every loop() iteration to keep the 64-byte STM32 hardware ring
// buffer clear at 9600 baud. All complete frames in the buffer are parsed;
// only the most recent valid frame is kept. Older frames (and frames with
// bad checksums) are discarded so the sample cycle always reads the freshest
// available reefer data.
//
// Frame format (8 bytes): [0xAA][0x55][setHi][setLo][actHi][actLo][status][xorCk]
// Checksum: XOR of bytes [2]–[6] must equal byte [7].
void drainReeferUart() {
    while (Serial1.available() >= (int)REEFER_PKT_LEN) {
        if ((uint8_t)Serial1.read() != REEFER_HDR_A) continue;
        if (Serial1.available() < (int)(REEFER_PKT_LEN - 1)) break;
        if ((uint8_t)Serial1.peek() != REEFER_HDR_B) continue;
        Serial1.read();  // consume HDR_B

        uint8_t p[REEFER_PKT_LEN - 2];
        for (int i = 0; i < (int)sizeof(p); i++) p[i] = (uint8_t)Serial1.read();

        uint8_t ck = 0;
        for (int i = 0; i < 5; i++) ck ^= p[i];
        if (ck != p[5]) continue;   // bad checksum — discard frame

        g_sensors.reeferSetF    = (int16_t)((uint16_t)(p[0] << 8) | p[1]) / 10.0f;
        g_sensors.reeferActualF = (int16_t)((uint16_t)(p[2] << 8) | p[3]) / 10.0f;
        // Persist frame-received state so a frame arriving late in a wake
        // (after updateReeferMissCount() has already run) is not lost across
        // the host-off sleep — updateReeferMissCount() on the next wake reads
        // this flag and correctly resets g_ps.reeferMissCount.
        g_ps.reeferFrameSeen   = true;
        // Arm the reefer_sensor_loss alert path. Until this flag is set the
        // alert is suppressed so a bench build with no J2497 modem connected
        // never fires spurious loss alerts.
        g_ps.j2497Commissioned = true;
    }
}

// =========================================================================
// TPMS receiver UART drain
// =========================================================================

// Drain the TPMS SoftwareSerial and update per-position pressure readings.
// Called on every loop() iteration to collect packets as they arrive (typically
// one per tire every 60–180 s). Valid frames reset the stale counter for that
// position; the stale counter is incremented once per summary window in
// sendSummary() so positions that stop reporting are aged out to the −9999
// sentinel after TPMS_STALE_COUNT consecutive missed windows.
//
// Frame format (6 bytes): [0xCC][posID][pressHi][pressLo][tempC][xorCk]
// Checksum: XOR of posID, pressHi, pressLo must equal xorCk.
void drainTpmsUart() {
    while (tpmsSerial.available() >= TPMS_FRAME_LEN) {
        if ((uint8_t)tpmsSerial.read() != 0xCCu) continue;
        uint8_t pos = (uint8_t)tpmsSerial.read();
        uint8_t ph  = (uint8_t)tpmsSerial.read();
        uint8_t pl  = (uint8_t)tpmsSerial.read();
        uint8_t tC  = (uint8_t)tpmsSerial.read(); (void)tC;
        uint8_t ck  = (uint8_t)tpmsSerial.read();
        if (((uint8_t)(pos ^ ph ^ pl)) != ck) continue;  // bad checksum
        if (pos >= NUM_TPMS_POS) continue;                 // out-of-range position
        g_sensors.tpmsPsi[pos]       = (uint16_t)((ph << 8) | pl) / 10.0f;
        // Persist the pressure alongside the live sensor value so it survives
        // the next host-off sleep interval. setup() restores tpmsPsiLast[] into
        // g_sensors.tpmsPsi[] on wakeup for positions that do not report a fresh
        // frame in the drain window.
        g_ps.tpmsPsiLast[pos]        = g_sensors.tpmsPsi[pos];
        g_ps.tpmsStaleCounts[pos]    = 0;     // keep at 0 for alert freshness check
        g_ps.tpmsSeenThisWindow[pos] = true;  // mark seen for per-window age accounting
    }
}

// =========================================================================
// Reefer miss-count management
// =========================================================================

// Assess whether a valid reefer frame arrived this wake cycle. Uses the
// persisted g_ps.reeferFrameSeen flag so that a frame received at any point
// during the wake — including late drains after this function has already
// run — is credited on whichever call to updateReeferMissCount() sees the
// flag set. Invalidates stored readings after REEFER_MISS_MAX consecutive
// missed wakes so summaries emit −9999 rather than a stale last-good value.
// Called once per wake in runSampleCycle(), after the pre-evaluateAlerts()
// UART drains and before evaluateAlerts().
void updateReeferMissCount() {
    if (g_ps.reeferFrameSeen) {
        g_ps.reeferMissCount = 0;
        g_ps.reeferFrameSeen = false;  // consume; may be re-set by later drains
    } else {
        if (++g_ps.reeferMissCount >= REEFER_MISS_MAX) {
            g_sensors.reeferSetF    = -9999.0f;
            g_sensors.reeferActualF = -9999.0f;
        }
    }
}

// =========================================================================
// Thermistor reader
// =========================================================================

// Convert a 12-bit ADC count from a thermistor voltage-divider wiper to °F
// using the Steinhart-Hart β approximation. Returns −9999 if the reading is
// railed (short-circuit or open-circuit fault).
static float adcToF(int raw) {
    if (raw <= 0 || raw >= (int)ADC_MAX) return -9999.0f;
    float r  = THERM_SERIES_OHM * (ADC_MAX / (float)raw - 1.0f);
    float tK = 1.0f / (1.0f / (THERM_NOM_C + 273.15f)
                        + logf(r / THERM_NOM_OHM) / THERM_BETA);
    return (tK - 273.15f) * 9.0f / 5.0f + 32.0f;
}

// Read both cargo-air thermistors into g_sensors. A 16-sample average
// suppresses ADC noise and refrigeration discharge-air spikes.
void readThermistors() {
    long s1 = 0, s2 = 0;
    for (int i = 0; i < 16; i++) {
        s1 += analogRead(PIN_THERM_1);
        s2 += analogRead(PIN_THERM_2);
    }
    g_sensors.airT1F = adcToF((int)(s1 / 16));
    g_sensors.airT2F = adcToF((int)(s2 / 16));
}

// =========================================================================
// Summary-window accumulation
// =========================================================================

// Accumulate temperature and door statistics for the current summary window.
//
// Door transitions and open-duration are measured at sample-cycle granularity
// (sampleIntervalSec). A closed→open or open→closed transition is detected
// only when the two consecutive sample readings differ, so an event that
// begins and ends fully within one sample interval is not counted. Open
// duration is computed from Notecard epoch deltas between the sample cycle
// that first observes an open and the one that first observes the close (or
// the summary boundary for a door still open at window end).
//
// Must be called after all sensor reads are complete for the sample cycle.
void accumulateSampleStats(uint32_t nowEpoch) {
    // Temperature accumulators — TempAccum::add() rejects −9999 sentinels.
    g_ps.reeferAccum.add(g_sensors.reeferActualF);
    if (g_sensors.reeferSetF > -1000.0f) g_ps.reeferSetLast = g_sensors.reeferSetF;
    g_ps.airT1Accum.add(g_sensors.airT1F);
    g_ps.airT2Accum.add(g_sensors.airT2F);

    bool nowOpen = g_sensors.doorOpen;
    if (nowOpen && !g_ps.prevDoorOpen) {
        // Closed → open transition: stamp open-start epoch and count the event.
        // Only stamp if epoch is valid; a zero epoch would corrupt subsequent
        // duration calculations. The event is still counted regardless.
        if (nowEpoch != 0) g_ps.doorOpenSinceEpoch = nowEpoch;
        g_ps.doorEventCount++;
    } else if (!nowOpen && g_ps.prevDoorOpen) {
        // Open → closed transition: credit elapsed open time.
        // Guard against nowEpoch==0 (no time sync) and doorOpenSinceEpoch==0
        // (door opened before time sync, so no valid start stamp).
        if (nowEpoch != 0 && g_ps.doorOpenSinceEpoch != 0) {
            uint32_t openSec = nowEpoch - g_ps.doorOpenSinceEpoch;
            g_ps.doorOpenMinAccum += openSec / 60.0f;
        }
        g_ps.doorOpenSinceEpoch = 0;
    }
    // Door remains open: duration will be credited at send time (sendSummary).
    // Door remains closed: nothing to add.

    g_ps.prevDoorOpen = nowOpen;
    g_ps.sampleCount++;
}
