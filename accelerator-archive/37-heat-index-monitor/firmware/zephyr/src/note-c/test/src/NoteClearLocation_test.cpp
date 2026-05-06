/*!
 * @file NoteClearLocation_test.cpp
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

TEST_CASE("NoteClearLocation")
{
    NoteSetFnDefault(malloc, free, NULL, NULL);

    RESET_FAKE(NoteNewRequest);
    RESET_FAKE(NoteRequest);

    SECTION("NoteNewRequest fails") {
        NoteNewRequest_fake.return_val = NULL;

        CHECK(!NoteClearLocation());
        CHECK(NoteRequest_fake.call_count == 0);
    }

    SECTION("NoteRequest") {
        NoteNewRequest_fake.return_val = JCreateObject();

        SECTION("Fails") {
            NoteRequest_fake.return_val = false;

            CHECK(!NoteClearLocation());
        }

        SECTION("Succeeds") {
            NoteRequest_fake.return_val = true;

            CHECK(NoteClearLocation());
        }

        CHECK(NoteRequest_fake.call_count > 0);

        JDelete(NoteNewRequest_fake.return_val);
    }
}

}

#endif // TEST
