/*!
 * @file JIsPresent_test.cpp
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

TEST_CASE("JIsPresent")
{
    NoteSetFnDefault(malloc, free, NULL, NULL);

    const char field[] = "req";
    J *json = JCreateObject();
    JAddStringToObject(json, field, "note.add");

    SECTION("NULL JSON") {
        CHECK(!JIsPresent(NULL, ""));
    }

    SECTION("Field not present") {
        CHECK(!JIsPresent(json, "cmd"));
    }

    SECTION("Field present") {
        CHECK(JIsPresent(json, "req"));
    }

    JDelete(json);
}

}

#endif // TEST
