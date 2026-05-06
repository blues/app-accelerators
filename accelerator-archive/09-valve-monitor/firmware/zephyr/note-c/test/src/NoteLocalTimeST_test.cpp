/*!
 * @file NoteLocalTimeST_test.cpp
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
#include "time_mocks.h"

DEFINE_FFF_GLOBALS
FAKE_VALUE_FUNC(bool, NoteTimeValidST)
FAKE_VALUE_FUNC(JTIME, NoteTimeST)
FAKE_VALUE_FUNC(bool, NoteRegion, char **, char **, char **, int *)

namespace
{

char country[] = "US";
char area[] = "CA";
char zone[] = "PDT";
int offset = -420; // PDT = UTC-7

bool NoteRegionFake(char **retCountry, char **retArea, char **retZone,
                    int *retZoneOffset)
{
    if (retCountry != NULL) {
        *retCountry = country;
    }
    if (retArea != NULL) {
        *retArea = area;
    }
    if (retZone != NULL) {
        *retZone = zone;
    }
    if (retZoneOffset != NULL) {
        *retZoneOffset = offset;
    }

    return true;
}

TEST_CASE("NoteLocalTimeST")
{
    NoteSetFnDefault(malloc, free, NULL, NULL);

    RESET_FAKE(NoteTimeValidST);
    RESET_FAKE(NoteTimeST);
    RESET_FAKE(NoteRegion);

    uint16_t retYear;
    uint8_t retMonth;
    uint8_t retDay;
    uint8_t retHour;
    uint8_t retMinute;
    uint8_t retSecond;
    char *retWeekday;
    char *retZone;

    SECTION("Time not valid") {
        NoteTimeValidST_fake.return_val = false;

        CHECK(!NoteLocalTimeST(&retYear, &retMonth, &retDay, &retHour,
                               &retMinute, &retSecond, &retWeekday, &retZone));
    }

    SECTION("Time valid") {
        // Thu Mar 16 02:44:16 PM PDT 2023.
        JTIME baseTime = 1679003056;
        NoteTimeST_fake.return_val = baseTime;
        NoteRegion_fake.custom_fake = NoteRegionFake;
        NoteTimeValidST_fake.return_val = true;

        CHECK(NoteLocalTimeST(&retYear, &retMonth, &retDay, &retHour,
                              &retMinute, &retSecond, &retWeekday, &retZone));
        CHECK(retYear == 2023);
        CHECK(retMonth == 3);
        CHECK(retDay == 16);
        CHECK(retHour == 14);
        CHECK(retMinute == 44);
        CHECK(retSecond == 16);
        CHECK(strcmp(retWeekday, "Thu") == 0);
        CHECK(strcmp(retZone, zone) == 0);
    }
}

}

#endif // TEST
