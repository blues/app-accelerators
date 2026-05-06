/*
  cold_storage_audit_monitor.ino

  Pharmacy/lab cold-storage monitoring reference application.

  Production temperature path: Adafruit Platinum RTD Sensor PT1000 3-Wire
  1 m (Product 3984) wired to an Adafruit MAX31865 PT1000 Amplifier (Product
  3648) via hardware SPI on the Notecarrier CX dual 16-pin header (CS → D10).
  The stainless-steel probe tip routes into the refrigerated compartment;
  the MAX31865 board mounts inside the weatherproof electronics enclosure.
  Before regulatory deployment the probe assembly must be submitted to an
  accredited calibration laboratory for a NIST-traceable calibration
  certificate — see README §3 and §9.

  Bench substitute: SparkFun TMP117 Qwiic breakout (SEN-15805). Replacing the
  MAX31865 path with TMP117 requires swapping the Adafruit_MAX31865 include and
  global (below) for a TMP117 object and updating readTemperatureC() in
  cold_storage_audit_monitor_helpers.cpp. The TMP117 bench build does not
  measure compartment interior temperature and is not a deployable compliance
  instrument — see README §3.

  Wakes every 5 minutes, reads the PT1000 temperature probe via MAX31865,
  VEML7700 lux sensor (direct Qwiic connection, no TMP117 intermediate),
  and a magnetic door switch, then enqueues one timestamped reading Note in
  the Notecard's flash-backed queue. Four alert rules evaluate every sample:
  temperature-too-high excursion, temperature-too-low excursion, prolonged
  door-open, and light/switch sensor disagreement. Rule trips produce
  immediate-sync alert Notes; reading Notes accumulate and flush on the
  60-minute scheduled cellular outbound session.

  All thresholds are controlled via Notehub environment variables so
  operators can adjust limits from the cloud without reflashing firmware.

  Hardware (production build):
    Blues Notecarrier CX (Cygnet STM32 host MCU, onboard)
    Blues Notecard Cell+WiFi MBGLW (M.2 slot)
    Adafruit MAX31865 PT1000 Amplifier (Product 3648) -- SPI, CS on D10
    Adafruit Platinum RTD Sensor PT1000 3-Wire 1 m (Product 3984) -- probe tip
      routes into refrigerated compartment; submit for NIST calibration before
      regulatory deployment
    Adafruit VEML7700 Lux Sensor, STEMMA QT / Qwiic (Product 4162)
    SparkFun Magnetic Contact Switch (COM-13247)
*/

#include "cold_storage_audit_monitor_helpers.h"

// ---------------------------------------------------------------------------
// Product UID — paste the ProductUID from your Notehub project here.
// ---------------------------------------------------------------------------
#ifndef PRODUCT_UID
#define PRODUCT_UID ""
#pragma message "PRODUCT_UID is not defined — set it before flashing."
#endif

// ---------------------------------------------------------------------------
// Compile-time defaults (all overridable via Notehub environment variables)
// ---------------------------------------------------------------------------
#define SAMPLE_INTERVAL_SEC_DEFAULT   300    // 5 minutes between readings
#define OUTBOUND_INTERVAL_MIN         60     // Notecard cellular outbound cadence
// Bench defaults sized for the exterior-mounted sensors, which read room
// temperature (~20–25 °C). A bench unit at room temperature will produce zero
// temperature alerts with these values. For production refrigerated storage
// (USP Chapter 659: 2–8 °C) override both values in Notehub environment
// variables — no reflash required.
#define TEMP_HIGH_ALERT_C_DEFAULT     30.0f  // Bench default: flag if ambient > 30 °C
#define TEMP_LOW_ALERT_C_DEFAULT      15.0f  // Bench default: flag if ambient < 15 °C
#define DOOR_OPEN_ALERT_MIN_DEFAULT   10     // alert if door open > 10 consecutive min
#define ALERT_COOLDOWN_MIN_DEFAULT    30     // minimum minutes between same alert type
// Set to 120000.0 via Notehub to disable sensor_disagreement on lamp-free or
// always-on-lamp units without a firmware rebuild.
#define DOOR_LUX_THRESHOLD            5.0f

// ---------------------------------------------------------------------------
// Globals (extern declarations are in cold_storage_audit_monitor_helpers.h)
// ---------------------------------------------------------------------------
// rtdAmp uses hardware SPI; D10 is the chip-select pin.
Notecard          notecard;
Adafruit_MAX31865 rtdAmp(MAX31865_CS_PIN);
Adafruit_VEML7700 lightSensor;
AppState          state;
bool              tempSensorOk  = false;
bool              lightSensorOk = false;

// ---------------------------------------------------------------------------
// Forward declarations for functions defined in this file
// ---------------------------------------------------------------------------
bool notecardConfigure();
bool defineTemplates();
void runSampleCycle();
static void    stashPendingAlert(const char *type, float tc, float lx,
                                 bool door, uint32_t dsec, bool tv, uint32_t epoch);
static void    updateAlertCooldown(const char *type, uint32_t t);
static uint8_t alertTypeToBit(const char *type);
static uint8_t recomputeAlertTypeMask();

// ===========================================================================
// setup() — runs fresh on every wake from Notecard-controlled sleep
// ===========================================================================
void setup() {
    // I2C bus must be up before notecard.begin() and any NotePayload calls.
    // SPI is initialised internally by Adafruit_MAX31865::begin().
    Wire.begin();

#if ENABLE_DEBUG
    Serial.begin(115200);
    notecard.setDebugOutputStream(Serial);
#endif

    // Halt immediately when PRODUCT_UID was not set before flashing. An empty
    // UID means the Notecard will never connect to Notehub; letting the device
    // continue silently makes the fault look like a cellular problem.
    if (strlen(PRODUCT_UID) == 0) {
#if ENABLE_DEBUG
        Serial.println("[FATAL] PRODUCT_UID is empty — set it before deploying.");
#endif
        while (true) { delay(1000); }
    }

    notecard.begin();

    // Rehydrate persisted state from Notecard flash. On cold boot (no valid
    // payload) or when STATE_MAGIC_VERSION does not match (firmware update,
    // PRODUCT_UID change, outbound-cadence change, or schema change),
    // zero-initialise and apply compile-time defaults. Clearing
    // notecard_configured and templates_defined on a version mismatch ensures
    // hub.set and note.template are re-applied with the current configuration
    // before the first sample cycle after the update.
    NotePayloadDesc payload;
    bool restored = NotePayloadRetrieveAfterSleep(&payload);
    if (restored) {
        restored &= NotePayloadGetSegment(&payload, STATE_SEG_ID,
                                          &state, sizeof(state));
        NotePayloadFree(&payload);
    }
    if (!restored || state.magic_version != STATE_MAGIC_VERSION) {
        memset(&state, 0, sizeof(state));
        state.magic_version       = STATE_MAGIC_VERSION;
        state.temp_high_c         = TEMP_HIGH_ALERT_C_DEFAULT;
        state.temp_low_c          = TEMP_LOW_ALERT_C_DEFAULT;
        state.door_alert_min      = DOOR_OPEN_ALERT_MIN_DEFAULT;
        state.alert_cooldown_sec  = (uint32_t)ALERT_COOLDOWN_MIN_DEFAULT * 60;
        state.sample_interval_sec = SAMPLE_INTERVAL_SEC_DEFAULT;
        state.lux_threshold       = DOOR_LUX_THRESHOLD;
    }

    // Retry hub.set and note.template on every wake until each step is
    // confirmed without error. Success flags are persisted so only the failing
    // step is retried — not both steps on every boot once one succeeds.
    if (!state.notecard_configured) state.notecard_configured = notecardConfigure();
    if (!state.templates_defined)   state.templates_defined   = defineTemplates();

    // Pull any updated environment variables from Notehub on every wake.
    fetchEnvOverrides();

    // Initialise the MAX31865 RTD amplifier for 3-wire PT1000 mode.
    // begin() configures the MAX31865 register, starts continuous conversion,
    // and returns false if the initial fault register read is non-zero (which
    // typically indicates no probe connected). Per-read fault checks in
    // readTemperatureC() catch faults that develop after init.
    tempSensorOk = rtdAmp.begin(MAX31865_3WIRE);
#if ENABLE_DEBUG
    if (!tempSensorOk) Serial.println("[ERR] MAX31865 init failed — check SPI wiring and probe connection");
#endif

    lightSensorOk = lightSensor.begin();
    if (lightSensorOk) {
        // Gain 1x, 100 ms integration: suitable for the 0–1000 lux range of a
        // cold-storage interior lamp. readLux() in helpers uses these settings.
        lightSensor.setGain(VEML7700_GAIN_1);
        lightSensor.setIntegrationTime(VEML7700_IT_100MS);
    }
#if ENABLE_DEBUG
    else { Serial.println("[ERR] VEML7700 not found — check Qwiic connection"); }
#endif

    // Reed switch: Normally Open; one terminal to D5, other to GND.
    pinMode(DOOR_SWITCH_PIN, INPUT_PULLUP);
}

// ===========================================================================
// loop() — runs one sample cycle then sleeps via card.attn / ATTN gate
// ===========================================================================
void loop() {
    runSampleCycle();
    goToSleep();
    // Reached only when ATTN-based power control is not wired (bench use).
    delay((unsigned long)state.sample_interval_sec * 1000UL);
}

// ===========================================================================
// notecardConfigure — hub.set (periodic mode) and accelerometer quiet
// Returns true only when hub.set is confirmed by the Notecard without error.
// ===========================================================================
bool notecardConfigure() {
    // Retry loop: allows up to ~10 s for the Notecard I²C interface to become
    // ready on cold boot and for hub.set to be acknowledged without a semantic
    // error in the response. Both transport failures (NULL response) and
    // Notecard-reported errors are retried so that a transient startup fault
    // cannot leave the device permanently misconfigured. Only a clean
    // Notecard acknowledgement advances notecard_configured to true.
    for (int attempt = 0; attempt < 5; attempt++) {
        if (attempt > 0) delay(2000);

        // inbound < outbound so env-var updates arrive more frequently than
        // the reading flush; with outbound = 60 min, inbound = 30 min.
        J *req = notecard.newRequest("hub.set");
        JAddStringToObject(req, "product", PRODUCT_UID);
        JAddStringToObject(req, "mode", "periodic");
        JAddNumberToObject(req, "outbound", OUTBOUND_INTERVAL_MIN);
        JAddNumberToObject(req, "inbound",  OUTBOUND_INTERVAL_MIN / 2);

        J *rsp = notecard.requestAndResponse(req);
        if (rsp == NULL) {
#if ENABLE_DEBUG
            Serial.print("[WARN] notecardConfigure: no response (attempt ");
            Serial.print(attempt + 1); Serial.println(")");
#endif
            continue;
        }
        if (notecard.responseError(rsp)) {
#if ENABLE_DEBUG
            Serial.print("[WARN] notecardConfigure: ");
            Serial.println(JGetString(rsp, "err"));
#endif
            notecard.deleteResponse(rsp);
            continue;
        }
        notecard.deleteResponse(rsp);

        // hub.set confirmed without error. Best-effort: stop the onboard
        // accelerometer to avoid interrupt blips during idle. Failure here
        // does not gate the configuration flag.
        J *mv = notecard.newRequest("card.motion.mode");
        JAddBoolToObject(mv, "stop", true);
        notecard.sendRequest(mv);
        return true;
    }
#if ENABLE_DEBUG
    Serial.println("[ERR] notecardConfigure: hub.set failed after all retries");
#endif
    return false;
}

// ===========================================================================
// defineTemplates — fixed-length schema for the per-sample reading Notefile
// Returns true only when note.template is confirmed by the Notecard.
// Reading Notes are not emitted until this step succeeds (see runSampleCycle).
// ===========================================================================
// Templates compress each Note to a fixed binary record rather than free-form
// JSON — important for a device that may run for years on a fixed data plan.
// Field type tokens: 14.1 = 4-byte float, 24 = 4-byte uint, true = boolean.
bool defineTemplates() {
    // Retry loop mirrors notecardConfigure(): a transient I²C failure on the
    // first boot wake would otherwise silently suppress all reading Notes
    // until the next wake, causing an undetectable gap in the audit trail.
    for (int attempt = 0; attempt < 5; attempt++) {
        if (attempt > 0) delay(2000);

        J *req = notecard.newRequest("note.template");
        JAddStringToObject(req, "file", NOTEFILE_READING);
        JAddNumberToObject(req, "port", 50);
        J *body = JAddObjectToObject(req, "body");
        JAddNumberToObject(body, "temp_c",           14.1);
        JAddNumberToObject(body, "lux",              14.1);
        JAddNumberToObject(body, "door_open_sec",    24);
        JAddBoolToObject(body,   "door_open",        true);
        // UTC epoch at sample time — preserved across retries for audit lineage.
        // 0 when time was not yet synced; pair with time_valid:false.
        JAddNumberToObject(body, "sample_epoch",     24);
        // false on samples taken before the Notecard RTC has synced with Notehub
        JAddBoolToObject(body,   "time_valid",       true);
        // Cumulative drop counters — observable in Notehub without a separate channel
        JAddNumberToObject(body, "dropped_readings", 24);
        JAddNumberToObject(body, "dropped_alerts",   24);

        J *rsp = notecard.requestAndResponse(req);
        if (rsp == NULL) {
#if ENABLE_DEBUG
            Serial.print("[WARN] defineTemplates: no response (attempt ");
            Serial.print(attempt + 1); Serial.println(")");
#endif
            continue;
        }
        if (notecard.responseError(rsp)) {
#if ENABLE_DEBUG
            Serial.print("[WARN] defineTemplates: ");
            Serial.println(JGetString(rsp, "err"));
#endif
            notecard.deleteResponse(rsp);
            continue;
        }
        notecard.deleteResponse(rsp);
        return true;
    }
#if ENABLE_DEBUG
    Serial.println("[ERR] defineTemplates: note.template failed after all retries");
#endif
    return false;
}

// ===========================================================================
// updateAlertCooldown — advance the per-type last-alert timestamp
// ===========================================================================
// Called after every successful alert send (first attempt or ring-buffer
// retry) so the cooldown state is always consistent with what has actually
// been enqueued. Keeping cooldown timestamps current prevents the same alert
// type from firing again as a duplicate within the same wake cycle.
static void updateAlertCooldown(const char *type, uint32_t t) {
    if (strcmp(type, "temp_excursion_high") == 0)
        state.last_temp_high_alert_time = t;
    else if (strcmp(type, "temp_excursion_low") == 0)
        state.last_temp_low_alert_time = t;
    else if (strcmp(type, "door_open_timeout") == 0)
        state.last_door_timeout_alert_time = t;
    else if (strcmp(type, "sensor_disagreement") == 0)
        state.last_sensor_disagree_alert_time = t;
}

// ===========================================================================
// alertTypeToBit — map an alert type string to its ALERT_BIT_* bitmask flag
// ===========================================================================
static uint8_t alertTypeToBit(const char *type) {
    if (strcmp(type, "temp_excursion_high") == 0)  return ALERT_BIT_TEMP_HIGH;
    if (strcmp(type, "temp_excursion_low") == 0)   return ALERT_BIT_TEMP_LOW;
    if (strcmp(type, "door_open_timeout") == 0)    return ALERT_BIT_DOOR_TIMEOUT;
    if (strcmp(type, "sensor_disagreement") == 0)  return ALERT_BIT_SENSOR_DISAGREE;
    return 0;
}

// ===========================================================================
// recomputeAlertTypeMask — derive pending_alert_type_mask from ring contents
// ===========================================================================
// Scans every live entry in the pending_alerts ring and returns the OR of
// their type bits. Call this after any mutation of the ring (stash or dequeue)
// to guarantee the mask is accurate. An OR-only approach leaves stale bits
// when an entry is evicted on ring-full overwrite (the dropped entry's type
// bit stays set even though no entry of that type remains in the ring), which
// can permanently suppress future alerts of that type.
static uint8_t recomputeAlertTypeMask() {
    uint8_t mask = 0;
    for (uint8_t i = 0; i < state.pending_alert_count; i++) {
        uint8_t slot = (state.pending_alert_head + i) % PENDING_RING_CAP;
        mask |= alertTypeToBit(state.pending_alerts[slot].type);
    }
    return mask;
}

// ===========================================================================
// stashPendingAlert — enqueue a failed alert payload into the ring buffer
// ===========================================================================
// Appends to the FIFO ring. When the ring is full the oldest entry is
// overwritten and dropped_alerts is incremented. The original trigger epoch
// is preserved in the entry so audit lineage survives retry delays.
// Cooldown state is NOT advanced here; the caller withholds the cooldown
// update on send failure so the alert remains eligible for retry without
// waiting through a full cooldown window. pending_alert_type_mask is
// recomputed from scratch after every ring mutation so that a dropped entry's
// type bit is cleared when it no longer has a live entry in the ring.
static void stashPendingAlert(const char *type, float tc, float lx,
                              bool door, uint32_t dsec, bool tv, uint32_t epoch) {
    uint8_t tail = (state.pending_alert_head + state.pending_alert_count)
                   % PENDING_RING_CAP;
    strncpy(state.pending_alerts[tail].type, type, PENDING_ALERT_TYPE_LEN - 1);
    state.pending_alerts[tail].type[PENDING_ALERT_TYPE_LEN - 1] = '\0';
    state.pending_alerts[tail].temp_c        = tc;
    state.pending_alerts[tail].lux           = lx;
    state.pending_alerts[tail].door_open     = door;
    state.pending_alerts[tail].door_open_sec = dsec;
    state.pending_alerts[tail].time_valid    = tv;
    state.pending_alerts[tail].epoch         = epoch;
    if (state.pending_alert_count < PENDING_RING_CAP) {
        state.pending_alert_count++;
    } else {
        // Ring is full — overwrite oldest entry and count the loss.
        state.pending_alert_head = (state.pending_alert_head + 1) % PENDING_RING_CAP;
        state.dropped_alerts++;
    }
    // Recompute the mask from current ring contents so that a type bit is
    // cleared when its last entry was just overwritten. An OR-only update
    // would leave the dropped entry's bit set, suppressing future alerts of
    // that type even though no entry of that type remains in the ring.
    state.pending_alert_type_mask = recomputeAlertTypeMask();
}

// ===========================================================================
// runSampleCycle — per-wake logic: retry pending → read → evaluate → record
// ===========================================================================
void runSampleCycle() {
    // Resolve current time first so it is available both for the ring-buffer
    // retry blocks below (cooldown updates) and for the alert evaluation later.
    uint32_t now        = getEpochTime();
    bool     time_valid = (now > 0);

    // --- Retry pending alerts (FIFO; drain until one fails or buffer empty) ---
    // Alerts are retried before new sensor data is collected to prioritise
    // excursion notification. For each successfully retried alert the matching
    // cooldown timestamp is advanced to now, preventing the same alert type
    // from firing again during this wake if the condition is still active.
    // now is preferred over pa.epoch so that the cooldown is anchored at retry
    // time; if now is not yet valid (time not synced) pa.epoch is used instead.
    while (state.pending_alert_count > 0) {
        PendingAlert &pa = state.pending_alerts[state.pending_alert_head];
        if (!sendAlert(pa.type, pa.temp_c, pa.lux, pa.door_open,
                       pa.door_open_sec, pa.time_valid, pa.epoch)) {
            break;  // Notecard still unreachable; leave remainder in buffer.
        }
        updateAlertCooldown(pa.type, now > 0 ? now : pa.epoch);
        state.pending_alert_head =
            (state.pending_alert_head + 1) % PENDING_RING_CAP;
        state.pending_alert_count--;
        // Recompute the mask from the remaining ring contents. An AND-NOT clear
        // of just the delivered type's bit would be wrong when two entries of
        // the same type were queued — it would clear the bit while the second
        // entry still waits. Scanning the live ring after every dequeue gives
        // the exact set of types that still have a pending entry.
        state.pending_alert_type_mask = recomputeAlertTypeMask();
    }

    // --- Retry pending readings (only after templates are confirmed) ---
    if (state.templates_defined) {
        while (state.pending_read_count > 0) {
            PendingReading &pr = state.pending_reads[state.pending_read_head];
            if (!sendReading(pr.temp_c, pr.lux, pr.door_open,
                             pr.door_open_sec, pr.time_valid, pr.sample_epoch)) {
                break;  // Notecard still unreachable; leave remainder in buffer.
            }
            state.pending_read_head =
                (state.pending_read_head + 1) % PENDING_RING_CAP;
            state.pending_read_count--;
        }
    }

    // --- Read sensors ---
    float temp_c = readTemperatureC();
    float lux    = readLightLux();
    bool  door   = readDoorOpen();

    // --- Door state machine ---
    // Timing is gated on having a valid epoch. When the Notecard has not yet
    // synced time (now == 0) door_open_since is left at 0, preventing a bogus
    // near-epoch open duration from triggering a false door_open_timeout alert
    // on the first sample after time becomes available.
    if (door) {
        if (state.door_open_since == 0 && now > 0) {
            state.door_open_since = now;
        }
        // door_open_timeout uses the same alert_cooldown_sec as the other alert
        // types. This means the alert can fire more than once during a single
        // long open episode (once per cooldown window) — consistent with how
        // temp_excursion behaves during a sustained excursion, and with the
        // alert_cooldown_min semantics documented in the Notehub env-var table.
        if (state.door_open_since > 0 && now > 0 && now >= state.door_open_since) {
            uint32_t open_sec = now - state.door_open_since;
            if (open_sec >= state.door_alert_min * 60 &&
                !(state.pending_alert_type_mask & ALERT_BIT_DOOR_TIMEOUT) &&
                (now - state.last_door_timeout_alert_time) >= state.alert_cooldown_sec) {
                if (sendAlert("door_open_timeout", temp_c, lux, door,
                              open_sec, time_valid, now)) {
                    updateAlertCooldown("door_open_timeout", now);
                } else {
                    // Enqueue failed — do not advance cooldown; stash for retry.
                    stashPendingAlert("door_open_timeout", temp_c, lux,
                                      door, open_sec, time_valid, now);
                }
            }
        }
    } else {
        state.door_open_since = 0;
        // last_door_timeout_alert_time is intentionally not reset here. The
        // cooldown window expires naturally, preventing immediate re-alert at
        // the start of a new open episode that follows shortly after a previous one.
    }

    uint32_t door_sec = (state.door_open_since > 0 && now > 0 && now >= state.door_open_since)
                        ? (now - state.door_open_since) : 0;

    // --- Light/door sensor disagreement: interior lamp ON, switch says CLOSED ---
    // On a door-actuated-lamp unit, lux above the threshold while D5 reads LOW
    // (door_open = false) indicates a stuck-closed or failed-closed reed switch.
    // Raise door_lux_threshold to 120000.0 in Notehub to suppress this rule on
    // always-on-lamp units without a firmware rebuild.
    bool light_on = (!isnan(lux) && lux > state.lux_threshold);
    if (light_on && !door && now > 0) {
        if (!(state.pending_alert_type_mask & ALERT_BIT_SENSOR_DISAGREE) &&
            (now - state.last_sensor_disagree_alert_time) >= state.alert_cooldown_sec) {
            if (sendAlert("sensor_disagreement", temp_c, lux, false,
                          0, time_valid, now)) {
                updateAlertCooldown("sensor_disagreement", now);
            } else {
                stashPendingAlert("sensor_disagreement", temp_c, lux,
                                  false, 0, time_valid, now);
            }
        }
    }

    // --- Temperature excursion alerts (each direction has its own cooldown) ---
    // Independent timers ensure a high-temp alert cannot suppress a simultaneous
    // low-temp alert on the same sample cycle.
    if (!isnan(temp_c) && now > 0) {
        if (temp_c > state.temp_high_c &&
            !(state.pending_alert_type_mask & ALERT_BIT_TEMP_HIGH) &&
            (now - state.last_temp_high_alert_time) >= state.alert_cooldown_sec) {
            if (sendAlert("temp_excursion_high", temp_c, lux, door,
                          door_sec, time_valid, now)) {
                updateAlertCooldown("temp_excursion_high", now);
            } else {
                stashPendingAlert("temp_excursion_high", temp_c, lux,
                                  door, door_sec, time_valid, now);
            }
        }
        if (temp_c < state.temp_low_c &&
            !(state.pending_alert_type_mask & ALERT_BIT_TEMP_LOW) &&
            (now - state.last_temp_low_alert_time) >= state.alert_cooldown_sec) {
            if (sendAlert("temp_excursion_low", temp_c, lux, door,
                          door_sec, time_valid, now)) {
                updateAlertCooldown("temp_excursion_low", now);
            } else {
                stashPendingAlert("temp_excursion_low", temp_c, lux,
                                  door, door_sec, time_valid, now);
            }
        }
    }

    // --- Per-sample reading Note ---
    // sendReading() is only called when the template is confirmed; free-form JSON
    // sent before note.template is applied would corrupt the binary schema.
    // When templates are not yet defined — or when note.add fails — the sample is
    // buffered into the FIFO ring so no reading is silently lost. sample_epoch is
    // captured now (at sensor-read time) and preserved through retries so that
    // the body always carries the authoritative original sample timestamp.
    bool sent = false;
    if (state.templates_defined) {
        sent = sendReading(temp_c, lux, door, door_sec, time_valid, now);
    }
    if (!sent) {
        // Enqueue into the FIFO ring buffer for retry next wake. When full,
        // the oldest entry is overwritten and dropped_readings is incremented.
        uint8_t tail = (state.pending_read_head + state.pending_read_count)
                       % PENDING_RING_CAP;
        state.pending_reads[tail].temp_c        = temp_c;
        state.pending_reads[tail].lux           = lux;
        state.pending_reads[tail].door_open     = door;
        state.pending_reads[tail].door_open_sec = door_sec;
        state.pending_reads[tail].time_valid    = time_valid;
        state.pending_reads[tail].sample_epoch  = now;
        if (state.pending_read_count < PENDING_RING_CAP) {
            state.pending_read_count++;
        } else {
            state.pending_read_head =
                (state.pending_read_head + 1) % PENDING_RING_CAP;
            state.dropped_readings++;
        }
    }
}
