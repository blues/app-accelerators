// cargo_lock.ino
//
// Connected Smart Cargo Lock Platform — entry point
//
// Host:     Blues Notecarrier CX (onboard STM32 Cygnet — no separate MCU needed)
// Notecard: Blues Notecard for Skylo (NOTE-NBGLWX)
//           LTE-M / NB-IoT / GPRS primary cellular + Skylo NTN satellite failover
//
// Sensors:
//   Shackle: Normally-closed reed switch on D5  (LOW = shackle magnet present)
//   Bolt:    Honeywell SS461A hall-effect switch on D6 (LOW = bolt magnet detected)
//            VCC supplied from host-gated +3V3; rated 3.0–24 VDC (3.3 V is within spec).
//   Tamper:  Notecard built-in accelerometer via card.motion / card.motion.mode
//
// Lock state machine:
//   LOCKED   — shackle present AND bolt engaged
//   UNLOCKED — shackle absent  OR  bolt withdrawn
//   UNKNOWN  — initial / indeterminate (cleared after first sensor read)
//
// Event types emitted to lock_event.qo (sync:true, immediate):
//   "locked"  — transition to LOCKED state
//   "opened"  — transition from LOCKED to UNLOCKED
//   "tamper"  — motion above TAMPER_THRESHOLD while LOCKED
//
// Periodic summary emitted to lock_status.qo every REPORT_INTERVAL_MIN.
//
// Wake cadence:
//   Host wakes every SAMPLE_INTERVAL_SEC via NotePayloadSaveAndSleep (card.attn sleep).
//   All runtime state survives the power cut via the Notecard's on-device flash.
//
// Optional BLE key authentication is described in the README but is NOT implemented
// here — it requires an external BLE module and a separate authentication library.
//
// Dependencies:
//   Blues Wireless Notecard  (note-arduino)  — install via Arduino Library Manager
//   stm32duino/Arduino_Core_STM32            — install via Arduino Boards Manager
//
// State-machine edge detection, pending-event retry, and template encoding are
// documented inline in cargo_lock_helpers.cpp.

#include <Wire.h>
#include <Notecard.h>
#include "cargo_lock_helpers.h"

#ifndef PRODUCT_UID
#define PRODUCT_UID "" // "com.my-company.my-name:cargo-lock"
#pragma message "PRODUCT_UID is not set. Claim one in Notehub, then define it here."
#endif

// -------- Default thresholds — all tunable via Notehub environment variables -----
uint32_t SAMPLE_INTERVAL_SEC = 60;    // 60 s between wakeups — matches card.motion minutes:1 bucket
uint32_t REPORT_INTERVAL_MIN = 360;   // 6 hours between status summaries
uint8_t  TAMPER_THRESHOLD    = 8;     // motion counts/min → declare tamper
uint32_t ALERT_COOLDOWN_SEC  = 1800;  // 30 min between repeated same-type alerts

// -------- Module-level globals (declared extern in cargo_lock_helpers.h) ---------
Notecard     notecard;
PersistState state;
const char   STATE_SEG_ID[] = "CKLK";

// -------- Setup / loop -----------------------------------------------------------
// This sketch uses the "host-is-off-when-idle" sleep pattern. After runSampleCycle()
// completes, loop() calls NotePayloadSaveAndSleep() which serializes state into the
// Notecard's flash and cuts host power via the ATTN pin. The Notecard then wakes the
// host SAMPLE_INTERVAL_SEC seconds later; setup() runs again, restores state, runs
// the next cycle, and sleeps again. On the Notecarrier CX, ATTN is routed to the
// Cygnet host power enable internally — no external jumper is required — so loop()
// is essentially never reached in normal deployed operation.

void setup() {
#if defined(DEBUG_SERIAL)
    usbSerial.begin(115200);
    for (uint32_t t0 = millis(); !usbSerial && (millis() - t0) < 3000; ) {}
#endif

#if defined(DEBUG_SERIAL)
    if (!PRODUCT_UID[0]) {
        usbSerial.println("[cargo_lock] ERROR: PRODUCT_UID is not set — device will not connect"
                         " to any Notehub project. Define PRODUCT_UID in cargo_lock.ino and reflash.");
#ifdef DEBUG
        // In debug builds, halt here so a developer does not waste time chasing
        // connectivity symptoms when the root cause is a missing ProductUID.
        usbSerial.println("[cargo_lock] Halting in DEBUG build until PRODUCT_UID is configured.");
        while (true) { delay(5000); }
#endif
    }
#endif

    pinMode(PIN_SHACKLE, INPUT_PULLUP);
    pinMode(PIN_BOLT,    INPUT_PULLUP);

    Wire.begin();
    notecard.begin();
#if defined(DEBUG_SERIAL)
    notecard.setDebugOutputStream(usbSerial);
#endif

    // Pass PRODUCT_UID to the helpers module once. PRODUCT_UID is a preprocessor
    // macro and cannot be shared across compilation units; helpers call
    // lockGetProductUID() to retrieve it wherever it is needed (e.g., hub.set).
    lockSetProductUID(PRODUCT_UID);

    // Attempt to restore runtime state from the Notecard's payload store.
    // If this is a cold boot (first power, or payload expired), restored == false
    // and we fall through to first-boot initialization.
    NotePayloadDesc payload;
    bool restored = NotePayloadRetrieveAfterSleep(&payload);
    if (restored) {
        restored &= NotePayloadGetSegment(&payload, STATE_SEG_ID, &state, sizeof(state));
        NotePayloadFree(&payload);
    }
    if (!restored) {
        memset(&state, 0, sizeof(state));
    }

    // Retry first-boot configuration on every wake until it fully succeeds.
    // A transient I²C failure on cold boot would otherwise leave note templates
    // undefined indefinitely, because the restored-state path skips this block
    // once state is saved — even if config was never completed.
    if (!state.config_complete) {
        hubConfigure();
        if (defineTemplates()) {
            state.config_complete = true;
        }
    }

    // Re-apply card.motion.mode on every wake — not just on first boot.
    // The Notecard retains its configuration in Notecard-side storage, but a full
    // Notecard power cycle or firmware update can reset that state. Because the host
    // restores config_complete from its own payload store it has no way to detect a
    // Notecard reset; issuing this unconditionally ensures tamper detection is always
    // armed and can never be silently disabled by a Notecard-side reset.
    // card.motion.mode with start:true is idempotent when already running.
    {
        J *mot_req = notecard.newRequest("card.motion.mode");
        if (mot_req) {
            JAddBoolToObject  (mot_req, "start",       true);
            JAddNumberToObject(mot_req, "sensitivity",  2);
            if (!notecard.sendRequest(mot_req)) {
#if defined(DEBUG_SERIAL)
                usbSerial.println("[cargo_lock] WARN: card.motion.mode failed;"
                                  " tamper detection may be inactive this wake");
#endif
            }
        }
    }

    // Re-read env vars on every wake. An operator can adjust thresholds in Notehub
    // and they will take effect on the next wake without re-flashing the device.
    fetchEnvOverrides();

    runSampleCycle();
}

void loop() {
    // Serialize runtime state into the Notecard's on-device flash, then cut host
    // power for SAMPLE_INTERVAL_SEC seconds via card.attn sleep mode.
    // On the Notecarrier CX, ATTN is routed to the Cygnet host power enable
    // internally — no external jumper needed. The Notecard drives ATTN to cut
    // the Cygnet's supply; the host cold-boots into setup() on the next wake.
    NotePayloadDesc payload = {0, 0, 0};
    NotePayloadAddSegment(&payload, STATE_SEG_ID, &state, sizeof(state));
    NotePayloadSaveAndSleep(&payload, SAMPLE_INTERVAL_SEC, NULL);

    // Only reached if NotePayloadSaveAndSleep could not cut host power via ATTN
    // (e.g., a non-CX carrier board or unusual bench configuration). Fall back to
    // a software delay so the loop cadence is preserved.
    delay(SAMPLE_INTERVAL_SEC * 1000UL);
}
