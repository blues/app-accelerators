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
// parseVEDLong — strict integer parse for a VE.Direct field value.
//
// VE.Direct broadcasts the literal three-dash sentinel "---" when a field
// has no data (e.g. T="---" when no temperature sensor is connected;
// SOC="---" on an unsynchronised SmartShunt).  atol("---") silently returns
// 0, which would let a missing sensor masquerade as a real 0°C / 0 %
// reading, producing false alerts and corrupting averaged metrics.
//
// Returns true only when `value` parses cleanly as an integer with no
// trailing characters; on any other input (empty, "---", "n/a", "1.2",
// "12abc"), returns false and leaves *out unchanged.  Callers must skip
// the field assignment on a false return so the struct's sentinel default
// is preserved.
// -------------------------------------------------------------------------
static bool parseVEDLong(const char *value, long *out) {
    if (!value || !*value) return false;
    char *end = NULL;
    long v = strtol(value, &end, 10);
    if (end == value || end == NULL || *end != '\0') return false;
    *out = v;
    return true;
}

// -------------------------------------------------------------------------
// Internal: apply parsed label/value pairs to a VEDirectData struct.
//
// Every numeric field is parsed through parseVEDLong() so that VE.Direct's
// "---" no-data sentinel is rejected rather than silently coerced to 0.
// On parse failure the field assignment is skipped, leaving the sentinel
// defaults set by readVEDirectFrame() in place.
// -------------------------------------------------------------------------
static void applyField(const char *label, const char *value, VEDirectData &d) {
    long v;

    // --- Battery / SmartShunt fields ---
    if (strcmp(label, "V") == 0) {
        if (parseVEDLong(value, &v)) d.bat_v = v / 1000.0f;       // mV → V

    } else if (strcmp(label, "I") == 0) {
        if (parseVEDLong(value, &v)) d.bat_a = v / 1000.0f;       // mA → A (signed)

    } else if (strcmp(label, "P") == 0) {
        if (parseVEDLong(value, &v)) d.bat_w = (float)v;          // W (signed)

    } else if (strcmp(label, "SOC") == 0) {
        // SmartShunt broadcasts "---" until the first 100 % synchronisation.
        // On parse failure, soc_pct stays at its sentinel (-1.0f) so the
        // accumulator and alert evaluator skip the sample rather than treat
        // an uncalibrated shunt as a 0 % reading and fire a false soc_low.
        if (parseVEDLong(value, &v)) d.soc_pct = v / 10.0f;       // ‰ → %

    } else if (strcmp(label, "T") == 0) {
        // T="---" is broadcast when no temperature sensor is connected to
        // the SmartShunt.  On parse failure, bat_temp_c stays at the
        // no-sensor sentinel (-99.0f) so accumulate's > -50°C gate
        // correctly skips it and the summary emits SUMMARY_SENTINEL_F.
        if (parseVEDLong(value, &v)) d.bat_temp_c = (float)v;     // °C

    } else if (strcmp(label, "TTG") == 0) {
        // Spec value -1 means "not discharging / N/A"; some firmwares
        // emit "---" for the same condition.  Treat both identically by
        // skipping on parse failure (default is already -1).
        if (parseVEDLong(value, &v)) d.ttg_min = (int32_t)v;       // minutes

    // --- Solar / SmartSolar MPPT fields ---
    } else if (strcmp(label, "VPV") == 0) {
        if (parseVEDLong(value, &v)) d.pv_v = v / 1000.0f;        // mV → V

    } else if (strcmp(label, "PPV") == 0) {
        if (parseVEDLong(value, &v)) d.pv_w = (float)v;           // W

    } else if (strcmp(label, "CS") == 0) {
        if (parseVEDLong(value, &v)) d.cs = (int16_t)v;           // charge state (int16: codes 245-252 exceed int8 range)

    } else if (strcmp(label, "H20") == 0) {
        if (parseVEDLong(value, &v)) d.yield_kwh = v / 100.0f;    // 0.01 kWh units → kWh

    } else if (strcmp(label, "ERR") == 0) {
        if (parseVEDLong(value, &v)) d.err = (int8_t)v;
    }
    // All other labels (HSDS, H1-H19, H21, Alarm, Relay, etc.) are ignored.
}

// -------------------------------------------------------------------------
// readVEDirectFrame — public API
// -------------------------------------------------------------------------
bool readVEDirectFrame(Stream &serial, VEDirectData &out, uint32_t timeout_ms) {
    // Initialise output to sentinel defaults.  Each "may-be-absent" field is
    // initialised so that, after a successful frame parse, downstream code can
    // distinguish "field absent or broadcast as ---" from a real zero reading.
    memset(&out, 0, sizeof(out));
    out.bat_temp_c = -99.0f;   // no temp sensor attached
    out.ttg_min    = -1;       // not discharging / N/A
    out.soc_pct    = -1.0f;    // SmartShunt unsynchronised (SOC="---")

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
                out.soc_pct    = -1.0f;
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
