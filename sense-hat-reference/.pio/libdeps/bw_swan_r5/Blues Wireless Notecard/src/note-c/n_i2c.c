/*!
 * @file n_i2c.c
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

// Forwards
static void _DelayIO(void);

/**************************************************************************/
/*!
  @brief  We've noticed that there's an instability in some cards'
  implementations of I2C, and as a result we introduce an intentional
  delay before each and every I2C I/O.The timing was computed
  empirically based on a number of commercial devices.
*/
/**************************************************************************/
static void _DelayIO()
{
    if (!cardTurboIO) {
        _DelayMs(6);
    }
}

/**************************************************************************/
/*!
  @brief  Given a JSON string, perform an I2C transaction with the Notecard.
  @param   json
  A c-string containing the JSON request object.
  @param   jsonResponse
  An out parameter c-string buffer that will contain the JSON
  response from the Notercard.
  @returns a c-string with an error, or `NULL` if no error ocurred.
*/
/**************************************************************************/
const char *i2cNoteTransaction(char *json, char **jsonResponse)
{

    // Append newline to the transaction
    int jsonLen = strlen(json);
    uint8_t *transmitBuf = (uint8_t *) _Malloc(jsonLen+1);
    if (transmitBuf == NULL) {
        return ERRSTR("insufficient memory",c_mem);
    }
    memcpy(transmitBuf, json, jsonLen);
    transmitBuf[jsonLen++] = '\n';

    // Transmit the request in chunks, but also in segments so as not to overwhelm the notecard's interrupt buffers
    const char *estr;
    uint8_t *chunk = transmitBuf;
    uint32_t sentInSegment = 0;
    while (jsonLen > 0) {
        int chunklen = (uint8_t) (jsonLen > (int)_I2CMax() ? (int)_I2CMax() : jsonLen);
        _LockI2C();
        _DelayIO();
        estr = _I2CTransmit(_I2CAddress(), chunk, chunklen);
        if (estr != NULL) {
            _Free(transmitBuf);
            _I2CReset(_I2CAddress());
            _UnlockI2C();
#ifdef ERRDBG
            _Debug("i2c transmit: ");
            _Debug(estr);
            _Debug("\n");
#endif
            return estr;
        }
        _UnlockI2C();
        chunk += chunklen;
        jsonLen -= chunklen;
        sentInSegment += chunklen;
        if (sentInSegment > CARD_REQUEST_I2C_SEGMENT_MAX_LEN) {
            sentInSegment = 0;
            if (!cardTurboIO) {
                _DelayMs(CARD_REQUEST_I2C_SEGMENT_DELAY_MS);
            }
        }
        if (!cardTurboIO) {
            _DelayMs(CARD_REQUEST_I2C_CHUNK_DELAY_MS);
        }
    }

    // Free the transmit buffer
    _Free(transmitBuf);

    // If no reply expected, we're done
    if (jsonResponse == NULL) {
        return NULL;
    }

    // Dynamically grow the buffer as we read.  Note that we always put the +1 in the alloc
    // so we can be assured that it can be null-terminated, which must be the case because
    // our json parser requires a null-terminated string.
    int growlen = ALLOC_CHUNK;
    int jsonbufAllocLen = growlen;
    char *jsonbuf = (char *) _Malloc(jsonbufAllocLen+1);
    if (jsonbuf == NULL) {
#ifdef ERRDBG
        _Debug("transaction: jsonbuf malloc failed\n");
#endif
        return ERRSTR("insufficient memory",c_mem);
    }

    // Loop, building a reply buffer out of received chunks.  We'll build the reply in the same
    // buffer we used to transmit, and will grow it as necessary.
    bool receivedNewline = false;
    int jsonbufLen = 0;
    int chunklen = 0;
    uint32_t startMs = _GetMs();
    while (true) {

        // Grow the buffer as necessary to read this next chunk
        if (jsonbufLen + chunklen > jsonbufAllocLen) {
            if (chunklen > growlen) {
                jsonbufAllocLen += chunklen;
            } else {
                jsonbufAllocLen += growlen;
            }
            char *jsonbufNew = (char *) _Malloc(jsonbufAllocLen+1);
            if (jsonbufNew == NULL) {
#ifdef ERRDBG
                _Debug("transaction: jsonbuf grow malloc failed\n");
#endif
                _Free(jsonbuf);
                return ERRSTR("insufficient memory",c_mem);
            }
            memcpy(jsonbufNew, jsonbuf, jsonbufLen);
            _Free(jsonbuf);
            jsonbuf = jsonbufNew;
        }

        // Read the chunk
        uint32_t available;
        _LockI2C();
        _DelayIO();
        const char *err = _I2CReceive(_I2CAddress(), (uint8_t *) &jsonbuf[jsonbufLen], chunklen, &available);
        _UnlockI2C();
        if (err != NULL) {
            _Free(jsonbuf);
#ifdef ERRDBG
            _Debug("i2c receive error\n");
#endif
            return err;
        }

        // We've now received the chunk
        jsonbufLen += chunklen;

        // If the last byte of the chunk is \n, chances are that we're done.  However, just so
        // that we pull everything pending from the module, we only exit when we've received
        // a newline AND there's nothing left available from the module.
        if (jsonbufLen > 0 && jsonbuf[jsonbufLen-1] == '\n') {
            receivedNewline = true;
        }

        // For the next iteration, read the min of what's available and what we're permitted to read
        chunklen = (int) (available > _I2CMax() ? _I2CMax() : available);

        // If there's something available on the notecard for us to receive, do it
        if (chunklen > 0) {
            continue;
        }

        // If there's nothing available AND we've received a newline, we're done
        if (receivedNewline) {
            break;
        }

        // If we've timed out and nothing's available, exit
        if (_GetMs() >= startMs + (NOTECARD_TRANSACTION_TIMEOUT_SEC*1000)) {
            _Free(jsonbuf);
#ifdef ERRDBG
            _Debug("reply to request didn't arrive from module in time\n");
#endif
            return ERRSTR("request or response was lost {io}",c_iotimeout);
        }

        // Delay, simply waiting for the Note to process the request
        if (!cardTurboIO) {
            _DelayMs(50);
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
  @brief  Initialize or re-initialize the I2C subsystem, returning false if
  anything fails.
  @returns a boolean. `true` if the reset was successful, `false`, if not.
*/
/**************************************************************************/
bool i2cNoteReset()
{

    // Reset the I2C subsystem and exit if failure
    _LockI2C();
    bool success = _I2CReset(_I2CAddress());
    _UnlockI2C();
    if (!success) {
        return false;
    }

    // Synchronize by guaranteeing not only that I2C works, but that after we send \n that we drain
    // the remainder of any pending partial reply from a previously-aborted session.
    // If we get a failure on transmitting the \n, it means that the notecard isn't even present.
    _LockI2C();
    _DelayIO();
    const char *transmitErr = _I2CTransmit(_I2CAddress(), (uint8_t *)"\n", 1);
    if (!cardTurboIO) {
        _DelayMs(CARD_REQUEST_I2C_SEGMENT_DELAY_MS);
    }
    _UnlockI2C();

    // This outer loop does retries on I2C error, and is simply here for robustness.
    bool notecardReady = false;
    int retries;
    for (retries=0; transmitErr==NULL && !notecardReady && retries<3; retries++) {

        // Loop to drain all chunks of data that may be ready to transmit to us
        int chunklen = 0;
        while (true) {

            // Read the next chunk of available data
            uint32_t available;
            uint8_t buffer[128];
            chunklen = (chunklen > (int)sizeof(buffer)) ? (int)sizeof(buffer) : chunklen;
            chunklen = (chunklen > (int)_I2CMax()) ? (int)_I2CMax() : chunklen;
            _LockI2C();
            _DelayIO();
            const char *err = _I2CReceive(_I2CAddress(), buffer, chunklen, &available);
            _UnlockI2C();
            if (err) {
                break;
            }

            // If nothing left, we're ready to transmit a command to receive the data
            if (available == 0) {
                notecardReady = true;
                break;
            }

            // Read everything that's left on the module
            chunklen = available;

        }

        // Exit loop if success
        if (notecardReady) {
            break;
        }

    }

    // Reinitialize i2c if there's no response
    if (!notecardReady) {
        _LockI2C();
        _I2CReset(_I2CAddress());
        _UnlockI2C();
        _Debug(ERRSTR("notecard not responding\n", "no notecard\n"));
    }

    // Done
    return notecardReady;
}
