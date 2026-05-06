/*!
 * @file JAllocString_test.cpp
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
#include "fff.h"

#include "n_lib.h"

DEFINE_FFF_GLOBALS
FAKE_VALUE_FUNC(void *, NoteMalloc, size_t)

namespace
{

TEST_CASE("JAllocString")
{
    RESET_FAKE(NoteMalloc);

    NoteSetFnDefault(NULL, free, NULL, NULL);
    NoteMalloc_fake.custom_fake = malloc;

    // "Hello Blues!"
    uint8_t buf[] = {
        0x48, 0x65, 0x6c, 0x6c, 0x6f, 0x20, 0x42, 0x6c, 0x75, 0x65, 0x73, 0x21
    };
    uint32_t len = sizeof(buf);

    SECTION("0 length") {
        char *str = JAllocString(buf, 0);
        CHECK(strcmp(str, "") == 0);
        NoteFree(str);
    }

    SECTION("NoteMalloc fails") {
        NoteMalloc_fake.custom_fake = NULL;
        NoteMalloc_fake.return_val = NULL;

        CHECK(JAllocString(buf, len) == NULL);
    }

    SECTION(">0 length") {
        char *str = JAllocString(buf, len);
        CHECK(strcmp(str, "Hello Blues!") == 0);
        NoteFree(str);
    }
}

}

#endif // TEST
