/*!
 * @file JNumberValue_test.cpp
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

TEST_CASE("JNumberValue")
{
    NoteSetFnDefault(malloc, free, NULL, NULL);

    J *json = NULL;

    SECTION("NULL JSON") {
        CHECK(JNumberValue(NULL) == 0);
    }

    SECTION("Not a number") {
        json = JCreateString("test");
        CHECK(JNumberValue(json) == 0);
    }

    SECTION("Valid number") {
        JNUMBER num = 1.5;
        json = JCreateNumber(num);
        CHECK(JNumberValue(json) == num);
    }

    JDelete(json);
}

}

#endif // TEST
