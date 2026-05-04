/*
 * activity_hub.ino — Ambient Home Well-Being & Activity Hub
 *
 * Monitors daily-life patterns for patients aging in place or recently
 * discharged: PIR motion, door contacts, bathroom humidity, and a
 * vibration-based bed-presence proxy. Sends hourly summaries to Notehub
 * and immediate alerts when expected patterns are missing — e.g., no
 * morning activity, repeated nighttime bathroom trips, or no bed
 * vibration during expected sleep hours.
 *
 * Hardware:
 *   Blues Notecarrier CX (Cygnet STM32L4 host)
 *   Blues Notecard Cell+WiFi (NOTE-MBGLW)
 *   Adafruit Mini PIR Motion Sensor 4871 on D6
 *   Adafruit Magnetic Contact Switch 375 on D9 (HIGH = door open, INPUT_PULLUP)
 *   Adafruit SHT31-D Humidity/Temp Breakout 2857 on I2C (0x44)
 *   SparkFun Piezo Vibration Sensor SEN-09197 on A0 (via voltage divider)
 *   Blues Mojo (bench power validation, inline on +VBAT)
 *
 * Power model: host MCU stays in STM32 Stop mode (deep sleep, SRAM retained)
 * between 5-minute sensor cycles. PIR and door-contact edges are captured by
 * EXTI interrupt service routines (ISRs) during Stop mode, so short-duration
 * events are never missed regardless of when they occur relative to the
 * scheduled sample cycle. Application state lives in a static SRAM global —
 * retained across Stop mode wakes, reset on power-on.
 *
 * User configuration (PRODUCT_UID, DEBUG_SERIAL, CALIBRATION_MODE) is in
 * app_state.h. All Notecard helpers live in notecard_helpers.h/.cpp and all
 * sensor/note helpers in sensor_alert.h/.cpp.
 */

#include <Notecard.h>
#include <Wire.h>
#include <Adafruit_SHT31.h>
#include <STM32LowPower.h>
#include "notecard_helpers.h"
#include "sensor_alert.h"

Notecard       notecard;
Adafruit_SHT31 sht31;

// ---------------------------------------------------------------------------
// Application state — kept in SRAM across Stop mode wakes.
// Initialised once in setup() on power-on; retained while the MCU is powered.
// ---------------------------------------------------------------------------
static AppState state;

// ISR event counters. Incremented in EXTI ISR context during Stop mode;
// copied and cleared atomically at the start of each sensor cycle. volatile
// because they are written outside normal program flow.
// Using counters (not boolean latches) ensures every rising edge is tallied
// separately — multiple events within one sleep interval each contribute to
// the accumulated total rather than collapsing to a single boolean.
static volatile uint8_t  g_pir_count        = 0;
static volatile uint8_t  g_door_count       = 0;
static volatile uint32_t g_door_last_isr_ms = 0;  // debounce timestamp for door ISR

// millis() timestamp of the last completed sensor cycle. Used in loop() to
// schedule the next cycle and to compute the remaining Stop mode sleep duration.
// millis() is compensated for time-in-sleep by the STM32LowPower library.
static uint32_t g_last_cycle_ms = 0;

// ---------------------------------------------------------------------------
// EXTI interrupt service routines — fired on PIR / door rising edges
// ---------------------------------------------------------------------------
static void pirISR() {
    // Saturating increment so counter overflow cannot wrap back to zero and
    // silently discard accumulated events.
    if (g_pir_count < 255) g_pir_count++;
}

static void doorISR() {
    // 50 ms software debounce prevents reed-switch contact bounce from
    // inflating the count on a single door-open event.
    // millis() is valid here: the STM32 SysTick re-enables before EXTI ISRs
    // fire when waking from Stop mode via LowPower.attachInterruptWakeup().
    uint32_t t = millis();
    if ((t - g_door_last_isr_ms) >= 50UL) {
        if (g_door_count < 255) g_door_count++;
        g_door_last_isr_ms = t;
    }
}

// ---------------------------------------------------------------------------
// Forward declaration
// ---------------------------------------------------------------------------
static void runCycle();

// ===========================================================================
// setup() — one-time hardware and Notecard initialisation on power-on.
//            NOT called on Stop mode wakes (SRAM is retained; execution
//            resumes in loop() from the point where deepSleep() was called).
// ===========================================================================
void setup() {
#ifdef DEBUG_SERIAL
    Serial.begin(115200);
#endif

    // Fix ADC resolution at 12 bits so bed_threshold tuning, comments, and
    // the 0–4095 count range are all consistent on STM32 cores.
    analogReadResolution(12);

    Wire.begin();
    notecard.begin();
#ifdef DEBUG_SERIAL
    notecard.setDebugOutputStream(Serial);
#endif

    pinMode(PIN_PIR,  INPUT);
    pinMode(PIN_DOOR, INPUT_PULLUP);  // HIGH = door open (magnet away)

    // -----------------------------------------------------------------------
    // Cold-initialise application state.
    // setup() runs only on power-on or hard reset; SRAM content from any
    // previous Stop mode session is gone. Sentinel values for humidity/
    // temperature distinguish "never read" from a legitimate near-zero sample.
    // -----------------------------------------------------------------------
    memset(&state, 0, sizeof(state));
    state.humidity_last                = -9999.0f;
    state.temp_last                    = -9999.0f;
    state.applied_summary_interval_min = DEFAULT_SUMMARY_INTERVAL_MIN;
    // Seed door state from the current GPIO level so the first edge detection
    // in runCycle() does not fire a false LOW→HIGH transition when the door
    // was already open at power-on.
    state.last_door_state              = (digitalRead(PIN_DOOR) == HIGH);

    // -----------------------------------------------------------------------
    // Register EXTI wakeup sources BEFORE the first deepSleep() call.
    //
    // RISING on PIR  — signal goes HIGH on motion (~2 s hold by AM312 sensor).
    // RISING on DOOR — LOW-to-HIGH = door just opened (pull-up released).
    //
    // The ISRs increment event counters; the next runCycle() call copies and
    // clears them atomically, so every event that occurs during sleep is
    // counted even if it completes before the next scheduled cycle.
    // -----------------------------------------------------------------------
    LowPower.begin();
    LowPower.attachInterruptWakeup(PIN_PIR,  pirISR,  RISING);
    LowPower.attachInterruptWakeup(PIN_DOOR, doorISR, RISING);

    // -----------------------------------------------------------------------
    // Probe Notecard I2C readiness. On a cold power-on the Notecard may need
    // a moment to initialise its I2C peripheral; this loop retries up to 5×
    // at 500 ms intervals before proceeding with whatever state is available.
    // -----------------------------------------------------------------------
    for (int probe = 0; probe < 5; probe++) {
        J *rsp = notecard.requestAndResponse(notecard.newRequest("card.version"));
        if (rsp) {
            bool ready = !notecard.responseError(rsp);
            notecard.deleteResponse(rsp);
            if (ready) break;
        }
        delay(500);
    }

    // Schedule the first sensor cycle to run immediately on the first loop().
    g_last_cycle_ms = millis() - (uint32_t)DEFAULT_SAMPLE_INTERVAL_SEC * 1000UL;
}

// ===========================================================================
// loop() — called repeatedly; sleeps the MCU in Stop mode between cycles.
//
// On each wake (from RTC alarm OR from a PIR/door EXTI interrupt):
//   1. Check whether the sample interval has elapsed since the last cycle.
//   2. If yes: run the full sensor-read / rule-evaluation / transmit cycle.
//   3. Compute the remaining time until the next cycle and re-enter Stop mode.
//
// When an EXTI wakes the MCU before the interval expires, g_pir_count or
// g_door_count is incremented. The loop goes back to sleep for the residual
// period, and the counter is consumed by the next scheduled runCycle() call.
// ===========================================================================
void loop() {
    uint32_t now_ms      = millis();
    uint32_t interval_ms = (uint32_t)g_sample_interval_sec * 1000UL;
    uint32_t elapsed_ms  = now_ms - g_last_cycle_ms;  // unsigned; wraps safely

    if (elapsed_ms >= interval_ms) {
        g_last_cycle_ms = now_ms;
        runCycle();
        // Re-read interval: fetchEnvOverrides() inside runCycle() may have
        // updated g_sample_interval_sec via a Notehub environment variable.
        interval_ms = (uint32_t)g_sample_interval_sec * 1000UL;
    }

    // Sleep for the remaining time in the current sample interval.
    // LowPower.deepSleep() enters STM32 Stop mode; SRAM is retained and EXTI
    // wakeup sources remain active. The STM32LowPower library compensates
    // millis() for time elapsed during sleep, so g_last_cycle_ms timing
    // remains accurate across Stop mode wakes.
    uint32_t spent_ms    = millis() - g_last_cycle_ms;
    uint32_t remaining_ms = (spent_ms < interval_ms) ? (interval_ms - spent_ms) : 0;
    if (remaining_ms > 0) {
        LowPower.deepSleep(remaining_ms);
    }
}

// ===========================================================================
// runCycle() — one complete sensor-read / rule-evaluation / transmit cycle.
//              Called from loop() on every scheduled 5-minute interval.
// ===========================================================================
static void runCycle() {
    // -----------------------------------------------------------------------
    // Idempotent Notecard configuration — retried on every power-on wake cycle
    // until the Notecard confirms success. hub_configured is tied to
    // hub_product_hash so a reflash with a new ProductUID forces hub.set to
    // re-run even if hub_configured is already true.
    // -----------------------------------------------------------------------
    {
        uint32_t curHash = productUidHash();
        if (!state.hub_configured || curHash != state.hub_product_hash) {
            if (hubConfigure()) {
                state.hub_configured   = true;
                state.hub_product_hash = curHash;
            }
        }
    }
    if (!state.templates_defined) state.templates_defined = defineTemplates();
    if (!state.motion_stopped)   state.motion_stopped   = motionStop();

    // SHT31 driver must be re-initialised each cycle. In Stop mode the I2C
    // peripheral resumes on wake, but calling begin() issues a sensor soft-
    // reset and confirms bus continuity before the humidity read.
    bool sht_ok = sht31.begin(0x44);
#ifdef DEBUG_SERIAL
    if (!sht_ok) Serial.println("SHT31 init failed -- humidity readings skipped");
#endif

    // fetchEnvOverrides() resets globals to compile-time defaults before
    // applying Notehub overrides, so removing a variable in Notehub correctly
    // reverts the setting rather than leaving the stale value active.
    fetchEnvOverrides(state);

    // Current time from Notecard (UTC epoch). Zero before first cellular sync.
    uint32_t now = notecardTime();

    // Seed the summary window start on first boot.
    if (state.summary_window_start == 0 && now > 0) {
        state.summary_window_start = now;
    }

    // Compute local time once; all time-of-day comparisons share this value.
    uint8_t hour = localHour(now);

    // Local calendar day index used by both day-boundary reset guards below.
    int32_t  local_s = (now > 0)
                       ? (int32_t)now + (int32_t)((int32_t)g_utc_offset_hours * 3600)
                       : 0;
    uint8_t today    = (now > 0) ? (uint8_t)((uint32_t)local_s / 86400UL & 0xFF)
                                 : state.last_reset_day;

    // -----------------------------------------------------------------------
    // First-sync guard — on the first cycle where the Notecard delivers valid
    // UTC time, clear morning state so activity silently discarded during the
    // pre-sync period cannot produce a false no_morning_activity alert.
    // -----------------------------------------------------------------------
    if (now > 0 && !state.time_initialized) {
        state.time_initialized = true;
        state.morning_activity = false;
        state.morning_alerted  = false;
        state.last_reset_day   = today;
        if (hour >= (uint8_t)g_morning_end_hour) {
            state.morning_alerted = true;
        }
    }

    // -----------------------------------------------------------------------
    // Morning reset — fires on the first cycle at or after morning_start_hour
    // each day. The last_reset_day guard prevents multiple firings when the
    // hub wakes more than once near the boundary.
    // -----------------------------------------------------------------------
    if (now > 0 && hour >= (uint8_t)g_morning_start_hour && today != state.last_reset_day) {
        state.morning_activity = false;
        state.morning_alerted  = false;
        state.last_reset_day   = today;
    }

    // -----------------------------------------------------------------------
    // Sleep-window reset — fires on the first cycle at or after sleep_end_hour
    // each day. Separated from the morning reset so operators can set
    // sleep_end_hour != morning_start_hour without breaking counter resets.
    // -----------------------------------------------------------------------
    if (now > 0 && hour >= (uint8_t)g_sleep_end_hour
                && today != state.last_night_reset_day) {
        state.night_bathroom_count = 0;
        state.night_bath_alerted   = false;
        state.sleep_quiet_samples  = 0;
        state.bed_empty_alerted    = false;
        state.last_night_reset_day = today;
    }

    // -----------------------------------------------------------------------
    // Sensor reads
    // -----------------------------------------------------------------------

    // Copy-and-clear ISR event counters atomically at the start of the cycle.
    // The critical section (noInterrupts/interrupts) prevents an ISR that fires
    // between the read and the clear from being silently discarded.
    noInterrupts();
    uint8_t pir_events  = g_pir_count;
    uint8_t door_events = g_door_count;
    g_pir_count  = 0;
    g_door_count = 0;
    interrupts();

    // 1. PIR — g_pir_count was incremented by pirISR() for every RISING edge
    //    captured during Stop mode sleep. If no ISR fired but the pin is
    //    currently HIGH, the PIR went active between setup() and the first
    //    deepSleep() call (before any RISING edge existed to capture); record
    //    it as one event so early-cycle motion is never silently dropped.
    //    Multiple events within one interval each contribute to pir_count.
    if (digitalRead(PIN_PIR) == HIGH && pir_events == 0) {
        pir_events = 1;
    }
    if (pir_events > 0) {
        uint16_t headroom = 0xFFFFU - state.pir_count;
        state.pir_count += (pir_events < headroom) ? pir_events : headroom;
        if (now > 0 && inWindow(hour, (uint8_t)g_morning_start_hour,
                                       (uint8_t)g_morning_end_hour)) {
            state.morning_activity = true;
        }
    }

    // 2. Door contact — g_door_count was incremented by doorISR() (with 50 ms
    //    debounce) for every RISING edge during Stop mode sleep. A bathroom
    //    trip that begins and ends entirely within a sleep interval is still
    //    counted because the rising edge was captured even if the door is
    //    already closed by sample time. Multiple openings in one interval each
    //    contribute to door_count and night_bathroom_count.
    //    If no ISR fired but a level transition is detected now (door is open
    //    and was last sampled as closed), add one event to catch a transition
    //    that occurred after deepSleep() returned but before digitalRead().
    bool door_now_open = (digitalRead(PIN_DOOR) == HIGH);
    if (door_now_open && !state.last_door_state && door_events == 0) {
        door_events = 1;
    }
    if (door_events > 0) {
        uint16_t headroom = 0xFFFFU - state.door_count;
        state.door_count += (door_events < headroom) ? door_events : headroom;
        if (now > 0 && inWindow(hour, (uint8_t)g_morning_start_hour,
                                       (uint8_t)g_morning_end_hour)) {
            state.morning_activity = true;
        }
        if (now > 0 && inWindow(hour, (uint8_t)g_sleep_start_hour,
                                       (uint8_t)g_sleep_end_hour)) {
            uint8_t night_space = 255 - state.night_bathroom_count;
            uint8_t to_add = (door_events < night_space) ? door_events : night_space;
            state.night_bathroom_count += to_add;
        }
    }
    state.last_door_state = door_now_open;

    // 3. Bathroom humidity / temperature
    float tempC = 0.0f, humPct = 0.0f;
    if (sht_ok && readHumidity(tempC, humPct)) {
        state.temp_last     = tempC;
        state.humidity_last = humPct;
        if (state.humidity_baseline < 1.0f) {
            state.humidity_baseline = humPct;
        } else {
            state.humidity_baseline =
                (1.0f - DEFAULT_HUMIDITY_BASELINE_ALPHA) * state.humidity_baseline
                + DEFAULT_HUMIDITY_BASELINE_ALPHA * humPct;
        }
    }

    // 4. Bed vibration — peak-to-peak amplitude of piezo signal over 500 ms.
    //    sleep_quiet_samples counts consecutive quiet reads only while the
    //    sleep window is active, preventing a daytime bed-empty period from
    //    pre-arming the counter before sleep begins.
    state.bed_samples++;
    bool bed_motion = readBedMotion();
    if (bed_motion) {
        state.bed_motion_samples++;
        // Vibration clears the quiet counter and rearms Rule C so a brief
        // wake-up followed by a genuine extended absence still fires.
        state.sleep_quiet_samples = 0;
        state.bed_empty_alerted   = false;
    } else if (now > 0 && inWindow(hour, (uint8_t)g_sleep_start_hour,
                                          (uint8_t)g_sleep_end_hour)) {
        if (state.sleep_quiet_samples < 255) state.sleep_quiet_samples++;
    }

    // -----------------------------------------------------------------------
    // Anomaly detection — all rules skipped until Notecard has valid UTC time
    // (now > 0) because localHour(0) == 0 falls inside the default 22–06
    // sleep window and would fire false alerts on every pre-sync cycle.
    // Alert state (latches, cooldown timestamps) is updated ONLY when
    // sendAlert() returns true; a transient I2C or Notecard failure leaves
    // the rule armed so it retries on the next cycle.
    // -----------------------------------------------------------------------
    if (now > 0) {
        // Ceil division: ensures quiet_minutes_for_alert is a true minimum.
        int quiet_s = g_quiet_minutes_for_alert * 60;
        int bed_empty_min_samples =
            (quiet_s + g_sample_interval_sec - 1) / g_sample_interval_sec;
        if (bed_empty_min_samples < 1) bed_empty_min_samples = 1;

        // Rule A: No morning activity
        if (hour >= (uint8_t)g_morning_end_hour &&
            !state.morning_activity &&
            !state.morning_alerted  &&
            cooldownExpired(state, ALERT_IDX_NO_MORNING, now)) {
            if (sendAlert("no_morning_activity",
                          "No motion or door activity detected during morning window")) {
                state.last_alert_time[ALERT_IDX_NO_MORNING] = now;
                state.morning_alerted = true;
            }
        }

        // Rule B: Repeated nighttime bathroom trips
        if (((int)state.night_bathroom_count >= g_night_bathroom_limit ||
             state.night_bath_pending) &&
            !state.night_bath_alerted &&
            cooldownExpired(state, ALERT_IDX_NIGHT_BATH, now)) {
            // First attempt only: snapshot the count so a delayed retry that
            // crosses the sleep-end reset still reports the value that
            // originally triggered the rule, not the post-reset zero.
            if (!state.night_bath_pending) {
                state.night_bath_pending_count = state.night_bathroom_count;
            }
            char detail[48];
            snprintf(detail, sizeof(detail), "night_trips=%u limit=%d",
                     state.night_bath_pending_count, g_night_bathroom_limit);
            if (sendAlert("night_bathroom_pattern", detail)) {
                state.last_alert_time[ALERT_IDX_NIGHT_BATH] = now;
                state.night_bath_alerted = true;
                state.night_bath_pending = false;
            } else {
                state.night_bath_pending = true;
            }
        }

        // Rule C: No bed vibration during sleep window
        bool bed_empty_condition =
            inWindow(hour, (uint8_t)g_sleep_start_hour, (uint8_t)g_sleep_end_hour) &&
            state.sleep_quiet_samples >= (uint8_t)bed_empty_min_samples;
        if ((bed_empty_condition || state.bed_empty_pending) &&
            !state.bed_empty_alerted &&
            cooldownExpired(state, ALERT_IDX_BED_EMPTY, now)) {
            char detail[80];
            snprintf(detail, sizeof(detail),
                     "No bed vibration for %d+ consecutive minutes during sleep window",
                     g_quiet_minutes_for_alert);
            if (sendAlert("no_bed_motion_during_sleep", detail)) {
                state.last_alert_time[ALERT_IDX_BED_EMPTY] = now;
                state.bed_empty_alerted = true;
                state.bed_empty_pending = false;
            } else {
                state.bed_empty_pending = true;
            }
        }
    }

    // -----------------------------------------------------------------------
    // Hourly summary — window counters are only reset after sendSummary()
    // confirms the Note was accepted. If queueing fails, the window stays
    // open and the next cycle retries, preventing silent data loss.
    // -----------------------------------------------------------------------
    uint32_t elapsed_min = (now > state.summary_window_start)
                           ? (now - state.summary_window_start) / 60 : 0;
    if (elapsed_min >= (uint32_t)g_summary_interval_min) {
        if (sendSummary(state)) {
            state.summary_window_start = now;
            state.pir_count            = 0;
            state.door_count           = 0;
            state.bed_samples          = 0;
            state.bed_motion_samples   = 0;
            // morning_activity and night_bathroom_count reset on daily cycle above
        }
    }
}

// loop() contains all per-cycle logic; nothing runs here after loop() returns.
