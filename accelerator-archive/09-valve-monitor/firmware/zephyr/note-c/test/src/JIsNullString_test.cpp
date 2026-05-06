/*!
 * @file JIsNullString_test.cpp
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

TEST_CASE("JIsNullString")
{
    NoteSetFnDefault(malloc, free, NULL, NULL);

    J *json = JCreateObject();
    REQUIRE(json != NULL);
    const char field[] = "field";

    SECTION("NULL JSON") {
        CHECK(!JIsNullString(NULL, field));
    }

    SECTION("Field doesn't exist") {
        CHECK(JIsNullString(json, "abc"));
    }

    SECTION("Not a string") {
        JAddNumberToObject(json, field, 1.5);
        CHECK(!JIsNullString(json, field));
    }

    SECTION("valuestring is null") {
        JAddStringToObject(json, field, "string");
        J *string = JGetObjectItem(json, field);
        REQUIRE(string != NULL);
        char *tmp = string->valuestring;
        string->valuestring = NULL;
        CHECK(JIsNullString(json, field));
        string->valuestring = tmp;
    }

    SECTION("String is NULL") {
        JAddStringToObject(json, field, "");
        CHECK(JIsNullString(json, field));
    }

    JDelete(json);
}

}

#endif // TEST
