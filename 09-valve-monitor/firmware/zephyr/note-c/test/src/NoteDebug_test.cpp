/*!
 * @file NoteDebug_test.cpp
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

typedef struct {
    char debugBuf[32];
    size_t debugBufIdx;
    bool debugOutputCalled;
} TestState;

TestState state;

size_t MyDebugOutput(const char *line)
{
    size_t len = 0;
    state.debugOutputCalled = true;

    if (line != NULL) {
        len = strlcpy(state.debugBuf + state.debugBufIdx, line,
                      sizeof(state.debugBuf) - state.debugBufIdx);
        state.debugBufIdx += len;
    }

    return len;
}

TEST_CASE("NoteDebug")
{
    memset(&state, 0, sizeof(state));

    SECTION("Hook not set") {
        CHECK(!NoteIsDebugOutputActive());

        NoteDebug(NULL);
        CHECK(!state.debugOutputCalled);
    }

    SECTION("Hook set") {
        NoteSetFnDebugOutput(MyDebugOutput);

        SECTION("Active") {
            CHECK(NoteIsDebugOutputActive());
        }

        SECTION("Hook called") {
            NoteDebug(NULL);

#ifdef NOTE_NODEBUG
            CHECK(!state.debugOutputCalled);
#else
            CHECK(state.debugOutputCalled);
#endif
        }

        SECTION("NoteDebugln") {
            NoteDebugln("test");
            CHECK(!strcmp(state.debugBuf, "test\r\n"));
        }

        SECTION("NoteDebugIntln") {
            const char* expectedString;

            SECTION("Just number") {
                expectedString = "1\r\n";
                NoteDebugIntln(NULL, 1);
            }

            SECTION("String and number") {
                expectedString = "test1\r\n";
                NoteDebugIntln("test", 1);
            }

#ifdef NOTE_NODEBUG
            CHECK(!state.debugOutputCalled);
#else
            CHECK(state.debugOutputCalled);
            CHECK(!strcmp(state.debugBuf, expectedString));
#endif
        }
    }
}

}

#endif // TEST
