/***************************************************************************
  apu_oem_telemetry - APU OEM Idle-Reduction Telemetry Platform

  Bridges an APU (auxiliary power unit) controller's RS-485 Modbus port to
  Blues Notehub via a Notecard for Skylo (NOTE-NBGLWX). Tracks APU runtime,
  fuel-saved estimates, fault codes, cab/ambient temperature, and ignition
  state. Runs on Notecarrier CX with onboard Cygnet STM32L433 host MCU.

  Hardware:
    - Blues Notecarrier CX (Cygnet STM32L433 host)
    - Notecard for Skylo NOTE-NBGLWX (cellular + Skylo satellite fallback)
    - SparkFun BOB-10124 RS-485 transceiver (SP3485) on UART2
    - 2× Adafruit 381 DS18B20 waterproof temperature probes (OneWire)
    - Voltage-divided ignition sense on A0

  See README.md for full wiring, Notehub setup, and validation instructions.
***************************************************************************/

#include <Notecard.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include "apu_oem_telemetry_helpers.h"

// ---------------------------------------------------------------------------
// Identity — required before flashing
// ---------------------------------------------------------------------------
#ifndef PRODUCT_UID
#define PRODUCT_UID ""  // "com.your-company.your-name:apu-telemetry"
#pragma message "PRODUCT_UID not set. See https://dev.blues.io/tools-and-sdks/samples/product-uid"
#endif

// ---------------------------------------------------------------------------
// Compile-time debug toggle — comment out for production builds.
// USB serial is always initialized (see setup()) so WARN/ERR messages remain
// visible over USB in production builds. When DEBUG is defined, two additional
// behaviors are added: a 3-second USB host-detection wait and the Notecard
// debug stream (verbose per-request I2C traffic). Both materially extend
// awake time on every NotePayloadSaveAndSleep wake and must be absent in
// deployed units.
// ---------------------------------------------------------------------------
// #define DEBUG

// ---------------------------------------------------------------------------
// Globals
// ---------------------------------------------------------------------------
Notecard          notecard;
AppState          g_state;
EnvConfig         g_env;
// Fatal-configuration latch — set when hub.set fails in the cold-boot path.
// Prevents loop() from running runSampleCycle() when NotePayloadSaveAndSleep
// does not cut host power. Cleared only by a successful cold-boot restart. [4]
bool              g_fatalConfigFailed = false;

OneWire           g_ow(ONE_WIRE_PIN);
DallasTemperature g_ds(&g_ow);
DeviceAddress     g_ambAddr, g_cabAddr;
// Per-probe presence flags — set each wake after ROM-ID matching in setup().
// Independent flags keep cab alerting valid even when the ambient probe is off.
bool              g_ambFound = false;
bool              g_cabFound = false;

// ---------------------------------------------------------------------------
// setup — runs on every ATTN-triggered wake and on cold boot
// ---------------------------------------------------------------------------
void setup() {
    // USB serial — always initialized so WARN/ERR messages are available when
    // a service technician connects USB to a deployed unit. The 3-second host-
    // detection wait and Notecard debug stream are only active in DEBUG mode;
    // both materially extend awake time on every NotePayloadSaveAndSleep wake
    // and must be absent in deployed units.
    Serial.begin(115200);
#ifdef DEBUG
    const uint32_t t0 = millis();
    while (!Serial && (millis() - t0) < 3000) {}
#endif

    // Set ADC resolution explicitly before any analogRead call. The STM32 Arduino
    // core default is not guaranteed to be 12-bit; IGNITION_THRESHOLD_ON/OFF_ADC
    // are calibrated for a 0–4095 range. [1]
    analogReadResolution(12);

#ifdef DEBUG
    notecard.setDebugOutputStream(Serial);
#endif
    notecard.begin();  // I2C to Notecard

    // RS-485 UART (direction pin LOW = receive by default)
    RS485_SERIAL.begin(DEFAULT_MODBUS_BAUD);
    pinMode(RS485_DE_PIN, OUTPUT);
    digitalWrite(RS485_DE_PIN, LOW);

    // Ignition sense input
    pinMode(IGNITION_PIN, INPUT);

    // Restore persisted state and env config from previous sleep cycle
    NotePayloadDesc payload;
    bool resumed = NotePayloadRetrieveAfterSleep(&payload);
    if (resumed) {
        resumed &= NotePayloadGetSegment(&payload, STATE_SEG_ID,
                                         &g_state, sizeof(g_state));
        // Restore last-known-good env config. If the ENV segment is absent
        // (e.g. first wake after a firmware update) fall back to compiled-in
        // defaults so the device always starts with a valid configuration. [2]
        if (!NotePayloadGetSegment(&payload, ENV_SEG_ID, &g_env, sizeof(g_env))) {
            initEnvDefaults(g_env);
        }
        NotePayloadFree(&payload);
    }
    if (!resumed) {
        // Cold boot: zero state and seed compiled-in env defaults.
        // Defaults are seeded here once; fetchEnvOverrides (called below) will
        // overwrite them on a successful env.get but will NOT reset them on
        // failure, preserving the known-good baseline across wakes. [2]
        memset(&g_state, 0, sizeof(g_state));
        initEnvDefaults(g_env);
        // hub.set with PRODUCT_UID — retry up to 5 times via requestAndResponse
        // (not sendRequestWithRetry, which returns bool and cannot expose the
        // "err" field). Handles the cold-boot I2C bring-up race. [1][8]
        bool hubOk = false;
        for (int attempt = 0; attempt < 5 && !hubOk; attempt++) {
            if (attempt > 0) delay(1000);
            J *req = notecard.newRequest("hub.set");
            JAddStringToObject(req, "product",  PRODUCT_UID);
            JAddStringToObject(req, "mode",     "periodic");
            JAddNumberToObject(req, "outbound", DEFAULT_SUMMARY_INTERVAL_MIN);
            JAddNumberToObject(req, "inbound",  120);
            J *hubRsp = notecard.requestAndResponse(req);
            if (hubRsp == NULL) {
                Serial.print("[boot] hub.set no response, attempt ");
                Serial.println(attempt + 1);
                continue;
            }
            const char *err = JGetString(hubRsp, "err");
            if (err && err[0] != '\0') {
                Serial.print("[boot] hub.set rejected: ");
                Serial.println(err);
                notecard.deleteResponse(hubRsp);
                continue;
            }
            notecard.deleteResponse(hubRsp);
            hubOk = true;
        }
        if (!hubOk) {
            // PRODUCT_UID is empty or the Notecard is unresponsive. Continuing
            // into normal operation would produce un-routed notes. Set the fatal
            // latch so loop() also stops if NotePayloadSaveAndSleep does not
            // cut host power. Retry on the next ATTN-triggered wake. [4][8]
            Serial.println("[boot] FATAL: hub.set failed after 5 attempts"
                           " — Notecard unresponsive or PRODUCT_UID not set");
            g_fatalConfigFailed = true;
            NotePayloadDesc p = {0, 0, 0};
            NotePayloadSaveAndSleep(&p, 60, NULL);
            delay(15000);
            return;
        }
        // Record the cadence just applied so the interval-change check below
        // does not issue a redundant hub.set on this same cold boot. [7]
        g_state.appliedSummaryIntervalMin = DEFAULT_SUMMARY_INTERVAL_MIN;
        // card.transport and both note.template registrations are mandatory.
        // Retry each up to 3 times inside their respective helpers; track
        // success in g_state.configuredOk so later wakes can retry the
        // failed steps before entering the sample cycle. [3]
        g_state.configuredOk = notecardConfigure(notecard) && defineTemplates(notecard);
        if (!g_state.configuredOk) {
            Serial.println("[boot] WARN: transport/template config incomplete"
                           " — will retry on next wake before sampling");
        }
#ifdef DEBUG
        Serial.println("[boot] cold start — Notecard configured");
#endif
    } else {
#ifdef DEBUG
        Serial.println("[boot] resumed from sleep");
#endif
        // Re-attempt mandatory configuration if a prior wake did not complete it.
        // Only retried if configuredOk is false; once true, skipped every wake. [3]
        if (!g_state.configuredOk) {
            Serial.println("[boot] re-attempting incomplete Notecard configuration");
            g_state.configuredOk = notecardConfigure(notecard) && defineTemplates(notecard);
        }
    }

    // Fetch env vars before DS18B20 commissioning so amb_rom_id / cab_rom_id
    // are available for deterministic probe-role assignment.
    // Re-apply RS-485 baud in case modbus_baud was overridden via Notehub.
    fetchEnvOverrides(notecard, g_env);
    RS485_SERIAL.begin(g_env.modbusBaud);

    // DS18B20 probe commissioning and presence check.
    //
    // Preferred path — operator-provisioned ROM IDs via Notehub env vars:
    //   Set amb_rom_id and cab_rom_id (16 lowercase hex chars each) in the
    //   Notehub fleet environment.  The firmware uses them directly each wake;
    //   no discovery-index assumption is made and role assignment is stable
    //   regardless of OneWire bus enumeration order.
    //
    // Fallback path — index-based (no env vars set):
    //   On the first wake where both probes are present, discovery index 0 is
    //   treated as ambient and index 1 as cab.  OneWire enumeration order is
    //   determined by ROM address value, NOT physical location — the roles may
    //   be swapped.  A warning is printed and ROM IDs are logged so the
    //   operator can verify the physical assignment and set the env vars.
    //   Both probes must be present simultaneously before commissioning is
    //   finalized: committing with one probe would leave the other's ROM ID
    //   zeroed and permanently unlearnable on later wakes. [6]
    g_ds.begin();
    g_ambFound = false;
    g_cabFound = false;
    {
        // Helper: lowercase hex print of an 8-byte ROM address
        auto printRomId = [](const DeviceAddress addr) {
            static const char kHex[] = "0123456789abcdef";
            char buf[17];
            for (uint8_t b = 0; b < 8; b++) {
                buf[b * 2]     = kHex[addr[b] >> 4];
                buf[b * 2 + 1] = kHex[addr[b] & 0x0F];
            }
            buf[16] = '\0';
            Serial.print(buf);
        };

        uint8_t probeCount = g_ds.getDeviceCount();

        if (g_env.romIdsFromEnv) {
            // --- Preferred: deterministic role assignment from env vars ---
            // Use env-var ROM IDs directly; no dependency on discovery index.
            memcpy(g_ambAddr, g_env.ambRomIdFromEnv, 8);
            memcpy(g_cabAddr, g_env.cabRomIdFromEnv, 8);
            for (uint8_t i = 0; i < probeCount; i++) {
                DeviceAddress found;
                if (g_ds.getAddress(found, i)) {
                    if (memcmp(found, g_ambAddr, 8) == 0) g_ambFound = true;
                    if (memcmp(found, g_cabAddr, 8) == 0) g_cabFound = true;
                }
            }
            if (g_ambFound || g_cabFound) g_ds.setResolution(12);
            if (!g_ambFound) Serial.println("[ds18b20] WARN: ambient ROM ID (env) not on bus");
            if (!g_cabFound) Serial.println("[ds18b20] WARN: cab ROM ID (env) not on bus");
#ifdef DEBUG
            Serial.println("[ds18b20] roles assigned from Notehub env vars (deterministic)");
#endif
        } else if (g_state.probesCommissioned) {
            // --- Resuming: match by previously persisted ROM IDs ---
            memcpy(g_ambAddr, g_state.ambRomId, 8);
            memcpy(g_cabAddr, g_state.cabRomId, 8);
            for (uint8_t i = 0; i < probeCount; i++) {
                DeviceAddress found;
                if (g_ds.getAddress(found, i)) {
                    if (memcmp(found, g_ambAddr, 8) == 0) g_ambFound = true;
                    if (memcmp(found, g_cabAddr, 8) == 0) g_cabFound = true;
                }
            }
            if (g_ambFound || g_cabFound) g_ds.setResolution(12);
            if (!g_ambFound) Serial.println("[ds18b20] WARN: ambient probe not on bus this wake");
            if (!g_cabFound) Serial.println("[ds18b20] WARN: cab probe not on bus this wake");
        } else if (probeCount >= 2) {
            // --- Fallback: index-based commissioning (no env vars, first boot) ---
            // WARNING: discovery order depends on ROM address value, not physical
            // position.  Index 0 may not be the probe routed as ambient and
            // index 1 may not be the cab probe.  The ROM IDs are printed below;
            // physically verify which probe is which, then set amb_rom_id and
            // cab_rom_id in the Notehub fleet environment to fix roles. [6]
            bool ambAddrOk = g_ds.getAddress(g_ambAddr, 0);
            bool cabAddrOk = g_ds.getAddress(g_cabAddr, 1);
            if (ambAddrOk && cabAddrOk) {
                memcpy(g_state.ambRomId, g_ambAddr, 8);
                memcpy(g_state.cabRomId, g_cabAddr, 8);
                g_ambFound = true;
                g_cabFound = true;
                g_state.probesCommissioned = true;
                g_ds.setResolution(12);
                // Print ROM IDs unconditionally so operators can set env vars.
                Serial.println("[ds18b20] WARN: probe roles assigned by discovery index —");
                Serial.println("[ds18b20] WARN: physical roles may be swapped. Verify and");
                Serial.println("[ds18b20] WARN: set Notehub env vars amb_rom_id / cab_rom_id.");
                Serial.print("[ds18b20] index-0 (assigned ambient) ROM: "); printRomId(g_ambAddr); Serial.println();
                Serial.print("[ds18b20] index-1 (assigned cab)     ROM: "); printRomId(g_cabAddr); Serial.println();
            } else {
                Serial.println("[ds18b20] WARN: getAddress failed during index commissioning");
            }
        } else if (probeCount > 0) {
            Serial.print("[ds18b20] WARN: commissioning deferred — need 2 probes, found ");
            Serial.println(probeCount);
        } else {
            Serial.println("[ds18b20] WARN: no probes found — cab alerting suppressed");
        }
    }

    // Re-apply Notecard outbound cadence immediately if summary_interval_min
    // changed via a Notehub fleet env-var push.  Without this, the Notecard
    // sync cadence stays at the old value until the next summary window fires,
    // which can be up to summaryIntervalMin minutes later. [7]
    if (g_state.appliedSummaryIntervalMin != g_env.summaryIntervalMin) {
        J *req = notecard.newRequest("hub.set");
        JAddStringToObject(req, "mode",     "periodic");
        JAddNumberToObject(req, "outbound", (int)g_env.summaryIntervalMin);
        JAddNumberToObject(req, "inbound",  120);
        if (ncSend(notecard, req)) {
            g_state.appliedSummaryIntervalMin = g_env.summaryIntervalMin;
#ifdef DEBUG
            Serial.print("[hub.set] outbound cadence updated to ");
            Serial.println(g_env.summaryIntervalMin);
#endif
        } else {
            Serial.println("[hub.set] WARN: outbound cadence update failed; will retry next wake");
        }
    }

    if (g_state.configuredOk) {
        runSampleCycle();
    } else {
        // Mandatory config still incomplete — save state and sleep so the next
        // ATTN-triggered wake re-attempts config before trying to sample. [3]
        Serial.println("[boot] WARN: skipping sample cycle — Notecard config incomplete");
        NotePayloadDesc sleepPayload = {0, 0, 0};
        NotePayloadAddSegment(&sleepPayload, STATE_SEG_ID, &g_state, sizeof(g_state));
        NotePayloadAddSegment(&sleepPayload, ENV_SEG_ID,   &g_env,   sizeof(g_env));
        NotePayloadSaveAndSleep(&sleepPayload, g_env.sampleIntervalSec, NULL);
        delay(15000);
    }
}

// ---------------------------------------------------------------------------
// loop — should never execute; host power is cut by card.attn after setup()
// ---------------------------------------------------------------------------
void loop() {
    // Fatal-configuration latch: hub.set failed in setup(). Do not proceed with
    // normal operation — a misprovisioned device must not emit un-routed notes.
    // NotePayloadSaveAndSleep may not have cut host power, so this guard is the
    // second line of defence. Wait briefly and let ATTN fire a fresh cold boot. [4]
    if (g_fatalConfigFailed) {
        Serial.println("[loop] WARN: fatal config failure — not sampling");
        delay(60000);
        return;
    }

    // If ATTN host power-gating is not wired, fall back to timed delay.
    // This should NOT occur in a correctly assembled Notecarrier CX deployment.
    Serial.println("[loop] WARN: host not sleeping — check ATTN/card.attn wiring");
    delay(g_env.sampleIntervalSec * 1000UL);
    fetchEnvOverrides(notecard, g_env);
    RS485_SERIAL.begin(g_env.modbusBaud);
    // Retry mandatory config on every loop iteration until it succeeds. [3]
    if (!g_state.configuredOk) {
        g_state.configuredOk = notecardConfigure(notecard) && defineTemplates(notecard);
    }
    if (g_state.appliedSummaryIntervalMin != g_env.summaryIntervalMin) {
        J *req = notecard.newRequest("hub.set");
        JAddStringToObject(req, "mode",     "periodic");
        JAddNumberToObject(req, "outbound", (int)g_env.summaryIntervalMin);
        JAddNumberToObject(req, "inbound",  120);
        if (ncSend(notecard, req)) {
            g_state.appliedSummaryIntervalMin = g_env.summaryIntervalMin;
        } else {
            Serial.println("[hub.set] WARN: outbound cadence update failed");
        }
    }
    if (g_state.configuredOk) {
        runSampleCycle();
    } else {
        Serial.println("[loop] WARN: config still incomplete — skipping sample");
    }
}

// ---------------------------------------------------------------------------
// runSampleCycle — one full sample → evaluate → emit → sleep pass
// ---------------------------------------------------------------------------
void runSampleCycle() {
    // Advance the monotonic sample counter before any other work. This is
    // persisted in g_state across sleep cycles and used for alert cooldown
    // so dedup remains correct regardless of card.time availability. [3]
    g_state.sampleCount++;

    // Read ignition state — discard one post-wake settling sample, then average
    // four reads to suppress transient noise from the vehicle bus or cranking.
    // Apply hysteresis: hold the previous ignition state when the averaged ADC
    // falls between IGNITION_THRESHOLD_OFF_ADC and IGNITION_THRESHOLD_ON_ADC
    // to prevent false state-change events near the threshold boundary. [1][9]
    (void)analogRead(IGNITION_PIN);   // discard — ADC settling after host wake
    delayMicroseconds(200);
    int32_t adcSum = 0;
    for (uint8_t i = 0; i < 4; i++) {
        adcSum += analogRead(IGNITION_PIN);
        delayMicroseconds(100);
    }
    int32_t adcAvg = adcSum / 4;
    bool ignitionOn;
    if      (adcAvg >= IGNITION_THRESHOLD_ON_ADC)  ignitionOn = true;
    else if (adcAvg <  IGNITION_THRESHOLD_OFF_ADC) ignitionOn = false;
    else                                           ignitionOn = g_state.lastIgnitionOn;
    g_state.lastIgnitionOn = ignitionOn;

    // Read DS18B20 temperature probes — per-probe validity tracked independently
    // so an ambient failure cannot suppress cab-overheat / cab-freeze alerts. [6]
    float ambF = -999.0f, cabF = -999.0f;
    bool ambOk = false, cabOk = false;
    readTemperatures(g_ds, g_ambFound, g_cabFound, g_ambAddr, g_cabAddr,
                     &ambF, &cabF, &ambOk, &cabOk);

    // Poll APU controller over Modbus RTU (3 retries)
    uint16_t regs[5] = {0};
    bool modbusOk = false;
    for (int i = 0; i < MODBUS_READ_RETRIES && !modbusOk; i++) {
        modbusOk = modbusReadHolding(g_env.modbusSlaveId,
                                     g_env.modbusRegBase, 5, regs, g_env.modbusBaud);
    }
    if (modbusOk) {
#ifdef DEBUG
        Serial.print("[modbus ok] regs=");
        for (uint8_t r = 0; r < 5; r++) {
            Serial.print(regs[r]); if (r < 4) Serial.print(" ");
        }
        Serial.println();
#endif
    } else {
        Serial.print("[modbus fail] retries="); Serial.println(MODBUS_READ_RETRIES);
    }
    // Derive APU state — or preserve last-known-good on Modbus failure.
    // Treating missing controller data as affirmative "APU off / no fault"
    // creates bogus state_change events, mis-classifies runtime/fuel stats,
    // and causes a still-active fault to be double-counted on recovery. [3]
    bool     apuOn, apuFault;
    uint16_t faultCode;
    float    dcVolts;
    uint16_t watts;
    if (modbusOk) {
        apuOn     = (regs[REG_OFFSET_STATUS] & 0x01) != 0;
        apuFault  = (regs[REG_OFFSET_STATUS] & 0x02) != 0;
        faultCode = regs[REG_OFFSET_FAULT_CODE];
        dcVolts   = regs[REG_OFFSET_DC_VOLTS_X10] / 10.0f;
        watts     = regs[REG_OFFSET_WATTS];
        // Wrap-aware lifetime runtime. The 16-bit register rolls over at
        // 6 553.5 h (~273 days). Detect a decrease vs. the persisted last
        // reading and increment runtimeWrapCount; the extended total
        // (wrapCount × 65 536 + rawX10) / 10 stays accurate across any
        // number of rollovers and host-power-off cycles. [4]
        uint16_t rawRtX10 = regs[REG_OFFSET_RUNTIME_X10];
        if (rawRtX10 < g_state.lastRawRuntimeX10 && g_state.lastRawRuntimeX10 != 0) {
            g_state.runtimeWrapCount++;
        }
        g_state.lastRawRuntimeX10 = rawRtX10;
        uint32_t lifetimeX10 = (uint32_t)g_state.runtimeWrapCount * 65536UL + rawRtX10;
        g_state.lastControllerRuntimeHr = lifetimeX10 / 10.0f;
    } else {
        // Controller data invalid this sample — fall back to last-known state
        // so no affirmative "off / clear" decision is forced. [3]
        apuOn     = (g_state.state == STATE_APU_ACTIVE);
        apuFault  = g_state.faultWasActive;
        faultCode = g_state.activeFaultCode;
        dcVolts   = 0.0f;
        watts     = 0;
    }

#ifdef DEBUG
    Serial.print("[sample] ign="); Serial.print(ignitionOn);
    Serial.print(" apu=");         Serial.print(apuOn);
    Serial.print(" fault=");       Serial.print(faultCode);
    Serial.print(" cabF=");        Serial.print(cabF, 1);
    Serial.print(" ambF=");        Serial.println(ambF, 1);
#endif

    // ------------------------------------------------------------------
    // State machine transitions — only when controller data is valid.
    // Skipping on Modbus failure prevents bogus state_change events from
    // a transient RS-485 glitch while the APU is actually running. [3]
    // ------------------------------------------------------------------
    if (modbusOk) {
        uint8_t prevState = g_state.state;
        if (apuOn)           g_state.state = STATE_APU_ACTIVE;
        else if (ignitionOn) g_state.state = STATE_IDLE_ON;
        else                 g_state.state = STATE_IDLE_OFF;

        if (g_state.state != prevState) {
            if (!sendAlert(notecard, g_state, "state_change", 0, ambF, cabF, dcVolts, watts)) {
                Serial.println("[event] WARN: state_change note.add failed");
            }
#ifdef DEBUG
            Serial.print("[event] state "); Serial.print(prevState);
            Serial.print(" -> "); Serial.println(g_state.state);
#endif
        }
    }

    // ------------------------------------------------------------------
    // Accumulate window statistics — only when controller data is valid.
    // Without confirmed APU state, accumulating runtime or fuel into the
    // wrong bucket corrupts warranty-relevant lifetime metrics. [3]
    // ------------------------------------------------------------------
    if (modbusOk) {
        if (g_state.state == STATE_APU_ACTIVE) {
            g_state.apuRuntimeSec += g_env.sampleIntervalSec;
            // Fuel accounting uses software estimation: APU runtime × rated
            // consumption from environment variables. [1]
            float fuelUsedThisSample  = g_env.apuFuelRateGph
                                        * (g_env.sampleIntervalSec / 3600.0f);
            float fuelSavedThisSample = (g_env.idleFuelRateGph - g_env.apuFuelRateGph)
                                        * (g_env.sampleIntervalSec / 3600.0f);
            // Per-summary-window accumulators (reset by sendSummary)
            g_state.fuelUsedGal  += fuelUsedThisSample;
            g_state.fuelSavedGal += fuelSavedThisSample;
            // Daily accumulators (reset by sendDailySummary once per calendar day)
            g_state.dailyApuRuntimeSec += g_env.sampleIntervalSec;
            g_state.dailyFuelUsedGal   += fuelUsedThisSample;
            g_state.dailyFuelSavedGal  += fuelSavedThisSample;
        } else if (g_state.state == STATE_IDLE_ON) {
            g_state.idleTimeSec      += g_env.sampleIntervalSec;
            g_state.dailyIdleTimeSec += g_env.sampleIntervalSec;
        }
        g_state.dcVoltsX10Acc += regs[REG_OFFSET_DC_VOLTS_X10];
        g_state.wattsAcc      += watts;
        g_state.powerSamples++;
    }
    // Temperature probes are independent of Modbus — accumulate regardless. [6]
    if (ambOk) { g_state.ambTempAccF += ambF; g_state.ambTempSamples++; }
    if (cabOk) { g_state.cabTempAccF += cabF; g_state.cabTempSamples++; }
    g_state.samplesInWindow++;

    // Cooldown expressed in samples — no card.time dependency. Ceiling division
    // ensures samplesForCooldown >= 1 even when alertCooldownSec is shorter than
    // sampleIntervalSec, so dedup is never bypassed for any legal env var
    // combination.  Lower-bound of 1 is an additional safety net. [2]
    uint32_t samplesForCooldown = max(
        (g_env.alertCooldownSec + g_env.sampleIntervalSec - 1)
            / max(g_env.sampleIntervalSec, 1UL),
        1UL);

    // ------------------------------------------------------------------
    // APU fault alert — only updated when controller data is valid.
    // Skipping on Modbus failure ensures faultWasActive is not cleared
    // for a still-active fault and activeFaultCode is not re-counted on
    // recovery from a transient comms outage. [3]
    // faultCount is edge-detected: only increments on a 0→nonzero
    // transition or a change in fault code, not every sample while a
    // fault remains latched. [5]
    // Cooldown is sample-count based — no card.time dependency. [3]
    // ------------------------------------------------------------------
    if (modbusOk) {
        if (apuFault && faultCode != 0) {
            // Edge-detect using activeFaultCode, which is persistent across summary
            // boundaries and is never reset by sendSummary().  Using lastFaultCode
            // here would mis-count a continuing fault as a new occurrence each time
            // a summary window resets it to 0. [4]
            if (!g_state.faultWasActive || faultCode != g_state.activeFaultCode) {
                g_state.faultCount++;
            }
            g_state.faultWasActive  = true;
            g_state.activeFaultCode = faultCode;  // persistent — not reset in sendSummary
            g_state.lastFaultCode   = faultCode;  // per-window reporting — reset in sendSummary

            // Bypass cooldown when the active code differs from the last alerted
            // code — a new fault type must never be hidden because a different
            // fault happened to start the current cooldown window. [3]
            bool codeChanged = (faultCode != g_state.lastAlertedFaultCode);
            bool cooldownElapsed =
                (g_state.lastAlertFaultSample == 0) ||
                ((g_state.sampleCount - g_state.lastAlertFaultSample) >= samplesForCooldown) ||
                codeChanged;
            if (cooldownElapsed) {
                if (sendAlert(notecard, g_state, "apu_fault", faultCode,
                              ambF, cabF, dcVolts, watts)) {
                    g_state.lastAlertFaultSample = g_state.sampleCount;
                    g_state.lastAlertedFaultCode = faultCode;
#ifdef DEBUG
                    Serial.print("[alert] apu_fault code="); Serial.println(faultCode);
#endif
                } else {
                    Serial.println("[alert] WARN: apu_fault note.add failed — will retry");
                }
            }
        } else {
            g_state.faultWasActive = false;
        }
    }

    // ------------------------------------------------------------------
    // Cab temperature alert — gated on cabOk alone so an ambient probe
    // failure does not suppress cab-protection logic. [6]
    // ------------------------------------------------------------------
    if (cabOk && (cabF > g_env.cabTempHighF || cabF < g_env.cabTempLowF)) {
        bool cooldownElapsed =
            (g_state.lastAlertCabSample == 0) ||
            ((g_state.sampleCount - g_state.lastAlertCabSample) >= samplesForCooldown);
        if (cooldownElapsed) {
            const char *evt = (cabF > g_env.cabTempHighF) ? "cab_overheat" : "cab_freeze";
            if (sendAlert(notecard, g_state, evt, 0, ambF, cabF, dcVolts, watts)) {
                g_state.lastAlertCabSample = g_state.sampleCount;
#ifdef DEBUG
                Serial.print("[alert] "); Serial.println(evt);
#endif
            } else {
                Serial.println("[alert] WARN: cab temp note.add failed — will retry");
            }
        }
    }

    // ------------------------------------------------------------------
    // Persistent sensor-failure alert — rate-limited by alertCooldownSec
    // to prevent alert floods during a sustained dual-sensor outage. [7]
    // ------------------------------------------------------------------
    // Both temperature probes AND Modbus must fail to increment badSensorCycles.
    // uint16_t avoids int8_t overflow during long cooldown windows (a 10-second
    // sample interval with a 1800-second cooldown = 180 samples before alert,
    // well within uint8_t range but uint16_t leaves no doubt). Saturate at
    // 0xFFFF rather than wrapping. [3][6]
    if (!ambOk && !cabOk && !modbusOk) {
        if (g_state.badSensorCycles < 0xFFFFu) g_state.badSensorCycles++;
    } else {
        g_state.badSensorCycles = 0;
    }
    if (g_state.badSensorCycles >= 3) {
        bool cooldownElapsed =
            (g_state.lastAlertSensorFailSample == 0) ||
            ((g_state.sampleCount - g_state.lastAlertSensorFailSample) >= samplesForCooldown);
        if (cooldownElapsed) {
            if (sendAlert(notecard, g_state, "sensor_fail", 0, ambF, cabF, dcVolts, watts)) {
                g_state.lastAlertSensorFailSample = g_state.sampleCount;
                g_state.badSensorCycles = 0;
#ifdef DEBUG
                Serial.println("[alert] sensor_fail");
#endif
            } else {
                Serial.println("[alert] WARN: sensor_fail note.add failed — will retry");
            }
        }
        // If cooldown has not elapsed, leave badSensorCycles intact so the
        // condition re-evaluates on the next sample without needing 3 fresh
        // consecutive failures.
    }

    // ------------------------------------------------------------------
    // Hourly summary — trigger on sample count to avoid wall-time dependency.
    // max(..., 1UL) ensures samplesPerSummary is never zero even if env vars
    // produce a sub-sample summary interval after clamping. [4]
    // ------------------------------------------------------------------
    uint32_t samplesPerSummary = max(
        (g_env.summaryIntervalMin * 60UL) / max(g_env.sampleIntervalSec, 1UL),
        1UL);
    if (g_state.samplesInWindow >= samplesPerSummary) {
        sendSummary(notecard, g_state, g_env);
#ifdef DEBUG
        Serial.println("[summary] sent — window reset");
#endif
        // Re-apply outbound cadence after each summary in case summaryIntervalMin
        // changed via env var mid-window.  Also updates appliedSummaryIntervalMin
        // so the at-boot check does not re-issue an identical request next wake. [7]
        J *req = notecard.newRequest("hub.set");
        JAddStringToObject(req, "mode",     "periodic");
        JAddNumberToObject(req, "outbound", (int)g_env.summaryIntervalMin);
        JAddNumberToObject(req, "inbound",  120);
        if (ncSend(notecard, req)) {
            g_state.appliedSummaryIntervalMin = g_env.summaryIntervalMin;
        } else {
            Serial.println("[hub.set] WARN: failed to update outbound cadence after summary");
        }
    }

    // ------------------------------------------------------------------
    // Daily fuel-saved rollup — emit apu_daily.qo once per calendar day.
    // card.time provides calendar-day alignment; sample-count fallback fires
    // when the Notecard has not yet synced time (fresh unit, pre-first-sync).
    // ------------------------------------------------------------------
    {
        uint32_t todayNum = 0;
        J *timeRsp = notecard.requestAndResponse(notecard.newRequest("card.time"));
        if (timeRsp) {
            uint32_t ep = (uint32_t)JGetNumber(timeRsp, "time");
            notecard.deleteResponse(timeRsp);
            // Sanity-check epoch: values below 2001-01-01 (978307200) mean the
            // Notecard has not yet acquired a time reference via GPS or cellular.
            if (ep > 978307200UL) todayNum = ep / 86400UL;
        }

        bool emitDailyRollup = false;
        if (todayNum > 0) {
            if (g_state.lastDailyEpochDay == 0) {
                // First valid time sync — anchor the day tracker without emitting
                // a partial-day note on first acquisition.
                g_state.lastDailyEpochDay = todayNum;
                g_state.samplesSinceDaily = 0;
            } else if (todayNum > g_state.lastDailyEpochDay) {
                emitDailyRollup = true;
                // lastDailyEpochDay updated below, only after confirmed success.
            }
        } else {
            // No time sync yet — count-based fallback (disabled once time is established).
            // Ceiling-divide 86400 by the configured sample interval so the fallback
            // fires approximately once per day at any sample rate.  The previous
            // 1440-sample floor was only correct for 60 s intervals; at 5 min it
            // would have deferred the first rollup by ~5 days instead of ~1 day.
            uint32_t siv = max(g_env.sampleIntervalSec, 1UL);
            uint32_t samplesPerDay = max((86400UL + siv - 1UL) / siv, 1UL);
            if (g_state.lastDailyEpochDay == 0 &&
                g_state.samplesSinceDaily >= samplesPerDay) {
                emitDailyRollup = true;
            }
        }
        g_state.samplesSinceDaily++;

        if (emitDailyRollup) {
            if (sendDailySummary(notecard, g_state)) {
                // Advance the day anchor only after confirmed delivery so a
                // failed send retries on the next wake of the same calendar day.
                if (todayNum > 0) g_state.lastDailyEpochDay = todayNum;
                else              g_state.samplesSinceDaily = 0;
#ifdef DEBUG
                Serial.println("[daily] day boundary — rollup complete");
#endif
            }
        }
    }

    // ------------------------------------------------------------------
    // Persist state and cut host power until next sample interval
    // ------------------------------------------------------------------
    NotePayloadDesc payload = {0, 0, 0};
    NotePayloadAddSegment(&payload, STATE_SEG_ID, &g_state, sizeof(g_state));
    // Persist g_env alongside g_state so the last-known-good configuration
    // survives across sleep cycles. fetchEnvOverrides only updates env on a
    // confirmed successful env.get; a transient failure on resume leaves this
    // persisted copy intact rather than reverting to compiled-in defaults. [2]
    NotePayloadAddSegment(&payload, ENV_SEG_ID,   &g_env,   sizeof(g_env));
    NotePayloadSaveAndSleep(&payload, g_env.sampleIntervalSec, NULL);

    // Should never reach here — host power is cut by card.attn
    delay(15000);
}
