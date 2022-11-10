#include "NoteLog_Arduino.hpp"

NoteLog *
make_note_log (
    NoteLog::param_t log_parameters_
)
{
    static NoteLog * note_log = nullptr;
    if (!log_parameters_) {
        if (note_log) {
            delete note_log;
            note_log = nullptr;
        }
    } else if (!note_log) {
        note_log = new NoteLog_Arduino(reinterpret_cast<Stream *>(log_parameters_));
    }
    return note_log;
}

NoteLog_Arduino::NoteLog_Arduino
(
    Stream * log_stream_
) :
    _notecardLog(log_stream_)
{ }

size_t
NoteLog_Arduino::print (
    const char * str_
)
{
    size_t result;

    result = _notecardLog->print(str_);

    return result;
}
