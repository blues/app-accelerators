#pragma once

#include "app.h"

/**
 * @brief Puts the Notecard and the host to sleep for a given period.
 * This is used to conserve power when running on battery power.
 * Once enabled, the host will wait for `delayBeforeSleep` seconds before sleeping, to avoid unnecessary
 * shutdowns on short power outages. If power doesn't resume, then the host and Notecard sleep
 * for `sleepPeriod` seconds, waking up either when that period has elapsed or mains power resumes.
 */
class NotecardSleep {

    uint32_t delayBeforeSleep;
    uint32_t sleepPeriod;
    bool enabled;
    uint32_t enabledAt;

    void sendSleepRequest(uint32_t duration) {
        J* req = notecard.newCommand("card.attn");
        JAddStringToObject(req, "mode", "sleep,usb");
        JAddNumberToObject(req, "seconds", duration);
        notecard.sendRequestWithRetry(req, 10);
        // the Host will enter low-power mode once the request is received IF
        // there is no other source of power to the host.
        // Otherwise if there is an alternative source of power, such as a USB serial connection,
        // it will not shutdown.

        uint32_t start = millis();
        bool notecardSleep = false;
        while ((millis()-start)<30*1000) {
            for (int i=0; i<3; i++)
            {
                delay(850);
                digitalWrite(LED_BUILTIN, HIGH);
                delay(150);
                digitalWrite(LED_BUILTIN, LOW);
            }
            if (!notecardSleep) {
                J* rsp = notecard.requestAndResponse(notecard.newRequest("card.voltage"));
                if (!rsp || notecard.responseError(rsp)) {
                    debug.println("Notecard has shutdown.");
                    notecardSleep = true;
                }
            }
            else {
                J* rsp = notecard.requestAndResponse(notecard.newRequest("card.voltage"));
                if (rsp && !notecard.responseError(rsp)) {
                    debug.println("Notecard has started.");
                    notecardSleep = false;
                }
                break;
            }
            debug.println("waiting for Host shutdown...");
        }
        debug.println("Host shutdown didn't happen!");
    }

public:

    /**
     * @brief Create a new NotecardSleep instance.
     * 
     * @param delayBeforeSleep  The number of seconds to delay after setEnabled(true) has been called, before putting the notecard to sleep, .
     * @param sleepPeriod       The number of seconds to sleep for.
     */
    NotecardSleep(uint32_t delayBeforeSleep, uint32_t sleepPeriod)
        : delayBeforeSleep(delayBeforeSleep*1000), sleepPeriod(sleepPeriod), enabled(false)
        {}

    /**
     * @brief Enables or disables the sleep timer. While disabled, such as when USB power is present,
     * the Notecard and host are not put to sleep. When enabled, the Notecard and host are put to sleep
     * after `delayBeforeSleep` seconds, for `sleepPeriod` seconds, or until USB power resumes.
     */
    void setEnabled(bool enabled) {
        if (this->enabled != enabled) {
            this->enabled = enabled;
            if (enabled) {
                enabledAt = millis();
            }
        }
    }

    void poll() {
        if (enabled && (millis()-enabledAt)>delayBeforeSleep) {
            debug.println("Putting Notecard and host to sleep.");
            sendSleepRequest(sleepPeriod);
        }
    }
};
