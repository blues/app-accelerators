#ifndef NOTE_TIME_H
#define NOTE_TIME_H

#include <stdint.h>

extern "C" {

/**************************************************************************/
/*!
    @brief  Halts the current unit of execution for the specified
            period in milliseconds
    @param    ms
              The number of milliseconds to halt
*/
/**************************************************************************/
void noteDelay(uint32_t ms);

/**************************************************************************/
/*!
    @brief  Reports the count of milliseconds that have transpired since
            the application began.
    @return The count of milliseconds
    @note   The counter is not guaranteed to be 32-bit and any algorithm
            leveraging this API should take rollover into consideration.
*/
/**************************************************************************/
uint32_t noteMillis(void);

}

#endif // NOTE_TIME_H
