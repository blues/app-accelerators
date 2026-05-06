/*!
 * @file serialNoteReset_test.cpp
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
FAKE_VALUE_FUNC(bool, NoteSerialReset)
FAKE_VOID_FUNC(NoteSerialTransmit, uint8_t *, size_t, bool)
FAKE_VALUE_FUNC(bool, NoteSerialAvailable)
FAKE_VALUE_FUNC(char, NoteSerialReceive)
FAKE_VALUE_FUNC(long unsigned int, NoteGetMs)

namespace
{

long unsigned int NoteGetMsMock()
{
    static long unsigned int ret = 500;

    // Cycle through returning 0, 1, and 500. 500 ms is the timeout of the
    // receive loop in serialNoteReset.
    switch (ret) {
    case 0:
        ret = 1;
        break;
    case 1:
        ret = 500;
        break;
    case 500:
        ret = 0;
        break;
    }

    return ret;
}

TEST_CASE("serialNoteReset")
{
    RESET_FAKE(NoteSerialReset);
    RESET_FAKE(NoteSerialTransmit);
    RESET_FAKE(NoteSerialAvailable);
    RESET_FAKE(NoteSerialReceive);
    RESET_FAKE(NoteGetMs);

    SECTION("NoteSerialReset fails") {
        NoteSerialReset_fake.return_val = false;

        CHECK(!serialNoteReset());
        CHECK(NoteSerialReset_fake.call_count == 1);
        CHECK(NoteSerialTransmit_fake.call_count == 0);
    }

    SECTION("Serial never available") {
        NoteSerialReset_fake.return_val = true;
        NoteSerialAvailable_fake.return_val = false;
        NoteGetMs_fake.custom_fake = NoteGetMsMock;

        CHECK(!serialNoteReset());
        // serialNoteReset has retry logic, so we expect retries.
        CHECK(NoteSerialTransmit_fake.call_count > 1);
        CHECK(NoteSerialReceive_fake.call_count == 0);
    }

    SECTION("Character received") {
        NoteGetMs_fake.custom_fake = NoteGetMsMock;
        bool serialAvailRetVals[] = {true, false};
        SET_RETURN_SEQ(NoteSerialAvailable, serialAvailRetVals, 2);

        SECTION("Non-control character received") {
            NoteSerialReceive_fake.return_val = 'a';

            SECTION("Retry") {
                NoteSerialReset_fake.return_val = true;

                CHECK(!serialNoteReset());
                // Expect retries.
                CHECK(NoteSerialTransmit_fake.call_count > 1);
            }

            SECTION("NoteSerialReset fails before retry possible") {
                bool noteSerialResetRetVals[] = {true, false};
                SET_RETURN_SEQ(NoteSerialReset, noteSerialResetRetVals, 2);

                CHECK(!serialNoteReset());
                // No retries.
                CHECK(NoteSerialTransmit_fake.call_count == 1);
            }
        }

        SECTION("Only control character received") {
            NoteSerialReset_fake.return_val = true;
            NoteSerialReceive_fake.return_val = '\n';

            CHECK(serialNoteReset());
            // There should be no retrying.
            CHECK(NoteSerialTransmit_fake.call_count == 1);
        }

        CHECK(NoteSerialReceive_fake.call_count > 0);
    }

    SECTION("NoteGetMs overflow") {
        NoteSerialReset_fake.return_val = true;
        NoteSerialReceive_fake.return_val = ' ' - 1;
        bool serialAvailRetVals[] = {true, false};
        SET_RETURN_SEQ(NoteSerialAvailable, serialAvailRetVals, 2);
        long unsigned int getMsReturnVals[] = {
            UINT32_MAX - 500,
            UINT32_MAX - 400,
            0
        };
        SET_RETURN_SEQ(NoteGetMs, getMsReturnVals, 3);

        CHECK(serialNoteReset());
    }
}

}

#endif // TEST
