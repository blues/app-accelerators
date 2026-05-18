/*!
 * @file NoteWake_test.cpp
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
FAKE_VALUE_FUNC(bool, NotePayloadRetrieveAfterSleep, NotePayloadDesc *)

namespace
{

const uint8_t expectedState[] = {0xDE, 0xAD, 0xBE, 0xEF};
const size_t stateLen = sizeof(expectedState);

bool NotePayloadRetrieveAfterSleepWrongLen(NotePayloadDesc *desc)
{
    memset(desc, 0, sizeof(NotePayloadDesc));
    desc->length = stateLen - 1;

    return true;
}

bool NotePayloadRetrieveAfterSleepValid(NotePayloadDesc *desc)
{
    memset(desc, 0, sizeof(NotePayloadDesc));
    desc->data = (uint8_t *)malloc(stateLen);
    memcpy(desc->data, expectedState, stateLen);
    desc->length = stateLen;

    return true;
}

TEST_CASE("NoteWake")
{
    RESET_FAKE(NotePayloadRetrieveAfterSleep);

    NoteSetFnDefault(malloc, free, NULL, NULL);
    uint8_t actualState[sizeof(expectedState)] = {0};

    SECTION("Errors") {
        SECTION("NotePayloadRetrieveAfterSleep fails") {
            NotePayloadRetrieveAfterSleep_fake.return_val = false;
        }

        SECTION("Wrong length") {
            NotePayloadRetrieveAfterSleep_fake.custom_fake =
                NotePayloadRetrieveAfterSleepWrongLen;
        }

        CHECK(!NoteWake(stateLen, actualState));
    }

    SECTION("Success") {
        NotePayloadRetrieveAfterSleep_fake.custom_fake =
            NotePayloadRetrieveAfterSleepValid;

        CHECK(NoteWake(stateLen, actualState));
        CHECK(memcmp(expectedState, actualState, stateLen) == 0);
    }
}

}

#endif // TEST
