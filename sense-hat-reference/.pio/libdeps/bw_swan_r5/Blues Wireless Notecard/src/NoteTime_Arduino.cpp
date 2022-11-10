
#include "NoteTime.h"

#include <stdint.h>

#ifndef NOTE_MOCK
#include <Arduino.h>
#else
#include "mock/mock-arduino.hpp"
#endif

uint32_t noteMillis(void)
{
    return static_cast<uint32_t>(::millis());
}

void noteDelay(uint32_t ms)
{
    ::delay(static_cast<unsigned long int>(ms));
}
