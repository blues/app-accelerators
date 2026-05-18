/*!
 * @file JIntValue_test.cpp
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

TEST_CASE("JIntValue")
{
    NoteSetFnDefault(malloc, free, NULL, NULL);

    J *json = NULL;

    SECTION("NULL JSON") {
        CHECK(JIntValue(NULL) == 0);
    }

    SECTION("Not a number") {
        json = JCreateString("test");
        CHECK(JIntValue(json) == 0);
    }

    SECTION("Valid number") {
        long int num = 123;
        json = JCreateNumber(num);
        CHECK(JIntValue(json) == num);
    }

    JDelete(json);
}

}

#endif // TEST
