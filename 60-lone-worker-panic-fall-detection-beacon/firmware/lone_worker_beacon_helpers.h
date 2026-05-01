/*
 * lone_worker_beacon_helpers.h — shared constants, extern declarations, and
 * function prototypes for lone_worker_beacon_helpers.cpp.
 *
 * Include this header from both lone_worker_beacon.ino and
 * lone_worker_beacon_helpers.cpp so all translation units share the same
 * constant definitions and type-safe extern declarations.
 */
#pragma once

// ── Product UID ───────────────────────────────────────────────────────────
// Defined here (not in the .ino) so the macro is visible to every translation
// unit that includes this header, including lone_worker_beacon_helpers.cpp.
// In a standard Arduino multi-file build the .ino is compiled as a separate
// TU; macros defined there are NOT visible inside .cpp files.
#ifndef PRODUCT_UID
#define PRODUCT_UID ""
#pragma message "PRODUCT_UID is not set. Set it before deploying to Notehub."
#endif

#include <Arduino.h>
#include <Notecard.h>
#include <SparkFunLIS3DH.h>
#include <Adafruit_DRV2605.h>
#include <math.h>
#include <string.h>

// ── Debug output ──────────────────────────────────────────────────────────
// Uncomment DEBUG_SERIAL to enable serial tracing in all sketch files.
// Leaving it defined adds a 3-second USB-enumeration wait at boot and
// continuous Serial traffic — both increase average current on a wearable.
// #define DEBUG_SERIAL Serial
#ifdef DEBUG_SERIAL
#  define DEBUG_PRINT(...)   DEBUG_SERIAL.print(__VA_ARGS__)
#  define DEBUG_PRINTLN(...) DEBUG_SERIAL.println(__VA_ARGS__)
#else
#  define DEBUG_PRINT(...)
#  define DEBUG_PRINTLN(...)
#endif

// ── GPIO ──────────────────────────────────────────────────────────────────
#define PANIC_BUTTON_PIN  9   // D9 → one button leg → GND (INPUT_PULLUP)

// ── Panic button debounce ─────────────────────────────────────────────────
#define DEBOUNCE_MS  30UL

// ── Fall-detection defaults ───────────────────────────────────────────────
#define DEFAULT_FREEFALL_G       0.55f
#define DEFAULT_IMPACT_G         2.5f
#define DEFAULT_FALL_WINDOW_MS   500UL
#define DEFAULT_FREEFALL_MIN_MS  80UL

// ── Behavior defaults ─────────────────────────────────────────────────────
#define DEFAULT_PANIC_HOLD_MS      2000UL
#define DEFAULT_ALERT_COOLDOWN_SEC 60UL
#define DEFAULT_GPS_TIMEOUT_SEC    90UL

// ── Environment-variable refresh interval ────────────────────────────────
// env.get is called once at boot and then every ENV_FETCH_INTERVAL_MS (2 h)
// so remotely-changed thresholds propagate to a device that has not sent an
// alert. The Notecard's inbound: 120 setting in hub.set keeps the Notecard's
// local env-var cache in sync with Notehub on the same 2-hour cadence.
#define ENV_FETCH_INTERVAL_MS  (120UL * 60000UL)   // 2 hours in ms

// ── Non-blocking alert retry queue ───────────────────────────────────────
// A fixed-depth circular queue replaces the previous single-slot state machine.
// Each entry carries its own event-time context (type + GPS-cache epoch +
// event_id) so a second event that fires before the first retry succeeds is
// enqueued separately rather than overwriting the earlier entry — preventing
// silent alert drops on a safety device under transient Notecard/I²C failures.
// ALERT_QUEUE_DEPTH > max concurrent pending types (fall pending retry = 1
// realistic maximum; 4 gives a comfortable safety margin).
#define ALERT_QUEUE_DEPTH        4
#define ALERT_RETRY_MAX          3
#define ALERT_RETRY_INTERVAL_MS  500UL

// Per-alert context stored in each queue slot.
// cacheEpoch is the GPS-fix epoch captured at event time; it is used as the
// freshness baseline for pollGpsSearch() (not for loc_age_s computation).
// locAgeS is the fix age in seconds computed once at event-fire time and
// preserved unchanged through every retry, so the reported freshness always
// reflects the original alert moment — not the retry time.
// event_id is the monotonic counter value assigned at alert-fire time;
// beacon_location.qo carries the same value so downstream systems can pair
// the two notes using (device, event_id) as the join key. event_id is
// device-local and resets on power cycle; it is not unique across devices.
struct AlertQueueEntry {
    char     type[16];       // alert type string ("fall", "panic")
    uint32_t cacheEpoch;     // card.location "time" epoch at event time
    uint32_t event_id;       // monotonic event counter for note correlation
    float    locAgeS;        // age of cached fix at event-fire time (−1.0 = unknown)
    uint8_t  retryCount;     // attempts made so far
    uint32_t lastAttemptMs;  // millis() at last attempt; pollAlertRetry() waits
                             // until (now - lastAttemptMs) >= ALERT_RETRY_INTERVAL_MS.
                             // Elapsed-time comparison is wraparound-safe across
                             // the 49.7-day millis() rollover; an absolute
                             // deadline would underflow at the rollover and
                             // either fire too early or too late.
};

// ── Worker ID ─────────────────────────────────────────────────────────────
// Hard maximum for worker_id (chars, excluding null terminator). A fixed-size
// char buffer prevents oversized identifiers from bloating Starnote compact
// packets. Values longer than WORKER_ID_MAX are silently truncated in
// fetchEnvVars(). Document this limit in the Notehub env-var README section.
#define WORKER_ID_MAX  24

// ── Accelerometer sampling and runtime health ─────────────────────────────
// ACCEL_SAMPLES_PER_LOOP inner samples run at ACCEL_SAMPLE_MS spacing inside
// each outer loop iteration, yielding ~100 Hz effective fall-detection rate.
// This ensures that a free-fall phase as short as DEFAULT_FREEFALL_MIN_MS
// (80 ms) is reliably observed regardless of the outer loop's ~10 Hz cadence.
#define ACCEL_SAMPLES_PER_LOOP  10     // inner samples per outer iteration
#define ACCEL_SAMPLE_MS         10     // ms between inner samples (LIS3DH at 100 Hz ODR)

// Runtime health: if totalG is near-zero (all-axis zero = I2C fault) or
// implausibly high (above the ±4 g full-scale range plus headroom), the read
// is considered bad. ACCEL_FAIL_THRESHOLD consecutive bad reads trigger a
// reinitialization attempt; after ACCEL_REINIT_MAX failed reinits the fault
// is latched and fall detection is disabled permanently until power-cycle.
#define ACCEL_FAIL_THRESHOLD    5      // consecutive bad reads before reinit
#define ACCEL_REINIT_MAX        3      // reinit attempts before permanent fault
#define ACCEL_PLAUSIBLE_G_MAX   10.0f  // g — above this is an implausible read
// Interval between persistent single-buzz fault alerts emitted by loop() when
// the accelerometer fault latches post-boot (runtime degradation to panic-only
// mode). Boot-time accel failure halts in setup() and never reaches loop().
#define ACCEL_FAULT_BUZZ_INTERVAL_MS  30000UL   // 30 s

// ── Haptic effect IDs (DRV2605 ROM library 1, ERM open-loop) ──────────────
#define HAPTIC_CLICK    1    // strong click
#define HAPTIC_BUZZ     14   // strong buzz
#define HAPTIC_STARTUP  58   // transition ramp

// ── Haptic state-machine timing ───────────────────────────────────────────
// Inter-pulse gap: long enough to let the longest waveform complete before
// the next pulse fires. Set to 220 ms — well above the 150 ms longest effect
// in the ROM library — so pulses are always distinct.
#define HAPTIC_PULSE_GAP_MS  220UL

// ── Environment-variable safe ranges ─────────────────────────────────────
// Values received from Notehub are clamped to these ranges. Values outside
// the range are rejected and the current runtime value is kept unchanged,
// with a debug log.
#define ENV_FREEFALL_G_MIN    0.10f     // g  — below 0.1 g ≈ always in free-fall
#define ENV_FREEFALL_G_MAX    0.90f     // g  — above 0.9 g barely filters 1-g rest
#define ENV_IMPACT_G_MIN      1.50f     // g  — below 1.5 g = normal walking impacts
#define ENV_IMPACT_G_MAX      8.00f     // g  — above 8 g not survivable-fall territory
#define ENV_FALL_WINDOW_MIN   100UL     // ms — too short to span a realistic fall
#define ENV_FALL_WINDOW_MAX   2000UL    // ms — too long → nuisance positives
#define ENV_FF_MIN_MS_MIN     20UL      // ms
#define ENV_FF_MIN_MS_MAX     500UL     // ms
#define ENV_PANIC_HOLD_MIN    500UL     // ms — under 500 ms → accidental triggers
#define ENV_PANIC_HOLD_MAX    10000UL   // ms — over 10 s impractical for gloved hands

// ── Shared objects (defined in lone_worker_beacon.ino) ────────────────────
extern Notecard          notecard;
extern LIS3DH            accel;
extern Adafruit_DRV2605  haptic;

// ── Runtime config (defined in lone_worker_beacon.ino) ────────────────────
extern float    g_freefallG;
extern float    g_impactG;
extern uint32_t g_fallWindowMs;
extern uint32_t g_freefallMinMs;
extern uint32_t g_panicHoldMs;
// Fixed-size buffer: see WORKER_ID_MAX. Prefer over Arduino String for a
// repeatedly transmitted satellite-bound identifier.
extern char     g_workerId[WORKER_ID_MAX + 1];

// ── Application state (defined in lone_worker_beacon.ino) ─────────────────
extern bool     g_accelReady;
extern bool     g_hapticReady;
extern bool     g_setupFault;

// Fall-detection state machine
extern bool     g_inFreefall;
extern uint32_t g_freefallStart;
extern bool     g_watchingImpact;
extern uint32_t g_impactWindowStart;  // start of impact window (wraparound-safe)

// Alert timing
extern uint32_t g_lastAlertMs;

// Fixed-depth alert retry queue (see pollAlertRetry() / enqueueAlert())
extern AlertQueueEntry g_alertQueue[ALERT_QUEUE_DEPTH];
extern uint8_t         g_alertQueueHead;    // index of oldest live entry
extern uint8_t         g_alertQueueCount;   // number of live entries

// Panic button
extern bool     g_btnRaw;
extern bool     g_btnStable;
extern uint32_t g_btnChangeMs;
extern uint32_t g_btnPressedAt;
extern bool     g_btnHeld;
extern bool     g_panicFired;

// Non-blocking GPS state machine
extern bool     g_gpsSearching;
extern uint32_t g_gpsSearchStart;     // start of search (wraparound-safe)
extern uint32_t g_gpsCacheEpoch;      // epoch of cached fix at alert time
extern char     g_gpsAlertType[16];   // alert type that triggered this search
extern uint32_t g_gpsLastPollMs;      // last card.location poll (throttle)
// g_gpsDisablePending: set when a card.location.mode:off request fails so
// pollGpsSearch() retries the disable on subsequent loop passes, preventing
// GNSS from remaining in continuous mode and draining the battery.
extern bool     g_gpsDisablePending;

// Event correlation: monotonic counter incremented on each new alert dispatch.
// g_gpsEventId carries the same value into beacon_location.qo so downstream
// systems can correlate the two notes using (device, event_id).
extern uint32_t g_alertEventId;
extern uint32_t g_gpsEventId;

// Accelerometer runtime health
extern uint8_t  g_accelFailCount;    // consecutive implausible/zero reads
extern bool     g_accelFaultLatched; // true after ACCEL_REINIT_MAX failed reinits

// Haptic state machine
extern uint8_t  g_hapticEffect;
extern uint8_t  g_hapticPulsesLeft;
extern uint32_t g_hapticLastPulseMs;
extern bool     g_hapticFirstPulse;   // fire first pulse without waiting gap

// Environment-variable refresh timer (armed in setup(); fires every 2 h)
extern uint32_t g_lastEnvFetchMs;

// Runtime accelerometer-fault buzz timer. Tracks the last time loop() emitted
// the single-buzz degraded-mode alert. Only active when g_accelFaultLatched is
// true and the device is in loop() (boot-time accel failure halts in setup()).
extern uint32_t g_accelFaultBuzzMs;

// ── Function prototypes ───────────────────────────────────────────────────
bool notecardConfigure();
bool defineTemplates();
void fetchEnvVars();
bool initAccel();
bool initHaptic();
bool pollFallDetection();
void tryReinitAccel();
bool checkPanicButton();
void beginGpsSearch(const char *alertType, uint32_t eventId);
void pollGpsSearch();
// sendAlert() makes a single note.add attempt (no internal blocking retries).
// On failure the caller calls enqueueAlert() to schedule non-blocking retries.
// eventId must match the value that will be written to any follow-up
// beacon_location.qo so downstream systems can correlate the two notes.
// locAgeS is the cached-fix age in seconds computed once at event-fire time
// by the caller (via card.time); it is passed through unchanged on retries
// so every attempt reports the original fix age, not the retry-time age.
bool sendAlert(const char *alertType, uint32_t eventId, float locAgeS);
// Enqueue a failed alert for non-blocking retry. Stores the event-time
// cacheEpoch (GPS freshness baseline for pollGpsSearch()), the pre-computed
// locAgeS (fix age at event time, preserved on all retries), and event_id.
// Drops the entry with a debug log if the queue is full.
void enqueueAlert(const char *alertType, uint32_t cacheEpoch, uint32_t eventId,
                  float locAgeS);
// Drain the head of the retry queue. Call once per outer loop pass before
// processing new fall/panic events so retries fire as early as possible.
void pollAlertRetry();
void triggerHaptic(uint8_t effect, int pulses = 1);
void pollHaptic();
