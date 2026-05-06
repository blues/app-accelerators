/*!
 * @file JGetInt_test.cpp
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

#include "n_lib.h"

namespace
{

TEST_CASE("JGetInt")
{
    NoteSetFnDefault(malloc, free, NULL, NULL);

    J *json = JCreateObject();
    REQUIRE(json != NULL);
    const char field[] = "field";
    long int val = 123;

    SECTION("NULL JSON") {
        CHECK(JGetInt(NULL, field) == 0);
    }

    SECTION("Wrong field") {
        JAddNumberToObject(json, field, val);
        CHECK(JGetInt(json, "abc") == 0);
    }

    SECTION("Not a number") {
        JAddStringToObject(json, field, "value");
        CHECK(JGetInt(json, field) == 0);
    }

    SECTION("Valid number") {
        JAddNumberToObject(json, field, val);
        CHECK(JGetInt(json, field) == val);
    }

    JDelete(json);
}

}

#endif // TEST
