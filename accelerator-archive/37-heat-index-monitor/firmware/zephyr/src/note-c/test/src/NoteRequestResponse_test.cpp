/*!
 * @file NoteRequestResponse_test.cpp
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
FAKE_VALUE_FUNC(J *, NoteTransaction, J *)

namespace
{

J *NoteTransactionValid(J *)
{
    return JCreateObject();
}

TEST_CASE("NoteRequestResponse")
{
    NoteSetFnDefault(malloc, free, NULL, NULL);

    RESET_FAKE(NoteTransaction);

    SECTION("Passing a NULL request returns NULL") {
        CHECK(NoteRequestResponse(NULL) == NULL);
    }


    SECTION("NoteTransaction returns NULL") {
        J *req = NoteNewRequest("note.add");
        REQUIRE(req != NULL);
        NoteTransaction_fake.return_val = NULL;

        CHECK(NoteRequestResponse(req) == NULL);
        CHECK(NoteTransaction_fake.call_count == 1);
    }

    SECTION("NoteTransaction returns a valid response") {
        J *req = NoteNewRequest("note.add");
        REQUIRE(req != NULL);
        NoteTransaction_fake.custom_fake = NoteTransactionValid;

        J *resp = NoteRequestResponse(req);
        REQUIRE(resp != NULL);
        CHECK(NoteTransaction_fake.call_count == 1);

        JDelete(resp);
    }
}

}

#endif // TEST
