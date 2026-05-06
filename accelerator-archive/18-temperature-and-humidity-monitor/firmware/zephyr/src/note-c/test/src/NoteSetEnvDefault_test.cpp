/*!
 * @file NoteSetEnvDefault_test.cpp
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

TEST_CASE("NoteSetEnvDefault")
{
    NoteSetFnDefault(malloc, free, NULL, NULL);

    RESET_FAKE(NoteNewRequest);
    RESET_FAKE(NoteRequest);

    const char var[] = "var";
    char val[] = "val";

    SECTION("NoteNewRequest returns NULL") {
        NoteNewRequest_fake.return_val = NULL;

        CHECK(!NoteSetEnvDefault(var, val));
    }

    SECTION("NoteNewRequest ok") {
        J *req = JCreateObject();
        REQUIRE(req != NULL);
        NoteNewRequest_fake.return_val = req;

        SECTION("NoteRequest fails") {
            NoteRequest_fake.return_val = false;

            CHECK(!NoteSetEnvDefault(var, val));
        }

        SECTION("NoteRequest succeeds") {
            NoteRequest_fake.return_val = true;

            CHECK(NoteSetEnvDefault(var, val));
        }

        JDelete(req);
    }
}

}

#endif // TEST
