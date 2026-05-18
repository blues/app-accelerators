/*!
 * @file NoteTimeSet_test.cpp
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
FAKE_VALUE_FUNC(long unsigned int, NoteGetMs)
FAKE_VALUE_FUNC(J *, NoteNewRequest, const char *)

namespace
{

long unsigned int NoteGetMsIncrement(void)
{
    static long unsigned int count = 0;

    // increment by 1 second
    count += 1000;
    // return count pre-increment
    return count - 1000;
}

TEST_CASE("NoteTimeSet")
{
    NoteSetFnDefault(malloc, free, NULL, NULL);

    RESET_FAKE(NoteGetMs);
    RESET_FAKE(NoteNewRequest);

    JTIME baseTime = 1679003056;
    int offset = 5;
    char zone[] = "PDT";
    char country[] = "US";
    char area[] = "CA";
    int retOffset = 0;
    char *retZone = NULL;
    char *retCountry = NULL;
    char *retArea = NULL;
    NoteGetMs_fake.custom_fake = NoteGetMsIncrement;

    SECTION("Set and unset") {
        NoteTimeSet(baseTime, offset, zone, country, area);
        JTIME currTime = NoteTime();
        CHECK(currTime > baseTime);
        CHECK(NoteRegion(&retCountry, &retArea, &retZone, &retOffset));
        // Verify that setting the region information with NoteTimeSet worked.
        CHECK(offset == retOffset);
        CHECK(strcmp(zone, retZone) == 0);
        CHECK(strcmp(country, retCountry) == 0);
        CHECK(strcmp(area, retArea) == 0);

        // Verify that passing a base time of 0 to NoteTimeSet causes us to go
        // back to using the time from the Notecard.
        NoteTimeSet(0, offset, zone, country, area);
        currTime = NoteTime();
        // Check that a card.time request was made.
        CHECK(strcmp(NoteNewRequest_fake.arg0_history[0], "card.time") == 0);
        // Setting the base time to 0 seconds also invalidates the region info.
        CHECK(!NoteRegion(&retCountry, &retArea, &retZone, &retOffset));
        CHECK(0 == retOffset);
        CHECK(strcmp("UTC", retZone) == 0);
        CHECK(strcmp("", retCountry) == 0);
        CHECK(strcmp("", retArea) == 0);
    }

    SECTION("NULL region info") {
        NoteTimeSet(baseTime, offset, NULL, NULL, NULL);
        CHECK(NoteRegion(&retCountry, &retArea, &retZone, &retOffset));
        CHECK(offset == retOffset);
        CHECK(strcmp("UTC", retZone) == 0);
        CHECK(strcmp("", retCountry) == 0);
        CHECK(strcmp("", retArea) == 0);
    }
}

}

#endif // TEST
