/*!
 * @file JBoolValue_test.cpp
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

TEST_CASE("JBoolValue")
{
    NoteSetFnDefault(malloc, free, NULL, NULL);

    J *json = NULL;

    SECTION("NULL JSON") {
        CHECK(!JBoolValue(NULL));
    }

    SECTION("False") {
        json = JCreateBool(false);
        CHECK(!JBoolValue(json));
    }

    SECTION("True") {
        json = JCreateBool(true);
        CHECK(JBoolValue(json));
    }

    SECTION("Not a bool") {
        json = JCreateNumber(1);
        CHECK(!JBoolValue(json));
    }

    JDelete(json);
}

}

#endif // TEST
