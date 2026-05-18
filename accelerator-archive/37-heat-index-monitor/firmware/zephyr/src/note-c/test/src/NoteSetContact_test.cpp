/*!
 * @file NoteSetContact_test.cpp
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

TEST_CASE("NoteSetContact")
{
    NoteSetFnDefault(malloc, free, NULL, NULL);

    RESET_FAKE(NoteNewRequest);
    RESET_FAKE(NoteRequest);

    char nameBuf[] = "John Doe";
    char orgBuf[] = "Blues";
    char roleBuf[] = "Engineer";
    char emailBuf[] = "jdoe@blues.com";

    J *req = JCreateObject();
    REQUIRE(req != NULL);

    SECTION("Errors") {
        SECTION("NoteNewRequest fails") {
            NoteNewRequest_fake.return_val = NULL;
        }

        SECTION("NoteRequest fails") {
            NoteNewRequest_fake.return_val = req;
            NoteRequest_fake.return_val = false;
        }

        CHECK(!NoteSetContact(nameBuf, orgBuf, roleBuf, emailBuf));
    }


    SECTION("Success") {
        NoteNewRequest_fake.return_val = req;
        NoteRequest_fake.return_val = true;

        SECTION("NULL buffers") {
            CHECK(NoteSetContact(NULL, NULL, NULL, NULL));
        }

        SECTION("Valid buffers") {
            CHECK(NoteSetContact(nameBuf, orgBuf, roleBuf, emailBuf));
        }
    }

    JDelete(req);
}

}

#endif // TEST
