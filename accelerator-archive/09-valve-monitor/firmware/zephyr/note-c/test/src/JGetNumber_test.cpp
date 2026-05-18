/*!
 * @file JGetNumber_test.cpp
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

TEST_CASE("JGetNumber")
{
    NoteSetFnDefault(malloc, free, NULL, NULL);

    J *json = JCreateObject();
    REQUIRE(json != NULL);
    const char field[] = "field";
    JNUMBER val = 1.5;

    SECTION("NULL JSON") {
        CHECK(JGetNumber(NULL, field) == 0);
    }

    SECTION("Wrong field") {
        JAddNumberToObject(json, field, val);
        CHECK(JGetNumber(json, "abc") == 0);
    }

    SECTION("Not a number") {
        JAddStringToObject(json, field, "value");
        CHECK(JGetNumber(json, field) == 0);
    }

    SECTION("Valid number") {
        JAddNumberToObject(json, field, val);
        CHECK(JGetNumber(json, field) == val);
    }

    JDelete(json);
}

}

#endif // TEST
