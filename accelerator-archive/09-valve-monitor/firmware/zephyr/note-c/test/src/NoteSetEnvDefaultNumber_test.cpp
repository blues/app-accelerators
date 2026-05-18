/*!
 * @file NoteSetEnvDefaultInt_test.cpp
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
#include "fff.h"

#include "n_lib.h"

DEFINE_FFF_GLOBALS
FAKE_VALUE_FUNC(bool, NoteSetEnvDefault, const char *, char *)

namespace
{

char valBuf[32] = {0};

bool NoteSetEnvDefaultSaveBuffer(const char *, char *buf)
{
    strcpy(valBuf, buf);

    return true;
}

TEST_CASE("NoteSetEnvDefaultNumber, NoteSetEnvDefaultInt")
{
    RESET_FAKE(NoteSetEnvDefault);
    NoteSetEnvDefault_fake.custom_fake = NoteSetEnvDefaultSaveBuffer;

    const char var[] = "var";
    long int val = -123456;

    CHECK(NoteSetEnvDefaultNumber(var, val));
    CHECK(strcmp("-123456", valBuf) == 0);

    memset(valBuf, 0, sizeof(valBuf));

    CHECK(NoteSetEnvDefaultInt(var, val));
    CHECK(strcmp("-123456", valBuf) == 0);
}

}

#endif // TEST
