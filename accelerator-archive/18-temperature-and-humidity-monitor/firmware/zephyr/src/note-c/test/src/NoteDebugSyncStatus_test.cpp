/*!
 * @file NoteDebugSyncStatus_test.cpp
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
#include "time_mocks.h"

DEFINE_FFF_GLOBALS
FAKE_VALUE_FUNC(long unsigned int, NoteGetMs)
FAKE_VALUE_FUNC(J *, NoteNewRequest, const char *)
FAKE_VALUE_FUNC(J *, NoteRequestResponse, J *)
FAKE_VOID_FUNC(NoteDebug, const char *)

namespace
{

TEST_CASE("NoteDebugSyncStatus")
{
    NoteSetFnDefault(malloc, free, NULL, NULL);

    RESET_FAKE(NoteGetMs);
    RESET_FAKE(NoteNewRequest);
    RESET_FAKE(NoteRequestResponse);
    RESET_FAKE(NoteDebug);

    int pollFrequencyMs = 2000;
    int maxLevel = 3;

    J *req = JCreateObject();
    REQUIRE(req != NULL);
    NoteGetMs_fake.custom_fake = NoteGetMsIncrement;

    SECTION("Errors") {
        SECTION("NoteNewRequest fails") {
            NoteNewRequest_fake.return_val = NULL;

            CHECK(!NoteDebugSyncStatus(pollFrequencyMs, maxLevel));
        }

        SECTION("NoteRequestResponse fails") {
            NoteNewRequest_fake.return_val = req;
            NoteRequestResponse_fake.return_val = NULL;

            CHECK(!NoteDebugSyncStatus(pollFrequencyMs, maxLevel));
        }

        SECTION("Response has error") {
            NoteNewRequest_fake.return_val = req;
            J *rsp = JCreateObject();
            REQUIRE(rsp != NULL);
            NoteRequestResponse_fake.return_val = rsp;
            JAddStringToObject(rsp, "err", "an error");

            CHECK(!NoteDebugSyncStatus(pollFrequencyMs, maxLevel));
        }
    }

    SECTION("Suppression") {
        NoteNewRequest_fake.return_val = req;
        J *rsp = JCreateObject();
        REQUIRE(rsp != NULL);
        JAddStringToObject(rsp, "err", "an error");
        NoteRequestResponse_fake.return_val = rsp;

        // This first call should fail because there's an error in the response.
        // It should save the current millisecond counter and use that to
        // suppress subsequent calls that come in before the interval implied by
        // pollFrequencyMs has lapsed.
        SECTION("Successful suppression") {
            CHECK(!NoteDebugSyncStatus(pollFrequencyMs, maxLevel));
            CHECK(!NoteDebugSyncStatus(pollFrequencyMs, maxLevel));
            // There should be no Notecard request for the second call, since it
            // should be suppressed.
            CHECK(NoteRequestResponse_fake.call_count == 1);
        }

        SECTION("Millisecond rollover") {
            NoteGetMs_fake.custom_fake = NULL;
            long unsigned int getMsReturnVals[3] = {8000, 9000, 500};
            SET_RETURN_SEQ(NoteGetMs, getMsReturnVals, 3);

            CHECK(!NoteDebugSyncStatus(pollFrequencyMs, maxLevel));

            rsp = JCreateObject();
            REQUIRE(rsp != NULL);
            JAddStringToObject(rsp, "err", "an error");
            NoteRequestResponse_fake.return_val = rsp;

            // This call should not be suppressed, but it will still fail
            // because there's an error in the response.
            CHECK(!NoteDebugSyncStatus(pollFrequencyMs, maxLevel));
            // Confirm it wasn't suppressed by making sure a Notecard request
            // was made for both NoteDebugSyncStatus calls.
            CHECK(NoteRequestResponse_fake.call_count == 2);
        }
    }

    SECTION("Success") {
        NoteNewRequest_fake.return_val = req;
        J *rsp = JCreateObject();
        REQUIRE(rsp != NULL);
        NoteRequestResponse_fake.return_val = rsp;

        SECTION("No body in response") {
            CHECK(NoteDebugSyncStatus(pollFrequencyMs, maxLevel));
        }

        SECTION("Response has valid body") {
            J *body = JCreateObject();
            REQUIRE(body != NULL);
            JAddStringToObject(body, "subsystem", "subsystem");
            JAddStringToObject(body, "text", "text");
            JAddItemToObject(rsp, "body", body);

            SECTION("Synclog level above max") {
                JAddNumberToObject(body, "level", maxLevel + 1);

                CHECK(NoteDebugSyncStatus(pollFrequencyMs, maxLevel));

                // Synclog level in the response was greater than the max, so we
                // shouldn't see any NoteDebug calls.
                CHECK(NoteDebug_fake.call_count == 0);
            }

            SECTION("Synclog level at max") {
                JAddNumberToObject(body, "level", maxLevel);

                CHECK(NoteDebugSyncStatus(pollFrequencyMs, maxLevel));

                CHECK(NoteDebug_fake.call_count > 0);
            }

            SECTION("Synclog level below max") {
                JAddNumberToObject(body, "level", maxLevel - 1);

                CHECK(NoteDebugSyncStatus(pollFrequencyMs, maxLevel));

                CHECK(NoteDebug_fake.call_count > 0);
            }

            SECTION("Negative max level (log everything)") {
                JAddNumberToObject(body, "level", 1);

                CHECK(NoteDebugSyncStatus(pollFrequencyMs, -1));

                CHECK(NoteDebug_fake.call_count > 0);
            }
        }
    }

    JDelete(req);
}

}

#endif // TEST
