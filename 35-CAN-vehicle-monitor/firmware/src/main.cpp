#include <CAN.h>

#include "can_id_manager.h"

// Uncomment this line and replace com.your-company:your-product-name with your
// ProductUID.
// #define PRODUCT_UID "com.my-company.my-name:my-project"

#ifndef PRODUCT_UID
#define PRODUCT_UID ""
#pragma message "PRODUCT_UID is not defined in this example. Please ensure your Notecard has a product identifier set before running this example or define it in code here. More details at https://bit.ly/product-uid"
#endif

#ifndef ENV_VAR_POLL_MS
#define ENV_VAR_POLL_MS (15 * 1000) // 15 seconds
#endif

#define REQUESTID_TEMPLATE 1
#define APPLICATION_NOTEFILE "data.qo"

#define MAX_CAN_DATA_BYTES 8

#ifndef HUB_SET_TIMEOUT
#define HUB_SET_TIMEOUT 5
#endif

Notecard notecard;
CanIdManager canIdManager = CanIdManager(notecard, ENV_VAR_POLL_MS);

// Register the notefile template for our data.
static bool registerNotefileTemplate()
{
    notecard.logDebugf("Registering %s template.\r\n", APPLICATION_NOTEFILE);
    J *req = notecard.newRequest("note.template");
    if (req == NULL) {
        notecard.logDebug("Failed to create note.template request.\r\n");
        return false;
    }

    J *body = JCreateObject();
    if (body == NULL) {
        JDelete(req);
        notecard.logDebug("Failed to create note.template request body.\r\n");
        return false;
    }

    // Add an ID to the request, which will be echo'ed back in the response by
    // the notecard itself. This helps us to identify the asynchronous response
    // without needing to have an additional state.
    JAddNumberToObject(req, "id", REQUESTID_TEMPLATE);
    // Specify the template Notefile.
    JAddStringToObject(req, "file", APPLICATION_NOTEFILE);
    // Add the CAN ID field.
    JAddNumberToObject(body, "id", TUINT32);
    // Add the CAN data field, which is an array of up to 8 bytes.
    const JNUMBER dataTemplate[MAX_CAN_DATA_BYTES] = {
        TUINT8, TUINT8, TUINT8, TUINT8, TUINT8, TUINT8, TUINT8, TUINT8
    };
    J *data = JCreateNumberArray(dataTemplate, MAX_CAN_DATA_BYTES);
    JAddItemToObject(body, "data", data);
    // Add the data length field, which indicates how many valid bytes are in
    // the data array.
    JAddNumberToObject(body, "length", TUINT8);
    JAddItemToObject(req, "body", body);

    if (!notecard.sendRequest(req)) {
        notecard.logDebug("Failed to send note.template request.\r\n");
        return false;
    }

    notecard.logDebug("Template registration succeeded.\r\n");

    return true;
}

static bool sendDataNote(uint32_t id, const uint8_t* data, int len)
{
    J *req = notecard.newRequest("note.add");
    if (req == NULL) {
        notecard.logDebug("Failed to create note.add request.\r\n");
        return false;
    }

    J *body = JCreateObject();
    if (body == NULL) {
        JDelete(req);
        notecard.logDebug("Failed to create note.add request body.\r\n");
        return false;
    }

    JAddStringToObject(req, "file", APPLICATION_NOTEFILE);
    // Add the CAN ID to the note.
    JAddNumberToObject(body, "id", id);
    // Add the CAN data to the note.
    J *dataArray = JAddArrayToObject(body, "data");
    for (int i = 0; i < len; ++i) {
        JAddItemToArray(dataArray, JCreateNumber(data[i]));
    }
    // Add the CAN data length to the note.
    JAddNumberToObject(body, "length", len);
    JAddItemToObject(req, "body", body);
    JAddBoolToObject(req, "sync", true);
    if (!notecard.sendRequest(req)) {
        notecard.logDebug("Failed to send data.\r\n");
        return false;
    }

    notecard.logDebug("Sending data note to Notehub.\r\n");

    return true;
}

void setup() {
    // Set up debug output via serial connection.
    delay(2500);
    Serial.begin(115200);

    // Initialize the physical I/O channel to the Notecard.
    Wire.begin();
    notecard.begin();

    notecard.setDebugOutputStream(Serial);

    pinMode(PIN_CAN_STANDBY, OUTPUT);
    digitalWrite(PIN_CAN_STANDBY, false); // turn off STANDBY
    pinMode(PIN_CAN_BOOSTEN, OUTPUT);
    digitalWrite(PIN_CAN_BOOSTEN, true); // turn on booster

    // start the CAN bus at 250 kbps
    if (!CAN.begin(250000)) {
        notecard.logDebug("CAN.begin failed!");
        return;
    }

    J *req = notecard.newRequest("hub.set");
    if (PRODUCT_UID[0]) {
       JAddStringToObject(req, "product", PRODUCT_UID);
    }
    JAddStringToObject(req, "mode", "continuous");
    JAddBoolToObject(req, "sync", true);
    // The hub.set request may fail if it's sent shortly after power up. We use
    // NoteRequestWithRetry to give it a chance to succeed.
    if (!NoteRequestWithRetry(req, HUB_SET_TIMEOUT)) {
        notecard.logDebug("hub.set failed");
    }

    registerNotefileTemplate();
}


void loop() {
    canIdManager.updateIdsFromEnvironment();

    // try to parse packet
    int packetSize = CAN.parsePacket();

    if (packetSize) {
        // received a packet
        notecard.logDebug("Received ");

        if (CAN.packetExtended()) {
            notecard.logDebug("extended ");
        }

        if (CAN.packetRtr()) {
            // Remote transmission request, packet contains no data
            notecard.logDebug("RTR ");
        }

        uint32_t packetId = (uint32_t)CAN.packetId();
        notecard.logDebugf("packet with id 0x%x.\r\n", packetId);

        size_t numIds = canIdManager.getNumIds();
        uint32_t *ids = canIdManager.getIds();
        bool ignoreId = true;
        for (size_t i = 0; i < numIds; ++i) {
            if (packetId == ids[i]) {
                notecard.logDebug("ID matches filter list, keeping packet."
                    "\r\n");
                ignoreId = false;
                break;
            }
        }

        if (ignoreId) {
            notecard.logDebug("ID not in filter list, dropping packet.\r\n");
        }
        else {
            int len = CAN.available();
            if (len > MAX_CAN_DATA_BYTES) {
                // This should never happen, but just to be safe...
                notecard.logDebugf("Error: %d bytes in packet, which is greater"
                    " than %d max. Dropping packet.\r\n", len,
                    MAX_CAN_DATA_BYTES);
            }
            else {
                notecard.logDebugf("%d bytes in packet. Reading...\r\n", len);

                uint8_t rxBuffer[MAX_CAN_DATA_BYTES];
                for (int i = 0; i < len; ++i) {
                    rxBuffer[i] = CAN.read();
                }

                if (!sendDataNote(packetId, rxBuffer, len)) {
                    notecard.logDebug("Failed to send data to Notehub.\r\n");
                }
            }
        }
    }
}

void NoteUserAgentUpdate(J *ua) {
    JAddStringToObject(ua, "app", "nf35");
}
