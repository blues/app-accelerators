/*!
 * @file JGetObject_test.cpp
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

#include "n_lib.h"

namespace
{

TEST_CASE("JGetObject")
{
    NoteSetFnDefault(malloc, free, NULL, NULL);

    const char field[] = "body";
    J *json = JCreateObject();
    REQUIRE(json != NULL);
    J *body = JCreateObject();
    REQUIRE(body != NULL);
    JAddItemToObject(json, field, body);

    SECTION("NULL JSON") {
        CHECK(JGetObject(NULL, "") == NULL);
    }

    SECTION("Field not present") {
        CHECK(JGetObject(json, "req") == NULL);
    }

    SECTION("Field isn't an object") {
        JAddNumberToObject(json, "num", 5);
        CHECK(JGetObject(json, "num") == NULL);
    }

    SECTION("Valid") {
        CHECK(JGetObject(json, field) != NULL);
    }

    JDelete(json);
}

}

#endif // TEST
