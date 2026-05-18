/*!
 * @file NoteSetFnI2CMutex_test.cpp
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

uint8_t lockI2CCalled = 0;
uint8_t unlockI2CCalled = 0;

void LockI2C()
{
    ++lockI2CCalled;
}

void UnlockI2C()
{
    ++unlockI2CCalled;
}

TEST_CASE("NoteSetFnI2CMutex")
{
    NoteSetFnI2CMutex(LockI2C, UnlockI2C);

    NoteLockI2C();
    CHECK(lockI2CCalled == 1);

    NoteUnlockI2C();
    CHECK(unlockI2CCalled == 1);

    // Unset the callbacks and ensure they aren't called again.
    NoteSetFnI2CMutex(NULL, NULL);

    NoteLockI2C();
    CHECK(lockI2CCalled == 1);

    NoteUnlockI2C();
    CHECK(unlockI2CCalled == 1);
}

}

#endif // TEST
