
#ifndef NOTECARRIER_SUPPORT_H
#define NOTECARRIER_SUPPORT_H

/*
 * \brief Adafruit Feather-Specific Definitions
 *
 * The Adafruit feather specification defines standard pinout names for digital
 * and analog pins. Unfortunately, the Adafruit Huzzah32 does not comply with
 * this specification. As such, we provide mappings for the pins that are
 * technically incorrect but which make it easier to write code that works
 * across different feathers.
 *
 * On the ESP32 for instance, the pinout for Digital Pin 14 is located where
 * D5 is usually located, so our symbol maps D5 to Digital Pin 14 on that
 * board, etc...
 *
 * \see https://learn.adafruit.com/adafruit-feather/feather-specification
 */

// Establish portable Notecarrier pin definitions

#ifndef D5
#define D5 5
#endif

#ifndef D6
#define D6 6
#endif

#ifndef D9
#define D9 9
#endif

#ifndef D10
#define D10 10
#endif

#ifndef D11
#define D11 11
#endif

#ifndef D12
#define D12 12
#endif

#ifndef D13
#define D13 13
#endif

#if defined(ARDUINO_APOLLO3_SFE_ARTEMIS_THING_PLUS)

#ifdef B0
#undef B0
#endif
#define B0 2

#ifdef D5
#undef D5
#endif
#define D5 3

#ifdef D6
#undef D6
#endif
#define D6 4

#ifdef D9
#undef D9
#endif
#define D9 5

#ifdef D10
#undef D10
#endif
#define D10 6

#ifdef D11
#undef D11
#endif
#define D11 7

#ifdef D12
#undef D12
#endif
#define D12 8

#ifdef D13
#undef D13
#endif
#define D13 9

#elif defined(ARDUINO_FEATHER_ESP32)

#ifdef B0
#undef B0
#endif
#define B0 21

#ifdef D5
#undef D5
#endif
#define D5 14

#ifdef D6
#undef D6
#endif
#define D6 32

#ifdef D9
#undef D9
#endif
#define D9 15

#ifdef D10
#undef D10
#endif
#define D10 33

#ifdef D11
#undef D11
#endif
#define D11 27

#ifdef D12
#undef D12
#endif
#define D12 12

#ifdef D13
#undef D13
#endif
#define D13 13

#elif defined(ARDUINO_FEATHER_F405)

#ifdef B0
#undef B0
#endif
#define B0 PNUM_NOT_DEFINED

#elif defined(ARDUINO_FEATHER_M4)

#ifdef B0
#undef B0
#endif
#define B0 4

#elif defined(ARDUINO_NRF52840_FEATHER)

#ifdef B0
#undef B0
#endif
#define B0 0

#elif defined(ARDUINO_SWAN_R5)

#ifdef CS
#undef CS
#endif
#define CS PD0

#ifdef B0
#undef B0
#endif
#define B0 CS

#endif

#endif // NOTECARRIER_SUPPORT_H
