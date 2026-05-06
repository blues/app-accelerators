/*!
 * @file NoteSetFn_test.cpp
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

bool mallocCalled = false;
bool freeCalled = false;
bool delayMsCalled = false;
bool getMsCalled = false;

void* MyMalloc(size_t)
{
    mallocCalled = true;
    return NULL;
}

void MyFree(void *)
{
    freeCalled = true;
}

void MyDelayMs(uint32_t)
{
    delayMsCalled = true;
}

uint32_t MyGetMs()
{
    getMsCalled = true;
    return 0;
}

TEST_CASE("NoteSetFn")
{
    NoteSetFnDefault(MyMalloc, MyFree, MyDelayMs, MyGetMs);

    SECTION("NoteSetFnDefault: Hooks not overridden") {
        // NoteSetFnDefault will only set the hooks if they're non-NULL, so
        // trying to set them to NULL here should have no effect, since they
        // were previously set to valid values.
        NoteSetFnDefault(NULL, NULL, NULL, NULL);

        void* buf = NoteMalloc(1);
        NoteFree(buf);
        NoteDelayMs(1);
        NoteGetMs();

        CHECK(mallocCalled);
        CHECK(freeCalled);
        CHECK(delayMsCalled);
        CHECK(getMsCalled);
    }

    SECTION("NoteSetFn: Hooks overridden") {
        NoteSetFn(NULL, NULL, NULL, NULL);

        void* buf = NoteMalloc(1);
        NoteFree(buf);
        NoteDelayMs(1);
        NoteGetMs();

        CHECK(!mallocCalled);
        CHECK(!freeCalled);
        CHECK(!delayMsCalled);
        CHECK(!getMsCalled);
    }

    mallocCalled = false;
    freeCalled = false;
    delayMsCalled = false;
    getMsCalled = false;
}

}

#endif // TEST
