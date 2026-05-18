/*!
 * @file JType_test.cpp
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

TEST_CASE("JType")
{
    NoteSetFnDefault(malloc, free, NULL, NULL);

    SECTION("NULL JSON") {
        CHECK(strcmp(JType(NULL), "") == 0);
    }

    SECTION("J Types") {
        J *items[] = {
            (J *)NoteMalloc(sizeof(J)),
            JCreateBool(true),
            JCreateBool(false),
            JCreateNull(),
            JCreateNumber(1.5),
            JCreateString("string"),
            JCreateRaw("raw"),
            JCreateObject(),
            JCreateArray()
        };
        memset(items[0], 0, sizeof(J));
        items[0]->type = JInvalid;

        const char *strings[] = {
            "invalid",
            "bool",
            "bool",
            "null",
            "number",
            "string",
            "string",
            "object",
            "array"
        };

        for (size_t i = 0; i < sizeof(items) / sizeof(*items); ++i) {
            CHECK(strcmp(JType(items[i]), strings[i]) == 0);
            JDelete(items[i]);
        }
    }
}

}

#endif // TEST
