#ifndef NOTE_I2C_ARDUINO_HPP
#define NOTE_I2C_ARDUINO_HPP

#include "NoteI2c.hpp"

#include "Notecard.h"

#ifndef NOTE_MOCK
#include <Wire.h>
#else
#include "mock/mock-arduino.hpp"
#include "mock/mock-parameters.hpp"
#endif

class NoteI2c_Arduino final : public NoteI2c
{
public:
    NoteI2c_Arduino(TwoWire & i2c_bus);
    const char * receive(uint16_t device_address, uint8_t * buffer, uint16_t requested_byte_count, uint32_t * available) override;
    bool reset(uint16_t device_address) override;
    const char * transmit(uint16_t device_address, uint8_t * buffer, uint16_t size) override;

private:
    TwoWire & _i2cPort;
};

#endif // NOTE_I2C_ARDUINO_HPP
