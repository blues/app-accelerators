/*!
 * @file JAtoI_test.cpp
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

TEST_CASE("JAtoI")
{
    const char* strings[] = {
        "0",
        "1",
        "1000",
        "0001",
        "   50",
        "+12345",
        "50   ",
        "-1",
        "-1000",
        "-0001",
        "   -50",
        "-50   "
    };
    long int nums[] = {
        0,
        1,
        1000,
        1,
        50,
        12345,
        50,
        -1,
        -1000,
        -1,
        -50,
        -50
    };

    for (size_t i = 0; i < sizeof(strings) / sizeof(*strings); ++i) {
        CHECK(JAtoI(strings[i]) == nums[i]);
    }
}

}

#endif // TEST
