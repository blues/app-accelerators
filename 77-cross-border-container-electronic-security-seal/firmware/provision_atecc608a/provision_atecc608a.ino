/*
 * provision_atecc608a.ino
 *
 * One-time ATECC608A provisioning utility for the Cross-Border Container
 * Electronic Security Seal.
 *
 * Purpose
 * -------
 * This sketch runs ONCE per device, before the container_seal.ino main
 * firmware is flashed and before the enclosure is sealed.  It:
 *   1. Verifies the ATECC608A responds at I²C address 0x60.
 *   2. Writes the SparkFun default configuration (sets slot 0 as an ECC
 *      P-256 key pair slot).
 *   3. Permanently locks the configuration zone.
 *   4. Generates a fresh ECC P-256 key pair in slot 0 (private key is
 *      created inside the chip and can never be read back).
 *   5. Prints the 64-byte uncompressed public key in hex to Serial.
 *   6. Permanently locks the data zone.
 *
 * WARNINGS
 * --------
 *   - Steps 3 and 6 are PERMANENT and IRREVERSIBLE.  Once the config and
 *     data zones are locked, the ATECC608A cannot be re-configured or
 *     re-keyed.  Run this sketch only once per device on a brand-new,
 *     unlocked chip.
 *   - The 64-byte public key printed in step 5 is the ONLY way to retrieve
 *     the key for downstream signature verification.  Record it immediately
 *     alongside the device serial number in your provisioning database
 *     BEFORE allowing lockDataAndOTP() to proceed.
 *   - Do NOT flash or run the main container_seal.ino firmware until
 *     provisioning is complete and the public key is recorded.
 *
 * Hardware
 * --------
 *   - Blues Swan (or any Arduino-compatible host) + Notecarrier XI (or any
 *     carrier with a Qwiic / I²C port).
 *   - SparkFun Qwiic ATECCX08A Breakout (DEV-18077) connected to the Qwiic
 *     port (SDA / SCL / 3.3 V / GND).  I²C address: 0x60.
 *   - USB serial monitor open at 115200 baud.
 *
 * Dependencies
 * ------------
 *   SparkFun_ATECCX08a_Arduino_Library
 *     Install via Arduino Library Manager (search "SparkFun ATECCX08a") or:
 *     arduino-cli lib install "SparkFun ATECCX08a Arduino Library"
 *
 * Procedure
 * ---------
 *   1. Build and flash this sketch (NOT container_seal.ino) onto the Swan.
 *   2. Open the serial monitor at 115200 baud.
 *   3. Follow the step-by-step prompts — the sketch pauses at each
 *      destructive step and requires you to type "YES" to continue.
 *   4. After step 5 (key generation), copy the printed 64-byte public key
 *      into your provisioning database alongside the device serial number.
 *      The public key is also stored as a Notehub device environment
 *      variable `pub_key_hex` so downstream routes can retrieve it.
 *   5. Type "YES" to confirm you have recorded the key, then the sketch
 *      proceeds to lock the data zone.
 *   6. After lockDataAndOTP() completes, reflash the device with the main
 *      container_seal.ino firmware.
 */

#include <Wire.h>
#include <SparkFun_ATECCX08a_Arduino_Library.h>

// ---------------------------------------------------------------------------
// ATECC608A I²C address (SparkFun default — must match SEAL_SIGN_KEY_SLOT
// in container_seal_sign.h)
// ---------------------------------------------------------------------------
#define ATECC_ADDR 0x60
#define KEY_SLOT   0

ATECCX08A atecc;

// ---------------------------------------------------------------------------
// Helper: wait for the user to type "YES" on Serial before continuing.
// Any other input aborts the sketch.
// ---------------------------------------------------------------------------
static bool waitForConfirm(const char *prompt) {
    Serial.println();
    Serial.print(">>> ");
    Serial.println(prompt);
    Serial.println("    Type YES (all-caps) and press Enter to continue,");
    Serial.println("    or any other input to ABORT.");
    Serial.print("    Response: ");

    while (!Serial.available()) { /* wait */ }
    String resp = Serial.readStringUntil('\n');
    resp.trim();
    if (resp == "YES") {
        Serial.println("YES — continuing.");
        return true;
    }
    Serial.print("'");
    Serial.print(resp);
    Serial.println("' — ABORTED.  Reflash and restart to try again.");
    return false;
}

// ---------------------------------------------------------------------------
// Helper: print a byte array as two-char lowercase hex, 16 bytes per line.
// ---------------------------------------------------------------------------
static void printHex(const uint8_t *buf, size_t len) {
    static const char kHex[] = "0123456789abcdef";
    for (size_t i = 0; i < len; i++) {
        if (i > 0 && i % 16 == 0) Serial.println();
        Serial.print(kHex[buf[i] >> 4]);
        Serial.print(kHex[buf[i] & 0xF]);
    }
    Serial.println();
}

// ---------------------------------------------------------------------------
// setup — all provisioning logic runs here; loop() is unreachable
// ---------------------------------------------------------------------------
void setup() {
    Serial.begin(115200);
    {
        const unsigned long t0 = millis();
        while (!Serial && (millis() - t0) < 5000) {}
    }
    Wire.begin();

    Serial.println();
    Serial.println("=========================================");
    Serial.println("  ATECC608A Provisioning Utility");
    Serial.println("  Cross-Border Container Security Seal");
    Serial.println("=========================================");
    Serial.println();
    Serial.println("WARNING: Steps 3 and 6 are PERMANENT and IRREVERSIBLE.");
    Serial.println("Run this sketch only on a brand-new, unlocked ATECC608A.");
    Serial.println();

    // ---- Step 1: Verify chip presence ------------------------------------
    Serial.println("Step 1: Verifying ATECC608A at I2C address 0x60...");
    if (!atecc.begin(ATECC_ADDR)) {
        Serial.println("  FAILED: chip not found at 0x60.");
        Serial.println("  Check the Qwiic cable connection and retry.");
        Serial.println("  Provisioning ABORTED.");
        for (;;) {}
    }
    Serial.println("  OK: chip responded.");

    // ---- Step 2: Write SparkFun default configuration --------------------
    Serial.println();
    Serial.println("Step 2: Writing SparkFun default configuration...");
    Serial.println("  (Sets slot 0 as an ECC P-256 key pair slot.)");

    // writeConfigSparkFun() returns true on success.
    if (!atecc.writeConfigSparkFun()) {
        Serial.println("  FAILED: writeConfigSparkFun() returned false.");
        Serial.println("  The chip may already be configured or locked.");
        Serial.println("  Provisioning ABORTED.");
        for (;;) {}
    }
    Serial.println("  OK: configuration written.");

    // ---- Step 3: Lock configuration zone (IRREVERSIBLE) -----------------
    Serial.println();
    Serial.println("Step 3: Lock configuration zone.");
    Serial.println("  This is PERMANENT — the configuration cannot be changed");
    Serial.println("  after locking.");
    if (!waitForConfirm("Confirm: lock the configuration zone?")) {
        for (;;) {}
    }

    if (!atecc.lockConfig()) {
        Serial.println("  FAILED: lockConfig() returned false.");
        Serial.println("  Provisioning ABORTED.");
        for (;;) {}
    }
    Serial.println("  OK: configuration zone locked.");

    // ---- Step 4: Generate ECC P-256 key pair in slot 0 ------------------
    Serial.println();
    Serial.println("Step 4: Generating ECC P-256 key pair in slot 0...");
    Serial.println("  The private key is generated and stored inside the chip.");
    Serial.println("  It can NEVER be read back.");

    if (!atecc.createNewKeyPair(KEY_SLOT)) {
        Serial.println("  FAILED: createNewKeyPair() returned false.");
        Serial.println("  Provisioning ABORTED.");
        for (;;) {}
    }
    Serial.println("  OK: key pair generated.");

    // ---- Step 5: Print public key — MUST be recorded before locking -----
    Serial.println();
    Serial.println("Step 5: PUBLIC KEY (64 bytes, uncompressed, hex)");
    Serial.println("  *** COPY THIS KEY AND RECORD IT NOW ***");
    Serial.println("  Store it alongside the device serial number in your");
    Serial.println("  provisioning database.  This is the ONLY opportunity");
    Serial.println("  to retrieve the public key; once the data zone is");
    Serial.println("  locked it cannot be re-generated or re-read.");
    Serial.println();

    // publicKey64Bytes[] is populated by createNewKeyPair().
    Serial.print("  pub_key_hex = ");
    printHex(atecc.publicKey64Bytes, 64);

    Serial.println();
    Serial.println("  Full hex (one line for copy-paste):");
    static const char kHex2[] = "0123456789abcdef";
    for (int i = 0; i < 64; i++) {
        Serial.print(kHex2[atecc.publicKey64Bytes[i] >> 4]);
        Serial.print(kHex2[atecc.publicKey64Bytes[i] & 0xF]);
    }
    Serial.println();
    Serial.println();
    Serial.println("  Recommended: add this as a Notehub device environment");
    Serial.println("  variable named 'pub_key_hex' so downstream routes can");
    Serial.println("  retrieve it for signature verification.");

    // ---- Step 6: Lock data zone (IRREVERSIBLE) ---------------------------
    Serial.println();
    Serial.println("Step 6: Lock data zone.");
    Serial.println("  This is PERMANENT — slot 0 cannot be overwritten after");
    Serial.println("  locking.  Confirm you have recorded the public key above");
    Serial.println("  before proceeding.");

    if (!waitForConfirm("Confirm: you have recorded the public key AND want to lock the data zone?")) {
        for (;;) {}
    }

    if (!atecc.lockDataAndOTP()) {
        Serial.println("  FAILED: lockDataAndOTP() returned false.");
        Serial.println("  Provisioning ABORTED.");
        for (;;) {}
    }
    Serial.println("  OK: data zone locked.");

    // ---- Provisioning complete -------------------------------------------
    Serial.println();
    Serial.println("=========================================");
    Serial.println("  Provisioning COMPLETE.");
    Serial.println("=========================================");
    Serial.println();
    Serial.println("Next steps:");
    Serial.println("  1. Record the public key in your provisioning database");
    Serial.println("     alongside the device serial number (if not done yet).");
    Serial.println("  2. Optionally set 'pub_key_hex' as a Notehub device");
    Serial.println("     environment variable for downstream route use.");
    Serial.println("  3. Reflash the device with the main container_seal.ino");
    Serial.println("     firmware.");
    Serial.println("  4. Seal the enclosure.");
    Serial.println();
    Serial.println("This provisioning sketch is no longer needed on this device.");
}

// ---------------------------------------------------------------------------
// loop — unreachable under normal operation
// ---------------------------------------------------------------------------
void loop() {
    // Provisioning is a one-shot operation; all logic runs in setup().
    // If somehow loop() is reached, idle indefinitely to prevent re-running.
    delay(60000);
}
