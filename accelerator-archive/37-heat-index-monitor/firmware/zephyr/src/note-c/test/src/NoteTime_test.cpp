/*!
 * @file NoteTime_test.cpp
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

#ifdef NOTE_C_TEST

#include <catch2/catch_test_macros.hpp>
#include "fff.h"

#include "n_lib.h"
#include "time_mocks.h"

DEFINE_FFF_GLOBALS
FAKE_VALUE_FUNC(J *, NoteNewRequest, const char *)
FAKE_VALUE_FUNC(J *, NoteRequestResponse, J *)
FAKE_VALUE_FUNC(long unsigned int, NoteGetMs)

namespace
{

TEST_CASE("NoteTime")
{
    NoteSetFnDefault(malloc, free, NULL, NULL);

    RESET_FAKE(NoteNewRequest);
    RESET_FAKE(NoteRequestResponse);
    RESET_FAKE(NoteGetMs);

    SECTION("No response to card.time") {
        NoteRequestResponse_fake.return_val = NULL;
        NoteGetMs_fake.return_val = 1000;

        // Unable to get time from Notecard, so we should get back the
        // seconds since boot.
        CHECK(NoteTime() == 1);
    }

    SECTION("card.time errors") {
        J* resp = JCreateObject();
        REQUIRE(resp != NULL);
        NoteRequestResponse_fake.return_val = resp;
        NoteGetMs_fake.return_val = 1000;

        SECTION("err field present") {
            JAddStringToObject(resp, "err", "an error");
        }

        SECTION("No time field") {
        }

        CHECK(NoteTime() == 1);
    }

    SECTION("card.time valid") {
        NoteGetMs_fake.custom_fake = NoteGetMsIncrement;
        char respRaw[] =
            "{ \
          \"time\": 1599769214, \
          \"area\": \"Beverly, MA\", \
          \"zone\": \"CDT,America/New York\", \
          \"minutes\": -300, \
          \"lat\": 42.5776, \
          \"lon\": -70.87134, \
          \"country\": \"US\" \
        }";
        J *resp = JParse(respRaw);
        REQUIRE(resp != NULL);
        NoteRequestResponse_fake.return_val = resp;

        JTIME time = NoteTime();
        // The returned time should be less than what's in the JSON response
        // from the Notecard because NoteTimeST subtracts the time it took to
        // fetch the time.
        CHECK((time > 0 && time < 1599769214));
    }

    SECTION("Millisecond rollover") {
        JTIME baseTime = 1679335667;
        long unsigned int baseTimeSetAtMs = 1000;
        long unsigned int rolloverMs = 500;
        long unsigned int getMsRetVals[] = {baseTimeSetAtMs, rolloverMs};
        SET_RETURN_SEQ(NoteGetMs, getMsRetVals, 2);

        // Set the time manually so that the base time is non-zero.
        NoteTimeSet(baseTime, 0, NULL, NULL, NULL);
        // Compute the new base time, taking into account the millisecond
        // rollover.
        JTIME newBaseTime = baseTime + (0x100000000LL + rolloverMs -
                                        baseTimeSetAtMs) / 1000;
        CHECK(NoteTime() == newBaseTime);
    }
}

}

#endif // TEST
