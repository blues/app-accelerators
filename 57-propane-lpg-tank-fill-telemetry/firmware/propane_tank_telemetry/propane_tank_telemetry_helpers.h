// propane_tank_telemetry_helpers.h
//
// Sensor math, fill-level calculation, and consumption tracking for the
// propane / LPG tank fill telemetry sketch.
// Split into a header to keep the main .ino file within the 500-line target.

#pragma once
#include <Arduino.h>
#include <OneWire.h>
#include <DallasTemperature.h>

// -------- ADC constants (Cygnet STM32L4 12-bit ADC @ 3.3 V ref) --------
static const float    ADC_VREF_V     = 3.30f;
static const uint16_t ADC_COUNTS     = 4095;
static const float    SHUNT_OHM      = 120.0f;  // 4 mA → 0.48 V, 20 mA → 2.40 V; 24 mA fault → 2.88 V (within 3.3 V ADC range)

// Transmitter current fault limits (open loop / short circuit)
static const float    CURRENT_MIN_MA = 3.5f;
static const float    CURRENT_MAX_MA = 21.0f;

// Refill detection: fill increase larger than this (fraction of capacity)
// triggers consumption-accumulator reset.
static const float REFILL_DELTA_FRAC = 0.05f;   // 5 % of tank capacity

// EWMA smoothing factor for daily consumption rate estimate.
static const float CONSUMPTION_ALPHA = 0.30f;

// Minimum cumulative fill-level drop (as a fraction of tank capacity) that
// must accumulate from the current anchor before a consumption-rate sample is
// fed into the EWMA.  The anchor is held fixed until the cumulative drop since
// the last anchor update exceeds this threshold, so slow residential
// consumption (≈3–15 gal/day on a 500-gal tank yields ≈0.03–0.16 gal per
// 15-minute sample) builds up across many sample intervals before a rate is
// computed.  Advancing the anchor on every sub-deadband sample — as a
// per-sample deadband would — resets the measurement window each cycle and
// prevents the EWMA from ever being seeded.
static const float CONSUMPTION_MIN_DELTA_GAL_FRAC = 0.002f;

// Sentinel value used when a metric has no valid samples.
static const float INVALID_SENTINEL  = -9999.0f;

// -------- Sensor reading --------

// Read the 4-20 mA gauge-port level transmitter: average 16 ADC samples,
// convert to mA. Returns NAN if current is outside the valid 3.5–21 mA window.
static float readTransmitterMA(uint8_t pin) {
  uint32_t acc = 0;
  for (int i = 0; i < 16; i++) acc += analogRead(pin);
  float v  = (acc / 16.0f / (float)ADC_COUNTS) * ADC_VREF_V;
  float ma = (v / SHUNT_OHM) * 1000.0f;
  if (ma < CURRENT_MIN_MA || ma > CURRENT_MAX_MA) return NAN;
  return ma;
}

// Convert 4-20 mA float-type level transmitter current to fill percentage (0–100 %).
//
// Design note: the initial project specification called for an ultrasonic or
// pressure level sensor with temperature-based vapor-pressure compensation in
// the fill measurement path. This implementation uses a float-type transmitter
// instead; see the top-of-file sensor model comment and README §6 for the full
// rationale. Because the float tracks the physical liquid propane surface
// directly, its 4–20 mA output is already proportional to fill level regardless
// of liquid density or temperature. Temperature is therefore NOT passed to this
// function; no vapor-pressure or density correction is applied.
//
// The Rochester Sensors M6300-LP + R6315-12 outputs:
//   4 mA  → 0 %   fill  (sensor_empty_ma)
//  20 mA  → 100 % fill  (sensor_full_ma)
//
// A simple linear interpolation converts the transmitter current to fill %:
//   fill_pct = (current_ma − sensor_empty_ma) / span × 100
//
// Returns NAN if current_ma is NAN (transmitter fault).
static float computeFillPct(float current_ma,
                             float sensor_empty_ma, float sensor_full_ma) {
  if (isnan(current_ma)) return NAN;
  float span = sensor_full_ma - sensor_empty_ma;
  if (span < 0.1f) return NAN;
  float fill_pct = (current_ma - sensor_empty_ma) / span * 100.0f;
  return fmaxf(0.0f, fminf(100.0f, fill_pct));
}

// Convert fill percentage to gallons via a simple linear scale.
// computeFillPct returns the fill percentage directly from the float transmitter;
// computeFillGal scales by the tank's usable capacity:
//   fill_gal = (fill_pct / 100) × tank_capacity_gal
static float computeFillGal(float fill_pct, float tank_capacity_gal) {
  if (isnan(fill_pct)) return NAN;
  return (fill_pct / 100.0f) * tank_capacity_gal;
}

// Read DS18B20 via DallasTemperature. Returns °C, or NAN on disconnect/fault.
//
// Validity window follows the DS18B20 datasheet (−55 °C to +125 °C).
// DEVICE_DISCONNECTED_C (−127 °C in the DallasTemperature library) is returned
// when no sensor responds on the bus; the explicit check catches it before the
// range test so the intent is clear. The old 85 °C upper cutoff was artificially
// narrow — 85 °C is the DS18B20's power-on reset default, not a fault condition,
// and the sensor is rated to 125 °C.
static float readTemperatureC(DallasTemperature &sensor) {
  sensor.requestTemperatures();
  float t = sensor.getTempCByIndex(0);
  if (t <= DEVICE_DISCONNECTED_C || t < -55.0f || t > 125.0f) return NAN;
  return t;
}

// -------- Consumption tracking --------

struct ConsumptionState {
  float    prev_fill_gal;
  uint32_t prev_fill_epoch;
  float    gal_per_day;          // EWMA-smoothed daily consumption rate
  bool     seeded;               // true once at least one reading is recorded
};

// Update consumption estimate from a new fill-gal reading + current epoch.
// Returns true if the EWMA was updated; false if this was the seed reading,
// if a refill was detected and the anchor was reset, or if the cumulative
// drop since the current anchor has not yet reached the deadband threshold.
static bool updateConsumption(ConsumptionState &c, float curr_gal,
                               uint32_t curr_epoch, float tank_capacity_gal) {
  if (isnan(curr_gal) || curr_gal < 0.0f) return false;

  if (!c.seeded) {
    c.prev_fill_gal   = curr_gal;
    c.prev_fill_epoch = curr_epoch;
    c.seeded          = true;
    return false;
  }

  if (curr_epoch <= c.prev_fill_epoch) return false;

  float delta_gal = c.prev_fill_gal - curr_gal;  // positive = consumption
  float delta_day = (float)(curr_epoch - c.prev_fill_epoch) / 86400.0f;

  if (delta_gal < -(REFILL_DELTA_FRAC * tank_capacity_gal)) {
    // Fill increased by more than REFILL_DELTA_FRAC → delivery occurred.
    // Reset anchor to the post-delivery reading without touching the EWMA.
    c.prev_fill_gal   = curr_gal;
    c.prev_fill_epoch = curr_epoch;
    return false;
  }

  float min_delta_gal = CONSUMPTION_MIN_DELTA_GAL_FRAC * tank_capacity_gal;
  if (delta_gal > min_delta_gal && delta_day > 0.0f) {
    float rate = delta_gal / delta_day;
    if (c.gal_per_day < 0.01f) {
      c.gal_per_day = rate;  // cold-start: seed directly
    } else {
      c.gal_per_day = (1.0f - CONSUMPTION_ALPHA) * c.gal_per_day
                    +  CONSUMPTION_ALPHA          * rate;
    }
    c.prev_fill_gal   = curr_gal;
    c.prev_fill_epoch = curr_epoch;
    return true;
  }

  // Cumulative drop from the anchor has not yet reached the deadband threshold
  // (or the level nudged upward slightly due to ADC noise).  Hold the anchor
  // fixed so the next sample measures the continued cumulative drop from the
  // same baseline.  Advancing the anchor here would reset the measurement
  // window every cycle, preventing the EWMA from seeding on normal-use tanks
  // where each 15-minute sample only moves the level a fraction of a gallon.
  return false;
}

// Compute days-until-empty from current fill and EWMA consumption rate.
//
// Return value semantics:
//   INVALID_SENTINEL  — fill data is missing (NAN from a transmitter fault, or the
//                       INVALID_SENTINEL produced by safeAvg() when no valid samples
//                       were collected in the summary window), OR the consumption EWMA
//                       hasn't been seeded yet.
//   0.0               — fill_gal is a valid, confirmed zero (tank is truly empty).
//   positive float    — projected days remaining at the current consumption rate.
//
// The cutoff at -1.0 distinguishes the INVALID_SENTINEL (-9999) from a valid empty
// reading (0.0) or tiny negative noise values near zero.
static float daysUntilEmpty(float fill_gal, float gal_per_day) {
  if (isnan(fill_gal) || fill_gal < -1.0f) return INVALID_SENTINEL;  // missing data
  if (fill_gal <= 0.0f) return 0.0f;                                   // valid empty tank
  if (gal_per_day < 0.01f) return INVALID_SENTINEL;                    // no rate estimate yet
  return fminf(fill_gal / gal_per_day, 9999.0f);
}

// Safe average: returns INVALID_SENTINEL when no valid samples were collected.
static float safeAvg(float sum, uint32_t n) {
  return n > 0 ? (sum / (float)n) : INVALID_SENTINEL;
}
