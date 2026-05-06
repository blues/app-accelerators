/*!
 * @file NoteGetEnv_test.cpp
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
FAKE_VALUE_FUNC(J *, NoteNewRequest, const char *)
FAKE_VALUE_FUNC(J *, NoteRequestResponse, J *)

namespace
{

TEST_CASE("NoteGetEnv")
{
    NoteSetFnDefault(malloc, free, NULL, NULL);

    RESET_FAKE(NoteNewRequest);
    RESET_FAKE(NoteRequestResponse);

    const char envKey[] = "MyKey";
    const char defaultVal[] = "my default val";
    const char nonDefaultVal[] = "a non-default value";
    char outBuf[32];

    SECTION("NoteNewRequest returns NULL") {
        NoteNewRequest_fake.return_val = NULL;

        CHECK(!NoteGetEnv(envKey, defaultVal, outBuf, sizeof(outBuf)));
        CHECK(NoteNewRequest_fake.call_count == 1);
        // No request should be made if we couldn't allocate the request object.
        CHECK(NoteRequestResponse_fake.call_count == 0);
    }

    SECTION("Response is invalid") {
        J *req = JCreateObject();
        REQUIRE(req != NULL);
        NoteNewRequest_fake.return_val = req;

        SECTION("Response is NULL") {
            NoteRequestResponse_fake.return_val = NULL;

            CHECK(!NoteGetEnv(envKey, defaultVal, outBuf, sizeof(outBuf)));
        }

        SECTION("Response has no text field") {
            NoteRequestResponse_fake.return_val = JCreateObject();

            // The response doesn't have an error, so we expect success.
            CHECK(NoteGetEnv(envKey, defaultVal, outBuf, sizeof(outBuf)));
        }

        SECTION("Response has an error") {
            J *resp = JCreateObject();
            JAddStringToObject(resp, "err", "an error");
            NoteRequestResponse_fake.return_val = resp;

            CHECK(!NoteGetEnv(envKey, defaultVal, outBuf, sizeof(outBuf)));
        }

        CHECK(NoteNewRequest_fake.call_count == 1);
        CHECK(NoteRequestResponse_fake.call_count == 1);
        // Ensure the default value was copied into outBuf.
        CHECK(!strcmp(outBuf, defaultVal));

        JDelete(req);
    }

    SECTION("Response is valid") {
        J *req = JCreateObject();
        REQUIRE(req != NULL);
        NoteNewRequest_fake.return_val = req;
        J *resp = JCreateObject();
        JAddStringToObject(resp, "text", "a non-default value");
        NoteRequestResponse_fake.return_val = resp;

        CHECK(NoteGetEnv(envKey, defaultVal, outBuf, sizeof(outBuf)));
        CHECK(NoteNewRequest_fake.call_count == 1);
        CHECK(NoteRequestResponse_fake.call_count == 1);
        CHECK(!strcmp(outBuf, nonDefaultVal));

        JDelete(req);
    }
}

}

#endif // TEST
