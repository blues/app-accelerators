/*!
 * @file NotePayloadSaveAndSleep_test.cpp
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
FAKE_VALUE_FUNC(void *, NoteMalloc, size_t)
FAKE_VALUE_FUNC(bool, NoteSleep, char *, uint32_t, const char *)

namespace
{

TEST_CASE("NotePayloadSaveAndSleep")
{
    NoteSetFnDefault(NULL, free, NULL, NULL);

    RESET_FAKE(NoteMalloc);
    RESET_FAKE(NoteSleep);

    uint8_t data[] = {0xDE, 0xAD, 0xBE, 0xEF};
    NotePayloadDesc payload;
    payload.data = data;
    payload.alloc = sizeof(data);
    payload.length = sizeof(data);

    SECTION("Success") {
        NoteMalloc_fake.custom_fake = malloc;
        NoteSleep_fake.return_val = true;

        SECTION("Valid payload") {
            CHECK(NotePayloadSaveAndSleep(&payload, 30, NULL));
        }

        SECTION("Payload NULL data") {
            payload.data = NULL;
            CHECK(NotePayloadSaveAndSleep(&payload, 30, NULL));
        }

        SECTION("Payload 0 length") {
            payload.length = 0;
            CHECK(NotePayloadSaveAndSleep(&payload, 30, NULL));
        }
    }

    SECTION("NoteMalloc fails") {
        NoteMalloc_fake.return_val = NULL;

        CHECK(!NotePayloadSaveAndSleep(&payload, 30, NULL));
    }

    SECTION("NoteSleep fails") {
        NoteMalloc_fake.custom_fake = malloc;
        NoteSleep_fake.return_val = false;

        CHECK(!NotePayloadSaveAndSleep(&payload, 30, NULL));
    }
}

}

#endif // TEST
