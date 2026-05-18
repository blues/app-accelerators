/*!
 * @file JGetString_test.cpp
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

TEST_CASE("JGetString")
{
    NoteSetFnDefault(malloc, free, NULL, NULL);

    const char field[] = "req";
    const char val[] = "note.add";
    J *json = JCreateObject();
    REQUIRE(json != NULL);
    JAddStringToObject(json, field, val);

    SECTION("NULL JSON") {
        char *ret = JGetString(NULL, "");
        CHECK(strcmp(ret, "") == 0);
    }

    SECTION("Field not present") {
        char *ret = JGetString(json, "cmd");
        CHECK(strcmp(ret, "") == 0);
    }

    SECTION("Field isn't a string") {
        JAddNumberToObject(json, "num", 5);
        char *ret = JGetString(json, "num");
        CHECK(strcmp(ret, "") == 0);
    }

    SECTION("valuestring is null") {
        J *string = JGetObjectItem(json, field);
        REQUIRE(string != NULL);
        char *tmp = string->valuestring;
        string->valuestring = NULL;
        char *ret = JGetString(json, field);
        string->valuestring = tmp;
        CHECK(strcmp(ret, "") == 0);
    }

    SECTION("Valid") {
        char *ret = JGetString(json, field);
        CHECK(strcmp(ret, val) == 0);
    }

    JDelete(json);
}

}

#endif // TEST
