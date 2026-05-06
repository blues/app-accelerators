/*!
 * @file NoteNewCommand_test.cpp
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
FAKE_VALUE_FUNC(J *, JCreateObject)

namespace
{

TEST_CASE("NoteNewCommand")
{
    NoteSetFnDefault(malloc, free, NULL, NULL);

    RESET_FAKE(JCreateObject);

    const char req[] = "{ \"req\": \"card.info\" }";

    SECTION("JCreateObject fails") {
        JCreateObject_fake.return_val = NULL;

        CHECK(!NoteNewCommand(req));
    }

    SECTION("JCreateObject succeeds") {
        J* reqJson = (J *)NoteMalloc(sizeof(J));
        REQUIRE(reqJson != NULL);
        memset(reqJson, 0, sizeof(J));
        reqJson->type = JObject;
        JCreateObject_fake.return_val = reqJson;

        CHECK(NoteNewCommand(req));

        JDelete(reqJson);
    }
}

}

#endif // TEST
