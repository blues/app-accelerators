#include "NoteSerial_Arduino.hpp"

NoteSerial *
make_note_serial (
    NoteSerial::param_t serial_parameters_
)
{
    static NoteSerial * note_serial = nullptr;
    if (!serial_parameters_) {
        if (note_serial) {
            delete note_serial;
            note_serial = nullptr;
        }
    } else if (!note_serial) {
        MakeNoteSerial_ArduinoParameters * arduino_parameters = reinterpret_cast<MakeNoteSerial_ArduinoParameters *>(serial_parameters_);
        note_serial = new NoteSerial_Arduino(arduino_parameters->hw_serial, arduino_parameters->baud_rate);
    }
    return note_serial;
}

NoteSerial_Arduino::NoteSerial_Arduino
(
    HardwareSerial & hw_serial_,
    size_t baud_rate_
) :
    _notecardSerial(hw_serial_),
    _notecardSerialSpeed(baud_rate_)
{
    _notecardSerial.begin(_notecardSerialSpeed);
}

size_t
NoteSerial_Arduino::available (
    void
)
{
    return _notecardSerial.available();
}

char
NoteSerial_Arduino::receive (
    void
)
{
    return _notecardSerial.read();
}

bool
NoteSerial_Arduino::reset (
    void
)
{
    _notecardSerial.begin(_notecardSerialSpeed);

    return true;
}

size_t
NoteSerial_Arduino::transmit (
    uint8_t *buffer,
    size_t size,
    bool flush
)
{
    size_t result;
    result = _notecardSerial.write(buffer, size);
    if (flush) {
        _notecardSerial.flush();
    }
    return result;
}
