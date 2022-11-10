/*!
 * @file Notecard.h
 *
 * The note-arduino Arduino library for communicating with the
 * <a href="https://blues.io">Blues Wireless</a>
 * Notecard via serial or I2C.
 *
 * This library allows you to control a Notecard by writing an Arduino sketch in
 * C or C++. Your sketch may programmatically configure Notecard and send Notes
 * to <a href="https://notehub.io">Notehub.io</a>.
 *
 * This library is a wrapper around the
 * <a href="https://github.com/blues/note-c">note-c library</a>, which it
 * includes as a git submodule.
 *
 * Written by Ray Ozzie and Brandon Satrom for Blues Inc.
 *
 * MIT License. Use of this source code is governed by licenses granted by the
 * copyright holder including that found in the
 * <a href="https://github.com/blues/note-arduino/blob/master/LICENSE">LICENSE</a>
 * file.
 *
 */
#ifndef Notecard_h
#define Notecard_h

#include <stddef.h>
#include <stdint.h>

#include "NoteI2c.hpp"
#include "NoteLog.hpp"
#include "NoteSerial.hpp"

#ifdef ARDUINO
#include <Arduino.h>
#include <Wire.h>
#include "NoteSerial_Arduino.hpp"
#endif

#ifndef NOTE_MOCK
#include <note-c/note.h>
#else
#include "mock/mock-arduino.hpp"
#include "mock/mock-parameters.hpp"
#endif

/**************************************************************************/
/*!
    @brief  Class that stores state and functions for interacting with the
    Blues Wireless Notecard.
*/
/**************************************************************************/
class Notecard
{
public:
    ~Notecard(void);
#ifdef ARDUINO
    inline void begin(uint32_t i2cAddress = NOTE_I2C_ADDR_DEFAULT,
                      uint32_t i2cMax = NOTE_I2C_MAX_DEFAULT,
                      TwoWire &wirePort = Wire) {
        begin(make_note_i2c(&wirePort), i2cAddress, i2cMax);
    }
    inline void begin(HardwareSerial &serial, uint32_t speed = 9600) {
        MakeNoteSerial_ArduinoParameters arduino_parameters(serial, speed);
        begin(make_note_serial(&arduino_parameters));
    }
    inline void setDebugOutputStream(Stream &dbgserial) {
        setDebugOutputStream(make_note_log(&dbgserial));
    }
#endif
    void begin(NoteI2c * noteI2c,
               uint32_t i2cAddress = NOTE_I2C_ADDR_DEFAULT,
               uint32_t i2cMax = NOTE_I2C_MAX_DEFAULT);
    void begin(NoteSerial * noteSerial);
    void setDebugOutputStream(NoteLog * noteLog);
    void clearDebugOutputStream(void);
    J *newRequest(const char *request);
    J *newCommand(const char *request);
    bool sendRequest(J *req);
    J *requestAndResponse(J *req);
    void deleteResponse(J *rsp);
    void logDebug(const char *message);
    void logDebugf(const char *format, ...);
    bool debugSyncStatus(int pollFrequencyMs, int maxLevel);
    bool responseError(J *rsp);

private:
    void platformInit (bool assignCallbacks);
};

#endif
