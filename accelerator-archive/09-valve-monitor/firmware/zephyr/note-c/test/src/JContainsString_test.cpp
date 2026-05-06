/*!
 * @file JContainsString_test.cpp
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

TEST_CASE("JContainsString")
{
    NoteSetFnDefault(malloc, free, NULL, NULL);

    J *json = JCreateObject();
    REQUIRE(json != NULL);
    const char field[] = "field";
    const char val[] = "value";
    const char substr[] = "alu";

    SECTION("NULL JSON") {
        CHECK(!JContainsString(NULL, field, substr));
    }

    SECTION("Field doesn't exist") {
        CHECK(!JContainsString(json, "abc", substr));
    }

    SECTION("Not a string") {
        JAddNumberToObject(json, field, 1.5);
        CHECK(!JContainsString(json, field, substr));
    }

    SECTION("valuestring is null") {
        JAddStringToObject(json, field, val);
        J *string = JGetObjectItem(json, field);
        REQUIRE(string != NULL);
        char *tmp = string->valuestring;
        string->valuestring = NULL;
        CHECK(!JContainsString(json, field, substr));
        string->valuestring = tmp;
    }

    SECTION("Empty string") {
        JAddStringToObject(json, field, val);
        CHECK(!JContainsString(json, field, ""));
    }

    SECTION("Match") {
        JAddStringToObject(json, field, val);
        CHECK(JContainsString(json, field, substr));
    }

    JDelete(json);
}

}

#endif // TEST
