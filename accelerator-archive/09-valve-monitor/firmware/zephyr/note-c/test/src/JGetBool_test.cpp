/*!
 * @file JGetBool_test.cpp
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

TEST_CASE("JGetBool")
{
    NoteSetFnDefault(malloc, free, NULL, NULL);

    J *json = JCreateObject();
    REQUIRE(json != NULL);
    const char field[] = "field";
    bool val = true;

    SECTION("NULL JSON") {
        CHECK(JGetBool(NULL, field) == 0);
    }

    SECTION("Wrong field") {
        JAddBoolToObject(json, field, val);
        CHECK(!JGetBool(json, "abc"));
    }

    SECTION("Not a bool") {
        JAddStringToObject(json, field, "value");
        CHECK(!JGetBool(json, field));
    }

    SECTION("Valid bol") {
        JAddBoolToObject(json, field, val);
        CHECK(JGetBool(json, field));
    }

    JDelete(json);
}

}

#endif // TEST
