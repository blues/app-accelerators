/*!
 * @file NoteGetLocation_test.cpp
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

TEST_CASE("NoteGetLocation")
{
    NoteSetFnDefault(malloc, free, NULL, NULL);

    RESET_FAKE(NoteNewRequest);
    RESET_FAKE(NoteRequestResponse);

    JNUMBER lat = 0;
    JNUMBER lon = 0;
    JTIME time = 0;
    char statusBuf[128];

    SECTION("No response") {
        NoteRequestResponse_fake.return_val = NULL;

        CHECK(!NoteGetLocation(&lat, &lon, &time, statusBuf,
                               sizeof(statusBuf)));
    }

    SECTION("Response") {
        const char respStatus[] = "GPS updated (58 sec, 41dB SNR, 9 sats) "
                                  "{gps-active} {gps-signal} {gps-sats} {gps}";
        const float respLat = 42.577600;
        const float respLon = -70.871340;
        const uint32_t respTime = 1598554399;

        J* resp = JCreateObject();
        REQUIRE(resp != NULL);
        JAddStringToObject(resp, "status", respStatus);
        JAddNumberToObject(resp, "lat", respLat);
        JAddNumberToObject(resp, "lon", respLon);
        JAddNumberToObject(resp, "time", respTime);
        NoteRequestResponse_fake.return_val = resp;

        CHECK(NoteGetLocation(&lat, &lon, &time, statusBuf, sizeof(statusBuf)));
        CHECK(!strcmp(statusBuf, respStatus));
        CHECK(lat == respLat);
        CHECK(lon == respLon);
        CHECK(time == respTime);
    }
}

}

#endif // TEST
