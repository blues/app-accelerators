/*!
 * @file JItoA_test.cpp
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

TEST_CASE("JItoA")
{
    long int n = 0;
    char text[16];

    JItoA(n, text);
    CHECK(strcmp(text, "0") == 0);

    n = 1;
    JItoA(n, text);
    CHECK(strcmp(text, "1") == 0);

    // Trailing 0s.
    n = 1234500;
    JItoA(n, text);
    CHECK(strcmp(text, "1234500") == 0);

    // Negative with trailing 0s.
    n = -1234500;
    JItoA(n, text);
    CHECK(strcmp(text, "-1234500") == 0);
}

}

#endif // TEST
