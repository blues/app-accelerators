/*!
 * @file note.h
 *
 * @mainpage C/C++ Library for the Notecard
 *
 * @section intro_sec Introduction
 *
 * The note-c C/C++ library for communicating with the
 * <a href="https://blues.io">Blues Wireless</a>
 * Notecard via serial or I2C.
 *
 * This library allows you to control a Notecard by writing a C or C++
 * program,. Your sketch may programmatically configure Notecard and send Notes
 * to <a href="https://notehub.io">Notehub.io</a>.
 *
 * @section dependencies Dependencies
 *
 * This library bundles the <a href="https://github.com/DaveGamble/cJSON">cJSON
 * JSON parser library</a>.
 *
 * In addition, this library requires a physical
 * connection to a Notecard over I2C or Serial to be functional.
 *
 * @section author Author
 *
 * Written by Ray Ozzie and Blues Inc. team.
 *
 * @section license License
 *
 * Copyright (c) 2019 Blues Inc. MIT License. Use of this source code is
 * governed by licenses granted by the copyright holder including that found in
 * the
 * <a href="https://github.com/blues/note-c/blob/master/LICENSE">LICENSE</a>
 * file.
 *
 */

#pragma once

// In case they're not yet defined
#include <float.h>
#include <stdbool.h>
#include <stdint.h>

// Determine our basic floating data type.  In most cases "double" is the right answer, however for
// very small microcontrollers we must use single-precision.
#if defined(FLT_MAX_EXP) && defined(DBL_MAX_EXP)
#if (FLT_MAX_EXP == DBL_MAX_EXP)
#define NOTE_FLOAT
#endif
#elif defined(__FLT_MAX_EXP__) && defined(__DBL_MAX_EXP__)
#if (__FLT_MAX_EXP__ == __DBL_MAX_EXP__)
#define NOTE_FLOAT
#endif
#else
#error What are floating point exponent length symbols for this compiler?
#endif

// If using a short float, we must be on a VERY small MCU.  In this case, define additional
// symbols that will save quite a bit of memory in the runtime image.
#ifdef NOTE_FLOAT
#define JNUMBER float
#define ERRSTR(x,y) (y)
#define NOTE_LOWMEM
#else
#define JNUMBER double
#define ERRSTR(x,y) (x)
#define ERRDBG
#endif

// UNIX Epoch time (also known as POSIX time) is the  number of seconds that have elapsed since
// 00:00:00 Thursday, 1 January 1970, Coordinated Universal Time (UTC).  In this project, it always
// originates from the Notecard, which synchronizes the time from both the cell network and GPS.
typedef unsigned long int JTIME;

// C-callable functions
#ifdef __cplusplus
extern "C" {
#endif

// Edge defs
#include "n_edge.h"

// cJSON wrappers
#include "n_cjson.h"

// Card callback functions
typedef void (*mutexFn) (void);
typedef void * (*mallocFn) (size_t size);
typedef void (*freeFn) (void *);
typedef void (*delayMsFn) (uint32_t ms);
typedef uint32_t (*getMsFn) (void);
typedef size_t (*debugOutputFn) (const char *text);
typedef bool (*serialResetFn) (void);
typedef void (*serialTransmitFn) (uint8_t *data, size_t len, bool flush);
typedef bool (*serialAvailableFn) (void);
typedef char (*serialReceiveFn) (void);
typedef bool (*i2cResetFn) (uint16_t DevAddress);
typedef const char * (*i2cTransmitFn) (uint16_t DevAddress, uint8_t* pBuffer, uint16_t Size);
typedef const char * (*i2cReceiveFn) (uint16_t DevAddress, uint8_t* pBuffer, uint16_t Size, uint32_t *avail);
typedef bool (*txnStartFn) (uint32_t timeoutMs);
typedef void (*txnStopFn) (void);

// External API
bool NoteReset(void);
void NoteResetRequired(void);
#define NoteNewBody JCreateObject
#define NoteAddBodyToObject(a, b) JAddItemToObject(a, "body", b)
#define NoteGetBody(a) JGetObject(a, "body")
J *NoteNewRequest(const char *request);
J *NoteNewCommand(const char *request);
J *NoteRequestResponse(J *req);
J *NoteRequestResponseWithRetry(J *req, uint32_t timeoutSeconds);
char *NoteRequestResponseJSON(char *reqJSON);
void NoteSuspendTransactionDebug(void);
void NoteResumeTransactionDebug(void);
#define SYNCSTATUS_LEVEL_MAJOR         0
#define SYNCSTATUS_LEVEL_MINOR         1
#define SYNCSTATUS_LEVEL_DETAILED      2
#define SYNCSTATUS_LEVEL_ALGORITHMIC   3
#define SYNCSTATUS_LEVEL_ALL          -1
bool NoteDebugSyncStatus(int pollFrequencyMs, int maxLevel);
bool NoteRequest(J *req);
bool NoteRequestWithRetry(J *req, uint32_t timeoutms);
#define NoteResponseError(rsp) (!JIsNullString(rsp, "err"))
#define NoteResponseErrorContains(rsp, errstr) (JContainsString(rsp, "err", errstr))
#define NoteDeleteResponse(rsp) JDelete(rsp)
J *NoteTransaction(J *req);
bool NoteErrorContains(const char *errstr, const char *errtype);
void NoteErrorClean(char *errbuf);
void NoteSetFnDebugOutput(debugOutputFn fn);
void NoteSetFnTransaction(txnStartFn startFn, txnStopFn stopFn);
void NoteSetFnMutex(mutexFn lockI2Cfn, mutexFn unlockI2Cfn, mutexFn lockNotefn, mutexFn unlockNotefn);
void NoteSetFnDefault(mallocFn mallocfn, freeFn freefn, delayMsFn delayfn, getMsFn millisfn);
void NoteSetFn(mallocFn mallocfn, freeFn freefn, delayMsFn delayfn, getMsFn millisfn);
void NoteSetFnSerial(serialResetFn resetfn, serialTransmitFn writefn, serialAvailableFn availfn, serialReceiveFn readfn);

#define NOTE_I2C_ADDR_DEFAULT	0x17
#ifndef NOTE_I2C_MAX_DEFAULT
#define NOTE_I2C_MAX_DEFAULT	30
#endif
#ifndef NOTE_I2C_MAX_MAX
#define NOTE_I2C_MAX_MAX		127
#endif
void NoteSetFnI2C(uint32_t i2caddr, uint32_t i2cmax, i2cResetFn resetfn, i2cTransmitFn transmitfn, i2cReceiveFn receivefn);
void NoteSetFnDisabled(void);
void NoteSetI2CAddress(uint32_t i2caddress);

// User agent
J *NoteUserAgent(void);
void NoteUserAgentUpdate(J *ua);
void NoteSetUserAgent(char *agent);
void NoteSetUserAgentOS(char *os_name, char *os_platform, char *os_family, char *os_version);
void NoteSetUserAgentCPU(int cpu_mem, int cpu_mhz, int cpu_cores, char *cpu_vendor, char *cpu_name);

// Calls to the functions set above
void NoteDebug(const char *message);
void NoteDebugln(const char *message);
void NoteDebugIntln(const char *message, int n);
void NoteDebugf(const char *format, ...);
void *NoteMalloc(size_t size);
void NoteFree(void *);
long unsigned int NoteGetMs(void);
void NoteDelayMs(uint32_t ms);
void NoteLockI2C(void);
void NoteUnlockI2C(void);
uint32_t NoteI2CAddress(void);
uint32_t NoteI2CMax(void);
uint32_t NoteMemAvailable(void);
bool NotePrint(const char *text);
void NotePrintln(const char *line);
bool NotePrintf(const char *format, ...);

// String helpers to help encourage the world to abandon the horribly-error-prone strn*
size_t strlcpy(char *dst, const char *src, size_t siz);
size_t strlcat(char *dst, const char *src, size_t siz);

// JSON helpers
void JInit(void);
void JCheck(void);
bool JIsPresent(J *rsp, const char *field);
char *JGetString(J *rsp, const char *field);
JNUMBER JGetNumber(J *rsp, const char *field);
J *JGetObject(J *rsp, const char *field);
long int JGetInt(J *rsp, const char *field);
bool JGetBool(J *rsp, const char *field);
JNUMBER JNumberValue(J *item);
char *JStringValue(J *item);
bool JBoolValue(J *item);
long int JIntValue(J *item);
bool JIsNullString(J *rsp, const char *field);
bool JIsExactString(J *rsp, const char *field, const char *teststr);
bool JContainsString(J *rsp, const char *field, const char *substr);
bool JAddBinaryToObject(J *req, const char *fieldName, const void *binaryData, uint32_t binaryDataLen);
bool JGetBinaryFromObject(J *rsp, const char *fieldName, uint8_t **retBinaryData, uint32_t *retBinaryDataLen);
const char *JGetItemName(const J * item);
char *JAllocString(uint8_t *buffer, uint32_t len);
const char *JType(J *item);

#define JTYPE_NOT_PRESENT		0
#define JTYPE_BOOL_TRUE			1
#define JTYPE_BOOL_FALSE		2
#define JTYPE_NULL				3
#define JTYPE_NUMBER_ZERO		4
#define JTYPE_NUMBER			5
#define JTYPE_STRING_BLANK		6
#define JTYPE_STRING_ZERO		7
#define JTYPE_STRING_NUMBER		8
#define JTYPE_STRING_BOOL_TRUE 	9
#define JTYPE_STRING_BOOL_FALSE	10
#define JTYPE_STRING			11
#define JTYPE_OBJECT			12
#define JTYPE_ARRAY				13
int JGetType(J *rsp, const char *field);

// Helper functions for apps that wish to limit their C library dependencies
#define JNTOA_PRECISION (16)
#define JNTOA_MAX       (44)
char * JNtoA(JNUMBER f, char * buf, int precision);
JNUMBER JAtoN(const char *string, char **endPtr);
void JItoA(long int n, char *s);
long int JAtoI(const char *s);
int JB64EncodeLen(int len);
int JB64Encode(char * coded_dst, const char *plain_src,int len_plain_src);
int JB64DecodeLen(const char * coded_src);
int JB64Decode(char * plain_dst, const char *coded_src);

// MD5 Helper functions
typedef struct {
    unsigned long buf[4];
    unsigned long bits[2];
    unsigned char in[64];
} NoteMD5Context;
#define NOTE_MD5_HASH_SIZE 16
#define NOTE_MD5_HASH_STRING_SIZE (((NOTE_MD5_HASH_SIZE)*2)+1)
void NoteMD5Init(NoteMD5Context *ctx);
void NoteMD5Update(NoteMD5Context *ctx, unsigned char const *buf, unsigned long len);
void NoteMD5Final(unsigned char *digest, NoteMD5Context *ctx);
void NoteMD5Transform(unsigned long buf[4], const unsigned char inraw[64]);
void NoteMD5Hash(unsigned char* data, unsigned long len, unsigned char *retHash);
void NoteMD5HashString(unsigned char *data, unsigned long len, char *strbuf, unsigned long buflen);
void NoteMD5HashToString(unsigned char *hash, char *strbuf, unsigned long buflen);

// High-level helper functions that are both useful and serve to show developers how to call the API
uint32_t NoteSetSTSecs(uint32_t secs);
bool NoteTimeValid(void);
bool NoteTimeValidST(void);
JTIME NoteTime(void);
JTIME NoteTimeST(void);
void NoteTimeRefreshMins(uint32_t mins);
void NoteTimeSet(JTIME secondsUTC, int offset, char *zone, char *country, char *area);
bool NoteLocalTimeST(uint16_t *retYear, uint8_t *retMonth, uint8_t *retDay, uint8_t *retHour, uint8_t *retMinute, uint8_t *retSecond, char **retWeekday, char **retZone);
bool NoteRegion(char **retCountry, char **retArea, char **retZone, int *retZoneOffset);
bool NoteLocationValid(char *errbuf, uint32_t errbuflen);
bool NoteLocationValidST(char *errbuf, uint32_t errbuflen);
void NoteTurboIO(bool enable);
long int NoteGetEnvInt(const char *variable, long int defaultVal);
JNUMBER NoteGetEnvNumber(const char *variable, JNUMBER defaultVal);
bool NoteGetEnv(const char *variable, const char *defaultVal, char *buf, uint32_t buflen);
bool NoteSetEnvDefault(const char *variable, char *buf);
bool NoteSetEnvDefaultNumber(const char *variable, JNUMBER defaultVal);
bool NoteSetEnvDefaultInt(const char *variable, long int defaultVal);
bool NoteIsConnected(void);
bool NoteIsConnectedST(void);
bool NoteGetNetStatus(char *statusBuf, int statusBufLen);
bool NoteGetVersion(char *versionBuf, int versionBufLen);
bool NoteGetLocation(JNUMBER *retLat, JNUMBER *retLon, JTIME *time, char *statusBuf, int statusBufLen);
bool NoteSetLocation(JNUMBER lat, JNUMBER lon);
bool NoteClearLocation(void);
bool NoteGetLocationMode(char *modeBuf, int modeBufLen);
bool NoteSetLocationMode(const char *mode, uint32_t seconds);
bool NoteGetServiceConfig(char *productBuf, int productBufLen, char *serviceBuf, int serviceBufLen, char *deviceBuf, int deviceBufLen, char *snBuf, int snBufLen);
bool NoteGetServiceConfigST(char *productBuf, int productBufLen, char *serviceBuf, int serviceBufLen, char *deviceBuf, int deviceBufLen, char *snBuf, int snBufLen);
bool NoteGetStatus(char *statusBuf, int statusBufLen, JTIME *bootTime, bool *retUSB, bool *retSignals);
bool NoteGetStatusST(char *statusBuf, int statusBufLen, JTIME *bootTime, bool *retUSB, bool *retSignals);
bool NoteSleep(char *stateb64, uint32_t seconds, const char *modes);
bool NoteWake(int stateLen, void *state);
bool NoteFactoryReset(bool deleteConfigSettings);
bool NoteSetSerialNumber(const char *sn);
bool NoteSetProductID(const char *productID);
bool NoteSetUploadMode(const char *uploadMode, int uploadMinutes, bool align);
bool NoteSetSyncMode(const char *uploadMode, int uploadMinutes, int downloadMinutes, bool align, bool sync);
#define NoteSend NoteAdd
bool NoteAdd(const char *target, J *body, bool urgent);
bool NoteSendToRoute(const char *method, const char *routeAlias, char *notefile, J *body);
bool NoteGetVoltage(JNUMBER *voltage);
bool NoteGetTemperature(JNUMBER *temp);
bool NoteGetContact(char *nameBuf, int nameBufLen, char *orgBuf, int orgBufLen, char *roleBuf, int roleBufLen, char *emailBuf, int emailBufLen);
bool NoteSetContact(char *nameBuf, char *orgBuf, char *roleBuf, char *emailBuf);

// Definitions necessary for payload descriptor
#define NP_SEGTYPE_LEN 4
#define NP_SEGLEN_LEN sizeof(uint32_t)
#define NP_SEGHDR_LEN (NP_SEGTYPE_LEN + NP_SEGLEN_LEN)
typedef struct {
    uint8_t *data;
    uint32_t alloc;
    uint32_t length;
} NotePayloadDesc;
bool NotePayloadSaveAndSleep(NotePayloadDesc *desc, uint32_t seconds, const char *modes);
bool NotePayloadRetrieveAfterSleep(NotePayloadDesc *desc);
void NotePayloadSet(NotePayloadDesc *desc, uint8_t *buf, uint32_t buflen);
void NotePayloadFree(NotePayloadDesc *desc);
bool NotePayloadAddSegment(NotePayloadDesc *desc, const char segtype[NP_SEGTYPE_LEN], void *pdata, uint32_t plen);
bool NotePayloadFindSegment(NotePayloadDesc *desc, const char segtype[NP_SEGTYPE_LEN], void *pdata, uint32_t *plen);
bool NotePayloadGetSegment(NotePayloadDesc *desc, const char segtype[NP_SEGTYPE_LEN], void *pdata, uint32_t len);

// C macro to convert a number to a string for use below
#define _tstring(x)     #x

// Hard-wired constants used to specify field types when creating note templates
#define TBOOL           true                // bool
#define TINT8           11                  // 1-byte signed integer
#define TINT16          12                  // 2-byte signed integer
#define TINT24          13                  // 3-byte signed integer
#define TINT32          14                  // 4-byte signed integer
#define TINT64          18                  // 8-byte signed integer (note-c support depends upon platform)
#define TUINT8          21                  // 1-byte unsigned integer (requires notecard firmware >= build 14444)
#define TUINT16         22                  // 2-byte unsigned integer (requires notecard firmware >= build 14444)
#define TUINT24         23                  // 3-byte unsigned integer (requires notecard firmware >= build 14444)
#define TUINT32         24                  // 4-byte unsigned integer (requires notecard firmware >= build 14444)
#define TFLOAT16        12.1                // 2-byte IEEE 754 floating point
#define TFLOAT32        14.1                // 4-byte IEEE 754 floating point (a.k.a. "float")
#define TFLOAT64        18.1                // 8-byte IEEE 754 floating point (a.k.a. "double")
#define TSTRING(N)      _tstring(N)         // UTF-8 text of N bytes maximum (fixed-length reserved buffer)
#define TSTRINGV        _tstring(0)         // variable-length string
bool NoteTemplate(const char *notefileID, J *templateBody);

// End of C-callable functions
#ifdef __cplusplus
}
#endif
