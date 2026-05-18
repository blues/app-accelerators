/*!
 * @file NoteSetFnMutex_test.cpp
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

uint8_t lockI2CCalled = 0;
uint8_t unlockI2CCalled = 0;
uint8_t lockNoteCalled = 0;
uint8_t unlockNoteCalled = 0;

void LockI2C()
{
    ++lockI2CCalled;
}

void UnlockI2C()
{
    ++unlockI2CCalled;
}

void LockNote()
{
    ++lockNoteCalled;
}

void UnlockNote()
{
    ++unlockNoteCalled;
}

TEST_CASE("NoteSetFnMutex")
{
    NoteSetFnMutex(LockI2C, UnlockI2C, LockNote, UnlockNote);

    NoteLockI2C();
    CHECK(lockI2CCalled == 1);

    NoteUnlockI2C();
    CHECK(unlockI2CCalled == 1);

    NoteLockNote();
    CHECK(lockNoteCalled == 1);

    NoteUnlockNote();
    CHECK(unlockNoteCalled == 1);

    // Unset the callbacks and ensure they aren't called again.
    NoteSetFnMutex(NULL, NULL, NULL, NULL);

    NoteLockI2C();
    CHECK(lockI2CCalled == 1);

    NoteUnlockI2C();
    CHECK(unlockI2CCalled == 1);

    NoteLockNote();
    CHECK(lockNoteCalled == 1);

    NoteUnlockNote();
    CHECK(unlockNoteCalled == 1);
}

}

#endif // TEST
