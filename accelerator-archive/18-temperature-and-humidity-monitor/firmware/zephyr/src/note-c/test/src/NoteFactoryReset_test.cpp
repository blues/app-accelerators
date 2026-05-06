/*!
 * @file NoteFactoryReset_test.cpp
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
FAKE_VALUE_FUNC(bool, NoteReset)

namespace
{

TEST_CASE("NoteFactoryReset")
{
    NoteSetFnDefault(malloc, free, NULL, NULL);

    RESET_FAKE(NoteNewRequest);
    RESET_FAKE(NoteRequest);
    RESET_FAKE(NoteReset);

    SECTION("NoteNewRequest fails") {
        NoteNewRequest_fake.return_val = NULL;

        CHECK(!NoteFactoryReset(true));
    }

    SECTION("NoteRequest fails") {
        J *req = JCreateObject();
        REQUIRE(req != NULL);
        NoteNewRequest_fake.return_val = req;
        NoteRequest_fake.return_val = false;

        CHECK(!NoteFactoryReset(true));

        JDelete(req);
    }

    SECTION("NoteRequest succeeds") {
        J *req = JCreateObject();
        REQUIRE(req != NULL);
        NoteNewRequest_fake.return_val = req;
        NoteRequest_fake.return_val = true;
        NoteReset_fake.return_val = true;

        SECTION("Delete config") {
            CHECK(NoteFactoryReset(true));
            CHECK(JGetBool(req, "delete"));
        }

        SECTION("Don't delete config") {
            CHECK(NoteFactoryReset(false));
            CHECK(!JGetBool(req, "delete"));
        }

        SECTION("NoteReset fails the first time") {
            bool noteResetRetVals[] = {false, true};
            SET_RETURN_SEQ(NoteReset, noteResetRetVals, 2);

            CHECK(NoteFactoryReset(true));
            CHECK(NoteReset_fake.call_count > 1);
        }

        JDelete(req);
    }
}

}

#endif // TEST
