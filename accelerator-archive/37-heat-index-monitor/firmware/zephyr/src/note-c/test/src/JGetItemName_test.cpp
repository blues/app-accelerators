/*!
 * @file JGetItemName_test.cpp
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

TEST_CASE("JGetItemName")
{
    NoteSetFnDefault(malloc, free, NULL, NULL);

    J *json = JCreateObject();
    REQUIRE(json != NULL);

    SECTION("Item NULL") {
        CHECK(strcmp(JGetItemName(NULL), "") == 0);
    }

    SECTION("No name") {
        CHECK(strcmp(JGetItemName(json), "") == 0);
    }

    SECTION("Valid name") {
        const char field[] = "req";
        JAddStringToObject(json, field, "note.add");
        J *item = JGetObjectItem(json, field);
        CHECK(strcmp(JGetItemName(item), field) == 0);
    }

    JDelete(json);
}

}

#endif // TEST
