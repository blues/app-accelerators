#include <stdint.h>
#include "Arduino.h"

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

struct ArduinoTicksTimer: public TicksTimer {
    inline void set(ticks_t duration) {
        TicksTimer::set(millis(), duration);
    }

    inline bool hasElapsed() {
        return TicksTimer::hasElapsed(millis());
    }
};