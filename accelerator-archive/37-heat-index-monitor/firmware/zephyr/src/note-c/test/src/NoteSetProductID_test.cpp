/*!
 * @file NoteSetProductID_test.cpp
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

TEST_CASE("NoteSetProductID")
{
    NoteSetFnDefault(malloc, free, NULL, NULL);

    RESET_FAKE(NoteNewRequest);
    RESET_FAKE(NoteRequest);

    const char id[] = "myid";

    SECTION("NoteNewRequest fails") {
        NoteNewRequest_fake.return_val = NULL;

        CHECK(!NoteSetProductID(id));
    }

    SECTION("NoteRequest") {
        J *req = JCreateObject();
        REQUIRE(req != NULL);
        NoteNewRequest_fake.return_val = req;

        SECTION("Fails") {
            NoteRequest_fake.return_val = false;

            CHECK(!NoteSetProductID(id));
        }

        SECTION("Succeeds") {
            NoteRequest_fake.return_val = true;

            CHECK(NoteSetProductID(id));
        }

        SECTION("Empty string") {
            NoteRequest_fake.return_val = true;

            CHECK(NoteSetProductID(""));
            // If the id is the empty string, the request should have
            // product: "-".
            CHECK(!strcmp("-", JGetString(req, "product")));
        }

        JDelete(req);
    }
}

}

#endif // TEST
