/*!
 * @file NoteRequestWithRetry_test.cpp
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
FAKE_VALUE_FUNC(J *, NoteTransaction, J *)
FAKE_VALUE_FUNC(long unsigned int, NoteGetMs)

namespace
{

J *NoteTransactionIOError(J *)
{
    J *resp = JCreateObject();
    assert(resp != NULL);
    JAddStringToObject(resp, c_err, c_ioerr);

    return resp;
}

J *NoteTransactionNonIOError(J *)
{
    J *resp = JCreateObject();
    assert(resp != NULL);
    JAddStringToObject(resp, c_err, c_bad);

    return resp;
}

TEST_CASE("NoteRequestWithRetry")
{
    NoteSetFnDefault(malloc, free, NULL, NULL);

    RESET_FAKE(NoteTransaction);
    RESET_FAKE(NoteGetMs);

    SECTION("NULL request") {
        CHECK(!NoteRequestWithRetry(NULL, 0));
    }

    SECTION("Non-NULL request") {
        // With this timeout configuration, NoteTransaction will be retried at
        // most one time.
        const uint32_t timeoutSec = 5;
        long unsigned int getMsReturnVals[3];

        J *req = NoteNewRequest("note.add");
        REQUIRE(req != nullptr);

        SECTION("Timeout expires") {
            SECTION("No millisecond overflow") {
                getMsReturnVals[0] = 0;
                getMsReturnVals[1] = 3000;
                getMsReturnVals[2] = 6000;

                SECTION("NULL responses") {
                    NoteTransaction_fake.return_val = NULL;
                }

                SECTION("I/O error responses") {
                    NoteTransaction_fake.custom_fake = NoteTransactionIOError;
                }
            }

            SECTION("Millisecond overflow") {
                const uint32_t timeoutMillis = 5 * 1000;
                // Setup overflow condition:
                //     1. First value is 5 seconds before overflow.
                //     2. Second value is 4 seconds before overflow.
                //     3. Third value is 3 seconds after overflow.
                getMsReturnVals[0] = UINT32_MAX - timeoutMillis;
                getMsReturnVals[1] = UINT32_MAX - (timeoutMillis  - 1000);
                getMsReturnVals[2] = 3000;
            }

            SET_RETURN_SEQ(NoteGetMs, getMsReturnVals, 3);

            CHECK(!NoteRequestWithRetry(req, timeoutSec));
            CHECK(NoteTransaction_fake.call_count == 2);
        }

        SECTION("Non-I/O error") {
            NoteTransaction_fake.custom_fake = NoteTransactionNonIOError;

            CHECK(!NoteRequestWithRetry(req, timeoutSec));
            CHECK(NoteTransaction_fake.call_count == 1);
        }

        SECTION("Response on first try") {
            NoteTransaction_fake.return_val = JCreateObject();

            CHECK(NoteRequestWithRetry(req, timeoutSec));
            CHECK(NoteTransaction_fake.call_count == 1);
        }

        SECTION("Response after retry") {
            J *noteTransactionReturnVals[2] = {NULL, JCreateObject()};
            SET_RETURN_SEQ(NoteTransaction, noteTransactionReturnVals, 2);

            CHECK(NoteRequestWithRetry(req, timeoutSec));
            CHECK(NoteTransaction_fake.call_count == 2);
        }
    }
}

}

#endif
