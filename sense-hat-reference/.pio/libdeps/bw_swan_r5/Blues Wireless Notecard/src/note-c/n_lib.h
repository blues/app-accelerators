/*!
 * @file n_lib.h
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

#pragma once

#include <string.h>
#include "note.h"

// C-callable functions
#ifdef __cplusplus
extern "C" {
#endif

/**************************************************************************/
/*!
    @brief  How long to wait for the card for any given transaction.
*/
/**************************************************************************/
#define NOTECARD_TRANSACTION_TIMEOUT_SEC     10

// The notecard is a real-time device that has a fixed size interrupt buffer.
// We can push data at it far, far faster than it can process it, therefore we
// push it in segments with a pause between each segment.

/**************************************************************************/
/*!
    @brief  The max length, in bytes, of each request segment when using I2C.
*/
/**************************************************************************/
#define CARD_REQUEST_I2C_SEGMENT_MAX_LEN 250
/**************************************************************************/
/*!
    @brief  The delay, in miliseconds, of each request when using I2C.
*/
/**************************************************************************/
#define CARD_REQUEST_I2C_SEGMENT_DELAY_MS 250
/**************************************************************************/
/*!
    @brief  The delay, in miliseconds, between each request chunk when using I2C.
*/
/**************************************************************************/
#define CARD_REQUEST_I2C_CHUNK_DELAY_MS 20
/**************************************************************************/
/*!
    @brief  The max length, in bytes, of each request segment when using Serial.
*/
/**************************************************************************/
#define CARD_REQUEST_SERIAL_SEGMENT_MAX_LEN 250
/**************************************************************************/
/*!
    @brief  The delay, in miliseconds, of each request when using Serial.
*/
/**************************************************************************/
#define CARD_REQUEST_SERIAL_SEGMENT_DELAY_MS 250

/**************************************************************************/
/*!
    @brief  Memory allocation chunk size.
*/
/**************************************************************************/
#ifdef NOTE_LOWMEM
#define ALLOC_CHUNK 64
#else
#define ALLOC_CHUNK 128
#endif

// Transactions
const char *i2cNoteTransaction(char *json, char **jsonResponse);
bool i2cNoteReset(void);
const char *serialNoteTransaction(char *json, char **jsonResponse);
bool serialNoteReset(void);

// Hooks
void NoteLockNote(void);
void NoteUnlockNote(void);
const char *NoteActiveInterface(void);
bool NoteSerialReset(void);
void NoteSerialTransmit(uint8_t *, size_t, bool);
bool NoteSerialAvailable(void);
char NoteSerialReceive(void);
bool NoteI2CReset(uint16_t DevAddress);
const char *NoteI2CTransmit(uint16_t DevAddress, uint8_t* pBuffer, uint16_t Size);
const char *NoteI2CReceive(uint16_t DevAddress, uint8_t* pBuffer, uint16_t Size, uint32_t *avail);
bool NoteHardReset(void);
const char *NoteJSONTransaction(char *json, char **jsonResponse);
bool NoteIsDebugOutputActive(void);

// Constants, a global optimization to save static string memory
extern const char *c_null;
#define c_null_len 4

extern const char *c_false;
#define c_false_len 5

extern const char *c_true;
#define c_true_len 4

extern const char *c_nullstring;
#define c_nullstring_len 0

extern const char *c_newline;
#define c_newline_len 2

extern const char *c_mem;
#define c_mem_len 3

extern const char *c_iotimeout;
#define c_iotimeout_len 12

extern const char *c_err;
#define c_err_len 3

extern const char *c_req;
#define c_req_len 3

extern const char *c_cmd;
#define c_cmd_len 3

extern const char *c_bad;
#define c_bad_len 3

extern const char *c_iobad;
#define c_iobad_len 8

extern const char *c_ioerr;
#define c_ioerr_len 4


// Readability wrappers.  Anything starting with _ is simply calling the wrapper
// function.
#define _LockNote NoteLockNote
#define _UnlockNote NoteUnlockNote
#define _SerialReset NoteSerialReset
#define _SerialTransmit NoteSerialTransmit
#define _SerialAvailable NoteSerialAvailable
#define _SerialReceive NoteSerialReceive
#define _I2CReset NoteI2CReset
#define _I2CTransmit NoteI2CTransmit
#define _I2CReceive NoteI2CReceive
#define _Reset NoteHardReset
#define _Transaction NoteJSONTransaction
#define _Malloc NoteMalloc
#define _Free NoteFree
#define _GetMs NoteGetMs
#define _DelayMs NoteDelayMs
#define _LockI2C NoteLockI2C
#define _UnlockI2C NoteUnlockI2C
#define _I2CAddress NoteI2CAddress
#define _I2CMax NoteI2CMax
#ifdef NOTE_NODEBUG
#define _Debug(x)
#define _Debugln(x)
#else
#define _Debug(x) NoteDebug(x)
#define _Debugln(x) NoteDebugln(x)
#endif

// End of C-callable functions
#ifdef __cplusplus
}
#endif
