/*!
 * @file NoteSetLocationMode_test.cpp
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

DEFINE_FFF_GLOBALS
FAKE_VALUE_FUNC(J *, NoteNewRequest, const char *)
FAKE_VALUE_FUNC(bool, NoteRequest, J *)

namespace
{

TEST_CASE("NoteSetLocationMode")
{
    NoteSetFnDefault(malloc, free, NULL, NULL);

    RESET_FAKE(NoteNewRequest);
    RESET_FAKE(NoteRequest);

    const char mode[] = "periodic";
    const uint32_t seconds = 3600;

    SECTION("NoteNewRequest fails") {
        NoteNewRequest_fake.return_val = NULL;

        CHECK(!NoteSetLocationMode(mode, seconds));
        CHECK(NoteRequest_fake.call_count == 0);
    }

    SECTION("NoteRequest") {
        NoteNewRequest_fake.return_val = JCreateObject();

        SECTION("Fails") {
            NoteRequest_fake.return_val = false;

            CHECK(!NoteSetLocationMode(mode, seconds));
        }

        SECTION("Succeeds") {
            NoteRequest_fake.return_val = true;

            CHECK(NoteSetLocationMode(mode, seconds));
        }

        SECTION("Mode empty string") {
            NoteRequest_fake.return_val = true;

            CHECK(NoteSetLocationMode("", seconds));
            // If the mode parameter is the empty string, the request should
            // have mode: "-".
            CHECK(!strcmp("-", JGetString(NoteRequest_fake.arg0_history[0],
                                          "mode")));
        }

        CHECK(NoteRequest_fake.call_count > 0);

        JDelete(NoteNewRequest_fake.return_val);
    }
}

}

#endif // TEST
