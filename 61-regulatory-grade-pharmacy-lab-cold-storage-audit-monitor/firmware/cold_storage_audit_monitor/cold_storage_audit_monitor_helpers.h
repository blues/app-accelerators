/*
  cold_storage_audit_monitor_helpers.h

  Shared definitions and declarations for the cold-storage audit monitor.
  Included by both cold_storage_audit_monitor.ino and
  cold_storage_audit_monitor_helpers.cpp.

  Production temperature path: Adafruit Platinum RTD Sensor PT1000 3-Wire
  (Product 3984) wired to an Adafruit MAX31865 PT1000 Amplifier (Product 3648)
  via SPI on the Notecarrier CX dual 16-pin header.  The MAX31865 is addressed
  with hardware SPI; D10 is the chip-select.

  Bench substitute: SparkFun TMP117 breakout (SEN-15805) connected via Qwiic.
  Swapping to the TMP117 bench path requires replacing the Adafruit_MAX31865
  include and global with a TMP117 object and updating readTemperatureC() in
  cold_storage_audit_monitor_helpers.cpp.  The production MAX31865 path is the
  default in this build.
*/
#pragma once

#include <Notecard.h>
#include <Adafruit_MAX31865.h>
#include <Adafruit_VEML7700.h>
#include <Wire.h>

// Set to 1 for bench bring-up; 0 for deployment (Serial off, no Notecard debug stream).
#ifndef ENABLE_DEBUG
#define ENABLE_DEBUG 0
#endif

// Notefile names
#define NOTEFILE_READING "storage_reading.qo"
#define NOTEFILE_ALERT "storage_alert.qo"

// Reed switch: Normally Open, terminal A → D5, terminal B → GND; INPUT_PULLUP.
// LOW = door closed (magnet closes contacts); HIGH = door open.
#define DOOR_SWITCH_PIN D5

// MAX31865 SPI chip-select pin on the Notecarrier CX dual 16-pin header.
// Uses hardware SPI (SCK/MOSI/MISO on the header); only CS is software-selectable.
// See §4 Wiring for the MOSI/MISO label-swap note specific to Notecarrier CX.
#define MAX31865_CS_PIN D10

// Float-field sentinels written when the corresponding sensor has no valid data.
// Downstream parsers must use the correct sentinel per field:
//   temp_c → -9999 (distinguishes MAX31865/probe failure from a near-zero reading)
//   lux    →    -1 (lux is always ≥ 0 in normal operation; -1 is unambiguously a fault)
#define SENTINEL_NO_DATA -9999.0f
#define SENTINEL_LUX_NO_DATA -1.0f

// Notecard NotePayload segment identifier
#define STATE_SEG_ID "STOR"

// Capacity (including '\0') of the pending alert-type string.
// "sensor_disagreement" is the longest type (19 chars); 24 provides headroom.
#define PENDING_ALERT_TYPE_LEN 24

// State magic + version guard. The high 16 bits (0xC5A0) are a fixed sentinel
// (Cold Storage Audit); the low 16 bits are the schema version counter.
// Increment the low 16 bits whenever any of the following change:
//   • AppState field layout
//   • PRODUCT_UID compile-time constant
//   • OUTBOUND_INTERVAL_MIN
//   • note.template schema (fields or types)
// A mismatch on restore forces full re-initialisation, clearing
// notecard_configured and templates_defined so the updated config is applied.
#define STATE_MAGIC_VERSION 0xC5A00003UL

// Capacity of the persisted pending-note ring buffers (readings and alerts).
// 4 slots covers four consecutive Notecard-unreachable wakes before the oldest
// unsent entry is overwritten. This is a deliberate design trade-off: the
// dropped_readings / dropped_alerts counters embedded in every successfully
// sent reading Note make any lost entries observable in Notehub. Increase
// PENDING_RING_CAP if the deployment environment expects longer connectivity
// gaps; the AppState struct (and therefore flash usage) grows proportionally.
#define PENDING_RING_CAP 4

// Bitmask flags for tracking which alert types have an undelivered entry in the
// pending_alerts ring. Used to suppress duplicate stashing while a send-failed
// alert of the same type is already queued for retry.
#define ALERT_BIT_TEMP_HIGH 0x01u
#define ALERT_BIT_TEMP_LOW 0x02u
#define ALERT_BIT_DOOR_TIMEOUT 0x04u
#define ALERT_BIT_SENSOR_DISAGREE 0x08u

// ---------------------------------------------------------------------------
// Pending-note element types
// ---------------------------------------------------------------------------

struct PendingReading
{
    float temp_c;
    float lux;
    bool door_open;
    uint32_t door_open_sec;
    bool time_valid;
    uint32_t sample_epoch; // UTC epoch at sample time; 0 if time was not yet synced
};

// A pending alert preserves the original event epoch so a retried Note can
// include it in the body for audit lineage. note.add has no timestamp-override
// field, so the Notecard stamps a retried Note with retry time in the envelope;
// event_epoch carries the authoritative original trigger time.
struct PendingAlert
{
    char type[PENDING_ALERT_TYPE_LEN];
    float temp_c;
    float lux;
    bool door_open;
    uint32_t door_open_sec;
    bool time_valid;
    uint32_t epoch; // UTC epoch when the alert condition was first detected
};

// ---------------------------------------------------------------------------
// Application state — serialised into Notecard flash across sleep cycles via
// NotePayloadSaveAndSleep / NotePayloadRetrieveAfterSleep.
// ---------------------------------------------------------------------------
struct AppState
{
    // Must be first: validated immediately on restore. A mismatch (firmware
    // update, PRODUCT_UID change, schema change) triggers full re-init so
    // notecard_configured and templates_defined are reapplied.
    uint32_t magic_version;

    // Door open timing. 0 means closed or pre-time-sync (see runSampleCycle).
    uint32_t door_open_since;

    // Per-type cooldown timestamps. Each type maintains an independent timer
    // so a high-temp alert cannot suppress a simultaneous low-temp alert.
    // door_open_timeout uses the same alert_cooldown_sec window as the other
    // types, giving it consistent env-var semantics.
    uint32_t last_temp_high_alert_time;
    uint32_t last_temp_low_alert_time;
    uint32_t last_sensor_disagree_alert_time;
    uint32_t last_door_timeout_alert_time;

    // Runtime configuration — overwritten from env vars on every wake.
    float temp_high_c;
    float temp_low_c;
    uint32_t door_alert_min;
    uint32_t alert_cooldown_sec;
    uint32_t sample_interval_sec;
    float lux_threshold;

    // Configuration flags — retried each wake until the Notecard confirms
    // each step without error. Both are cleared on magic_version mismatch so
    // config/schema updates are always applied after a firmware change.
    bool notecard_configured;
    bool templates_defined;

    // Cumulative note.add failure counters. Included in every successfully
    // sent reading Note so losses are visible in Notehub; reset to 0 after
    // each successful send.
    uint32_t dropped_readings;
    uint32_t dropped_alerts;

    // Pending reading ring buffer. FIFO: pending_read_head is the next slot to
    // dequeue; pending_read_count is the number of valid entries (0..PENDING_RING_CAP).
    // When the ring is full the oldest entry is overwritten and dropped_readings
    // is incremented. Readings are buffered whenever a send fails or templates
    // are not yet confirmed; retries only proceed once templates_defined = true.
    PendingReading pending_reads[PENDING_RING_CAP];
    uint8_t pending_read_head;
    uint8_t pending_read_count;

    // Pending alert ring buffer. Same FIFO semantics as the reading buffer.
    // Each entry preserves the original event epoch for body-field audit lineage.
    // Alert cooldown timestamps are NOT advanced on a failed send, so the alert
    // remains eligible without waiting through a full cooldown window.
    PendingAlert pending_alerts[PENDING_RING_CAP];
    uint8_t pending_alert_head;
    uint8_t pending_alert_count;

    // Bitmask of ALERT_BIT_* flags. A set bit means at least one undelivered
    // entry of that alert type is currently in pending_alerts. The mask is
    // recomputed from scratch by scanning ring contents after every stash or
    // dequeue operation — never updated with OR-only writes, which would leave
    // stale bits when an entry is overwritten on ring-full or dequeued without
    // being sent. Only entries actually present in the live ring contribute
    // a set bit.
    uint8_t pending_alert_type_mask;
};

// ---------------------------------------------------------------------------
// Globals defined in the .ino; accessible to helpers.cpp via these externs.
// ---------------------------------------------------------------------------
extern Notecard notecard;
extern Adafruit_MAX31865 rtdAmp;
extern Adafruit_VEML7700 lightSensor;
extern AppState state;
extern bool tempSensorOk;
extern bool lightSensorOk;

// ---------------------------------------------------------------------------
// Prototypes for functions implemented in cold_storage_audit_monitor_helpers.cpp
// ---------------------------------------------------------------------------
void fetchEnvOverrides();

float readTemperatureC();
float readLightLux();
bool readDoorOpen();
uint32_t getEpochTime();

// Both return true when the Note was successfully enqueued by the Notecard,
// false when all retry attempts were exhausted.
//
// sendReading: on false, push the payload (including sample_epoch) into the
//   pending reading ring buffer (pending_reads) for retry at the start of
//   the next wake.
// sendAlert:   on false, the caller must NOT advance the cooldown timestamp
//   (so the alert remains eligible for retry without waiting a full window)
//   and should push the payload into the pending alert ring buffer.
//   event_epoch is the UTC epoch when the alert condition was first detected;
//   it is always included as a body field so audit lineage is preserved even
//   when the Note is retried and stamped with retry time in the envelope.
bool sendReading(float temp_c, float lux, bool door_open,
                 uint32_t door_open_sec, bool time_valid, uint32_t sample_epoch);
bool sendAlert(const char *alert_type, float temp_c, float lux,
               bool door_open, uint32_t door_open_sec, bool time_valid,
               uint32_t event_epoch);

void goToSleep();
