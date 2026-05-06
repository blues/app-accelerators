/*!
 * @file NoteSetUploadMode_test.cpp
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
FAKE_VALUE_FUNC(bool, NoteRequest, J *)

namespace
{

TEST_CASE("NoteSetUploadMode")
{
    NoteSetFnDefault(malloc, free, NULL, NULL);

    RESET_FAKE(NoteNewRequest);
    RESET_FAKE(NoteRequest);

    const char mode[] = "periodic";
    int mins = 30;
    bool align = true;

    SECTION("NoteNewRequest fails") {
        NoteNewRequest_fake.return_val = NULL;

        CHECK(!NoteSetUploadMode(mode, mins, align));
    }

    SECTION("NoteRequest") {
        J *req = JCreateObject();
        REQUIRE(req != NULL);
        NoteNewRequest_fake.return_val = req;

        SECTION("Fails") {
            NoteRequest_fake.return_val = false;

            CHECK(!NoteSetUploadMode(mode, mins, align));
        }

        SECTION("Succeeds") {
            NoteRequest_fake.return_val = true;

            SECTION("Zero minutes") {
                mins = 0;
                CHECK(NoteSetUploadMode(mode, mins, align));
                CHECK(JGetObjectItem(req, "outbound") == NULL);
                CHECK(JGetObjectItem(req, "align") == NULL);
            }

            SECTION("Non-zero minutes") {
                mins = 30;
                CHECK(NoteSetUploadMode(mode, mins, align));
                CHECK(JGetInt(req, "outbound") == mins);
                CHECK(JGetBool(req, "align"));
            }
        }

        JDelete(req);
    }
}

}

#endif // TEST
