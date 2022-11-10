#include "NoteI2c_Arduino.hpp"

#if defined(NOTE_LOWMEM)
static const char *i2cerr = "i2c {io}";
#endif

NoteI2c *
make_note_i2c (
    NoteI2c::param_t i2c_parameters_
)
{
    static NoteI2c * note_i2c = nullptr;
    if (!i2c_parameters_) {
        if (note_i2c) {
            delete note_i2c;
            note_i2c = nullptr;
        }
    } else if (!note_i2c) {
        note_i2c = new NoteI2c_Arduino(*reinterpret_cast<TwoWire *>(i2c_parameters_));
    }
    return note_i2c;
}

NoteI2c_Arduino::NoteI2c_Arduino
(
    TwoWire & i2c_bus_
) :
    _i2cPort(i2c_bus_)
{
    _i2cPort.begin();
}

const char *
NoteI2c_Arduino::receive (
    uint16_t device_address_,
    uint8_t * buffer_,
    uint16_t requested_byte_count_,
    uint32_t * available_
)
{
    const char *result = nullptr;
    uint8_t transmission_error = 0;

    // Request response data from Notecard
    for (size_t i = 0 ; i < 3 ; ++i) {
        _i2cPort.beginTransmission(static_cast<uint8_t>(device_address_));
        _i2cPort.write(static_cast<uint8_t>(0));
        _i2cPort.write(static_cast<uint8_t>(requested_byte_count_));
        transmission_error = _i2cPort.endTransmission();

        // Break out of loop on success
        if (!transmission_error) {
            break;
        }

        switch (transmission_error) {
        case 1:
            result = ERRSTR("i2c: data too long to fit in transmit buffer {io}",i2cerr);
            break;
        case 2:
            result = ERRSTR("i2c: received NACK on transmit of address {io}",i2cerr);
            break;
        case 3:
            result = ERRSTR("i2c: received NACK on transmit of data {io}",i2cerr);
            break;
        case 4:
            result = ERRSTR("i2c: unknown error on TwoWire::endTransmission() {io}",i2cerr);
            break;
        case 5:
            result = ERRSTR("i2c: timeout {io}",i2cerr);
            break;
        default:
            result = ERRSTR("i2c: unknown error encounter during I2C transmission {io}",i2cerr);
        }
    }

    // Read and cache response from Notecard
    if (!transmission_error) {
        const int request_length = requested_byte_count_ + NoteI2c::REQUEST_HEADER_SIZE;
        const int response_length = _i2cPort.requestFrom((int)device_address_, request_length);
        if (!response_length) {
            result = ERRSTR("serial-over-i2c: no response to read request {io}",i2cerr);
        } else if (response_length != request_length) {
            result = ERRSTR("serial-over-i2c: unexpected raw byte count {io}",i2cerr);
        } else {
            // Update available with remaining bytes
            *available_ = _i2cPort.read();
            // Ensure protocol response length matches size request
            if (requested_byte_count_ != static_cast<uint8_t>(_i2cPort.read())) {
                result = ERRSTR("serial-over-i2c: unexpected protocol byte count {io}",i2cerr);
            } else {
                for (size_t i = 0 ; i < requested_byte_count_ ; ++i) {
                    //TODO: Perf test against indexed buffer writes
                    *buffer_++ = _i2cPort.read();
                }
            }
        }
    }

    return result;
}

bool
NoteI2c_Arduino::reset (
    uint16_t device_address_
)
{
    (void)device_address_;
#if WIRE_HAS_END
    _i2cPort.end();
#endif
    _i2cPort.begin();
    return true;
}

const char *
NoteI2c_Arduino::transmit (
    uint16_t device_address_,
    uint8_t * buffer_,
    uint16_t size_
)
{
    const char * result = nullptr;
    uint8_t transmission_error = 0;

    _i2cPort.beginTransmission(static_cast<uint8_t>(device_address_));
    _i2cPort.write(static_cast<uint8_t>(size_));
    _i2cPort.write(buffer_, size_);
    transmission_error = _i2cPort.endTransmission();

    if (transmission_error) {
        switch (transmission_error) {
        case 1:
            result = ERRSTR("i2c: data too long to fit in transmit buffer {io}",i2cerr);
            break;
        case 2:
            result = ERRSTR("i2c: received NACK on transmit of address {io}",i2cerr);
            break;
        case 3:
            result = ERRSTR("i2c: received NACK on transmit of data {io}",i2cerr);
            break;
        case 4:
            result = ERRSTR("i2c: unknown error on TwoWire::endTransmission() {io}",i2cerr);
            break;
        case 5:
            result = ERRSTR("i2c: timeout {io}",i2cerr);
            break;
        default:
            result = ERRSTR("i2c: unknown error encounter during I2C transmission {io}",i2cerr);
        }
    }

    return result;
}
