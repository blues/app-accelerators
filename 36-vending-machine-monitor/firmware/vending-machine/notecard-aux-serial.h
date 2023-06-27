// Copyright 2023 Blues Inc.  All rights reserved.
// Use of this source code is governed by licenses granted by the
// copyright holder including that found in the LICENSE file.

#pragma once

#include "app.h"
#include "debug.h"

struct AuxSerialHandler;

struct AuxSerial {
    virtual void add(AuxSerialHandler& t)=0;
};

/**
 * @brief A base class for handling a certain type of notification from the Notecard via the AUX serial
 * port.
 */
class AuxSerialHandler {

public:
    /**
     * @brief The card.aux.serial mode this handler responds to.
     */
    const char* const mode;

    AuxSerialHandler* next;

    AuxSerialHandler(AuxSerial& auxSerial, const char* mode) : mode(mode), next(nullptr) {
        auxSerial.add(*this);
    }

    /**
     * @brief Allows this handler to augment the Notecard card.aux.serial request
     * @param request
     */
    virtual void updateRequest(J* request) {}

    /**
     * @brief Handles a notification from the Notecard.
     * 
     * @param notification The notification that was received.
     */
    virtual void handleNotification(J* notification)=0;

};


class NotecardAuxSerial : public AuxSerial {

    /**
     * @brief The head of the list of handlers, or nullptr if no handlers have been added.
     */
    AuxSerialHandler* handler;

    /**
     * @brief When enabled, the handlers receive notifications. When not enabled,
     * the aux serial communication is reduced to a minimum.
     */
    bool enabled;

    void notifyError(const char* errmsg) {
        debug.print("AuxSerial: ");
        debug.println(errmsg);
    }

    /**
     * @brief Find the handler for a given mode.
     * 
     * @param mode  The name of the `aux.serial` mode
     * @return Handler*     The registered handler, or nullptr if no handler with that name is registered.
     */
    AuxSerialHandler* handlerFor(const char* mode) {
        for (AuxSerialHandler* handler = this->handler; handler; handler = handler->next) {
            if (!strcmp(mode, handler->mode)) {
                return handler;
            }
        }
        return nullptr;
    }

    void notificationReceived(const char* json, size_t length) {
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

        AuxSerialHandler* handler = handlerFor(notificationType);
        if (handler) {
            handler->handleNotification(notification);
        }
        else {
            notifyError("no handler for notification");
        }
        JDelete(notification);
    }

public:
    static const uint32_t DEFAULT_BAUD_RATE = 115200;

    /**
     * @brief The Stream used to communicate with the notecard via aux serial.
     */
    Stream& serial;

    /**
     * @brief The baudrate the serial connection operates at. This is sent in the
     * card.aux.serial request.
     */
    const unsigned baudRate;

    NotecardAuxSerial(Stream& serial, unsigned baudRate=DEFAULT_BAUD_RATE)
        : serial(serial), baudRate(baudRate), handler(nullptr) 
        {}

    void add(AuxSerialHandler& handler) {
        handler.next = this->handler;
        this->handler = &handler;
    }

    /**
     * @brief Enables or disables card.aux.serial notifications based on the `enabled` state.
     */
    bool initialize() {
        J* req = notecard.newRequest("card.aux.serial");

        String modeStr("notify");
        for (AuxSerialHandler* handler = this->handler; handler && enabled; handler = handler->next) {
            modeStr += ',';
            modeStr += handler->mode;
            handler->updateRequest(req);
        }
        JAddStringToObject(req, "mode", modeStr.c_str());
        if (baudRate!=DEFAULT_BAUD_RATE) {
            JAddNumberToObject(req, "rate", baudRate);
        }
        bool success = notecard.sendRequest(req);
        return success;
    }

    /**
     * @brief Polls the serial stream for data. If there is data available the stream is read
     * until the next newline.
     */
    void poll() {
        // See if we've got any data available on the serial port
        if (enabled && serial.available()) {
            String receivedString = serial.readStringUntil('\n');
            notificationReceived(receivedString.c_str(), receivedString.length());
        }
    }

    void setEnabled(bool enabled) {
        if (this->enabled != enabled) {
            this->enabled = enabled;
            initialize();
        }
    }

    bool isEnabled() { return enabled; }
};

/**
 * @brief Handles accelerometer readings from Notecard
 * 
 */
class NotecardAccelerometer : public AuxSerialHandler {
    unsigned duration;

public:
    NotecardAccelerometer(AuxSerial& auxSerial, int duration) : AuxSerialHandler(auxSerial, "accel"), duration(duration) {};

    /**
     * @brief An accelerometer reading. Represents the acceleration along x, y and z axes.
     */
    struct Reading {
        float x, y, z;

        inline bool isSet() const {
            return x || y || z;
        }

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
        // if a duration is specified, add this to the card.aux request
        if (duration) {
            JAddNumberToObject(req, "duration", duration);
        }
    }

    /**
     * @brief Parses the accelerometer readings into a `Reading` instance which is
     * passed to `accelerometerUpdate()`.
     * 
     * @param notification The accelerometer notification
     */
    virtual void handleNotification(J* notification) override {
        float x = JGetNumber(notification, "y");
        float y = JGetNumber(notification, "y");
        float z = JGetNumber(notification, "z");
        Reading r = {x, y, z};
        accelerometerUpdate(r);
    }

    /**
     * @brief Handle the most recent reading from the accelerometer.
     * 
     * @param reading The latest reading from the accelerometer.
     */
    virtual void accelerometerUpdate(const Reading& reading)=0;
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

    const uint8_t NORMAL_ORIENTATION_SAMPLES = 10;
    const float NORMAL_ORIENTATION_MAX_TILT = 3;

    /**
     * @brief The number of readings to take to determine the normal (non-tilted)
     * orientation of the Notecard.
     */
    uint8_t normalReadingsRemaining;

    Reading normal;

    /**
     * @brief The most recently computed angle.
     */
    float angle;

public:
    /**
     * @brief Construct a new Notecard Tilt Sensor object
     * 
     * @param duration How often to sample the accelerometer readings.
     */
    NotecardTiltSensor(AuxSerial& auxSerial, unsigned duration=1000)
    : NotecardAccelerometer(auxSerial, duration)
    {
        reset();
    }

    void reset() {
        angle = NAN;
        normal = {0,0,0};
        normalReadingsRemaining = NORMAL_ORIENTATION_SAMPLES;
    }

    void setEnabled(bool enabled) {
        if (!enabled) {
            reset();
        }
    }

    virtual void accelerometerUpdate(const Reading& reading) override {
        if (normalReadingsRemaining) {
            normalReadingsRemaining--;
            if (normal.isSet()) {
                updateNormalReading(normal, reading);
            }
            else {
                normal = reading;
            }
            if (!normalReadingsRemaining) {
                debug.println("Notecard orientation calibrated.");
            }
        }
        else {
            angle = angleFromNormal(normal, reading);
        }
    }

    void updateNormalReading(Reading& normal, const Reading& latest) {
        float angle = angleFromNormal(normal, latest);
        if (angle <= NORMAL_ORIENTATION_MAX_TILT) {
            normal = latest;
        }
        else {
            reset();
            debug.println("Notecard tilt moved during calibration. Resetting.");
        }
    }

    static float angleFromNormal(const Reading& normal, const Reading& reading) {
        return Reading::angle(normal, reading);
    }

    bool isTiltSensed() { return !isnan(angle); }

    /**
     * @brief Returns the tilt of the Notecard as an angle in radians.
     * 
     * @return float The current tilt, or NaN if tilt has not been determined.
     */
    float angleFromNormal() { return angle; }

};
