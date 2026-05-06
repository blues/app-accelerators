/*!
 * @file NoteSetSyncMode_test.cpp
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
FAKE_VALUE_FUNC(J *, NoteNewRequest, const char *)
FAKE_VALUE_FUNC(bool, NoteRequest, J *)

namespace
{

TEST_CASE("NoteSetSyncMode")
{
    NoteSetFnDefault(malloc, free, NULL, NULL);

    RESET_FAKE(NoteNewRequest);
    RESET_FAKE(NoteRequest);

    const char mode[] = "periodic";
    int uploadMins = 30;
    int downloadMins = 45;
    bool align = true;
    bool sync = false;

    SECTION("NoteNewRequest fails") {
        NoteNewRequest_fake.return_val = NULL;

        CHECK(!NoteSetSyncMode(mode, uploadMins, downloadMins, align, sync));
    }

    SECTION("NoteRequest") {
        J *req = JCreateObject();
        REQUIRE(req != NULL);
        NoteNewRequest_fake.return_val = req;

        SECTION("Fails") {
            NoteRequest_fake.return_val = false;

            CHECK(!NoteSetSyncMode(mode, uploadMins, downloadMins, align, sync));
        }

        SECTION("Succeeds") {
            NoteRequest_fake.return_val = true;

            SECTION("Zero upload minutes") {
                uploadMins = 0;
                CHECK(NoteSetSyncMode(mode, uploadMins, downloadMins, align,
                                      sync));
                CHECK(JGetObjectItem(req, "outbound") == NULL);
                CHECK(JGetObjectItem(req, "align") == NULL);
            }

            SECTION("Non-zero upload minutes") {
                uploadMins = 30;
                CHECK(NoteSetSyncMode(mode, uploadMins, downloadMins, align,
                                      sync));
                CHECK(JGetInt(req, "outbound") == uploadMins);
                CHECK(JGetBool(req, "align"));
            }

            SECTION("Zero download minutes") {
                downloadMins = 0;
                CHECK(NoteSetSyncMode(mode, uploadMins, downloadMins, align,
                                      sync));
                CHECK(JGetObjectItem(req, "inbound") == NULL);
            }

            SECTION("Non-zero download minutes") {
                downloadMins = 45;
                CHECK(NoteSetSyncMode(mode, uploadMins, downloadMins, align,
                                      sync));
                CHECK(JGetInt(req, "inbound") == downloadMins);
            }
        }

        JDelete(req);
    }
}

}

#endif // TEST
