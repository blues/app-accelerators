#include <stdint.h>
#include "Arduino.h"

/**
 * @brief Provides a duration timer that can measure durations up to 2^31-1 milliseconds safely with
 * wrap around when the ticks counter resets to 0 after 2^32 ticks.
 */
struct TicksTimer {

    typedef uint32_t ticks_t;

    ticks_t start_time = 0;
    ticks_t duration = 0;

    inline void set(ticks_t start_time, ticks_t duration) {
        this->start_time = start_time;
        this->duration = duration;
    }

    inline bool hasElapsed(ticks_t current_ticks) {
        return (current_ticks-start_time)>duration;
    }
};

/**
 * @brief Convenience class that implements TicksTimer using arduino millis().
 */
struct ArduinoTicksTimer: public TicksTimer {
    inline void set(ticks_t duration) {
        TicksTimer::set(millis(), duration);
    }

    inline bool hasElapsed() {
        return TicksTimer::hasElapsed(millis());
    }
};
