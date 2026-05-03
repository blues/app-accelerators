/***************************************************************************
  apu_oem_telemetry_helpers.cpp - Helper implementations for APU OEM Telemetry

  Implements: ncSend, notecardConfigure, defineTemplates, initEnvDefaults,
  fetchEnvOverrides, modbusCRC16, modbusReadHolding, readTemperatures,
  sendAlert, and sendSummary.

  See README.md for full design rationale and validation instructions.
***************************************************************************/
#include "apu_oem_telemetry_helpers.h"

// RS-485 UART instance on Cygnet USART2 (PA3=RX, PA2=TX). Declared extern in
// the header; the STM32duino Cygnet variant does not provide a default Serial2.
HardwareSerial RS485_SERIAL(PA3, PA2);

// ---------------------------------------------------------------------------
// ncSend — checked Notecard request helper.
// Sends req via requestAndResponse, inspects the response for a NULL return
// or a non-empty "err" field, logs any failure, and frees the response.
// Returns true on confirmed success; false on NULL response or error string.
// Use instead of sendRequest() wherever a silent failure would corrupt state
// (e.g. note.add in sendSummary, template registration, transport config).
// ---------------------------------------------------------------------------
bool ncSend(Notecard &notecard, J *req) {
    if (!req) return false;
    J *rsp = notecard.requestAndResponse(req);
    if (!rsp) {
        Serial.println("[notecard] ERR: no response");
        return false;
    }
    const char *err = JGetString(rsp, "err");
    bool ok = (!err || err[0] == '\0');
    if (!ok) {
        Serial.print("[notecard] ERR: ");
        Serial.println(err);
    }
    notecard.deleteResponse(rsp);
    return ok;
}

// ---------------------------------------------------------------------------
// parseRomIdHex — parse a 16-character lowercase hex string into an 8-byte
// OneWire ROM address. Returns true on success.
// Example: "2800a1b2c3d4e501" → {0x28,0x00,0xa1,0xb2,0xc3,0xd4,0xe5,0x01}
// ---------------------------------------------------------------------------
static bool parseRomIdHex(const char *hex, uint8_t *out) {
    if (!hex || strlen(hex) != 16) return false;
    for (uint8_t i = 0; i < 8; i++) {
        uint8_t hi, lo;
        char ch;
        ch = hex[i * 2];
        if      (ch >= '0' && ch <= '9') hi = ch - '0';
        else if (ch >= 'a' && ch <= 'f') hi = ch - 'a' + 10;
        else if (ch >= 'A' && ch <= 'F') hi = ch - 'A' + 10;
        else return false;
        ch = hex[i * 2 + 1];
        if      (ch >= '0' && ch <= '9') lo = ch - '0';
        else if (ch >= 'a' && ch <= 'f') lo = ch - 'a' + 10;
        else if (ch >= 'A' && ch <= 'F') lo = ch - 'A' + 10;
        else return false;
        out[i] = (hi << 4) | lo;
    }
    return true;
}

// ---------------------------------------------------------------------------
// notecardConfigure — transport, GPS mode, accelerometer off.
// card.transport to "cell-ntn" is mandatory: without it the Notecard cannot
// engage Skylo satellite fallback. card.location.mode is also mandatory for
// this Skylo design: trucks are mobile and GPS underlies both _lat/_lon
// population and satellite orbital positioning. Both are retried up to 3
// times; a single transient I2C failure must not leave a deployed unit
// permanently without satellite transport or periodic position fixes.
// card.motion.mode is best-effort (warn on failure, does not block).
// Returns true only if both card.transport AND card.location.mode succeeded. [3]
// ---------------------------------------------------------------------------
bool notecardConfigure(Notecard &notecard) {
    // Cellular-first, Skylo satellite fallback transport — mandatory.
    // Retry up to 3 times; a transient I2C hiccup should not permanently
    // disable satellite coverage for a deployed unit.
    bool transportOk = false;
    for (int attempt = 0; attempt < 3 && !transportOk; attempt++) {
        if (attempt > 0) delay(500);
        J *req = notecard.newRequest("card.transport");
        JAddStringToObject(req, "method", "cell-ntn");
        transportOk = ncSend(notecard, req);
    }
    if (!transportOk) {
        Serial.println("[cfg] ERR: card.transport failed after retries"
                       " — satellite fallback NOT guaranteed");
    }

    // Periodic GPS — mandatory for this Skylo design. Trucks are mobile so
    // position belongs on every note, and Skylo GEO satellites need a GPS
    // fix to locate orbital position. A single transient failure must not
    // leave a field unit permanently without periodic location mode. [2]
    bool locationOk = false;
    for (int attempt = 0; attempt < 3 && !locationOk; attempt++) {
        if (attempt > 0) delay(500);
        J *req = notecard.newRequest("card.location.mode");
        JAddStringToObject(req, "mode",    "periodic");
        JAddNumberToObject(req, "seconds", 600);
        locationOk = ncSend(notecard, req);
    }
    if (!locationOk) {
        Serial.println("[cfg] ERR: card.location.mode failed after retries"
                       " — GPS/satellite positioning NOT active");
    }

    // Disable accelerometer to reduce idle power and scope noise on bench
    {
        J *req = notecard.newRequest("card.motion.mode");
        JAddBoolToObject(req, "stop", true);
        if (!ncSend(notecard, req)) Serial.println("[cfg] WARN: card.motion.mode failed");
    }

    return transportOk && locationOk;
}

// ---------------------------------------------------------------------------
// defineTemplates — compact Notefile templates registered once on cold boot.
// Compact format is required so notes can transit Skylo's 256-byte satellite
// payload limit when cellular is unavailable.  All three templates are mandatory:
// if any fail the device cannot send satellite-safe compact payloads.
// Each template is retried up to 3 times. Returns true only if all three succeed.
//
// _lat, _lon, and _time are Notecard-reserved compact-template keywords. The
// Notecard automatically fills them from its onboard GPS and RTC each time a
// note is stored — the host does NOT set these fields in note.add requests.
// Type sentinels: TFLOAT32 == 14.1 (4-byte float) as documented at
// https://dev.blues.io/notecard/notecard-walkthrough/low-bandwidth-design/
// ---------------------------------------------------------------------------
bool defineTemplates(Notecard &notecard) {
    bool allOk = true;

    // Hourly telemetry — compact format required for Skylo 256-byte limit
    bool tmplOk = false;
    for (int attempt = 0; attempt < 3 && !tmplOk; attempt++) {
        if (attempt > 0) delay(500);
        J *req = notecard.newRequest("note.template");
        JAddStringToObject(req, "file",   NOTEFILE_TELEMETRY);
        JAddNumberToObject(req, "port",   50);
        JAddStringToObject(req, "format", "compact");
        J *body = JAddObjectToObject(req, "body");
        if (body) {
            JAddNumberToObject(body, "state",           TUINT8);
            JAddNumberToObject(body, "apu_runtime_min", TUINT16);
            JAddNumberToObject(body, "idle_time_min",   TUINT16);
            JAddNumberToObject(body, "fuel_saved_gal",  TFLOAT32);
            JAddNumberToObject(body, "fuel_used_gal",   TFLOAT32);
            JAddNumberToObject(body, "amb_temp_f",      TFLOAT32);
            JAddNumberToObject(body, "cab_temp_f",      TFLOAT32);
            JAddNumberToObject(body, "dc_volts",        TFLOAT32);
            JAddNumberToObject(body, "output_watts",    TUINT16);
            // power_valid = 1 when dc_volts/output_watts are real averages;
            // = 0 when no valid Modbus samples were collected in the window
            // (dc_volts = -1.0, output_watts = 0xFFFF in that case). [5]
            JAddNumberToObject(body, "power_valid",     TUINT8);
            JAddNumberToObject(body, "fault_count",          TUINT16); // matches uint16_t in AppState [10]
            JAddNumberToObject(body, "last_fault",           TUINT16);
            JAddNumberToObject(body, "controller_runtime_hr",TFLOAT32);
            // Reserved compact-template keywords — auto-filled by the Notecard
            // from its GPS fix and RTC when the note is stored. Host leaves these
            // unset in note.add; the Notecard populates them automatically.
            JAddNumberToObject(body, "_lat",  TFLOAT32);
            JAddNumberToObject(body, "_lon",  TFLOAT32);
            JAddNumberToObject(body, "_time", TUINT32);
        }
        tmplOk = ncSend(notecard, req);
    }
    if (!tmplOk) {
        Serial.println("[tmpl] ERR: telemetry template failed after retries"
                       " — compact format NOT active");
        allOk = false;
    }

    // Fault / alert events — compact for satellite-safe delivery
    tmplOk = false;
    for (int attempt = 0; attempt < 3 && !tmplOk; attempt++) {
        if (attempt > 0) delay(500);
        J *req = notecard.newRequest("note.template");
        JAddStringToObject(req, "file",   NOTEFILE_EVENT);
        JAddNumberToObject(req, "port",   51);
        JAddStringToObject(req, "format", "compact");
        J *body = JAddObjectToObject(req, "body");
        if (body) {
            JAddStringToObject(body, "event",        "16");  // max 16 char string
            JAddNumberToObject(body, "fault_code",   TUINT16);
            JAddNumberToObject(body, "state",        TUINT8);
            JAddNumberToObject(body, "amb_temp_f",   TFLOAT32);
            JAddNumberToObject(body, "cab_temp_f",   TFLOAT32);
            JAddNumberToObject(body, "dc_volts",     TFLOAT32);
            JAddNumberToObject(body, "output_watts", TUINT16);
            // Reserved compact-template keywords — auto-filled by the Notecard
            JAddNumberToObject(body, "_lat",  TFLOAT32);
            JAddNumberToObject(body, "_lon",  TFLOAT32);
            JAddNumberToObject(body, "_time", TUINT32);
        }
        tmplOk = ncSend(notecard, req);
    }
    if (!tmplOk) {
        Serial.println("[tmpl] ERR: event template failed after retries"
                       " — compact format NOT active");
        allOk = false;
    }

    // Daily fuel-saved rollup — one note per calendar day
    tmplOk = false;
    for (int attempt = 0; attempt < 3 && !tmplOk; attempt++) {
        if (attempt > 0) delay(500);
        J *req = notecard.newRequest("note.template");
        JAddStringToObject(req, "file",   NOTEFILE_DAILY);
        JAddNumberToObject(req, "port",   52);
        JAddStringToObject(req, "format", "compact");
        J *body = JAddObjectToObject(req, "body");
        if (body) {
            JAddNumberToObject(body, "fuel_saved_gal",  TFLOAT32);
            JAddNumberToObject(body, "fuel_used_gal",   TFLOAT32);
            JAddNumberToObject(body, "apu_runtime_min", TUINT16);
            JAddNumberToObject(body, "idle_time_min",   TUINT16);
            JAddNumberToObject(body, "_lat",  TFLOAT32);
            JAddNumberToObject(body, "_lon",  TFLOAT32);
            JAddNumberToObject(body, "_time", TUINT32);
        }
        tmplOk = ncSend(notecard, req);
    }
    if (!tmplOk) {
        Serial.println("[tmpl] ERR: daily template failed after retries"
                       " — compact format NOT active");
        allOk = false;
    }

    return allOk;
}

// ---------------------------------------------------------------------------
// initEnvDefaults — seed env with compiled-in defaults.
// Call once on cold boot before fetchEnvOverrides. Defaults are NOT re-applied
// on every wake; fetchEnvOverrides preserves last-known-good config across
// transient Notecard / env.get failures.
// ---------------------------------------------------------------------------
void initEnvDefaults(EnvConfig &env) {
    env.sampleIntervalSec  = DEFAULT_SAMPLE_INTERVAL_SEC;
    env.summaryIntervalMin = DEFAULT_SUMMARY_INTERVAL_MIN;
    env.alertCooldownSec   = DEFAULT_ALERT_COOLDOWN_SEC;
    env.modbusSlaveId      = DEFAULT_MODBUS_SLAVE_ID;
    env.modbusBaud         = DEFAULT_MODBUS_BAUD;
    env.modbusRegBase      = DEFAULT_MODBUS_REG_BASE;
    env.apuFuelRateGph     = 0.5f;
    env.idleFuelRateGph    = 1.0f;
    env.cabTempHighF       = 85.0f;
    env.cabTempLowF        = 32.0f;
    memset(env.ambRomIdFromEnv, 0, sizeof(env.ambRomIdFromEnv));
    memset(env.cabRomIdFromEnv, 0, sizeof(env.cabRomIdFromEnv));
    env.romIdsFromEnv      = false;
}

// ---------------------------------------------------------------------------
// fetchEnvOverrides — pull environment variables from Notehub.
// On success, all config fields are updated and clamped to sane bounds.
// On any failure (NULL response, "err" field, or missing body) the function
// returns immediately without modifying env, preserving the last-known-good
// configuration so a transient Notecard or I2C error cannot silently revert
// a fielded unit to compiled-in defaults. [2]
// ---------------------------------------------------------------------------
void fetchEnvOverrides(Notecard &notecard, EnvConfig &env) {
    J *rsp = notecard.requestAndResponse(notecard.newRequest("env.get"));
    if (rsp == NULL) {
        Serial.println("[env] WARN: env.get returned no response"
                       " — keeping last-known-good config");
        return;
    }
    const char *fetchErr = JGetString(rsp, "err");
    if (fetchErr && fetchErr[0] != '\0') {
        Serial.print("[env] WARN: env.get error: ");
        Serial.println(fetchErr);
        notecard.deleteResponse(rsp);
        return;
    }
    J *body = JGetObject(rsp, "body");
    if (!body) {
        // No env vars have been configured in Notehub yet (normal on a freshly
        // provisioned device) — keep existing config untouched.
        notecard.deleteResponse(rsp);
        return;
    }
    // Confirmed successful response — update only fields present in the body.
    // Use JGetObjectItem to detect key presence so explicit zero values
    // (e.g., modbus_reg_base=0) can override defaults.  JGetNumber returning 0
    // is ambiguous — absent key and value=0 are indistinguishable without the
    // presence check.
#define ENVF(key, field) do { if (JGetObjectItem(body, key) != NULL) env.field = (float)JGetNumber(body, key); } while(0)
#define ENVL(key, field) do { if (JGetObjectItem(body, key) != NULL) env.field = (uint32_t)JGetNumber(body, key); } while(0)
    ENVL("sample_interval_sec",   sampleIntervalSec);
    ENVL("summary_interval_min",  summaryIntervalMin);
    ENVL("alert_cooldown_sec",    alertCooldownSec);
    ENVL("modbus_slave_id",       modbusSlaveId);
    ENVL("modbus_baud",           modbusBaud);
    // modbus_reg_base is uint16_t — read raw to catch out-of-range values before
    // truncation silently wraps a negative or oversized Notehub value into an
    // unintended register address. [8]
    if (JGetObjectItem(body, "modbus_reg_base") != NULL) {
        double rawReg = JGetNumber(body, "modbus_reg_base");
        if (rawReg < 0.0 || rawReg > 65535.0) {
            Serial.print("[env] WARN: modbus_reg_base ");
            Serial.print((int32_t)rawReg);
            Serial.println(" out of [0..65535]; keeping current value");
        } else {
            env.modbusRegBase = (uint16_t)rawReg;
        }
    }
    ENVF("apu_fuel_rate_gph",     apuFuelRateGph);
    ENVF("idle_fuel_rate_gph",    idleFuelRateGph);
    ENVF("cab_temp_high_f",       cabTempHighF);
    ENVF("cab_temp_low_f",        cabTempLowF);
#undef ENVF
#undef ENVL

    // DS18B20 probe role assignment via operator-provisioned ROM IDs.
    // amb_rom_id and cab_rom_id are 16-char hex strings (upper or lower case)
    // matching the 64-bit OneWire ROM address printed on first commissioning.
    // Both must be present and parseable for romIdsFromEnv to be set true;
    // a partial or malformed pair is rejected without modifying env state.
    {
        const char *ambHex = JGetString(body, "amb_rom_id");
        const char *cabHex = JGetString(body, "cab_rom_id");
        if (ambHex && ambHex[0] != '\0' && cabHex && cabHex[0] != '\0') {
            uint8_t parsedAmb[8], parsedCab[8];
            if (parseRomIdHex(ambHex, parsedAmb) && parseRomIdHex(cabHex, parsedCab)) {
                memcpy(env.ambRomIdFromEnv, parsedAmb, 8);
                memcpy(env.cabRomIdFromEnv, parsedCab, 8);
                env.romIdsFromEnv = true;
            } else {
                Serial.println("[env] WARN: amb_rom_id/cab_rom_id malformed"
                               " — must be 16 hex chars each; keeping current commissioning");
            }
        } else if ((ambHex && ambHex[0] != '\0') || (cabHex && cabHex[0] != '\0')) {
            // Only one of the two is set — incomplete pair, do not partially apply.
            Serial.println("[env] WARN: only one of amb_rom_id/cab_rom_id is set;"
                           " both required for deterministic commissioning");
        }
        // If neither is set, leave romIdsFromEnv as-is (preserves a previously
        // parsed pair across wakes where the body key is simply absent).
    }

    notecard.deleteResponse(rsp);

    // ------------------------------------------------------------------
    // Clamp all env vars to sane operating bounds. [4]
    // ------------------------------------------------------------------
    env.sampleIntervalSec  = constrain(env.sampleIntervalSec,  10UL,   3600UL);
    env.summaryIntervalMin = constrain(env.summaryIntervalMin, 1UL,    1440UL);
    env.alertCooldownSec   = constrain(env.alertCooldownSec,   60UL,   86400UL);
    env.modbusSlaveId      = constrain(env.modbusSlaveId,
                                       (uint8_t)1, (uint8_t)247);
    env.cabTempHighF       = constrain(env.cabTempHighF, -40.0f, 185.0f);
    env.cabTempLowF        = constrain(env.cabTempLowF,  -40.0f, 185.0f);

    // Modbus baud must be a standard RS-485 rate supported by the STM32 UART.
    // modbusReadHolding() derives its timeout from the configured baud rate, so
    // all listed speeds — including 1200 and 2400 — are correctly handled.
    static const uint32_t kValidBauds[] = {
        1200, 2400, 4800, 9600, 19200, 38400, 57600, 115200
    };
    bool baudOk = false;
    for (uint8_t i = 0; i < sizeof(kValidBauds)/sizeof(kValidBauds[0]); i++) {
        if (env.modbusBaud == kValidBauds[i]) { baudOk = true; break; }
    }
    if (!baudOk) {
        Serial.print("[env] WARN: invalid modbus_baud ");
        Serial.print(env.modbusBaud);
        Serial.println("; reverting to default");
        env.modbusBaud = DEFAULT_MODBUS_BAUD;
    }

    // APU fuel rate must be positive; idle rate must be >= APU rate to keep
    // fuelSavedGal non-negative
    if (env.apuFuelRateGph <= 0.0f) {
        Serial.println("[env] WARN: apu_fuel_rate_gph must be > 0; reverting to 0.5");
        env.apuFuelRateGph = 0.5f;
    }
    if (env.idleFuelRateGph < env.apuFuelRateGph) {
        Serial.println("[env] WARN: idle_fuel_rate_gph < apu_fuel_rate_gph;"
                       " clamping to apu rate");
        env.idleFuelRateGph = env.apuFuelRateGph;
    }

    // Temperature window must be non-inverted
    if (env.cabTempHighF <= env.cabTempLowF) {
        Serial.println("[env] WARN: cab_temp_high_f <= cab_temp_low_f;"
                       " reverting to defaults");
        env.cabTempHighF = 85.0f;
        env.cabTempLowF  = 32.0f;
    }

    // Summary window must span at least one sample period; otherwise
    // samplesPerSummary evaluates to 0 and every wake triggers a summary
    if (env.summaryIntervalMin * 60UL < env.sampleIntervalSec) {
        Serial.println("[env] WARN: summary window shorter than sample interval;"
                       " adjusting summary_interval_min");
        env.summaryIntervalMin = (env.sampleIntervalSec + 59UL) / 60UL;
    }
}

// ---------------------------------------------------------------------------
// modbusCRC16 — standard Modbus CRC (polynomial 0xA001)
// ---------------------------------------------------------------------------
static uint16_t modbusCRC16(const uint8_t *data, uint8_t len) {
    uint16_t crc = 0xFFFF;
    for (uint8_t i = 0; i < len; i++) {
        crc ^= (uint16_t)data[i];
        for (uint8_t b = 0; b < 8; b++) {
            crc = (crc & 0x0001) ? ((crc >> 1) ^ 0xA001) : (crc >> 1);
        }
    }
    return crc;
}

// ---------------------------------------------------------------------------
// modbusReadHolding — read `count` holding registers from RS-485 slave.
// Half-duplex direction controlled by RS485_DE_PIN.
// baud is the current serial baud rate; the response timeout is derived from
// it so the function remains correct at all supported speeds (including
// 1200 and 2400 baud, where a fixed 200 ms budget would be insufficient).
// Returns true on success; results[] receives decoded 16-bit register values.
// ---------------------------------------------------------------------------
bool modbusReadHolding(uint8_t slaveId, uint16_t startAddr,
                       uint8_t count, uint16_t *results, uint32_t baud) {
    // Drain stale RX bytes before transmitting. On a noisy or partially
    // buffered RS-485 link, leftover bytes from a previous transaction can
    // corrupt the response frame of the next one. [8]
    while (RS485_SERIAL.available()) RS485_SERIAL.read();

    // Build Modbus RTU request frame
    uint8_t req[8];
    req[0] = slaveId;
    req[1] = 0x03;  // Read Holding Registers function code
    req[2] = (startAddr >> 8) & 0xFF;
    req[3] = startAddr & 0xFF;
    req[4] = 0;
    req[5] = count;
    uint16_t crc = modbusCRC16(req, 6);
    req[6] = crc & 0xFF;
    req[7] = (crc >> 8) & 0xFF;

    // Assert DE for transmit, flush, then release to receive
    digitalWrite(RS485_DE_PIN, HIGH);
    delayMicroseconds(500);
    RS485_SERIAL.write(req, 8);
    RS485_SERIAL.flush();
    delayMicroseconds(500);
    digitalWrite(RS485_DE_PIN, LOW);

    // Collect response: slaveId + fn + byteCount + (count×2 data bytes) + 2 CRC
    uint8_t expectedLen = 5 + count * 2;
    uint8_t rsp[32];
    uint8_t idx = 0;
    // Derive response timeout from baud rate and expected frame length.
    // Response = (5 + count×2) bytes × 11 bits/char, plus 3.5-char inter-frame
    // gap (38 bits); apply 1.5× margin, floor at 50 ms for slave processing.
    // At 1200 baud / 5 registers: ~253 ms.  At 19200+: 50 ms floor applies.
    uint32_t responseBits = (uint32_t)(5u + (uint32_t)count * 2u) * 11u + 38u;
    uint32_t timeoutMs    = max(responseBits * 1500UL / baud, 50UL);
    unsigned long deadline = millis() + timeoutMs;
    while (millis() < deadline && idx < expectedLen) {
        if (RS485_SERIAL.available()) rsp[idx++] = RS485_SERIAL.read();
    }
    if (idx < expectedLen) return false;

    // Validate CRC
    uint16_t recvCRC = (uint16_t)rsp[expectedLen - 2] |
                       ((uint16_t)rsp[expectedLen - 1] << 8);
    if (modbusCRC16(rsp, expectedLen - 2) != recvCRC) return false;

    // Validate slave ID, function code, and byte count to reject garbage
    // frames that happen to pass the CRC check. [8]
    if (rsp[0] != slaveId)              return false;
    if (rsp[1] != 0x03)                 return false;
    if (rsp[2] != (uint8_t)(count * 2)) return false;

    for (uint8_t i = 0; i < count; i++) {
        results[i] = ((uint16_t)rsp[3 + i * 2] << 8) | rsp[4 + i * 2];
    }
    return true;
}

// ---------------------------------------------------------------------------
// readTemperatures — DS18B20 conversion and independent per-probe read.
// ambFound / cabFound indicate whether each probe's ROM address was located on
// the OneWire bus this wake.  Each probe is evaluated in isolation: an absent
// or failed ambient probe does NOT suppress the cab validity flag, ensuring
// cab_overheat / cab_freeze alerts remain active regardless of ambient state.
// requestTemperatures() is only called when at least one probe is present.
// *ambOk / *cabOk are set true only when the respective probe returns a valid
// reading; *ambF / *cabF are set to -999.0f (sentinel) on failure or absence.
// [6]
void readTemperatures(DallasTemperature &ds, bool ambFound, bool cabFound,
                      DeviceAddress ambAddr, DeviceAddress cabAddr,
                      float *ambF, float *cabF, bool *ambOk, bool *cabOk) {
    *ambF = *cabF = -999.0f;
    *ambOk = *cabOk = false;
    if (!ambFound && !cabFound) return;   // nothing on bus; skip conversion
    ds.requestTemperatures();
    if (ambFound) {
        float ambC = ds.getTempC(ambAddr);
        if (ambC != DEVICE_DISCONNECTED_C) {
            *ambF  = ambC * 9.0f / 5.0f + 32.0f;
            *ambOk = true;
        }
    }
    if (cabFound) {
        float cabC = ds.getTempC(cabAddr);
        if (cabC != DEVICE_DISCONNECTED_C) {
            *cabF  = cabC * 9.0f / 5.0f + 32.0f;
            *cabOk = true;
        }
    }
}

// ---------------------------------------------------------------------------
// sendAlert — queues an immediate apu_event.qo with sync:true.
// Returns true on confirmed note.add success; false on any Notecard error.
// Callers gate cooldown-timestamp and failure-counter updates on the return
// value so a transient I2C error does not suppress retry attempts for the
// full cooldown window. [2]
// ---------------------------------------------------------------------------
bool sendAlert(Notecard &notecard, const AppState &state,
               const char *event, uint16_t faultCode,
               float ambF, float cabF, float dcVolts, uint16_t watts) {
    J *req = notecard.newRequest("note.add");
    if (req == NULL) return false;
    JAddStringToObject(req, "file", NOTEFILE_EVENT);
    JAddBoolToObject(req,   "sync", true);
    J *body = JAddObjectToObject(req, "body");
    if (body) {
        JAddStringToObject(body, "event",        event);
        JAddNumberToObject(body, "fault_code",   faultCode);
        JAddNumberToObject(body, "state",        state.state);
        JAddNumberToObject(body, "amb_temp_f",   ambF);
        JAddNumberToObject(body, "cab_temp_f",   cabF);
        JAddNumberToObject(body, "dc_volts",     dcVolts);
        JAddNumberToObject(body, "output_watts", watts);
    }
    bool ok = ncSend(notecard, req);
    if (!ok) Serial.println("[alert] ERR: note.add failed");
    return ok;
}

// ---------------------------------------------------------------------------
// sendSummary — emit hourly apu_telemetry.qo and reset window accumulators.
// Accumulators are cleared ONLY after a confirmed successful note.add so that
// a communication failure does not silently discard a full hour of data. [2]
// ---------------------------------------------------------------------------
void sendSummary(Notecard &notecard, AppState &state, const EnvConfig &env) {
    uint16_t apuMin   = (uint16_t)(state.apuRuntimeSec / 60);
    uint16_t idleMin  = (uint16_t)(state.idleTimeSec   / 60);
    // Ambient and cab probes are tracked with independent sample counters so that
    // a failed ambient probe does not zero out a valid cab average. [6]
    float    avgAmbF  = state.ambTempSamples ? state.ambTempAccF / state.ambTempSamples : -999.0f;
    float    avgCabF  = state.cabTempSamples ? state.cabTempAccF / state.cabTempSamples : -999.0f;
    // When no valid Modbus power samples were collected in the window, use explicit
    // sentinels so missing data is distinguishable from a legitimate zero reading.
    // Consumers must check power_valid=0 before interpreting dc_volts/output_watts. [5]
    bool     powerOk  = (state.powerSamples > 0);
    float    avgVolts = powerOk
                        ? (state.dcVoltsX10Acc / state.powerSamples) / 10.0f : -1.0f;
    uint16_t avgWatts = powerOk
                        ? (uint16_t)(state.wattsAcc / state.powerSamples) : 0xFFFFu;

    J *req = notecard.newRequest("note.add");
    if (req == NULL) {
        Serial.println("[summary] ERR: newRequest returned NULL; accumulators preserved");
        return;
    }
    JAddStringToObject(req, "file", NOTEFILE_TELEMETRY);
    J *body = JAddObjectToObject(req, "body");
    if (body) {
        JAddNumberToObject(body, "state",           state.state);
        JAddNumberToObject(body, "apu_runtime_min", apuMin);
        JAddNumberToObject(body, "idle_time_min",   idleMin);
        JAddNumberToObject(body, "fuel_saved_gal",  state.fuelSavedGal);
        JAddNumberToObject(body, "fuel_used_gal",   state.fuelUsedGal);
        JAddNumberToObject(body, "amb_temp_f",      avgAmbF);
        JAddNumberToObject(body, "cab_temp_f",      avgCabF);
        JAddNumberToObject(body, "dc_volts",        avgVolts);
        JAddNumberToObject(body, "output_watts",    avgWatts);
        JAddNumberToObject(body, "power_valid",     powerOk ? 1 : 0);
        JAddNumberToObject(body, "fault_count",           state.faultCount);
        JAddNumberToObject(body, "last_fault",            state.lastFaultCode);
        JAddNumberToObject(body, "controller_runtime_hr", state.lastControllerRuntimeHr);
    }

    if (!ncSend(notecard, req)) {
        // note.add failed — do NOT clear accumulators. They will be included
        // in the next summary attempt so no window data is silently dropped.
        Serial.println("[summary] ERR: note.add failed; accumulators preserved");
        return;
    }

    // Only reset window accumulators after confirmed successful delivery.
    // Preserve state machine state, alert timestamps, and fault tracking.
    state.apuRuntimeSec  = 0;
    state.idleTimeSec    = 0;
    state.fuelSavedGal   = 0.0f;
    state.fuelUsedGal    = 0.0f;
    state.cabTempAccF    = 0.0f;
    state.ambTempAccF    = 0.0f;
    state.ambTempSamples = 0;
    state.cabTempSamples = 0;
    state.dcVoltsX10Acc  = 0;
    state.wattsAcc       = 0;
    state.powerSamples   = 0;
    state.faultCount     = 0;
    state.lastFaultCode  = 0;
    state.samplesInWindow = 0;
}

// ---------------------------------------------------------------------------
// sendDailySummary — emit apu_daily.qo with the day's accumulated totals.
// Returns true on confirmed note.add success and resets daily accumulators.
// On failure, accumulators are preserved so the note is retried on the next
// calendar-day boundary check rather than silently dropped. [2]
// ---------------------------------------------------------------------------
bool sendDailySummary(Notecard &notecard, AppState &state) {
    uint16_t apuMin  = (uint16_t)(state.dailyApuRuntimeSec / 60);
    uint16_t idleMin = (uint16_t)(state.dailyIdleTimeSec   / 60);

    J *req = notecard.newRequest("note.add");
    if (req == NULL) {
        Serial.println("[daily] ERR: newRequest returned NULL; daily totals preserved");
        return false;
    }
    JAddStringToObject(req, "file", NOTEFILE_DAILY);
    J *body = JAddObjectToObject(req, "body");
    if (body) {
        JAddNumberToObject(body, "fuel_saved_gal",  state.dailyFuelSavedGal);
        JAddNumberToObject(body, "fuel_used_gal",   state.dailyFuelUsedGal);
        JAddNumberToObject(body, "apu_runtime_min", apuMin);
        JAddNumberToObject(body, "idle_time_min",   idleMin);
    }

    if (!ncSend(notecard, req)) {
        Serial.println("[daily] ERR: note.add failed; daily totals preserved for retry");
        return false;
    }

    // Reset daily accumulators only after confirmed success.
    state.dailyFuelSavedGal  = 0.0f;
    state.dailyFuelUsedGal   = 0.0f;
    state.dailyApuRuntimeSec = 0;
    state.dailyIdleTimeSec   = 0;
    Serial.println("[daily] rollup sent — daily accumulators reset");
    return true;
}
