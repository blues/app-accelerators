/*!
 * @file NoteGetStatus_test.cpp
 *
 * Written by the Blues Inc. team.
 *
 * Copyright (c) 2023 Blues Inc. MIT License. Use of this source code is
 * governed by licenses granted by the copyright holder including that found in
 * the
 * <a href="https://github.com/blues/note-c/blob/master/LICENSE">LICENSE</a>
 * file.
 *
 */

#ifdef TEST

#include <catch2/catch_test_macros.hpp>
#include "fff.h"

#include "n_lib.h"

DEFINE_FFF_GLOBALS
FAKE_VALUE_FUNC(J *, NoteNewRequest, const char *)
FAKE_VALUE_FUNC(J *, NoteRequestResponse, J *)

namespace
{

TEST_CASE("NoteGetStatus")
{
    NoteSetFnDefault(malloc, free, NULL, NULL);

    RESET_FAKE(NoteNewRequest);
    RESET_FAKE(NoteRequestResponse);

    char statusBuf[16] = {1};
    JTIME bootTime = 1;
    bool usb = true;
    bool signals = true;

    SECTION("No response") {
        NoteRequestResponse_fake.return_val = NULL;

        CHECK(!NoteGetStatus(statusBuf, sizeof(statusBuf), &bootTime, &usb,
                             &signals));
        CHECK(!strcmp(statusBuf, ""));
        CHECK(bootTime == 0);
        CHECK(!usb);
        CHECK(!signals);
    }

    SECTION("Response") {
        const char respStatus[] = "{normal}";
        JTIME respBootTime = 1599684765;
        bool respUsb = true;
        bool respConnected;
        uint32_t respSignals = 1;
        bool expectedSignals;

        usb = false;
        signals = false;

        SECTION("Connected") {
            respConnected = true;
            expectedSignals = true;
        }

        SECTION("Not connected") {
            respConnected = false;
            expectedSignals = false;
        }

        SECTION("Connected but no signals") {
            respConnected = true;
            respSignals = 0;
            expectedSignals = false;
        }

        J* resp = JCreateObject();
        REQUIRE(resp != NULL);
        JAddStringToObject(resp, "status", respStatus);
        JAddNumberToObject(resp, "time", respBootTime);
        JAddBoolToObject(resp, "usb", respUsb);
        JAddBoolToObject(resp, "connected", respConnected);
        JAddNumberToObject(resp, "signals", respSignals);
        NoteRequestResponse_fake.return_val = resp;

        CHECK(NoteGetStatus(statusBuf, sizeof(statusBuf), &bootTime, &usb,
                            &signals));
        CHECK(!strcmp(statusBuf, respStatus));
        CHECK(bootTime == respBootTime);
        CHECK(usb);
        CHECK(signals == expectedSignals);
    }
}

}

#endif // TEST
