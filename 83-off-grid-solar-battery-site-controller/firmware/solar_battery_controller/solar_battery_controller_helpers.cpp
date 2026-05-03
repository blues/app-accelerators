/***************************************************************************
  solar_battery_controller_helpers - VE.Direct Parser for Off-Grid Solar
  Battery Site Controller

  Implements readVEDirectFrame() which listens on a UART stream and returns
  a parsed VEDirectData struct. Works with any Victron device that speaks
  the VE.Direct text protocol (SmartShunt, SmartSolar MPPT, BMV-7xx, etc.).

  VE.Direct frame format (broadcast, no polling required):
    <LABEL>\t<VALUE>\r\n   (one field per line)
    ...
    Checksum\t<byte>\r\n   (single byte, frame delimiter)

  The checksum byte makes the sum of all frame bytes (including the newline
  after Checksum) equal zero modulo 256. We verify the checksum here.
  A failed checksum discards the frame and waits for the next one.
***************************************************************************/

#include "solar_battery_controller_helpers.h"
#include <string.h>
#include <stdlib.h>

// -------------------------------------------------------------------------
// Internal: apply parsed label/value pairs to a VEDirectData struct.
// -------------------------------------------------------------------------
static void applyField(const char *label, const char *value, VEDirectData &d) {
    // --- Battery / SmartShunt fields ---
    if (strcmp(label, "V") == 0) {
        d.bat_v = atol(value) / 1000.0f;       // mV → V

    } else if (strcmp(label, "I") == 0) {
        d.bat_a = atol(value) / 1000.0f;       // mA → A (signed)

    } else if (strcmp(label, "P") == 0) {
        d.bat_w = (float)atol(value);           // W (signed)

    } else if (strcmp(label, "SOC") == 0) {
        d.soc_pct = atol(value) / 10.0f;       // ‰ → %

    } else if (strcmp(label, "T") == 0) {
        // Only present when a battery temp sensor is wired to the SmartShunt.
        d.bat_temp_c = (float)atol(value);     // °C

    } else if (strcmp(label, "TTG") == 0) {
        d.ttg_min = atol(value);                // minutes; -1 = not discharging

    // --- Solar / SmartSolar MPPT fields ---
    } else if (strcmp(label, "VPV") == 0) {
        d.pv_v = atol(value) / 1000.0f;        // mV → V

    } else if (strcmp(label, "PPV") == 0) {
        d.pv_w = (float)atol(value);           // W

    } else if (strcmp(label, "CS") == 0) {
        d.cs = (int16_t)atoi(value);           // charge state (int16: codes 245-252 exceed int8 range)

    } else if (strcmp(label, "H20") == 0) {
        d.yield_kwh = atol(value) / 100.0f;    // 0.01 kWh units → kWh

    } else if (strcmp(label, "ERR") == 0) {
        d.err = (int8_t)atoi(value);
    }
    // All other labels (HSDS, H1-H19, H21, Alarm, Relay, etc.) are ignored.
}

// -------------------------------------------------------------------------
// readVEDirectFrame — public API
// -------------------------------------------------------------------------
bool readVEDirectFrame(Stream &serial, VEDirectData &out, uint32_t timeout_ms) {
    // Initialise output to sentinel defaults.
    memset(&out, 0, sizeof(out));
    out.bat_temp_c = -99.0f;   // no temp sensor attached
    out.ttg_min    = -1;       // not discharging / N/A

    // Per-line parser state
    char   label[VED_FIELD_LEN] = {0};
    char   value[VED_FIELD_LEN] = {0};
    uint8_t lpos = 0, vpos = 0;
    bool   in_value = false;

    // Checksum accumulator across the whole frame.
    uint8_t checksum = 0;

    uint32_t deadline = millis() + timeout_ms;

    while (millis() < deadline) {
        if (!serial.available()) {
            delay(1);
            continue;
        }

        char c = (char)serial.read();
        checksum += (uint8_t)c;

        if (c == '\r') {
            continue;   // ignore CR; \n terminates the line
        }

        if (c == '\t') {
            // Tab: switch from label accumulation to value accumulation.
            label[lpos] = '\0';
            in_value = true;

            // Special case: the Checksum field value is a raw byte that may
            // equal '\t', '\r', or '\n'.  Reading it through the normal
            // delimiter logic would mis-parse an otherwise valid frame, so
            // we capture it directly and then consume the trailing CR/LF.
            if (strcmp(label, "Checksum") == 0) {
                while (millis() < deadline && !serial.available()) { delay(1); }
                if (!serial.available()) break;   // deadline hit before byte arrived
                checksum += (uint8_t)serial.read();
                // Consume trailing CR/LF bytes
                while (millis() < deadline) {
                    if (!serial.available()) { delay(1); continue; }
                    uint8_t trail = (uint8_t)serial.read();
                    checksum += trail;
                    if (trail == '\n') break;
                }
                if (checksum == 0) {
                    out.valid = true;
                    return true;
                }
                // Bad checksum — reset and wait for the next frame.
                memset(&out, 0, sizeof(out));
                out.bat_temp_c = -99.0f;
                out.ttg_min    = -1;
                checksum = 0;
                memset(label, 0, sizeof(label));
                memset(value, 0, sizeof(value));
                lpos = 0; vpos = 0; in_value = false;
            }
            continue;
        }

        if (c == '\n') {
            // End of a non-Checksum line.
            value[vpos] = '\0';
            applyField(label, value, out);

            // Reset line parser for the next field.
            memset(label, 0, sizeof(label));
            memset(value, 0, sizeof(value));
            lpos = 0; vpos = 0;
            in_value = false;
            continue;
        }

        // Accumulate character into label or value buffer.
        if (!in_value) {
            if (lpos < VED_FIELD_LEN - 1) {
                label[lpos++] = c;
            }
        } else {
            if (vpos < VED_FIELD_LEN - 1) {
                value[vpos++] = c;
            }
        }
    }

    // Timed out without a complete, valid frame.
    return false;
}
