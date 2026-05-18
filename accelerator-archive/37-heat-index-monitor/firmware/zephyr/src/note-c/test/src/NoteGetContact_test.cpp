/*!
 * @file NoteGetContact_test.cpp
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
FAKE_VALUE_FUNC(J *, NoteRequestResponse, J *)

namespace
{

TEST_CASE("NoteGetContact")
{
    NoteSetFnDefault(malloc, free, NULL, NULL);

    RESET_FAKE(NoteNewRequest);
    RESET_FAKE(NoteRequestResponse);

    char nameBuf[16];
    // Set the first character of each buffer to something non-zero so that we
    // can verify that, on failure, each buffer holds the empty string.
    nameBuf[0] = 'a';
    char orgBuf[16];
    orgBuf[0] = 'a';
    char roleBuf[16];
    roleBuf[0] = 'a';
    char emailBuf[16];
    emailBuf[0] = 'a';

    J *req = JCreateObject();
    REQUIRE(req != NULL);
    J *rsp = JCreateObject();
    REQUIRE(rsp != NULL);

    SECTION("Errors") {
        SECTION("NoteNewRequest fails") {
            NoteNewRequest_fake.return_val = NULL;
        }

        SECTION("NoteRequestResponse fails") {
            NoteNewRequest_fake.return_val = req;
            NoteRequestResponse_fake.return_val = NULL;
        }

        SECTION("Response has error") {
            J *rsp = JCreateObject();
            REQUIRE(rsp != NULL);
            NoteRequestResponse_fake.return_val = rsp;
            JAddStringToObject(rsp, "err", "an error");
        }

        CHECK(!NoteGetContact(nameBuf, sizeof(nameBuf), orgBuf, sizeof(orgBuf),
                              roleBuf, sizeof(roleBuf), emailBuf, sizeof(emailBuf)));
        CHECK(strcmp(nameBuf, "") == 0);
        CHECK(strcmp(orgBuf, "") == 0);
        CHECK(strcmp(roleBuf, "") == 0);
        CHECK(strcmp(emailBuf, "") == 0);

        JDelete(rsp);
    }

    SECTION("Success") {
        char rspName[] = "abc";
        char rspOrg[] = "blues";
        char rspRole[] = "engineer";
        char rspEmail[] = "abc@blues.com";
        NoteNewRequest_fake.return_val = req;
        NoteRequestResponse_fake.return_val = rsp;
        JAddStringToObject(rsp, "name", rspName);
        JAddStringToObject(rsp, "org", rspOrg);
        JAddStringToObject(rsp, "role", rspRole);
        JAddStringToObject(rsp, "email", rspEmail);

        SECTION("NULL buffers") {
            CHECK(NoteGetContact(NULL, 0, NULL, 0, NULL, 0, NULL, 0));
        }

        SECTION("Valid buffers") {
            CHECK(NoteGetContact(nameBuf, sizeof(nameBuf), orgBuf, sizeof(orgBuf),
                                 roleBuf, sizeof(roleBuf), emailBuf, sizeof(emailBuf)));

            CHECK(strcmp(nameBuf, rspName) == 0);
            CHECK(strcmp(orgBuf, rspOrg) == 0);
            CHECK(strcmp(roleBuf, rspRole) == 0);
            CHECK(strcmp(emailBuf, rspEmail) == 0);
        }
    }

    JDelete(req);
}

}

#endif // TEST
