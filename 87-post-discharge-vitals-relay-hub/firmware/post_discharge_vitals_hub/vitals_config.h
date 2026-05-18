/***************************************************************************
  vitals_config.h — Shared build-time configuration for post_discharge_vitals_hub.

  Included by the main sketch and all .cpp modules so that compile-time
  flags, interval constants, and the DBG logging macros are consistent
  across compilation units.
***************************************************************************/
#pragma once

// ─── Product identity ─────────────────────────────────────────────────────────
// Set PRODUCT_UID to the ProductUID from your Notehub project
// (e.g. "com.company.name:post-discharge-vitals-hub").
//
// An empty ProductUID is silently accepted by the Notecard but the device will
// never associate its readings with a Notehub project — for a patient-facing
// hub that is an operational error, not just a warning.  PRODUCT_UID is
// therefore a compile-time requirement: failing to define it is a build error.
//
// If you are working locally without a Notehub project yet, add
// -DALLOW_EMPTY_PRODUCT_UID to your build flags as an explicit development
// override.  That flag must never appear in a shipping build.
#ifndef PRODUCT_UID
#  ifndef ALLOW_EMPTY_PRODUCT_UID
#    error "PRODUCT_UID is not set. Define it as your Notehub ProductUID before flashing (e.g. -DPRODUCT_UID='\"com.company.name:post-discharge-vitals-hub\"'). For local development without a project, add -DALLOW_EMPTY_PRODUCT_UID to suppress this error — that flag must not appear in a shipping build."
#  else
#    define PRODUCT_UID ""
#    pragma message "PRODUCT_UID is empty (ALLOW_EMPTY_PRODUCT_UID override active) — hub will not associate readings with any Notehub project"
#  endif
#endif

// ─── Release / debug guards ───────────────────────────────────────────────────
// DEBUG_VITALS enables verbose serial logging and raw Notecard debug output.
// MUST be 0 (the default) in deployed units — patient vitals must not be
// printed over USB serial in a clinical setting.
//
// If DEBUG_VITALS is left non-zero without also defining ALLOW_DEBUG_BUILD,
// this header emits a hard compile-time error so a verbose build cannot be
// accidentally shipped to a patient hub.
//
// To build in debug mode intentionally, pass -DALLOW_DEBUG_BUILD in your
// build flags (Arduino IDE: Sketch → Export Compiled Binary → extra flags).
#ifndef DEBUG_VITALS
#define DEBUG_VITALS 0
#endif

#if DEBUG_VITALS && !defined(ALLOW_DEBUG_BUILD)
#error "DEBUG_VITALS is non-zero. Add -DALLOW_DEBUG_BUILD to your build flags to confirm this is a development build that must not be shipped to patients."
#endif

// ─── Development / commissioning bypass ──────────────────────────────────────
// ALLOW_UNENROLLED_DEVICES_FOR_DEV=1 allows the hub to connect to any nearby
// device that advertises a target health-service UUID, bypassing the
// bonded-device / MAC allow-list identity gate.
//
// Use this flag ONLY during initial BLE commissioning — i.e., when pairing
// patient devices to the hub for the first time so their bond keys are stored
// in flash — or during bench testing.  Once the patient's devices are bonded,
// rebuild with ALLOW_UNENROLLED_DEVICES_FOR_DEV=0 (the default) before shipping.
//
// With the default of 0 the hub is fail-closed: unrecognized devices are
// silently ignored regardless of which service UUIDs they advertise.
//
// A SECOND explicit acknowledgment — ALLOW_COMMISSIONING_BUILD — is ALSO
// required whenever ALLOW_UNENROLLED_DEVICES_FOR_DEV is enabled.  Both flags
// must appear together in your build flags; requiring two independent edits
// ensures the identity bypass cannot be enabled by a single accidental change.
// Neither flag may appear in a shipping build.
#ifndef ALLOW_UNENROLLED_DEVICES_FOR_DEV
#  define ALLOW_UNENROLLED_DEVICES_FOR_DEV 0
#endif

#if ALLOW_UNENROLLED_DEVICES_FOR_DEV && !defined(ALLOW_COMMISSIONING_BUILD)
#  error "ALLOW_UNENROLLED_DEVICES_FOR_DEV=1 bypasses the patient-device identity gate. You must also add -DALLOW_COMMISSIONING_BUILD to your build flags to confirm this is an intentional commissioning build. Both flags must be set together and neither may appear in a shipping build."
#endif

// ─── MAC allow-list (secondary identity gate) ─────────────────────────────────
// BLE bonding with persistent IRK storage is the primary enrollment control.
// The MAC allow-list below is a secondary gate for devices known to use a
// stable public or static-random Bluetooth address.  Such devices do NOT set
// peer_addr.addr_id_peer=1 in scan reports (that flag is only set when the
// SoftDevice resolves a Resolvable Private Address via a stored IRK).  They
// must therefore be listed here so the hub can connect and re-connect to them
// across reboots without needing ALLOW_UNENROLLED_DEVICES_FOR_DEV=1.
//
// MAC filtering MUST NOT be the sole identity gate.  It provides no protection
// for devices that rotate their address (Bluetooth Privacy) and no spoofing
// resistance even for static-address devices.
//
// How to populate after commissioning:
//   The bond_established note written to commissioning.db on Notehub contains
//   the ble_addr (colon-separated, MSB-first) and addr_type for each enrolled
//   device.  For any device whose addr_type is 0x00 (public) or 0x01
//   (random_static), add an entry below — inverting the byte order to LSB-first
//   — and rebuild with ALLOW_UNENROLLED_DEVICES_FOR_DEV=0.
//
//   Devices with addr_type 0x02 (random private resolvable / RPA) do NOT need
//   an entry here: the SoftDevice resolves the rotating RPA using the stored
//   IRK and sets addr_id_peer=1 in the scan report, so the hub recognizes them
//   automatically across reboots and address rotations.
//
//   Devices with addr_type 0x03 (random private NON-resolvable) are NOT
//   supported in the fail-closed production flow.  Non-resolvable addresses
//   rotate without any IRK linkage — the hub has no way to re-identify the
//   same physical device across address changes.  A patient device that reports
//   addr_type 0x03 requires a different identity mechanism (e.g. a proprietary
//   advertisement payload or out-of-band enrollment record) before it can be
//   used in a production deployment.
//
// Both addr and addr_type are checked to prevent false matches between a
// public-address device and a static-random device that happens to share the
// same six address bytes.
//
// Address bytes are in LSB-first order (the reverse of the human-readable
// "AA:BB:CC:DD:EE:FF" notation printed by phone BLE scanner apps).
// addr_type: BLE_GAP_ADDR_TYPE_PUBLIC = 0x00, BLE_GAP_ADDR_TYPE_RANDOM_STATIC = 0x01.
//
// Set ENROLLED_DEVICE_COUNT to the number of entries and fill the array.
// Leave at 0 when relying entirely on BLE bonding / IRK resolution for identity.

struct EnrolledDevice {
    uint8_t addr[6];   // LSB-first (as stored in ble_gap_addr_t.addr)
    uint8_t addr_type; // BLE_GAP_ADDR_TYPE_PUBLIC (0x00) or BLE_GAP_ADDR_TYPE_RANDOM_STATIC (0x01)
};

#define ENROLLED_DEVICE_COUNT  0

#if ENROLLED_DEVICE_COUNT > 0
static const EnrolledDevice ENROLLED_DEVICES[ENROLLED_DEVICE_COUNT] = {
    // { { 0x12, 0xA0, 0x3C, 0x44, 0x1F, 0xAB }, 0x00 },  // AB:1F:44:3C:A0:12 public — example
};
#endif

// ─── Notecard sync cadence ────────────────────────────────────────────────────
#define OUTBOUND_INTERVAL_MIN  15
#define INBOUND_INTERVAL_MIN   60

// How often to poll cached env-var thresholds from the Notecard.
// env.get is resolved locally on the Notecard (no cellular round-trip), so a
// 2-minute poll interval is cheap.  The Notecard fetches fresh values from
// Notehub on every inbound session (INBOUND_INTERVAL_MIN, default 60 min); a
// 2-minute local poll ensures new thresholds are applied within 2 minutes of
// that inbound sync rather than sitting unused for up to a full additional hour.
#define ENV_POLL_MS  (2UL * 60UL * 1000UL)    // 2 minutes

// ─── BLE scanner duty-cycle ───────────────────────────────────────────────────
// 100 ms window / 200 ms interval (in 0.625 ms units) — 50% duty cycle.
// Catches devices that advertise every 100–500 ms without saturating the radio.
#define BLE_SCAN_INTERVAL  320   // 200 ms
#define BLE_SCAN_WINDOW    160   // 100 ms

// ─── HR device reconnect suppression ─────────────────────────────────────────
// Heart Rate Service devices (e.g. Polar H10) stream notifications continuously
// while worn; disconnect after the first sample and suppress reconnection for
// this interval to bound the activity.qo note rate.
#define HR_SAMPLE_INTERVAL_MS  (15UL * 60UL * 1000UL)   // 15 minutes

// ─── Alert cooldown ───────────────────────────────────────────────────────────
// Minimum interval between consecutive vitals_alert.qo notes of the same type.
// Prevents alert storms when a metric stays out-of-range across multiple
// readings.  Threshold-tripping measurement notes still carry sync:true during
// cooldown and upload immediately — only the companion vitals_alert.qo note is
// held back.  The immediate upload is driven by sync:true on the measurement
// note itself; no separate hub.sync call is used.
#define ALERT_COOLDOWN_MS  (5UL * 60UL * 1000UL)   // 5 minutes

// ─── Connection watchdog ──────────────────────────────────────────────────────
// Maximum time a peripheral may remain connected without delivering a reading.
// Covers hangs inside discover() and devices that subscribe but never send.
#define MEASUREMENT_TIMEOUT_MS  (30UL * 1000UL)   // 30 seconds

// ─── Alert note retry ────────────────────────────────────────────────────────
// Number of times to retry a failed vitals_alert.qo enqueue before giving up.
#define ALERT_ENQUEUE_RETRIES  3

// ─── Debug logging macros ─────────────────────────────────────────────────────
#if DEBUG_VITALS
  #define DBG_PRINT(...)  Serial.printf(__VA_ARGS__)
  #define DBG_PRINTLN(s)  Serial.println(s)
#else
  #define DBG_PRINT(...)  do {} while (0)
  #define DBG_PRINTLN(s)  do {} while (0)
#endif
