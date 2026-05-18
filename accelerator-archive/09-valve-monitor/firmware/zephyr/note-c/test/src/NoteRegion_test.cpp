/*!
 * @file NoteRegion_test.cpp
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

TEST_CASE("NoteRegion")
{
    NoteSetFnDefault(malloc, free, NULL, NULL);

    RESET_FAKE(NoteNewRequest);
    RESET_FAKE(NoteRequestResponse);

    char *country = NULL;
    char *area = NULL;
    char *tz = NULL;
    int tzOffset = 1;

    SECTION("Invalid") {
        NoteRequestResponse_fake.return_val = NULL;

        SECTION("NULL buffers") {
            CHECK(!NoteRegion(NULL, NULL, NULL, NULL));
        }

        SECTION("Valid buffers") {
            CHECK(!NoteRegion(&country, &area, &tz, &tzOffset));

            REQUIRE(country != NULL);
            CHECK(!strcmp(country, ""));

            REQUIRE(area != NULL);
            CHECK(!strcmp(area, ""));

            REQUIRE(tz != NULL);
            CHECK(!strcmp(tz, "UTC"));

            CHECK(tzOffset == 0);
        }
    }

    SECTION("Valid") {
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

        SECTION("NULL buffers") {
            CHECK(NoteRegion(NULL, NULL, NULL, NULL));
        }

        SECTION("Valid buffers") {
            CHECK(NoteRegion(&country, &area, &tz, &tzOffset));

            REQUIRE(country != NULL);
            CHECK(!strcmp(country, "US"));

            REQUIRE(area != NULL);
            CHECK(!strcmp(area, "Beverly, MA"));

            REQUIRE(tz != NULL);
            CHECK(!strcmp(tz, "CDT"));

            CHECK(tzOffset == -300);
        }
    }
}

}

#endif // TEST
