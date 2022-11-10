/*!
 * @file n_serial.c
 *
 * Written by Ray Ozzie and Blues Inc. team.
 *
 * Copyright (c) 2019 Blues Inc. MIT License. Use of this source code is
 * governed by licenses granted by the copyright holder including that found in
 * the
 * <a href="https://github.com/blues/note-c/blob/master/LICENSE">LICENSE</a>
 * file.
 *
 */

#include "n_lib.h"

// Turbo I/O mode
extern bool cardTurboIO;

/**************************************************************************/
/*!
    @brief  Given a JSON string, perform an Serial transaction with the Notecard.
    @param   json
               A c-string containing the JSON request object.
    @param   jsonResponse
               An out parameter c-string buffer that will contain the JSON
               response from the Notercard.
  @returns a c-string with an error, or `NULL` if no error ocurred.
*/
/**************************************************************************/
const char *serialNoteTransaction(char *json, char **jsonResponse)
{

    // Append newline to the transaction
    int jsonLen = strlen(json);
    uint8_t *transmitBuf = (uint8_t *) _Malloc(jsonLen+c_newline_len);
    if (transmitBuf == NULL) {
        return ERRSTR("insufficient memory",c_mem);
    }
    memcpy(transmitBuf, json, jsonLen);
    memcpy(&transmitBuf[jsonLen], c_newline, c_newline_len);
    jsonLen += c_newline_len;

    // Transmit the request in segments so as not to overwhelm the notecard's interrupt buffers
    uint32_t segOff = 0;
    uint32_t segLeft = jsonLen;
    while (true) {
        size_t segLen = segLeft;
        if (segLen > CARD_REQUEST_SERIAL_SEGMENT_MAX_LEN) {
            segLen = CARD_REQUEST_SERIAL_SEGMENT_MAX_LEN;
        }
        _SerialTransmit((uint8_t *)&transmitBuf[segOff], segLen, false);
        segOff += segLen;
        segLeft -= segLen;
        if (segLeft == 0) {
            break;
        }
        if (!cardTurboIO) {
            _DelayMs(CARD_REQUEST_SERIAL_SEGMENT_DELAY_MS);
        }
    }

    // Free the transmit buffer
    _Free(transmitBuf);

    // If no reply expected, we're done
    if (jsonResponse == NULL) {
        return NULL;
    }

    // Wait for something to become available, processing timeout errors up-front
    // because the json parse operation immediately following is subject to the
    // serial port timeout. We'd like more flexibility in max timeout and ultimately
    // in our error handling.
    uint32_t startMs;
    for (startMs = _GetMs(); !_SerialAvailable(); ) {
        if (_GetMs() >= startMs + (NOTECARD_TRANSACTION_TIMEOUT_SEC*1000)) {
#ifdef ERRDBG
            _Debug("reply to request didn't arrive from module in time\n");
#endif
            return ERRSTR("transaction timeout {io}",c_iotimeout);
        }
        if (!cardTurboIO) {
            _DelayMs(10);
        }
    }

    // Allocate a buffer for input, noting that we always put the +1 in the alloc so we can be assured
    // that it can be null-terminated.  This must be the case because json parsing requires a
    // null-terminated string.
    int jsonbufAllocLen = ALLOC_CHUNK;
    char *jsonbuf = (char *) _Malloc(jsonbufAllocLen+1);
    if (jsonbuf == NULL) {
#ifdef ERRDBG
        _Debug("transaction: jsonbuf malloc failed\n");
#endif
        return ERRSTR("insufficient memory",c_mem);
    }
    int jsonbufLen = 0;
    char ch = 0;
    startMs = _GetMs();
    while (ch != '\n') {
        if (!_SerialAvailable()) {
            ch = 0;
            if (_GetMs() >= startMs + (NOTECARD_TRANSACTION_TIMEOUT_SEC*1000)) {
#ifdef ERRDBG
                jsonbuf[jsonbufLen] = '\0';
                _Debug("received only partial reply after timeout:\n");
                _Debug(jsonbuf);
                _Debug("\n");
#endif
                _Free(jsonbuf);
                return ERRSTR("transaction incomplete {io}",c_iotimeout);
            }
            if (!cardTurboIO) {
                _DelayMs(1);
            }
            continue;
        }
        ch = _SerialReceive();

        // Because serial I/O can be error-prone, catch common bad data early, knowing that we only accept ASCII
        if (ch == 0 || (ch & 0x80) != 0) {
#ifdef ERRDBG
            _Debug("invalid data received on serial port from notecard\n");
#endif
            _Free(jsonbuf);
            return ERRSTR("serial communications error {io}",c_iotimeout);
        }

        // Append into the json buffer
        jsonbuf[jsonbufLen++] = ch;
        if (jsonbufLen >= jsonbufAllocLen) {
            jsonbufAllocLen += ALLOC_CHUNK;
            char *jsonbufNew = (char *) _Malloc(jsonbufAllocLen+1);
            if (jsonbufNew == NULL) {
#ifdef ERRDBG
                _Debug("transaction: jsonbuf malloc grow failed\n");
#endif
                _Free(jsonbuf);
                return ERRSTR("insufficient memory",c_mem);
            }
            memcpy(jsonbufNew, jsonbuf, jsonbufLen);
            _Free(jsonbuf);
            jsonbuf = jsonbufNew;
        }
    }

    // Null-terminate it, using the +1 space that we'd allocated in the buffer
    jsonbuf[jsonbufLen] = '\0';

    // Return it
    *jsonResponse = jsonbuf;
    return NULL;

}

//**************************************************************************/
/*!
    @brief  Initialize or re-initialize the Serial bus, returning false if
            anything fails.
    @returns a boolean. `true` if the reset was successful, `false`, if not.
*/
/**************************************************************************/
bool serialNoteReset()
{

    // Initialize, or re-initialize.  Because we've observed Arduino serial driver flakiness,
    _DelayMs(250);
    if (!_SerialReset()) {
        return false;
    }

    // The guaranteed behavior for robust resyncing is to send two newlines
    // and  wait for two echoed blank lines in return.
    bool notecardReady = false;
    int retries;
    for (retries=0; retries<10; retries++) {

        // Send a newline to the module to clean out request/response processing
        _SerialTransmit((uint8_t *)c_newline, c_newline_len, true);

        // Drain all serial for 500ms
        bool somethingFound = false;
        bool nonControlCharFound = false;
        uint32_t startMs = _GetMs();
        while (_GetMs() < startMs+500) {
            while (_SerialAvailable()) {
                somethingFound = true;
                if (_SerialReceive() >= ' ') {
                    nonControlCharFound = true;
                }
            }
            _DelayMs(1);
        }

        // If all we got back is newlines, we're ready
        if (somethingFound && !nonControlCharFound) {
            notecardReady = true;
            break;
        }

#ifdef ERRDBG
        _Debug(somethingFound ? "unrecognized data from notecard\n" : "notecard not responding\n");
#else
        _Debug("no notecard\n");
#endif
        _DelayMs(500);
        _SerialReset();

    }

    // Done
    return notecardReady;
}
