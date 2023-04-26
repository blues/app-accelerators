// Copyright 2023 Blues Inc.  All rights reserved.
// Use of this source code is governed by licenses granted by the
// copyright holder including that found in the LICENSE file.

#pragma once

#include "notecard-component.h"
#include "debug.h"
#include <map>

// for now, this is coded to arduino streams. Later, this can be easily be templated to work with other frameworks
#include <Arduino.h>

template<typename T>
struct Registry {
    virtual void add(T& t)=0;
};

class AuxSerialHandler {

public:
    /**
     * @brief The card.aux.serial mode this handler responds to.
     */
    const char* const mode;
    AuxSerialHandler(Registry<AuxSerialHandler>& auxSerial, const char* mode) : mode(mode) {
        auxSerial.add(*this);
    }

    /**
     * @brief Allows this handler to augment the Notecard card.aux.serial request
     * @param request
     */
    virtual void updateRequest(J* request) {}

    virtual void handle(J* notification)=0;

};


/**
 * @brief Use a template because there is no abstract base class for all types of Serial.
 * Stream doesn't have begin/end.
 *
 * @tparam S The type of Serial port
 */
template<typename S>
class NotecardAuxSerial : public Registry<AuxSerialHandler>, public NotecardComponent {
    using Handler = AuxSerialHandler;

    struct StrCompare {
        bool operator()(char const *a, char const *b) const {
            return strcmp(a, b) < 0;
        }
    };

    S& serial;
    unsigned baudRate;
    // map name to handler
    std::map<const char*, Handler*, StrCompare> handlers;

protected:
    // todo - expand this to include printf arguments and error code
    // push down to NotecardComponent
    void notifyError(const char* errmsg) {
        debug.print("AuxSerial: ");
        debug.println(errmsg);
    }

    Handler* handlerFor(const char* name) {
        auto it = handlers.find(name);
        return it==handlers.end() ? nullptr : it->second;
    }

    String& modeString(String& s) {
        s += "notify";
        for (const auto& item : handlers) {
            s += ',';
            s += item.first;    // the name
        }
        return s;
    }

public:

    NotecardAuxSerial(Notecard& notecard, S& serial, unsigned baudRate)
        : NotecardComponent(notecard, "aux.serial"), serial(serial), baudRate(baudRate) {}

    void add(Handler& handler) {
        handlers[handler.mode] = &handler;
    }

    bool initialize() {
        serial.begin(baudRate);
        J* req = notecard.newCommand("card.aux.serial");

        String modeStr("notify");
        for (const auto& h : handlers) {
            modeStr += ',';
            modeStr += h.first;             // name
            h.second->updateRequest(req);   // handler
        }
        JAddStringToObject(req, "mode", modeStr.c_str());
        JAddNumberToObject(req, "rate", baudRate);
        bool success = notecard.sendRequest(req);
        return success;
    }

    void poll() {
        // See if we've got any data available on the serial port
        if (serial.available()) {
            String receivedString = serial.readStringUntil('\n');
            notification(receivedString.c_str(), receivedString.length());
        }
    }

    void notification(const char* json, size_t length) {
        // todo logInfo(...)
        debug.printf("notification: %s\n", json);
        J* notification = JParse(json);
        if (!notification) {
            notifyError("not a JSON object");
            return;
        }

        const char *notificationType = JGetString(notification, "type");
        if (!notificationType) {
            notifyError("notification type not present");
        }

        Handler* handler = handlerFor(notificationType);
        if (handler) {
            handler->handle(notification);
        }
        else {
            notifyError("no handler for notification");
        }
        JDelete(notification);
    }

    bool reset() {
        return true;
    }
};

/**
 * @brief The aspect of NotecardAuxSerial that allows the handlers to register themselves.
 */
typedef Registry<AuxSerialHandler> AuxSerialRegistry;

class NotecardAccelerometer : public AuxSerialHandler {
    unsigned duration;

public:
    NotecardAccelerometer(AuxSerialRegistry& auxSerial, int duration) : AuxSerialHandler(auxSerial, "accel"), duration(duration) {};

    struct Reading {
        float x, y, z;

        inline float magnitude() const {
            return sqrtf(x*x + y*y + z*z);
        }

        static float dotProduct(const Reading& r1, const Reading& r2) {
            return r1.x * r2.x + r1.y * r2.y + r1.z * r2.z;
        }

        static float angle(const Reading& r1, const Reading& r2) {
            // https://www.omnicalculator.com/math/angle-between-two-vectors
            return acosf(dotProduct(r1, r2) / (r1.magnitude() * r2.magnitude()));
        }
    };

    virtual void updateRequest(J* req) override {
        if (duration) {
            JAddNumberToObject(req, "duration", duration);
        }
    }

    virtual void handle(J* notification) override {
        float x = JGetNumber(notification, "y");
        float y = JGetNumber(notification, "y");
        float z = JGetNumber(notification, "z");
        Reading r = {x, y, z};
        accelerometerUpdate(r);
    }

    virtual void accelerometerUpdate(const Reading& reading)=0;
};

struct NotecardOrientation {
    enum Enum {
        ANGLED,
        FACE_UP,
        FACE_DOWN,
        // todo - etc..
    };

    NotecardAccelerometer::Reading orientation(Enum e, float magnitude) {
        switch (e) {
            default:
            case FACE_UP: return { 0, 0, magnitude };
            case FACE_DOWN: return { 0, 0, -magnitude };
        }
    }
};

/**
 * @brief Uses the raw accelerometer readings to determine the tilt angle relative to the normal
 * orientation of the Notecard. When the Notecard is not in motion the only forces on the accelerometer
 * are 1g downwards. For example, when face-up, x- and y-axes have zero acceleration, while the z-axis has 1g).
 * As the notecard is tilted, the acceleration due to gravity is still pulling downwards, but due to the tilt,
 * the resulting vector is no longer directly downwards relative to the notecard. Tilt is computed
 * as the angle between the expected vector due to gravity and the actual vector.
 */
class NotecardTiltSensor : public NotecardAccelerometer {

    /**
     * @brief The most recently computed angle.
     */
    float angle;

    static constexpr Reading NORMAL = { 0, 0, 1024 };

public:
    /**
     * @brief Construct a new Notecard Tilt Sensor object
     * 
     * @param duration How often to sample the accelerometer readings.
     */
    NotecardTiltSensor(AuxSerialRegistry& auxSerial, unsigned duration=1000) : NotecardAccelerometer(auxSerial, duration) {}

    virtual void accelerometerUpdate(const Reading& reading) override {
        angle = angleFromNormal(NORMAL, reading);
    }

    static float angleFromNormal(const Reading& normal, const Reading& reading) {
        return Reading::angle(normal, reading);
    }

    float angleFromNormal() { return angle; }
};

template<typename S>
NotecardAuxSerial<S> newNotecardAuxSerial(Notecard& notecard, S& s, unsigned baudRate=115200) {
    return NotecardAuxSerial<S>(notecard, s, baudRate);
}
