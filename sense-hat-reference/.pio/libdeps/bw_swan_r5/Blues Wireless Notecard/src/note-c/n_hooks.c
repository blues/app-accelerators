/*!
 * @file n_hooks.c
 *
 * Hooks allow libraries dependent on note-c to provide platform- or
 * MCU-specific functions for common functions like I2C locking/unlocking,
 * memory allocation and freeing, delays, and communicating with the Notecard
 * over I2C and Serial. Using these hooks, note-c is able to manage Notecard
 * transaction logic, and defer to platform functionality, when needed.
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

#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include "n_lib.h"

//**************************************************************************/
/*!
  @brief  Show malloc operations for debugging in very low mem environments.
*/
/**************************************************************************/
#define NOTE_SHOW_MALLOC  false
#if NOTE_SHOW_MALLOC
#include <string.h>
void *malloc_show(size_t len);
#endif

// Which I/O port to use
#define interfaceNone       0
#define interfaceSerial     1
#define interfaceI2C        2

// Externalized Hooks
//**************************************************************************/
/*!
  @brief  Hook for the calling platform's debug interface, if any.
*/
/**************************************************************************/
debugOutputFn hookDebugOutput = NULL;
//**************************************************************************/
/*!
  @brief  Hook for the calling platform's I2C lock function.
*/
/**************************************************************************/
mutexFn hookLockI2C = NULL;
//**************************************************************************/
/*!
  @brief  Hook for the calling platform's I2C unlock function.
*/
/**************************************************************************/
mutexFn hookUnlockI2C = NULL;
//**************************************************************************/
/*!
  @brief  Hook for the calling platform's Notecard lock function.
*/
/**************************************************************************/
mutexFn hookLockNote = NULL;
//**************************************************************************/
/*!
  @brief  Hook for the calling platform's Notecard lock function.
*/
/**************************************************************************/
mutexFn hookUnlockNote = NULL;
//**************************************************************************/
/*!
  @brief  Hook for the calling platform's memory allocation function.
*/
/**************************************************************************/
mallocFn hookMalloc = NULL;
//**************************************************************************/
/*!
  @brief  Hook for the calling platform's memory free function.
*/
/**************************************************************************/
freeFn hookFree = NULL;
//**************************************************************************/
/*!
  @brief  Hook for the calling platform's delay function.
*/
/**************************************************************************/
delayMsFn hookDelayMs = NULL;
//**************************************************************************/
/*!
  @brief  Hook for the calling platform's millis timing function.
*/
/**************************************************************************/
getMsFn hookGetMs = NULL;
//**************************************************************************/
/*!
  @brief  Hook for the calling platform's current active interface. Value is
  one of:
  - interfaceNone = 0 (default)
  - interfaceSerial = 1
  - interfaceI2C = 2
*/
/**************************************************************************/
uint32_t hookActiveInterface = interfaceNone;

//**************************************************************************/
/*!
  @brief  Hook for the calling platform's Serial reset function.
*/
/**************************************************************************/
serialResetFn hookSerialReset = NULL;
//**************************************************************************/
/*!
  @brief  Hook for the calling platform's Serial transmit function.
*/
/**************************************************************************/
serialTransmitFn hookSerialTransmit = NULL;
//**************************************************************************/
/*!
  @brief  Hook for the calling platform's Serial data available function.
*/
/**************************************************************************/
serialAvailableFn hookSerialAvailable = NULL;
//**************************************************************************/
/*!
  @brief  Hook for the calling platform's Serial receive function.
*/
/**************************************************************************/
serialReceiveFn hookSerialReceive = NULL;

//**************************************************************************/
/*!
  @brief  Hook for the calling platform's I2C address.
*/
/**************************************************************************/
uint32_t i2cAddress = 0;
//**************************************************************************/
/*!
  @brief  Hook for the calling platform's I2C maximum segment size, in bytes.
*/
/**************************************************************************/
uint32_t i2cMax = 0;
//**************************************************************************/
/*!
  @brief  Hook for the calling platform's I2C reset function.
*/
/**************************************************************************/
i2cResetFn hookI2CReset = NULL;
//**************************************************************************/
/*!
  @brief  Hook for the calling platform's transmit function.
*/
/**************************************************************************/
i2cTransmitFn hookI2CTransmit = NULL;
//**************************************************************************/
/*!
  @brief  Hook for the calling platform's I2C receive function.
*/
/**************************************************************************/
i2cReceiveFn hookI2CReceive = NULL;

// Internal hooks
typedef bool (*nNoteResetFn) (void);
typedef const char * (*nTransactionFn) (char *, char **);
static nNoteResetFn notecardReset = NULL;
static nTransactionFn notecardTransaction = NULL;

//**************************************************************************/
/*!
  @brief  Set the default memory and timing hooks if they aren't already set
  @param   mallocfn  The default memory allocation `malloc`
  function to use.
  @param   freefn  The default memory free
  function to use.
  @param   delayfn  The default delay function to use.
  @param   millisfn  The default 'millis' function to use.
*/
/**************************************************************************/
void NoteSetFnDefault(mallocFn mallocfn, freeFn freefn, delayMsFn delayfn, getMsFn millisfn)
{
    if (hookMalloc == NULL) {
        hookMalloc = mallocfn;
    }
    if (hookFree == NULL) {
        hookFree = freefn;
    }
    if (hookDelayMs == NULL) {
        hookDelayMs = delayfn;
    }
    if (hookGetMs == NULL) {
        hookGetMs = millisfn;
    }
}

//**************************************************************************/
/*!
  @brief  Set the platform-specific memory and timing hooks.
  @param   mallocfn  The platform-specific memory allocation `malloc`
  function to use.
  @param   freefn  The platform-specific memory free
  function to use.
  @param   delayfn  The platform-specific delay function to use.
  @param   millisfn  The platform-specific 'millis' function to use.
*/
/**************************************************************************/
void NoteSetFn(mallocFn mallocfn, freeFn freefn, delayMsFn delayfn, getMsFn millisfn)
{
    hookMalloc = mallocfn;
    hookFree = freefn;
    hookDelayMs = delayfn;
    hookGetMs = millisfn;
}

//**************************************************************************/
/*!
  @brief  Set the platform-specific debug output function.
  @param   fn  A function pointer to call for debug output.
*/
/**************************************************************************/
void NoteSetFnDebugOutput(debugOutputFn fn)
{
    hookDebugOutput = fn;
}

//**************************************************************************/
/*!
  @brief  Determine if a debug output function has been set.
  @returns  A boolean indicating whether a debug ouput function was
  provided.
*/
/**************************************************************************/
bool NoteIsDebugOutputActive()
{
    return hookDebugOutput != NULL;
}

//**************************************************************************/
/*!
  @brief  Set the platform-specific mutex functions for I2C and the
  Notecard.
  @param   lockI2Cfn  The platform-specific I2C lock function to use.
  @param   unlockI2Cfn  The platform-specific I2C unlock function to use.
  @param   lockNotefn  The platform-specific Notecard lock function to use.
  @param   unlockNotefn  The platform-specific Notecard unlock function
  to use.
*/
/**************************************************************************/
void NoteSetFnMutex(mutexFn lockI2Cfn, mutexFn unlockI2Cfn, mutexFn lockNotefn, mutexFn unlockNotefn)
{
    hookLockI2C = lockI2Cfn;
    hookUnlockI2C = unlockI2Cfn;
    hookLockNote = lockNotefn;
    hookUnlockNote = unlockNotefn;
}

//**************************************************************************/
/*!
  @brief  Set the platform-specific Serial communication functions for the
  Notecard.
  @param   resetfn  The platform-specific Serial reset function to use.
  @param   transmitfn  The platform-specific Serial transmit function to use.
  @param   availfn  The platform-specific Serial available function to use.
  @param   receivefn  The platform-specific Serial receive function to use.
*/
/**************************************************************************/
void NoteSetFnSerial(serialResetFn resetfn, serialTransmitFn transmitfn, serialAvailableFn availfn, serialReceiveFn receivefn)
{
    hookActiveInterface = interfaceSerial;

    hookSerialReset = resetfn;
    hookSerialTransmit = transmitfn;
    hookSerialAvailable = availfn;
    hookSerialReceive = receivefn;

    notecardReset = serialNoteReset;
    notecardTransaction = serialNoteTransaction;
}

//**************************************************************************/
/*!
  @brief  Set the platform-specific I2C communication functions for the
  Notecard.
  @param   i2caddress  The I2C address to use for Notecard communication.
  @param   i2cmax  The I2C maximum segment size to use for Notecard
  communication.
  @param   resetfn  The platform-specific I2C reset function to use.
  @param   transmitfn  The platform-specific I2C transmit function to use.
  @param   receivefn  The platform-specific I2C receive function to use.
*/
/**************************************************************************/
void NoteSetFnI2C(uint32_t i2caddress, uint32_t i2cmax, i2cResetFn resetfn, i2cTransmitFn transmitfn, i2cReceiveFn receivefn)
{
    i2cAddress = i2caddress;
    i2cMax = i2cmax;

    hookActiveInterface = interfaceI2C;

    hookI2CReset = resetfn;
    hookI2CTransmit = transmitfn;
    hookI2CReceive = receivefn;

    notecardReset = i2cNoteReset;
    notecardTransaction = i2cNoteTransaction;
}

//**************************************************************************/
/*!
  @brief  Set the platform-specific communications method to be disabled
*/
/**************************************************************************/
void NoteSetFnDisabled()
{

    hookActiveInterface = interfaceNone;

    notecardReset = NULL;
    notecardTransaction = NULL;

}

// Runtime hook wrappers

//**************************************************************************/
/*!
  @brief  Write a number to the debug stream and output a newline.
  @param   line  A debug string for output.
*/
/**************************************************************************/
void NoteDebugIntln(const char *line, int n)
{
    if (line != NULL) {
        NoteDebug(line);
    }
    char str[16];
    JItoA(n, str);
    NoteDebug(str);
    NoteDebug(c_newline);
}

//**************************************************************************/
/*!
  @brief  Write text to the debug stream and output a newline.
  @param   line  A debug string for output.
*/
/**************************************************************************/
void NoteDebugln(const char *line)
{
    NoteDebug(line);
    NoteDebug(c_newline);
}

//**************************************************************************/
/*!
  @brief  Write to the debug stream.
  @param   line  A debug string for output.
*/
/**************************************************************************/
void NoteDebug(const char *line)
{
#ifndef NOTE_NODEBUG
    if (hookDebugOutput != NULL) {
        hookDebugOutput(line);
    }
#endif
}

//**************************************************************************/
/*!
  @brief  Get the current milliseconds value from the platform-specific
  hook.
  @returns  The current milliseconds value.
*/
/**************************************************************************/
long unsigned int NoteGetMs()
{
    if (hookGetMs == NULL) {
        return 0;
    }
    return hookGetMs();
}

//**************************************************************************/
/*!
  @brief  Delay milliseconds using the platform-specific hook.
  @param   ms the milliseconds delay value.
*/
/**************************************************************************/
void NoteDelayMs(uint32_t ms)
{
    if (hookDelayMs != NULL) {
        hookDelayMs(ms);
    }
}

#if NOTE_SHOW_MALLOC
//**************************************************************************/
/*!
  @brief  If set for low-memory platforms, show a malloc call.
  @param   len the number of bytes of memory allocated by the last call.
*/
/**************************************************************************/
void htoa32(uint32_t n, char *p);
void htoa32(uint32_t n, char *p)
{
    int i;
    for (i=0; i<8; i++) {
        uint32_t nibble = (n >> 28) & 0xff;
        n = n << 4;
        if (nibble >= 10) {
            *p++ = 'A' + (nibble-10);
        } else {
            *p++ = '0' + nibble;
        }
    }
    *p = '\0';
}
void *malloc_show(size_t len)
{
    char str[16];
    JItoA(len, str);
    hookDebugOutput("malloc ");
    hookDebugOutput(str);
    void *p = hookMalloc(len);
    if (p == NULL) {
        hookDebugOutput("FAIL");
    } else {
        htoa32((uint32_t)p, str);
        hookDebugOutput(str);
    }
    return p;
}
#endif

//**************************************************************************/
/*!
  @brief  Allocate a memory chunk using the platform-specific hook.
  @param   size the number of bytes to allocate.
*/
/**************************************************************************/
void *NoteMalloc(size_t size)
{
    if (hookMalloc == NULL) {
        return NULL;
    }
#if NOTE_SHOW_MALLOC
    return malloc_show(size);
#else
    return hookMalloc(size);
#endif
}

//**************************************************************************/
/*!
  @brief  Free memory using the platform-specific hook.
  @param   p A pointer to the memory address to free.
*/
/**************************************************************************/
void NoteFree(void *p)
{
    if (hookFree != NULL) {
#if NOTE_SHOW_MALLOC
        char str[16];
        htoa32((uint32_t)p, str);
        hookDebugOutput("free");
        hookDebugOutput(str);
#endif
        hookFree(p);
    }
}

//**************************************************************************/
/*!
  @brief  Lock the I2C bus using the platform-specific hook.
*/
/**************************************************************************/
void NoteLockI2C()
{
    if (hookLockI2C != NULL) {
        hookLockI2C();
    }
}

//**************************************************************************/
/*!
  @brief  Unlock the I2C bus using the platform-specific hook.
*/
/**************************************************************************/
void NoteUnlockI2C()
{
    if (hookUnlockI2C != NULL) {
        hookUnlockI2C();
    }
}

//**************************************************************************/
/*!
  @brief  Lock the Notecard using the platform-specific hook.
*/
/**************************************************************************/
void NoteLockNote()
{
    if (hookLockNote != NULL) {
        hookLockNote();
    }
}

//**************************************************************************/
/*!
  @brief  Unlock the Notecard using the platform-specific hook.
*/
/**************************************************************************/
void NoteUnlockNote()
{
    if (hookUnlockNote != NULL) {
        hookUnlockNote();
    }
}

//**************************************************************************/
/*!
  @brief  Get the active interface's name
  @returns A string
*/
/**************************************************************************/
const char *NoteActiveInterface()
{
    switch (hookActiveInterface) {
    case interfaceSerial:
        return "serial";
    case interfaceI2C:
        return "i2c";
    }
    return "unknown";
}

//**************************************************************************/
/*!
  @brief  Reset the Serial bus using the platform-specific hook.
  @returns A boolean indicating whether the Serial bus was reset successfully.
*/
/**************************************************************************/
bool NoteSerialReset()
{
    if (hookActiveInterface == interfaceSerial && hookSerialReset != NULL) {
        return hookSerialReset();
    }
    return true;
}

//**************************************************************************/
/*!
  @brief  Transmit bytes over Serial using the platform-specific hook.
  @param   text The bytes to transmit.
  @param   len The length of bytes.
  @param   flush `true` to flush the bytes upon transmit.
*/
/**************************************************************************/
void NoteSerialTransmit(uint8_t *text, size_t len, bool flush)
{
    if (hookActiveInterface == interfaceSerial && hookSerialTransmit != NULL) {
        hookSerialTransmit(text, len, flush);
    }
}

//**************************************************************************/
/*!
  @brief  Determine if Serial bus is available using the platform-specific
  hook.
  @returns A boolean indicating whether the Serial bus is available to read.
*/
/**************************************************************************/
bool NoteSerialAvailable()
{
    if (hookActiveInterface == interfaceSerial && hookSerialAvailable != NULL) {
        return hookSerialAvailable();
    }
    return false;
}

//**************************************************************************/
/*!
  @brief  Obtain a character from the Serial bus using the platform-specific
  hook.
  @returns A character from the Serial bus.
*/
/**************************************************************************/
char NoteSerialReceive()
{
    if (hookActiveInterface == interfaceSerial && hookSerialReceive != NULL) {
        return hookSerialReceive();
    }
    return 0;
}

//**************************************************************************/
/*!
  @brief  Reset the I2C bus using the platform-specific hook.
  @returns A boolean indicating whether the I2C bus was reset successfully.
*/
/**************************************************************************/
bool NoteI2CReset(uint16_t DevAddress)
{
    if (hookActiveInterface == interfaceI2C && hookI2CReset != NULL) {
        return hookI2CReset(DevAddress);
    }
    return true;
}

//**************************************************************************/
/*!
  @brief  Transmit bytes over I2C using the platform-specific hook.
  @param   DevAddress the I2C address for transmission.
  @param   pBuffer The bytes to transmit.
  @param   Size The length of bytes.
  @returns A c-string from the platform-specific hook, or an error string
  if the bus is not active.
*/
/**************************************************************************/
const char *NoteI2CTransmit(uint16_t DevAddress, uint8_t* pBuffer, uint16_t Size)
{
    if (hookActiveInterface == interfaceI2C && hookI2CTransmit != NULL) {
        return hookI2CTransmit(DevAddress, pBuffer, Size);
    }
    return "i2c not active";
}

//**************************************************************************/
/*!
  @brief  Receive bytes from I2C using the platform-specific hook.
  @param   DevAddress the I2C address for transmission.
  @param   pBuffer (out) A buffer in which to place received bytes.
  @param   Size The length of bytes.
  @param   available (out) The number of bytes left to read.
  @returns A c-string from the platform-specific hook, or an error string
  if the bus is not active.
*/
/**************************************************************************/
const char *NoteI2CReceive(uint16_t DevAddress, uint8_t* pBuffer, uint16_t Size, uint32_t *available)
{
    if (hookActiveInterface == interfaceI2C && hookI2CReceive != NULL) {
        return hookI2CReceive(DevAddress, pBuffer, Size, available);
    }
    return "i2c not active";
}

//**************************************************************************/
/*!
  @brief  Get the I2C address of the Notecard.
  @returns The current I2C address.
*/
/**************************************************************************/
uint32_t NoteI2CAddress()
{
    if (i2cAddress == 0) {
        return NOTE_I2C_ADDR_DEFAULT;
    }
    return i2cAddress;
}

//**************************************************************************/
/*!
  @brief  Set the I2C address for communication with the Notecard.
  @param   i2caddress the I2C address to use for the Notecard.
*/
/**************************************************************************/
void NoteSetI2CAddress(uint32_t i2caddress)
{
    i2cAddress = i2caddress;
}

//**************************************************************************/
/*!
  @brief  Determine the maximum number of bytes for each segment of
  data sent to the Notecard over I2C.
  @returns A 32-bit integer of the maximum number of bytes per I2C segment.
*/
/**************************************************************************/
uint32_t NoteI2CMax()
{
    // Many Arduino libraries (such as ESP32) have a limit less than 32, so if the max isn't specified
    // we must assume the worst and segment the I2C messages into very tiny chunks.
    if (i2cMax == 0) {
        return NOTE_I2C_MAX_DEFAULT;
    }
    // Note design specs
    if (i2cMax > NOTE_I2C_MAX_MAX) {
        i2cMax = NOTE_I2C_MAX_MAX;
    }
    return i2cMax;
}


//**************************************************************************/
/*!
  @brief  Perform a hard reset on the Notecard using the platform-specific
  hook.
  @returns A boolean indicating whether the Notecard has been reset successfully.
*/
/**************************************************************************/
bool NoteHardReset()
{
    if (notecardReset == NULL) {
        return true;
    }
    return notecardReset();
}


//**************************************************************************/
/*!
  @brief  Perform a JSON request to the Notecard using the currently-set
  platform hook.
  @param   json the JSON request.
  @param   jsonResponse (out) A buffer with the JSON response.
  @returns NULL if successful, or an error string if the transaction failed
  or the hook has not been set.
*/
/**************************************************************************/
const char *NoteJSONTransaction(char *json, char **jsonResponse)
{
    if (notecardTransaction == NULL || hookActiveInterface == interfaceNone) {
        return "i2c or serial interface must be selected";
    }
    return notecardTransaction(json, jsonResponse);
}
