#ifndef NOTE_LOG_ARDUINO_HPP
#define NOTE_LOG_ARDUINO_HPP

#include "NoteLog.hpp"

#ifndef NOTE_MOCK
#include <Arduino.h>
#else
#include "mock/mock-arduino.hpp"
#endif

class NoteLog_Arduino final : public NoteLog
{
public:
    NoteLog_Arduino(Stream * log_stream_);
    size_t print(const char * message) override;

private:
    Stream * const _notecardLog;
};

#endif // NOTE_LOG_ARDUINO_HPP
