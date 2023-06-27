#include <ArduinoRS485.h>
#include <ArduinoModbus.h>
#include <Notecard.h>

// This is the GPIO pin that will be connected to the Notecard's ATTN pin.
#ifndef ATTN_INPUT_PIN
// PA5 is the pin labeled F_D13 on the Notecarrier F.
#define ATTN_INPUT_PIN PA5
#endif

// Uncomment this line and replace com.your-company:your-product-name with your
// ProductUID.
// #define PRODUCT_UID "com.my-company.my-name:my-project"

#ifndef PRODUCT_UID
#define PRODUCT_UID ""
#pragma message "PRODUCT_UID is not defined in this example. Please ensure your Notecard has a product identifier set before running this example or define it in code here. More details at https://bit.ly/product-uid"
#endif

#ifndef HUB_SET_TIMEOUT_SECONDS
#define HUB_SET_TIMEOUT_SECONDS 5
#endif

Notecard notecard;
volatile bool attnTriggered;

// These are Modbus function codes. This code supports the most common ones. For
// a full listing, check the Modbus spec:
// https://www.modbus.org/docs/Modbus_Application_Protocol_V1_1b3.pdf
enum FunctionCode
{
    READ_COILS                 = 1,
    READ_DISCRETE_INPUTS       = 2,
    READ_MULTIPLE_HOLDING_REGS = 3,
    READ_INPUTS_REGS           = 4,
    WRITE_SINGLE_COIL          = 5,
    WRITE_SINGLE_REGISTER      = 6,
    WRITE_MULTIPLE_COILS       = 15,
    WRITE_MULTIPLE_REGS        = 16
};

// Writes to coils and holding registers are acknowledged with a note to
// responses.qo containing just the sequence number of the write request.
int acknowledgeWrite(long int seqNum)
{
    int ret = 0;

    J *req = notecard.newRequest("note.add");
    J *body = JCreateObject();
    if (req == NULL || body == NULL) {
        JDelete(req);
        JDelete(body);
        Serial.println("acknowledgeWrite error: Out of memory.");
        ret = -1;
    }
    else {
        JAddItemToObject(req, "body", body);
        JAddNumberToObject(body, "seq_num", seqNum);

        JAddStringToObject(req, "file", "responses.qo");
        JAddBoolToObject(req, "sync", true);

        if (!notecard.sendRequest(req)) {
            Serial.println("acknowledgeWrite error: note.add to "
                "responses.qo failed.");
            ret = -1;
        }
    }

    return ret;
}

int sendError(long int seqNum, const char* msg)
{
    int ret = 0;

    J *req = notecard.newRequest("note.add");
    J *body = JCreateObject();
    if (req == NULL || body == NULL) {
        JDelete(req);
        JDelete(body);
        Serial.println("sendError error: Out of memory.");
        ret = -1;
    }
    else {
        JAddItemToObject(req, "body", body);
        JAddNumberToObject(body, "seq_num", seqNum);
        JAddStringToObject(body, "error", msg);

        JAddStringToObject(req, "file", "responses.qo");
        JAddBoolToObject(req, "sync", true);

        if (!notecard.sendRequest(req)) {
            Serial.println("sendError error: note.add to "
                "responses.qo failed.");
            ret = -1;
        }
    }

    return ret;
}

// This function is used to read both coils and discrete inputs. The bits read
// out of the Modbus server are packed into a byte array. The LSB of the first
// byte corresponds to the bit at the start address, the next most significant
// bit to the start address + 1, and so on.
int readBits(long int serverAddr, long int funcCode, long int seqNum, J *data)
{
    int ret = 0;

    if (!JIsPresent(data, "addr")) {
        const char errMsg[] = "No addr field in request data.";
        Serial.println(errMsg);
        sendError(seqNum, errMsg);
        ret = -1;
    }
    else if (!JIsPresent(data, "num_bits")) {
        const char errMsg[] = "No num_bits field in request data.";
        Serial.println(errMsg);
        sendError(seqNum, errMsg);
        ret = -1;
    }
    else {
        long int addr = JGetInt(data, "addr");
        long int numBits = JGetInt(data, "num_bits");

        int type = (funcCode == READ_COILS) ? COILS : DISCRETE_INPUTS;
        if (!ModbusRTUClient.requestFrom(serverAddr, type, addr, numBits)) {
            sendError(seqNum, ModbusRTUClient.lastError());
            ret = -1;
        }
        else {
            int avail = ModbusRTUClient.available();
            if (avail > 0) {
                int numBytes = (numBits + 7) / 8;
                long int *bits = (long int *)malloc(numBytes * sizeof(long int));
                if (bits == NULL) {
                    Serial.println("readBits error: Out of memory.");
                    ret = -1;
                }
                else {
                    memset(bits, 0, numBytes * sizeof(long int));
                    for (int i = 0; i < avail; ++i) {
                        int byteIdx = i / 8;
                        int bit = ModbusRTUClient.read();
                        bits[byteIdx] |= (bit << (i % 8));
                    }

                    J *note = notecard.newRequest("note.add");
                    J *body = JCreateObject();
                    if (note == NULL || body == NULL) {
                        JDelete(note);
                        JDelete(body);
                        Serial.println("readBits error: Out of memory.");
                        ret = -1;
                    }
                    else {
                        JAddStringToObject(note, "file", "responses.qo");
                        JAddBoolToObject(note, "sync", true);
                        JAddItemToObject(note, "body", body);
                        J *bitArray = JCreateIntArray(bits, numBytes);
                        if (bitArray == NULL) {
                            JDelete(note);
                            Serial.println("readBits error: Out of memory.");
                            ret = -1;
                        }
                        else {
                            JAddItemToObject(body, "bits", bitArray);
                            JAddNumberToObject(body, "seq_num", seqNum);

                            if (!notecard.sendRequest(note)) {
                                Serial.println("readBits error: note.add to "
                                    "responses.qo failed.");
                                ret = -1;
                            }
                        }
                    }

                    free(bits);
                }
            }
            else {
                Serial.println("readBits error: No bytes available to read.");
                ret = -1;
            }
        }
    }

    return ret;
}

// This function is used to read both input and holding registers.
int readRegisters(long int serverAddr, long int funcCode, long int seqNum,
    J *data)
{
    int ret = 0;

    if (!JIsPresent(data, "addr")) {
        const char errMsg[] = "No addr field in request data.";
        Serial.println(errMsg);
        sendError(seqNum, errMsg);
        ret = -1;
    }
    else if (!JIsPresent(data, "num_regs")) {
        const char errMsg[] = "No num_regs field in request data.";
        Serial.println(errMsg);
        sendError(seqNum, errMsg);
        ret = -1;
    }
    else {
        long int addr = JGetInt(data, "addr");
        long int numRegs = JGetInt(data, "num_regs");

        int type = (funcCode == READ_MULTIPLE_HOLDING_REGS) ?
                   HOLDING_REGISTERS : INPUT_REGISTERS;
        if (!ModbusRTUClient.requestFrom(serverAddr, type, addr, numRegs)) {
            sendError(seqNum, ModbusRTUClient.lastError());
            ret = -1;
        }
        else {
            int avail = ModbusRTUClient.available();
            if (avail > 0) {
                long int *regs = (long int *)malloc(avail * sizeof(long int));
                if (regs == NULL) {
                    Serial.println("readRegisters error: Out of memory.");
                    ret = -1;
                }
                else {
                    memset(regs, 0, avail * sizeof(long int));
                    for (int i = 0; i < avail; ++i) {
                        regs[i] = ModbusRTUClient.read();
                    }

                    J *note = notecard.newRequest("note.add");
                    J *body = JCreateObject();
                    if (note == NULL || body == NULL) {
                        JDelete(note);
                        JDelete(body);
                        Serial.println("readRegisters error: Out of memory.");
                        ret = -1;
                    }
                    else {
                        JAddItemToObject(note, "body", body);
                        J *regArray = JCreateIntArray(regs, avail);
                        if (regArray == NULL) {
                            JDelete(note);
                            Serial.println("readRegisters error: Out of memory"
                                ".");
                            ret = -1;
                        }
                        else {
                            JAddItemToObject(body, "regs", regArray);
                            JAddNumberToObject(body, "seq_num", seqNum);

                            JAddStringToObject(note, "file", "responses.qo");
                            JAddBoolToObject(note, "sync", true);

                            if (!notecard.sendRequest(note)) {
                                Serial.println("readRegisters error: note.add "
                                    "to responses.qo failed.");
                                ret = -1;
                            }
                        }
                    }

                    free(regs);
                }
            }
            else {
                Serial.println("readRegisters error: No bytes available to read"
                    ".");
                ret = -1;
            }
        }
    }

    return ret;
}

// This function is used to write to a single coil. Writing a single coil uses
// a different function code than writing multiple.
int writeSingleCoil(long int serverAddr, long int seqNum, J *data)
{
    int ret = 0;

    if (!JIsPresent(data, "addr")) {
        const char errMsg[] = "No addr field in request data.";
        Serial.println(errMsg);
        sendError(seqNum, errMsg);
        ret = -1;
    }
    else if (!JIsPresent(data, "val")) {
        const char errMsg[] = "No val field in request data.";
        Serial.println(errMsg);
        sendError(seqNum, errMsg);
        ret = -1;
    }
    else {
        long int addr = JGetInt(data, "addr");
        long int val = JGetInt(data, "val");

        if (!ModbusRTUClient.coilWrite(serverAddr, addr, val)) {
            sendError(seqNum, ModbusRTUClient.lastError());
            ret = -1;
        }
        else if (acknowledgeWrite(seqNum) != 0) {
            ret = -1;
        }
    }

    return ret;
}

int writeMultipleCoils(long int serverAddr, long int seqNum, J *data)
{
    int ret = 0;

    if (!JIsPresent(data, "addr")) {
        const char errMsg[] = "No addr field in request data.";
        Serial.println(errMsg);
        sendError(seqNum, errMsg);
        ret = -1;
    }
    else if (!JIsPresent(data, "num_bits")) {
        const char errMsg[] = "No num_bits field in request data.";
        Serial.println(errMsg);
        sendError(seqNum, errMsg);
        ret = -1;
    }
    else if (!JIsPresent(data, "coil_bytes")) {
        const char errMsg[] = "No coil_bytes field in request data.";
        Serial.println(errMsg);
        sendError(seqNum, errMsg);
        ret = -1;
    }
    else {
        long int addr = JGetInt(data, "addr");
        long int numBits = JGetInt(data, "num_bits");
        J *coilBytes = JGetObjectItem(data, "coil_bytes");
        int numCoilBytes = JGetArraySize(coilBytes);
        int validBitsInLastByte = (numBits % 8) == 0 ? 8 : (numBits % 8);

        if (!ModbusRTUClient.beginTransmission(serverAddr, COILS, addr,
                numBits)) {
            Serial.println("writeMultipleCoils error: "
                "ModbusRTUClient.beginTransmission failed.");
            sendError(seqNum, ModbusRTUClient.lastError());
            ret = -1;
        }
        else {
            for (int i = 0; i < numCoilBytes; i++) {
                long int coilByte = JIntValue(JGetArrayItem(coilBytes, i));

                int bitsToWrite = (i == numCoilBytes - 1) ?
                                  validBitsInLastByte : 8;
                for (int j = 0; j < bitsToWrite; ++j) {
                    unsigned int coilBit = (coilByte & (1 << j)) >> j;
                    ModbusRTUClient.write(coilBit);
                }
            }

            if (!ModbusRTUClient.endTransmission()) {
                sendError(seqNum, ModbusRTUClient.lastError());
                ret = -1;
            }
            else if (acknowledgeWrite(seqNum) != 0) {
                ret = -1;
            }
        }
    }

    return ret;
}

int writeSingleRegister(long int serverAddr, long int seqNum, J *data)
{
    int ret = 0;

    if (!JIsPresent(data, "addr")) {
        const char errMsg[] = "No addr field in request data.";
        Serial.println(errMsg);
        sendError(seqNum, errMsg);
        ret = -1;
    }
    else if (!JIsPresent(data, "val")) {
        const char errMsg[] = "No val field in request data.";
        Serial.println(errMsg);
        sendError(seqNum, errMsg);
        ret = -1;
    }
    else {
        long int addr = JGetInt(data, "addr");
        long int val = JGetInt(data, "val");

        if (!ModbusRTUClient.holdingRegisterWrite(serverAddr, addr, val)) {
            sendError(seqNum, ModbusRTUClient.lastError());
            ret = -1;
        }
        else if (acknowledgeWrite(seqNum) != 0) {
            ret = -1;
        }
    }

    return ret;
}

int writeMultipleRegisters(long int serverAddr, long int seqNum, J *data)
{
    int ret = 0;

    if (!JIsPresent(data, "addr")) {
        const char errMsg[] = "No addr field in request data.";
        Serial.println(errMsg);
        sendError(seqNum, errMsg);
        ret = -1;
    }
    else if (!JIsPresent(data, "vals")) {
        const char errMsg[] = "No vals field in request data.";
        Serial.println(errMsg);
        sendError(seqNum, errMsg);
        ret = -1;
    }
    else {
        long int addr = JGetInt(data, "addr");
        J *vals = JGetObjectItem(data, "vals");
        int numVals = JGetArraySize(vals);

        if (!ModbusRTUClient.beginTransmission(serverAddr, HOLDING_REGISTERS,
                addr, numVals)) {
            Serial.println("writeMultipleRegisters error: "
                "ModbusRTUClient.beginTransmission failed.");
            sendError(seqNum, ModbusRTUClient.lastError());
            ret = -1;
        }
        else {
            for (int i = 0; i < numVals; i++) {
                long int val = JIntValue(JGetArrayItem(vals, i));
                ModbusRTUClient.write(val);
            }

            if (!ModbusRTUClient.endTransmission()) {
                sendError(seqNum, ModbusRTUClient.lastError());
                ret = -1;
            }
            else if (acknowledgeWrite(seqNum) != 0) {
                ret = -1;
            }
        }
    }

    return ret;
}

int handleRequest(J *req)
{
    int ret = 0;
    J *body;
    long int seqNum;

    body = JGetObject(req, "body");
    if (body == NULL) {
        Serial.println("handleRequest error: No \"body\" field in request.");
        ret = -1;
    }
    else if (!JIsPresent(body, "seq_num")) {
        const char errMsg[] = "No seq_num field in request.";
        Serial.println(errMsg);
        // No sequence number, so use -1 in the error note.
        sendError(-1, errMsg);
        ret = -1;
    }
    else {
        seqNum = JGetInt(body, "seq_num");
    }

    if (ret == 0) {
        if (!JIsPresent(body, "server_addr")) {
            const char errMsg[] = "No server_addr field in request.";
            Serial.println(errMsg);
            sendError(seqNum, errMsg);
            ret = -1;
        }
        else if (!JIsPresent(body, "seq_num")) {
            const char errMsg[] = "No seq_num field in request.";
            Serial.println(errMsg);
            sendError(seqNum, errMsg);
            ret = -1;
        }
        else if (!JIsPresent(body, "func")) {
            const char errMsg[] = "No func field in request.";
            Serial.println(errMsg);
            sendError(seqNum, errMsg);
            ret = -1;
        }
        else if (!JIsPresent(body, "data")) {
            const char errMsg[] = "No data field in request.";
            Serial.println(errMsg);
            sendError(seqNum, errMsg);
            ret = -1;
        }
        else {
            long int serverAddr = JGetInt(body, "server_addr");
            long int funcCode = JGetInt(body, "func");
            J *data = JGetObject(body, "data");

            switch (funcCode) {
                case READ_COILS:
                case READ_DISCRETE_INPUTS:
                    ret = readBits(serverAddr, funcCode, seqNum, data);
                    break;
                case READ_MULTIPLE_HOLDING_REGS:
                case READ_INPUTS_REGS:
                    ret = readRegisters(serverAddr, funcCode, seqNum, data);
                    break;
                case WRITE_SINGLE_COIL:
                    ret = writeSingleCoil(serverAddr, seqNum, data);
                    break;
                case WRITE_SINGLE_REGISTER:
                    ret = writeSingleRegister(serverAddr, seqNum, data);
                    break;
                case WRITE_MULTIPLE_COILS:
                    ret = writeMultipleCoils(serverAddr, seqNum, data);
                    break;
                case WRITE_MULTIPLE_REGS:
                    ret = writeMultipleRegisters(serverAddr, seqNum, data);
                    break;
                default:
                    const char errMsg[] = "Function code not supported.";
                    Serial.println(errMsg);
                    sendError(seqNum, errMsg);
                    ret = -1;
                    break;
            }
        }
    }

    return ret;
}

void attnISR()
{
    // This flag will be read in the main loop. We keep the ISR lean and do all
    // the logic in the main loop.
    attnTriggered = true;
}

// Arm the ATTN interrupt.
void attnArm()
{
    // Once ATTN has triggered, it stays set until explicitly reset. Reset it
    // here. It will trigger again after a change to the watched Notefile.
    attnTriggered = false;
    J *req = notecard.newRequest("card.attn");
    JAddStringToObject(req, "mode", "reset");
    if (!notecard.sendRequest(req)) {
        Serial.println("card.attn reset failed");
    }
}

void NoteUserAgentUpdate(J *ua)
{
    JAddStringToObject(ua, "app", "nf34");
}

void setup()
{
    Serial.begin(115200);
#ifndef NF34_NO_WAIT_FOR_SERIAL
    while (!Serial);
#endif

    notecard.begin();
    notecard.setDebugOutputStream(Serial);

    if (!ModbusRTUClient.begin(115200)) {
        Serial.println("Failed to start Modbus RTU Client!");
        while (1) {}
    }

    J *req = notecard.newRequest("hub.set");
    if (PRODUCT_UID[0]) {
       JAddStringToObject(req, "product", PRODUCT_UID);
    }
    JAddStringToObject(req, "mode", "continuous");
    JAddBoolToObject(req, "sync", true);
    // The hub.set request may fail if it's sent shortly after power up. We use
    // sendRequestWithRetry to give it a chance to succeed.
    if (!notecard.sendRequestWithRetry(req, HUB_SET_TIMEOUT_SECONDS)) {
        Serial.println("hub.set failed");
    }

    // Disarm ATTN to clear any previous state before re-arming.
    req = notecard.newRequest("card.attn");
    JAddStringToObject(req, "mode", "disarm,-files");
    if (!notecard.sendRequest(req)) {
        Serial.println("card.attn disarming failed");
    }

    // Configure ATTN to watch for changes to requests.qi.
    req = notecard.newRequest("card.attn");
    const char *filesToWatch[] = {"requests.qi"};
    int numFilesToWatch = sizeof(filesToWatch) / sizeof(const char *);
    J *filesArray = JCreateStringArray(filesToWatch, numFilesToWatch);
    JAddItemToObject(req, "files", filesArray);
    JAddStringToObject(req, "mode", "files");
    if (!notecard.sendRequest(req)) {
        Serial.println("card.attn configuration failed");
    }

    // Set up the GPIO pin connected to ATTN as an input.
    pinMode(ATTN_INPUT_PIN, INPUT);
    // When that pin detects a rising edge, jump to the attnISR interrupt
    // handler.
    attachInterrupt(digitalPinToInterrupt(ATTN_INPUT_PIN), attnISR, RISING);

    // Arm the interrupt, so that we are notified whenever ATTN rises.
    attnArm();
}

void loop()
{
    if (attnTriggered) {
        while (true) {
            // Pop the next available note from requests.qi.
            J *req = NoteNewRequest("note.get");
            JAddStringToObject(req, "file", "requests.qi");
            JAddBoolToObject(req, "delete", true);
            J *rsp = NoteRequestResponse(req);

            if (rsp != NULL) {
                // If an error is returned, this means that no response is
                // pending. Note that it's expected that this might return
                // either a "note does not exist" error if there are no
                // pending inbound notes, or a "file does not exist" error
                // if the inbound queue hasn't yet been created on Notehub.
                if (notecard.responseError(rsp)) {
                    notecard.deleteResponse(rsp);
                    break;
                }

                handleRequest(rsp);
                J *hubSyncReq = notecard.newRequest("hub.sync");
                if (!notecard.sendRequest(hubSyncReq)) {
                    Serial.println("hub.sync failed.");
                }

                notecard.deleteResponse(rsp);
            }
            else {
                Serial.println("note.get from requests.qi failed.");
            }
        }

        // Re-arm the ATTN interrupt.
        attnArm();
    }
}
