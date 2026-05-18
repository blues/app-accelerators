/***************************************************************************
  ble_parsers.h - BLE characteristic parsing helpers for post_discharge_vitals_hub

  Decodes the four Bluetooth SIG standard health measurement characteristics
  used by the vitals relay hub.  All functions are static inline; include
  this header once (from ble_central.cpp).

  Spec references (Bluetooth GATT Specification Supplement rev. 10):
    Weight Measurement        0x2A9D  §3.237
    Blood Pressure Measurement 0x2A35 §3.34
    PLX Spot-Check Measurement 0x2A5E §3.144
    Heart Rate Measurement    0x2A37  §3.104
***************************************************************************/

#pragma once

#include <stdint.h>
#include <math.h>

// ---------------------------------------------------------------------------
// IEEE 11073 SFLOAT decoder
//
// SFLOAT is a 16-bit format used in the Blood Pressure and PLX Spot-Check
// BLE health profiles (NOT used for Weight Measurement or Heart Rate —
// see per-characteristic notes below):
//   Bits [15:12]  — 4-bit signed exponent (two's complement)
//   Bits [11:0]   — 12-bit signed mantissa (two's complement)
//   Value = mantissa × 10^exponent
//
// Reserved sentinel values — the full 16-bit encoding is significant:
//   0x07FF = NaN    0x0800 = NaN
//   0x07FE = +Inf   0x0802 = -Inf   0x0801 = Reserved
// ---------------------------------------------------------------------------
static inline float parseSfloat(uint16_t raw) {
    // Compare the full 16-bit raw value against each reserved sentinel before
    // decoding mantissa/exponent.  Checking only the masked 12-bit mantissa
    // field would incorrectly reject valid encodings whose mantissa bits happen
    // to match a sentinel pattern but whose exponent is nonzero (e.g. 0x17FF
    // is a valid number with exponent=1, mantissa=2047, value=20470).
    if (raw == 0x07FFu || raw == 0x0800u ||
        raw == 0x07FEu || raw == 0x0802u ||
        raw == 0x0801u) {
        return NAN;
    }
    // Sign-extend mantissa: bits [11:0] → int16
    int16_t mantissa = (int16_t)(raw & 0x0FFFu);
    if (mantissa & 0x0800) {
        mantissa |= (int16_t)0xF000;   // propagate sign bit
    }
    // Exponent: sign-extend bits [15:12] to a full int8
    int8_t exponent = (int8_t)((raw >> 12) & 0x0F);
    if (exponent & 0x08) {
        exponent |= (int8_t)0xF0;      // propagate sign bit
    }
    return (float)mantissa * powf(10.0f, (float)exponent);
}

// ---------------------------------------------------------------------------
// Weight Measurement (0x2A9D)
//
// Per the Bluetooth GATT Specification Supplement, the weight field is a
// plain UINT16 — NOT SFLOAT.
//
// Byte layout:
//   [0]    Flags (uint8)  bit 0: 0=SI (kg), 1=Imperial (lb)
//   [1–2]  Weight (UINT16, little-endian)
//          If SI      : raw × 0.005  → value in kg  (resolution 5 g)
//          If Imperial: raw × 0.01   → value in lb  (then converted to kg)
//   [3+]   Optional timestamp / user ID / BMI (ignored here)
//
// 0xFFFF is the "measurement unsuccessful" sentinel defined in the spec.
// Returns weight in kg, or -1.0f on parse error / unsuccessful sentinel.
// ---------------------------------------------------------------------------
static inline float parseWeightKg(const uint8_t *data, uint16_t len) {
    if (len < 3) return -1.0f;
    uint8_t  flags = data[0];
    uint16_t raw   = (uint16_t)data[1] | ((uint16_t)data[2] << 8);
    if (raw == 0xFFFF) return -1.0f;  // measurement unsuccessful sentinel
    if (flags & 0x01) {
        // Imperial (lb) with 0.01 lb resolution — convert to kg
        float lb = raw * 0.01f;
        if (lb <= 0.0f) return -1.0f;
        return lb * 0.453592f;
    }
    // SI (kg) with 0.005 kg (5 g) resolution
    float kg = raw * 0.005f;
    return (kg > 0.0f) ? kg : -1.0f;
}

// ---------------------------------------------------------------------------
// Blood Pressure Measurement (0x2A35)
//
// Byte layout (GATT Specification Supplement §3.34):
//   [0]    Flags (uint8)
//          bit 0: 0=mmHg, 1=kPa
//          bit 1: Timestamp present (7 bytes)
//          bit 2: Pulse Rate present (SFLOAT, 2 bytes)
//          bit 3: User ID present (uint8, 1 byte)
//          bit 4: Measurement Status present (uint16, 2 bytes)
//   [1–2]  Systolic     (SFLOAT, little-endian)
//   [3–4]  Diastolic    (SFLOAT, little-endian)
//   [5–6]  Mean Arterial Pressure (SFLOAT, little-endian — not used)
//   [7+]   Optional fields in spec order:
//            Timestamp           (7 bytes)  — present if flags bit 1
//            Pulse Rate (SFLOAT, 2 bytes)   — present if flags bit 2
//            User ID    (uint8,  1 byte)    — present if flags bit 3
//            Measurement Status (uint16, 2 bytes) — present if flags bit 4
//
//   Pulse rate offset = 7 (mandatory header end)
//                     + 7 if timestamp present (flags bit 1)
//   i.e., byte [7] when no timestamp; byte [14] when timestamp present.
//
// Populates *systolic, *diastolic, and *pulse_bpm (0 if not present).
// All output in mmHg and bpm.
// ---------------------------------------------------------------------------
static inline void parseBpMmhg(const uint8_t *data, uint16_t len,
                                int16_t *systolic, int16_t *diastolic,
                                int16_t *pulse_bpm) {
    *systolic = 0; *diastolic = 0; *pulse_bpm = 0;
    if (len < 7) return;

    uint8_t flags = data[0];
    float sys = parseSfloat((uint16_t)data[1] | ((uint16_t)data[2] << 8));
    float dia = parseSfloat((uint16_t)data[3] | ((uint16_t)data[4] << 8));
    if (isnan(sys) || isnan(dia)) return;

    // Convert kPa → mmHg when needed (1 kPa = 7.50062 mmHg)
    if (flags & 0x01) { sys *= 7.50062f; dia *= 7.50062f; }

    *systolic  = (int16_t)roundf(sys);
    *diastolic = (int16_t)roundf(dia);

    // Walk optional fields that precede pulse rate in the spec ordering.
    // Only the timestamp (flags bit 1, 7 bytes) appears before pulse rate
    // (flags bit 2).  Start after the mandatory 7-byte header; add 7 if the
    // timestamp optional field is present.
    if (flags & 0x04) {  // pulse rate present (flags bit 2)
        uint16_t pr_offset = 7u;
        if (flags & 0x02) pr_offset += 7u;  // skip timestamp if present (flags bit 1)
        if (len >= (uint16_t)(pr_offset + 2u)) {
            float pr = parseSfloat((uint16_t)data[pr_offset] |
                                   ((uint16_t)data[pr_offset + 1u] << 8));
            if (!isnan(pr) && pr > 0.0f) {
                *pulse_bpm = (int16_t)roundf(pr);
            }
        }
    }
}

// ---------------------------------------------------------------------------
// PLX Spot-Check Measurement (0x2A5E)
//
// Byte layout (GATT Specification Supplement §3.144):
//   [0]    Flags (uint8)  (bit layout not needed here; optional fields ignored)
//   [1–2]  SpO2 (SFLOAT, little-endian, in %)
//   [3–4]  Pulse rate (SFLOAT, little-endian, in bpm)
//   [5+]   Optional timestamp / measurement status / device-sensor status /
//          pulse amplitude index (all ignored for the POC)
//
// Note: the Flags field is 1 byte (uint8), not 2.  SpO2 is therefore at
// bytes [1–2] and pulse rate at bytes [3–4].  A minimum valid frame is
// 5 bytes (1 Flags + 2 SpO2 SFLOAT + 2 pulse SFLOAT).
//
// Populates *spo2_pct and *pulse_bpm; sets to 0 on error.
// ---------------------------------------------------------------------------
static inline void parseSpO2(const uint8_t *data, uint16_t len,
                              int16_t *spo2_pct, int16_t *pulse_bpm) {
    *spo2_pct = 0; *pulse_bpm = 0;
    if (len < 5) return;

    float sp = parseSfloat((uint16_t)data[1] | ((uint16_t)data[2] << 8));
    float pr = parseSfloat((uint16_t)data[3] | ((uint16_t)data[4] << 8));

    if (!isnan(sp) && sp > 0.0f) *spo2_pct  = (int16_t)roundf(sp);
    if (!isnan(pr) && pr > 0.0f) *pulse_bpm = (int16_t)roundf(pr);
}

// ---------------------------------------------------------------------------
// Heart Rate Measurement (0x2A37)
//
// Byte layout:
//   [0]    Flags (uint8)
//          bit 0: 0=HR value is uint8, 1=HR value is uint16
//          bit 4: RR-interval data present (not used here)
//   [1]    Heart rate value (uint8) — if flags bit 0 = 0
//   [1–2]  Heart rate value (uint16, little-endian) — if flags bit 0 = 1
//
// Returns heart rate in bpm, or 0 on error.
// ---------------------------------------------------------------------------
static inline uint16_t parseHeartRate(const uint8_t *data, uint16_t len) {
    if (len < 2) return 0;
    uint8_t flags = data[0];
    if (flags & 0x01) {
        // 16-bit format
        if (len < 3) return 0;
        return (uint16_t)data[1] | ((uint16_t)data[2] << 8);
    }
    // 8-bit format (most common on consumer activity bands)
    return (uint16_t)data[1];
}

// ---------------------------------------------------------------------------
// Physiological plausibility guards
//
// Applied by the data callbacks before queuing a reading.  Values outside
// these ranges indicate a parser error, a noncompliant device, or a
// measurement-unsuccessful condition that the sentinel check didn't catch.
// The bounds are deliberately wide — they span the full human-survival range
// rather than the normal clinical range so they don't suppress valid readings
// at extremes; alert thresholds from env vars provide the clinical narrowing.
//
// References:
//   BP:   AHA/ACC clinical guidelines + documented survival extremes
//   SpO2: GATT spec minimum reportable value is 0%; 50% is never physiological
//   HR:   documented extremes (elite bradycardia ~20 bpm; VT/VF excluded)
//   Wt:   1 kg covers neonates; 500 kg is the documented patient-handling limit
// ---------------------------------------------------------------------------

// Returns true if parsed weight is within human-plausible range.
static inline bool weightKgPlausible(float kg) {
    return kg >= 1.0f && kg <= 500.0f;
}

// Returns true if parsed blood pressure values are within human-plausible
// range and internally consistent (diastolic strictly less than systolic).
// pulse_bpm == 0 is accepted (field absent per GATT flags).
static inline bool bpPlausible(int16_t sys, int16_t dia, int16_t pulse) {
    if (sys  < 50  || sys  > 260) return false;
    if (dia  < 30  || dia  > 160) return false;
    if (dia >= sys)                return false;  // impossible physiology
    if (pulse != 0 && (pulse < 20 || pulse > 300)) return false;
    return true;
}

// Returns true if parsed SpO2 and pulse rate are within human-plausible range.
// pulse_bpm == 0 is accepted (field absent per GATT flags).
static inline bool spo2Plausible(int16_t spo2, int16_t pulse) {
    if (spo2 < 50 || spo2 > 100) return false;
    if (pulse != 0 && (pulse < 20 || pulse > 300)) return false;
    return true;
}

// Returns true if parsed heart rate is within human-plausible range.
static inline bool hrPlausible(uint16_t hr) {
    return hr >= 20 && hr <= 300;
}
