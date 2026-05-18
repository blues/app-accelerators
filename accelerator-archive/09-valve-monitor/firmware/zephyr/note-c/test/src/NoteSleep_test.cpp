/*!
 * @file NoteSleep_test.cpp
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
FAKE_VALUE_FUNC(J *, NoteNewCommand, const char *)
FAKE_VALUE_FUNC(bool, NoteRequest, J *)

namespace
{

TEST_CASE("NoteSleep")
{
    NoteSetFnDefault(malloc, free, NULL, NULL);

    RESET_FAKE(NoteNewCommand);
    RESET_FAKE(NoteRequest);

    char payload[] = "ewogICJpbnRlcnZhbHMiOiI2MCwxMiwxNCIKfQ==";
    uint32_t seconds = 10;
    const char modes[] = "modea,modeb";

    SECTION("NoteNewCommand fails") {
        NoteNewCommand_fake.return_val = NULL;

        CHECK(!NoteSleep(payload, seconds, modes));
        CHECK(NoteRequest_fake.call_count == 0);
    }

    SECTION("NoteNewCommand succeeds") {
        J *cmd = JCreateObject();
        REQUIRE(cmd != NULL);
        NoteNewCommand_fake.return_val = cmd;

        SECTION("NoteRequest fails") {
            NoteRequest_fake.return_val = false;

            CHECK(!NoteSleep(payload, seconds, NULL));
            JDelete(cmd);
        }

        SECTION("NoteRequest succeeds") {
            NoteRequest_fake.return_val = true;

            SECTION("Additional modes") {
                CHECK(NoteSleep(payload, seconds, modes));
                CHECK(!strcmp("sleep,modea,modeb",
                              JGetString(NoteRequest_fake.arg0_history[0], "mode")));
            }

            SECTION("No additional modes") {
                CHECK(NoteSleep(payload, seconds, NULL));
                CHECK(NoteRequest_fake.call_count > 0);
            }

            CHECK(NoteRequest_fake.call_count > 0);
            JDelete(cmd);
        }
    }
}

}

#endif // TEST
