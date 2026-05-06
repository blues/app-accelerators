/*!
 * @file JIsExactString_test.cpp
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

TEST_CASE("JIsExactString")
{
    NoteSetFnDefault(malloc, free, NULL, NULL);

    J *json = JCreateObject();
    REQUIRE(json != NULL);
    const char field[] = "field";
    const char val[] = "val";

    SECTION("NULL JSON") {
        CHECK(!JIsExactString(NULL, field, val));
    }

    SECTION("Field doesn't exist") {
        CHECK(!JIsExactString(json, "abc", val));
    }

    SECTION("Not a string") {
        JAddNumberToObject(json, field, 1.5);
        CHECK(!JIsExactString(json, field, val));
    }

    SECTION("valuestring is null") {
        JAddStringToObject(json, field, val);
        J *string = JGetObjectItem(json, field);
        REQUIRE(string != NULL);
        char *tmp = string->valuestring;
        string->valuestring = NULL;
        CHECK(!JIsExactString(json, field, val));
        string->valuestring = tmp;
    }

    SECTION("Empty string") {
        JAddStringToObject(json, field, "");
        /*
         * For some reason, trying to match the empty string always returns
         * false.
         */
        CHECK(!JIsExactString(json, field, ""));
    }

    SECTION("Match") {
        JAddStringToObject(json, field, val);
        CHECK(JIsExactString(json, field, val));
    }

    JDelete(json);
}

}

#endif // TEST
