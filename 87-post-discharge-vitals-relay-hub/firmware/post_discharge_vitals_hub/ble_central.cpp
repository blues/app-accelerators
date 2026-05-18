/***************************************************************************
  ble_central.cpp — BLE Central scanning, connection management, and GATT
  characteristic callbacks for post_discharge_vitals_hub.

  Implements the enrolled-device identity gate (BLE bonding primary,
  MAC allow-list secondary), the four data callbacks, and the connect /
  disconnect / scan callbacks.  Calls initBLE() once from setup().
***************************************************************************/

#include "ble_central.h"
#include "ble_parsers.h"
#include "notecard_helpers.h"   // for notecard extern + sendCommissioningNote

// ─── BLE client service + characteristic objects ──────────────────────────────
// Declared at module scope so the nRF52 BLE stack can register them.
// .begin() on each is called in initBLE() after Bluefruit.begin().

static BLEClientService        s_weightSvc   (0x181D);
static BLEClientCharacteristic s_weightChar  (0x2A9D);  // Weight Measurement (indicate)

static BLEClientService        s_bpSvc       (0x1810);
static BLEClientCharacteristic s_bpChar      (0x2A35);  // Blood Pressure Measurement (indicate)

static BLEClientService        s_pulseOxSvc  (0x1822);
static BLEClientCharacteristic s_pulseOxChar (0x2A5E);  // PLX Spot-Check Measurement (indicate)

static BLEClientService        s_hrSvc       (0x180D);
static BLEClientCharacteristic s_hrChar      (0x2A37);  // Heart Rate Measurement (notify)

// ─────────────────────────────────────────────────────────────────────────────
// COMMISSIONING STATE  (compiled in only when the identity gate is bypassed)
// ─────────────────────────────────────────────────────────────────────────────
// These variables enforce a one-device-at-a-time commissioning flow so the hub
// cannot accidentally enroll the wrong device when multiple compatible devices
// are nearby.  They are guarded by ALLOW_UNENROLLED_DEVICES_FOR_DEV so they
// add zero overhead to normal (non-commissioning) builds.

#if ALLOW_UNENROLLED_DEVICES_FOR_DEV

// True while a commissioning bond attempt is in progress (from the moment the
// scan callback initiates a connection until pairing completes or the
// connection closes).  The scan callback checks this flag and rejects all new
// connections while it is set.
static bool     s_commissioningSlotTaken = false;

// Connection handle for which pairing was requested but has not yet been
// confirmed by blePairCompleteCallback.  BLE_CONN_HANDLE_INVALID means no
// pending pairing.  If the connection closes while this is set, the disconnect
// callback treats it as a commissioning failure.
static uint16_t s_pairingPendingHandle   = BLE_CONN_HANDLE_INVALID;

// Enqueue a commissioning event note to commissioning.db with immediate sync,
// checked delivery, and retry — so operators can verify which device was bonded
// (or detect a failed attempt) without relying on USB serial logs.  Uses the
// same robustness pattern as the runtime measurement/alert path: retries up to
// ALERT_ENQUEUE_RETRIES times, inspects the response err field on each attempt,
// and logs clearly on final failure so the fault is visible in the serial
// monitor rather than being silently swallowed.
//
// connHandle may be BLE_CONN_HANDLE_INVALID when no active connection is
// available (e.g. the "mode active" boot note or a post-disconnect failure
// note).  The peer address is captured before the retry loop so retries do not
// depend on the connection still being open — important when called from
// blePairCompleteCallback, where the link may be closing concurrently.
static void sendCommissioningNote(const char *event, uint16_t connHandle) {
    // Capture peer address up-front before any potential disconnect.
    // Format as "AA:BB:CC:DD:EE:FF" (MSB-first, matching BLE scanner app
    // conventions) so operators can cross-reference the address against the
    // device label on the patient kit.
    char    addr_str[18] = {};
    uint8_t addr_type    = 0xFF;  // sentinel: no address available
    if (connHandle != BLE_CONN_HANDLE_INVALID) {
        BLEConnection *conn = Bluefruit.Connection(connHandle);
        if (conn) {
            ble_gap_addr_t peer = conn->getPeerAddr();
            snprintf(addr_str, sizeof(addr_str),
                     "%02X:%02X:%02X:%02X:%02X:%02X",
                     peer.addr[5], peer.addr[4], peer.addr[3],
                     peer.addr[2], peer.addr[1], peer.addr[0]);
            addr_type = peer.addr_type;
        }
    }

    bool ok = false;
    for (uint8_t attempt = 1; attempt <= ALERT_ENQUEUE_RETRIES; ++attempt) {
        J *req = notecard.newRequest("note.add");
        if (!req) {
            DBG_PRINT("[COMMS] sendCommissioningNote '%s': newRequest failed (attempt %d/%d)\n",
                      event, attempt, (int)ALERT_ENQUEUE_RETRIES);
            if (attempt < ALERT_ENQUEUE_RETRIES) delay(50);
            continue;
        }
        JAddStringToObject(req, "file", "commissioning.db");
        JAddBoolToObject  (req, "sync", true);
        J *body = JAddObjectToObject(req, "body");
        JAddStringToObject(body, "event", event);
        if (addr_str[0] != '\0') {
            JAddStringToObject(body, "ble_addr",  addr_str);
            JAddNumberToObject(body, "addr_type", (double)addr_type);
        }
        J *rsp = notecard.requestAndResponse(req);
        if (!rsp) {
            DBG_PRINT("[COMMS] sendCommissioningNote '%s': no Notecard response (attempt %d/%d)\n",
                      event, attempt, (int)ALERT_ENQUEUE_RETRIES);
            if (attempt < ALERT_ENQUEUE_RETRIES) delay(100);
            continue;
        }
        const char *err = JGetString(rsp, "err");
        ok = !(err && *err);
        if (!ok) {
            DBG_PRINT("[COMMS] sendCommissioningNote '%s' error (attempt %d/%d): %s\n",
                      event, attempt, (int)ALERT_ENQUEUE_RETRIES, err);
        }
        notecard.deleteResponse(rsp);
        if (ok) return;
        if (attempt < ALERT_ENQUEUE_RETRIES) delay(100);
    }
    if (!ok) {
        DBG_PRINT("[COMMS] sendCommissioningNote '%s': all %d attempts failed — commissioning record may not appear in Notehub\n",
                  event, (int)ALERT_ENQUEUE_RETRIES);
    }
}

#endif  // ALLOW_UNENROLLED_DEVICES_FOR_DEV

// ─────────────────────────────────────────────────────────────────────────────
// DEVICE IDENTITY CHECK
// ─────────────────────────────────────────────────────────────────────────────
// Returns true when a scan report comes from a device the hub is authorized
// to connect to.
//
// Primary gate — BLE bonding / IRK resolution:
//   The nRF52840 SoftDevice resolves Resolvable Private Addresses (RPAs) using
//   stored IRKs before the scan event reaches the application layer.  When
//   resolution succeeds, peer_addr.addr_id_peer is set to 1 and addr contains
//   the resolved identity address.  This covers devices using Bluetooth Privacy
//   (randomized/resolvable addresses), which includes most modern BLE health
//   wearables.
//
//   IMPORTANT LIMITATION: addr_id_peer is set ONLY when RPA resolution
//   succeeds.  Bonded peripherals that advertise a stable public or static-
//   random address never trigger RPA resolution, so addr_id_peer remains 0
//   for them even after bonding.  Those devices must be listed in the
//   ENROLLED_DEVICES allow-list in vitals_config.h.  The bond_established note
//   written to commissioning.db during commissioning contains the ble_addr and
//   addr_type needed to construct the list entry.
//
// Secondary gate — MAC allow-list (addr + addr_type):
//   Covers bonded devices with a stable public or static-random address
//   (addr_id_peer == 0).  Both address bytes AND addr_type are compared to
//   prevent cross-type false matches (e.g. a public-address device sharing six
//   bytes with a static-random entry).  See vitals_config.h for the
//   EnrolledDevice struct and commissioning workflow.
//   This gate must not be the sole identity check: it provides no protection
//   for Privacy-enabled devices and no spoofing resistance for static-address
//   devices.
//
// Development / commissioning bypass:
//   Build with ALLOW_UNENROLLED_DEVICES_FOR_DEV=1 to bypass both gates and
//   accept any matching-service device.  Use this only during initial pairing
//   (commissioning run) or bench testing.  See vitals_config.h.

static bool isIdentifiedDevice(const ble_gap_evt_adv_report_t *report) {
#if ALLOW_UNENROLLED_DEVICES_FOR_DEV
    // DEV BYPASS: accept all matching-service devices for commissioning / bench testing
    (void)report;
    return true;
#else
    // Primary: bonded device whose RPA was resolved using a stored IRK
    if (report->peer_addr.addr_id_peer) return true;

    // Secondary: stable public/static address in the allow-list (addr + addr_type)
#if ENROLLED_DEVICE_COUNT > 0
    for (uint8_t i = 0; i < ENROLLED_DEVICE_COUNT; ++i) {
        if (report->peer_addr.addr_type == ENROLLED_DEVICES[i].addr_type &&
            memcmp(report->peer_addr.addr, ENROLLED_DEVICES[i].addr, 6) == 0) return true;
    }
#endif

    return false;
#endif
}

// ─────────────────────────────────────────────────────────────────────────────
// BLE CHARACTERISTIC DATA CALLBACKS (SoftDevice task context)
// ─────────────────────────────────────────────────────────────────────────────
// Each callback parses the raw characteristic bytes, applies physiological
// plausibility checks, and sets the corresponding buffered struct's `valid`
// flag.  The main loop drains the structs inside critical sections.
// Frames that fail plausibility are logged and silently discarded so a
// malformed or noncompliant device cannot inject impossible readings or trip
// alert thresholds.

static void weightDataCallback(BLEClientCharacteristic * /*chr*/,
                                uint8_t *data, uint16_t len) {
    float kg = parseWeightKg(data, len);
    if (kg < 0.0f) return;  // parse error or unsuccessful sentinel
    if (!weightKgPlausible(kg)) {
        DBG_PRINT("[BLE] Weight %.2f kg outside plausible range (1–500 kg) — discarding\n", kg);
        return;
    }
    g_weight.prev_kg = g_last_weight_kg;
    g_weight.kg      = kg;
    g_weight.valid   = true;
}

static void bpDataCallback(BLEClientCharacteristic * /*chr*/,
                            uint8_t *data, uint16_t len) {
    int16_t sys, dia, pulse;
    parseBpMmhg(data, len, &sys, &dia, &pulse);
    if (sys <= 0) return;  // parse error
    if (!bpPlausible(sys, dia, pulse)) {
        DBG_PRINT("[BLE] BP %d/%d mmHg pulse %d outside plausible range — discarding\n",
                  sys, dia, pulse);
        return;
    }
    g_bp.systolic  = sys;
    g_bp.diastolic = dia;
    g_bp.pulse_bpm = pulse;
    g_bp.valid     = true;
}

static void spo2DataCallback(BLEClientCharacteristic * /*chr*/,
                              uint8_t *data, uint16_t len) {
    int16_t sp, pulse;
    parseSpO2(data, len, &sp, &pulse);
    if (sp <= 0) return;  // parse error
    if (!spo2Plausible(sp, pulse)) {
        DBG_PRINT("[BLE] SpO2 %d%% pulse %d outside plausible range — discarding\n", sp, pulse);
        return;
    }
    g_spo2.spo2_pct  = sp;
    g_spo2.pulse_bpm = pulse;
    g_spo2.valid     = true;
}

static void hrDataCallback(BLEClientCharacteristic * /*chr*/,
                            uint8_t *data, uint16_t len) {
    uint16_t hr = parseHeartRate(data, len);
    if (hr == 0) return;  // parse error
    if (!hrPlausible(hr)) {
        DBG_PRINT("[BLE] Heart rate %d bpm outside plausible range (20–300) — discarding\n", hr);
        return;
    }
    g_activity.heart_rate_bpm = hr;
    g_activity.valid          = true;
}

// ─────────────────────────────────────────────────────────────────────────────
// SERVICE DISCOVERY HELPER
// ─────────────────────────────────────────────────────────────────────────────
// Called from bleSecuredCallback once the link is encrypted, and from
// bleConnectCallback in the rare case where the link is already secured when
// the connection event fires.  Always runs over an encrypted link.
// Discovers which of the four target service types is present, subscribes to
// the appropriate characteristic, and sets g_oneshot_conn_handle or
// g_hrConnHandle.  Disconnects and returns false on any failure so the
// caller can abort cleanly without leaving a stale Central slot.

static bool doDiscoverAndSubscribe(uint16_t connHandle) {
    if (s_weightSvc.discover(connHandle)) {
        DBG_PRINTLN("[BLE] Weight scale — subscribing");
        s_weightChar.setIndicateCallback(weightDataCallback);
        if (!s_weightChar.discover() ||
            !s_weightChar.enableIndicate()) {
            DBG_PRINTLN("[BLE] Weight: subscription failed — disconnecting");
            Bluefruit.disconnect(connHandle);
            return false;
        }
        g_oneshot_conn_handle = connHandle;

    } else if (s_bpSvc.discover(connHandle)) {
        DBG_PRINTLN("[BLE] Blood pressure cuff — subscribing");
        s_bpChar.setIndicateCallback(bpDataCallback);
        if (!s_bpChar.discover() ||
            !s_bpChar.enableIndicate()) {
            DBG_PRINTLN("[BLE] BP: subscription failed — disconnecting");
            Bluefruit.disconnect(connHandle);
            return false;
        }
        g_oneshot_conn_handle = connHandle;

    } else if (s_pulseOxSvc.discover(connHandle)) {
        DBG_PRINTLN("[BLE] Pulse oximeter — subscribing");
        s_pulseOxChar.setIndicateCallback(spo2DataCallback);
        if (!s_pulseOxChar.discover() ||
            !s_pulseOxChar.enableIndicate()) {
            DBG_PRINTLN("[BLE] SpO2: subscription failed — disconnecting");
            Bluefruit.disconnect(connHandle);
            return false;
        }
        g_oneshot_conn_handle = connHandle;

    } else if (s_hrSvc.discover(connHandle)) {
        DBG_PRINTLN("[BLE] Activity band (Heart Rate Service) — subscribing");
        s_hrChar.setNotifyCallback(hrDataCallback);
        if (!s_hrChar.discover() ||
            !s_hrChar.enableNotify()) {
            DBG_PRINTLN("[BLE] HR: subscription failed — disconnecting");
            Bluefruit.disconnect(connHandle);
            return false;
        }
        g_hrConnHandle = connHandle;

    } else {
        // Not one of our target service types — free the Central slot immediately.
        DBG_PRINTLN("[BLE] Unknown service set — disconnecting");
        Bluefruit.disconnect(connHandle);
        return false;
    }
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// BLE SECURITY CALLBACKS
// ─────────────────────────────────────────────────────────────────────────────

// Called by the BSP when a pairing/bonding procedure completes.
// pair_complete_cb_t = void(*)(uint16_t conn_hdl, uint8_t auth_status).
// authStatus == BLE_GAP_SEC_STATUS_SUCCESS (0) means bond keys were stored in
// flash; any other value means the pairing was rejected or failed.
//
// Service discovery is NOT triggered here.  It is always deferred to
// bleSecuredCallback (below) so that both fresh-pairing and bond-re-encryption
// reconnect paths share a single unified secure-then-discover flow.
//
// This callback is only meaningful in commissioning builds where we need to
// verify the outcome and send a status note.  In production builds all accepted
// devices are already bonded, so a pairing-failure disconnect is handled
// naturally by the watchdog; auth_status is silently ignored.
static void blePairCompleteCallback(uint16_t connHandle, uint8_t authStatus) {
#if ALLOW_UNENROLLED_DEVICES_FOR_DEV
    if (authStatus != BLE_GAP_SEC_STATUS_SUCCESS) {
        // Pairing failed — bond NOT stored in flash.  Release the commissioning
        // slot immediately so the operator can retry.  bleSecuredCallback will
        // not fire for a failed pair; disconnect cleanly so the link does not
        // linger in a half-authenticated state.
        s_pairingPendingHandle   = BLE_CONN_HANDLE_INVALID;
        s_commissioningSlotTaken = false;
        DBG_PRINT("[BLE] COMMISSIONING: pairing failed auth=0x%02X conn=0x%04X — device not enrolled\n",
                  authStatus, connHandle);
        sendCommissioningNote("pairing_failed", connHandle);
        Bluefruit.disconnect(connHandle);
        return;
    }
    // Pairing succeeded: clear the pending-pairing sentinel so that
    // bleDisconnectCallback does not misclassify a subsequent normal disconnect
    // as a commissioning failure.
    s_pairingPendingHandle = BLE_CONN_HANDLE_INVALID;
    // Persist the bonded device address to Notehub so the operator can verify
    // the correct device was enrolled.  The note includes ble_addr and addr_type;
    // for devices with a stable public/static address (addr_id_peer=0 on future
    // scans) these values should be added to ENROLLED_DEVICES in vitals_config.h
    // before rebuilding for production.
    sendCommissioningNote("bond_established", connHandle);
    DBG_PRINT("[BLE] Bond established — conn=0x%04X; device will be recognized on future scans\n",
              connHandle);
    // s_commissioningSlotTaken is released in bleSecuredCallback (which fires
    // immediately after a successful pairing) so the slot stays held until
    // discovery completes — preventing a second device from being enrolled while
    // the first is still being set up.
#else
    (void)connHandle;
    (void)authStatus;
#endif
}

// Called by the BSP when the link transitions to an encrypted security level —
// fires for BOTH a new successful pairing (BLE_GAP_EVT_CONN_SEC_UPDATE fires
// after BLE_GAP_EVT_AUTH_STATUS) AND a re-encryption using stored bond keys on
// a reconnect.  This is the single, unified trigger for service discovery and
// characteristic subscription in all build configurations.
//
// Using one callback for discovery guarantees that GATT attribute access
// (discover(), CCCD enableIndicate/enableNotify) always runs over an encrypted
// link — required by peripherals that gate reads or subscriptions behind a
// secure connection.
//
// In commissioning builds, blePairCompleteCallback has already verified
// auth_status and cleared s_pairingPendingHandle before this fires on a
// successful pair.  On a pairing failure, blePairCompleteCallback disconnects
// and clears s_commissioningSlotTaken before this can run; the discover() calls
// will fail gracefully on the closing link.
static void bleSecuredCallback(uint16_t connHandle) {
    DBG_PRINT("[BLE] Link secured conn=0x%04X — starting service discovery\n", connHandle);
    doDiscoverAndSubscribe(connHandle);
#if ALLOW_UNENROLLED_DEVICES_FOR_DEV
    s_commissioningSlotTaken = false;
#endif
}

// ─────────────────────────────────────────────────────────────────────────────
// BLE CENTRAL EVENT CALLBACKS
// ─────────────────────────────────────────────────────────────────────────────

// Called when a peripheral has connected.
//
// Single flow for all builds: if the link is not yet secured, request
// pairing/encryption immediately and defer doDiscoverAndSubscribe() to
// bleSecuredCallback.  This guarantees GATT attribute access (discover(),
// CCCD writes) always runs over an encrypted link — required by peripherals
// that gate reads or subscriptions behind security — for both commissioning
// (new pairing) and production (bond re-encryption on reconnect).
//
// In the rare case where the peripheral has already established security
// before our connect callback fires (e.g., peripheral-initiated encryption),
// bleSecuredCallback will not fire again; discovery is driven directly from
// this callback instead.
//
// Any failure disconnects immediately to free the single Central slot.
static void bleConnectCallback(uint16_t connHandle) {
    // Record connection start time for the measurement timeout watchdog in
    // loop().  Set before requestPairing() so the watchdog fires even if the
    // security negotiation hangs.
    g_active_conn_handle = connHandle;
    g_conn_start_ms      = millis();
    DBG_PRINT("[BLE] Connected 0x%04X\n", connHandle);

    BLEConnection *conn = Bluefruit.Connection(connHandle);
    if (!conn) {
        Bluefruit.disconnect(connHandle);
        return;
    }

    if (conn->secured()) {
        // Link already secured before our callback ran (peripheral-initiated).
        // bleSecuredCallback will not fire again at this security level, so
        // drive discovery directly from here.
        DBG_PRINT("[BLE] Link already secured on connect conn=0x%04X — discovering now\n", connHandle);
        doDiscoverAndSubscribe(connHandle);
#if ALLOW_UNENROLLED_DEVICES_FOR_DEV
        s_commissioningSlotTaken = false;
#endif
        return;
    }

    // Link not yet secured.  Request pairing/encryption; discovery is deferred
    // to bleSecuredCallback so GATT reads and CCCD writes run over an encrypted
    // link in all build configurations.
#if ALLOW_UNENROLLED_DEVICES_FOR_DEV
    // Record the handle so bleDisconnectCallback can detect a commissioning
    // failure if the link drops before the pair-complete callback fires.
    s_pairingPendingHandle = connHandle;
#endif
    if (!conn->requestPairing()) {
        DBG_PRINT("[BLE] requestPairing() failed conn=0x%04X — disconnecting\n", connHandle);
#if ALLOW_UNENROLLED_DEVICES_FOR_DEV
        s_pairingPendingHandle   = BLE_CONN_HANDLE_INVALID;
        s_commissioningSlotTaken = false;
        sendCommissioningNote("pairing_request_failed", connHandle);
#endif
        Bluefruit.disconnect(connHandle);
        return;
    }
    // Discovery deferred to bleSecuredCallback.
}

// Called on disconnect.  The scanner restarts automatically because
// restartOnDisconnect(true) was set in initBLE().
static void bleDisconnectCallback(uint16_t connHandle, uint8_t reason) {
    if (connHandle == g_hrConnHandle)        g_hrConnHandle        = BLE_CONN_HANDLE_INVALID;
    if (connHandle == g_oneshot_conn_handle) g_oneshot_conn_handle = BLE_CONN_HANDLE_INVALID;
    if (connHandle == g_active_conn_handle)  g_active_conn_handle  = BLE_CONN_HANDLE_INVALID;
    DBG_PRINT("[BLE] Disconnected conn=0x%04X reason=0x%02X\n", connHandle, reason);
#if ALLOW_UNENROLLED_DEVICES_FOR_DEV
    if (s_pairingPendingHandle == connHandle) {
        // Connection closed before blePairCompleteCallback fired — the device
        // was NOT successfully bonded.  Log the failure to Notehub (address is
        // no longer resolvable from the handle, so none is recorded) and clear
        // the commissioning slot so the operator can retry.
        s_pairingPendingHandle   = BLE_CONN_HANDLE_INVALID;
        s_commissioningSlotTaken = false;
        DBG_PRINTLN("[BLE] COMMISSIONING: link closed before bond completed — device not enrolled; retry");
        sendCommissioningNote("bond_failed_device_not_enrolled", BLE_CONN_HANDLE_INVALID);
    } else {
        // Normal post-bond disconnect (or measurement-only connection):
        // always release the commissioning slot so the next device can be enrolled.
        s_commissioningSlotTaken = false;
    }
#endif
}

// Called for each advertisement packet seen during a scan.
// Connects only to identified devices (bonded IRK-resolved or MAC-listed)
// that advertise one of the four target service UUIDs.
// With ALLOW_UNENROLLED_DEVICES_FOR_DEV=1, the identity gate is bypassed.
static void bleScanCallback(ble_gap_evt_adv_report_t *report) {
    // Reject devices not passing the identity gate before inspecting services.
    if (!isIdentifiedDevice(report)) {
        Bluefruit.Scanner.resume();
        return;
    }

#if ALLOW_UNENROLLED_DEVICES_FOR_DEV
    // Enforce one-device-at-a-time commissioning: while a bond attempt is in
    // progress, ignore all other advertisers.  This prevents the hub from
    // accidentally enrolling a second nearby device (e.g. another patient's
    // cuff in the same staging area) during the same commissioning session.
    if (s_commissioningSlotTaken) {
        Bluefruit.Scanner.resume();
        return;
    }
#endif

    // HR Service devices advertise continuously while worn.  Gate reconnections
    // on HR_SAMPLE_INTERVAL_MS to prevent rapid connect-sample-disconnect loops
    // that would flood activity.qo.
    bool hr_due = (millis() - g_last_hr_sample_ms >= HR_SAMPLE_INTERVAL_MS);

    if (Bluefruit.Scanner.checkReportForService(report, s_weightSvc)  ||
        Bluefruit.Scanner.checkReportForService(report, s_bpSvc)      ||
        Bluefruit.Scanner.checkReportForService(report, s_pulseOxSvc) ||
        (hr_due && Bluefruit.Scanner.checkReportForService(report, s_hrSvc))) {
#if ALLOW_UNENROLLED_DEVICES_FOR_DEV
        // Claim the commissioning slot before calling connect so that any
        // subsequent scan callback firing in the same window (for a different
        // nearby device) sees the lock immediately.
        s_commissioningSlotTaken = true;
#endif
        if (!Bluefruit.Central.connect(report)) {
            // connect() failed synchronously — release any held state and
            // resume scanning so the hub does not stall permanently.
#if ALLOW_UNENROLLED_DEVICES_FOR_DEV
            s_commissioningSlotTaken = false;
#endif
            Bluefruit.Scanner.resume();
        }
    } else {
        Bluefruit.Scanner.resume();
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// BLE INITIALIZATION
// ─────────────────────────────────────────────────────────────────────────────

void initBLE() {
    // Start as Central only: 0 peripheral slots, 1 central slot
    Bluefruit.begin(0, 1);
    Bluefruit.setName("VitalsHub");
    Bluefruit.setTxPower(4);  // +4 dBm — adequate for a bedside room

    // ── Explicit BLE security / bonding policy ──────────────────────────────
    // IO caps = NONE → Just Works association model (no PIN/passkey).
    // For consumer health devices that have no display or keyboard this is the
    // only practical pairing mode; explicit configuration documents the intent
    // and prevents the BSP default from silently changing if the BSP is updated.
    //
    // MITM protection is disabled (consistent with Just Works / no IO caps).
    // Just Works provides encryption but no authentication — acceptable for
    // the enrollment use-case because isIdentifiedDevice() provides the
    // application-level enrollment gate; BLE encryption protects data in
    // transit after bonding.
    //
    // Security flow (all build configurations):
    //   1. bleConnectCallback: requestPairing() if link not yet secured.
    //   2. blePairCompleteCallback: checks auth_status; on failure, releases
    //      the commissioning slot and disconnects (commissioning builds only).
    //   3. bleSecuredCallback: single gating point for doDiscoverAndSubscribe();
    //      fires for both a new successful pairing (after BLE_GAP_EVT_AUTH_STATUS)
    //      and a bond re-encryption on reconnect (BLE_GAP_EVT_CONN_SEC_UPDATE).
    //
    // The nRF52 BSP stores LTK/IRK pairs in a dedicated flash page and reloads
    // them at every boot.  For Privacy-enabled devices the SoftDevice resolves
    // the RPA using the stored IRK automatically (addr_id_peer=1 in scan reports).
    // Devices with stable public/static addresses must be listed in ENROLLED_DEVICES.
    Bluefruit.Security.setIOCaps(false, false, false);  // No display, no yes/no, no keyboard → Just Works
    Bluefruit.Security.setMITM(false);                  // consistent with Just Works / no IO caps
    Bluefruit.Security.setPairCompleteCallback(blePairCompleteCallback);
    Bluefruit.Security.setSecuredCallback(bleSecuredCallback);

    // Initialize client services and characteristics.  Must be called after
    // Bluefruit.begin() and before scanning starts.
    s_weightSvc.begin();   s_weightChar.begin();
    s_bpSvc.begin();       s_bpChar.begin();
    s_pulseOxSvc.begin();  s_pulseOxChar.begin();
    s_hrSvc.begin();       s_hrChar.begin();

    Bluefruit.Central.setConnectCallback(bleConnectCallback);
    Bluefruit.Central.setDisconnectCallback(bleDisconnectCallback);

    // 50% scan duty cycle: 100 ms window every 200 ms interval.
    Bluefruit.Scanner.setRxCallback(bleScanCallback);
    Bluefruit.Scanner.restartOnDisconnect(true);  // resume scanning after disconnect
    Bluefruit.Scanner.setInterval(BLE_SCAN_INTERVAL, BLE_SCAN_WINDOW);
    Bluefruit.Scanner.useActiveScan(true);         // active scan pulls the complete device name
    Bluefruit.Scanner.start(0);                    // 0 = scan indefinitely
    DBG_PRINTLN("[BLE] Scanner started");

#if ALLOW_UNENROLLED_DEVICES_FOR_DEV
    DBG_PRINTLN("[BLE] WARNING: ALLOW_UNENROLLED_DEVICES_FOR_DEV=1 — identity gate bypassed");
    // Persist a commissioning-mode-active note to Notehub immediately so an
    // operator monitoring the device from the cloud can detect a commissioning
    // build that was accidentally deployed — without relying on USB serial logs.
    sendCommissioningNote("commissioning_mode_active", BLE_CONN_HANDLE_INVALID);
#endif
}
