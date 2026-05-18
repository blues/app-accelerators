/*!
 * @file NoteGetEnvNumber_test.cpp
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
FAKE_VALUE_FUNC(bool, NoteGetEnv, const char *, const char *, char *, uint32_t)

namespace
{

const long int num = 112345;
const char numStr[] = "112345";

bool NoteGetEnvVarExists(const char *variable, const char *defaultVal,
                         char *buf, uint32_t buflen)
{
    strcpy(buf, numStr);

    return true;
}

bool NoteGetEnvVarNotFound(const char *variable, const char *defaultVal,
                           char *buf, uint32_t buflen)
{
    strcpy(buf, defaultVal);

    return false;
}

TEST_CASE("NoteGetEnvNumber, NoteGetEnvInt")
{
    RESET_FAKE(NoteGetEnv);

    const char var[] = "var";
    long int defaultVal = -7897;

    SECTION("Var exists") {
        NoteGetEnv_fake.custom_fake = NoteGetEnvVarExists;

        CHECK(NoteGetEnvNumber(var, defaultVal) == num);
        CHECK(NoteGetEnvInt(var, defaultVal) == num);
    }

    SECTION("Var does not exist") {
        NoteGetEnv_fake.custom_fake = NoteGetEnvVarNotFound;

        CHECK(NoteGetEnvNumber(var, defaultVal) == defaultVal);
        CHECK(NoteGetEnvInt(var, defaultVal) == defaultVal);
    }

}

}

#endif // TEST
