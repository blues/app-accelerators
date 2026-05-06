/*!
 * @file NoteSetFnNoteMutex_test.cpp
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

uint8_t lockNoteCalled = 0;
uint8_t unlockNoteCalled = 0;

void LockNote()
{
    ++lockNoteCalled;
}

void UnlockNote()
{
    ++unlockNoteCalled;
}

TEST_CASE("NoteSetFnNoteMutex")
{
    NoteSetFnNoteMutex(LockNote, UnlockNote);

    NoteLockNote();
    CHECK(lockNoteCalled == 1);

    NoteUnlockNote();
    CHECK(unlockNoteCalled == 1);

    // Unset the callbacks and ensure they aren't called again.
    NoteSetFnNoteMutex(NULL, NULL);

    NoteLockNote();
    CHECK(lockNoteCalled == 1);

    NoteUnlockNote();
    CHECK(unlockNoteCalled == 1);
}

}

#endif // TEST
