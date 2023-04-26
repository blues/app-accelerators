#pragma once

// `debug` is the Stream interface to send debugging/logging separate from the app's serial output.
// Comment-in this when using USB serial for debugging, and comment it out when using the ST-Link V3
#define debug Serial

// Define the debug output stream device, as well as a method enabling us
// to determine whether or not the Serial device is available for app usage.
#ifndef debug
#ifdef APP_MAIN
    #if defined(ARDUINO_SWAN_R5)
    HardwareSerial debug(PG8, PG7);
    #else
    #error MCU support for ST-Link VCP serial channel for debug is not available for this board.
    #endif
#else
extern HardwareSerial debug;
#endif
#endif
