#pragma once

#include <Notecard.h>
#include "monitor.h"

struct NotecardSensor {

    NotecardSensor(Notecard& notecard) : notecard(notecard) {}

    bool initialize(bool first) {
        return sendRequestHandleResponse(&NotecardSensor::buildInitialRequest, &NotecardSensor::handleInitialResponse);
    }

    bool readValues() {
        return sendRequestHandleResponse(&NotecardSensor::buildRequest, &NotecardSensor::handleResponse);
    }

protected:
    virtual J* buildInitialRequest()=0;
    virtual J* buildRequest()=0;

    virtual bool handleInitialResponse(J* rsp) { return true; }
    virtual bool handleResponse(J* rsp)=0;

    using RequestBuilder = J* (NotecardSensor::*)();
    using ResponseHandler = bool (NotecardSensor::*)(J* rsp);

    /**
     * @brief Uses a request builder function to create the request which is sent to notecard. The notecard reponse
     * is passed to the RequestHandler.
     * 
     * @param builder   The builder that creates the request
     * @param handler   The handler that handles the response
     * @return true     If the request and response were successfully handled.
     * @return false    If the request or response were not successfully handled.
     */
    bool sendRequestHandleResponse(RequestBuilder builder, ResponseHandler handler) {
        bool success = false;
        J* req = (this->*builder)();
        if (req) {
            J* rsp = NoteRequestResponse(req);
            if (rsp) {
                if (!NoteResponseError(rsp)) {
                    success = (this->*handler)(rsp);
                }
                NoteDeleteResponse(rsp);
            }
        }
        return success;
    }

    Notecard& notecard;
};

template <unsigned N, class AppClass>
class NotecardSensorPeripheral : public NotecardSensor, public MultiSensorPeripheral<N, AppClass> {
    using baseSensor = MultiSensorPeripheral<N, AppClass>;
    using Values = typename baseSensor::Values;

public:
    NotecardSensorPeripheral(Notecard& notecard, const Values&& sensors) :
        NotecardSensor(notecard), baseSensor(std::forward<const Values>(sensors))
        {}
};

/**
 * @brief Retrieves the notecard voltage and whether USB is powered or not.
 * Sensor value 0: voltage
 * Sensor value 1: usb connection state
 */
class NotecardPowerSensor: public NotecardSensorPeripheral<2, NotecardPowerSensor> {

    using base = NotecardSensorPeripheral<2, NotecardPowerSensor>;

    static constexpr const char* card_voltage = "card.voltage";
    static constexpr const char* usb = "usb";
    static constexpr const char* value = "value";

protected:

    J* buildInitialRequest() {
        J* req = NoteNewRequest(card_voltage);
        JAddBoolToObject(req, usb, true);
        JAddBoolToObject(req, "alert", true);
        JAddBoolToObject(req, "sync", true);
        JAddStringToObject(req, "mode", "lipo");
        return req;
    }

    virtual J* buildRequest() override {
        return NoteNewRequest(card_voltage);
    }

    virtual bool handleResponse(J* rsp) override {
        setValue(0, JGetNumber(rsp, value));
        setValue(1, JGetBool(rsp, usb));
        return true;
    }

public:
    NotecardPowerSensor(Notecard& notecard)
    : base(notecard, {
        Sensor("voltage", "V"),
        Sensor("usb", "")
    }) {}
};
