#pragma once

#ifdef ARDUINO

class AnalogSensor : public Sensor {
    using pin_t = uint8_t;
    pin_t pin;

public:

    AnalogSensor(const char* name, const char* unit, pin_t pin)
    : Sensor(name, unit), pin(pin) {}

    bool begin() {
        pinMode(pin, INPUT);
        read();
        return true;
    }

    bool read() {
        setValue(analogRead(pin));
        return true;
    }
};

#endif