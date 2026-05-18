/*!
 * @file NoteGetTemperature_test.cpp
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
FAKE_VALUE_FUNC(J *, NoteRequestResponse, J *)

namespace
{

TEST_CASE("NoteGetTemperature")
{
    NoteSetFnDefault(malloc, free, NULL, NULL);

    RESET_FAKE(NoteNewRequest);
    RESET_FAKE(NoteRequestResponse);

    JNUMBER temp = 1.0;
    J *req = JCreateObject();
    REQUIRE(req != NULL);

    SECTION("NoteNewRequest fails") {
        NoteNewRequest_fake.return_val = NULL;

        CHECK(!NoteGetTemperature(&temp));
        CHECK(temp == 0);
    }

    SECTION("NoteRequestResponse fails") {
        NoteNewRequest_fake.return_val = req;
        NoteRequestResponse_fake.return_val = NULL;

        CHECK(!NoteGetTemperature(&temp));
        CHECK(temp == 0);
    }

    SECTION("NoteRequestResponse succeeds") {
        NoteNewRequest_fake.return_val = req;
        J *rsp = JCreateObject();
        REQUIRE(rsp != NULL);
        NoteRequestResponse_fake.return_val = rsp;

        SECTION("Response has error") {
            JAddStringToObject(rsp, "err", "an error");

            CHECK(!NoteGetTemperature(&temp));
            CHECK(temp == 0);
        }

        SECTION("Response ok") {
            JNUMBER rspTemp = 24.5;
            JAddNumberToObject(rsp, "value", rspTemp);

            CHECK(NoteGetTemperature(&temp));
            CHECK(temp == rspTemp);
        }
    }

    JDelete(req);
}

}

#endif // TEST
