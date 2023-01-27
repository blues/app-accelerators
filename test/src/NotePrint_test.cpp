/*!
 * @file NotePrint_test.cpp
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
FAKE_VALUE_FUNC(bool, NoteIsDebugOutputActive)
FAKE_VALUE_FUNC(J *, NoteNewRequest, const char *)
FAKE_VALUE_FUNC(bool, NoteRequest, J *)
FAKE_VOID_FUNC(NoteDebug, const char *)

namespace
{


TEST_CASE("NotePrint")
{
    NoteSetFnDefault(malloc, free, NULL, NULL);

    RESET_FAKE(NoteIsDebugOutputActive);
    RESET_FAKE(NoteNewRequest);
    RESET_FAKE(NoteRequest);
    RESET_FAKE(NoteDebug);

    const char msg[] = "Hello world!";

    SECTION("Debug") {
        NoteIsDebugOutputActive_fake.return_val = true;

        CHECK(NotePrint(msg));
        CHECK(NoteDebug_fake.call_count > 0);
    }

    SECTION("card.log") {
        NoteIsDebugOutputActive_fake.return_val = false;

        SECTION("NoteNewRequest fails") {
            NoteNewRequest_fake.return_val = NULL;

            CHECK(!NotePrint(msg));
        }

        SECTION("NoteNewRequest succeeds") {
            J *req = JCreateObject();
            REQUIRE(req != NULL);
            NoteNewRequest_fake.return_val = req;

            SECTION("NoteRequest fails") {
                NoteRequest_fake.return_val = false;

                CHECK(!NotePrint(msg));
            }

            SECTION("NoteRequest succeeds") {
                NoteRequest_fake.return_val = true;

                CHECK(NotePrint(msg));
                CHECK(NoteRequest_fake.call_count > 0);
            }

            JDelete(req);
        }

        // Should only go through card.log, never NoteDebug.
        CHECK(NoteDebug_fake.call_count == 0);

    }
}

}

#endif // TEST
