/*!
 * @file JStringValue_test.cpp
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

TEST_CASE("JStringValue")
{
    NoteSetFnDefault(malloc, free, NULL, NULL);

    J *json = NULL;

    SECTION("NULL JSON") {
        CHECK(strcmp(JStringValue(NULL), "") == 0);
    }

    SECTION("Not a string") {
        json = JCreateNumber(1);
        CHECK(JStringValue(json) == NULL);
    }

    SECTION("Valid string") {
        const char str[] = "test";
        json = JCreateString(str);
        CHECK(strcmp(JStringValue(json), str) == 0);
    }

    JDelete(json);
}

}

#endif // TEST
