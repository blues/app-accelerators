/*!
 * @file NoteTransaction_test.cpp
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
FAKE_VALUE_FUNC(bool, NoteReset)
FAKE_VALUE_FUNC(const char *, NoteJSONTransaction, char *, char **)
FAKE_VALUE_FUNC(bool, NoteTransactionStart, uint32_t)
FAKE_VALUE_FUNC(J *, NoteUserAgent)

namespace
{

const char *NoteJSONTransactionValid(char *, char **resp)
{
    static char respString[] = "{ \"total\": 1 }";

    if (resp) {
        char* respBuf = reinterpret_cast<char *>(malloc(sizeof(respString)));
        memcpy(respBuf, respString, sizeof(respString));
        *resp = respBuf;
    }

    return NULL;
}

const char *NoteJSONTransactionBadJSON(char *, char **resp)
{
    static char respString[] = "Bad JSON";

    if (resp) {
        char* respBuf = reinterpret_cast<char *>(malloc(sizeof(respString)));
        memcpy(respBuf, respString, sizeof(respString));
        *resp = respBuf;
    }

    return NULL;
}

TEST_CASE("NoteTransaction")
{
    NoteSetFnDefault(malloc, free, NULL, NULL);

    RESET_FAKE(NoteReset);
    RESET_FAKE(NoteJSONTransaction);
    RESET_FAKE(NoteTransactionStart);

    // NoteReset's mock should succeed unless the test explicitly instructs
    // it to fail.
    NoteReset_fake.return_val = true;
    NoteTransactionStart_fake.return_val = true;

    SECTION("Passing a NULL request returns NULL") {
        CHECK(NoteTransaction(NULL) == NULL);
    }

    SECTION("NoteTransactionStart fails") {
        NoteTransactionStart_fake.return_val = false;
        J *req = NoteNewRequest("note.add");
        REQUIRE(req != NULL);

        CHECK(NoteTransaction(req) == NULL);

        JDelete(req);
    }

    SECTION("A response is expected and the response is valid") {
        J *req = NoteNewRequest("note.add");
        REQUIRE(req != NULL);
        NoteJSONTransaction_fake.custom_fake = NoteJSONTransactionValid;

        J *resp = NoteTransaction(req);

        CHECK(NoteJSONTransaction_fake.call_count == 1);
        CHECK(resp != NULL);
        // Ensure there's no error in the response.
        CHECK(!NoteResponseError(resp));

        JDelete(req);
        JDelete(resp);
    }

    SECTION("A response is expected and the response has an error") {
        J *req = NoteNewRequest("note.add");
        REQUIRE(req != NULL);
        NoteJSONTransaction_fake.return_val = "This is an error.";

        J *resp = NoteTransaction(req);

        CHECK(NoteJSONTransaction_fake.call_count == 1);
        // Ensure there's an error in the response.
        CHECK(resp != NULL);
        CHECK(NoteResponseError(resp));

        JDelete(req);
        JDelete(resp);
    }

    SECTION("A reset is required and it fails") {
        J *req = NoteNewRequest("note.add");
        REQUIRE(req != NULL);
        NoteResetRequired();
        // Force NoteReset failure.
        NoteReset_fake.return_val = false;

        J *resp = NoteTransaction(req);

        CHECK(NoteReset_fake.call_count == 1);
        // The transaction shouldn't be attempted if reset failed.
        CHECK(NoteJSONTransaction_fake.call_count == 0);
        // The response should be null if reset failed.
        CHECK(resp == NULL);

        JDelete(req);
    }

    SECTION("Serializing the JSON request fails") {
        // Create an invalid J object.
        J *req = reinterpret_cast<J *>(malloc(sizeof(J)));
        REQUIRE(req != NULL);
        memset(req, 0, sizeof(J));

        J *resp = NoteTransaction(req);

        // The transaction shouldn't be attempted if the request couldn't be
        // serialized.
        CHECK(NoteJSONTransaction_fake.call_count == 0);
        // Ensure there's an error in the response.
        CHECK(resp != NULL);
        CHECK(NoteResponseError(resp));

        JDelete(req);
        JDelete(resp);
    }

    SECTION("No response is expected") {
        J *req = NoteNewCommand("note.add");
        REQUIRE(req != NULL);
        NoteJSONTransaction_fake.custom_fake = NoteJSONTransactionValid;

        J *resp = NoteTransaction(req);

        CHECK(NoteJSONTransaction_fake.call_count == 1);
        CHECK(resp != NULL);
        // Ensure there's no error in the response.
        CHECK(!NoteResponseError(resp));
        // Ensure a blank object was returned.
        J *expectedResp = JCreateObject();
        CHECK(JCompare(resp, expectedResp, true));

        JDelete(req);
        JDelete(resp);
        JDelete(expectedResp);
    }

    SECTION("Parsing the JSON response fails") {
        J *req = NoteNewRequest("note.add");
        REQUIRE(req != NULL);
        NoteJSONTransaction_fake.custom_fake = NoteJSONTransactionBadJSON;

        J *resp = NoteTransaction(req);

        CHECK(NoteJSONTransaction_fake.call_count == 1);
        CHECK(resp != NULL);
        // Ensure there's an error in the response.
        CHECK(NoteResponseError(resp));

        JDelete(req);
        JDelete(resp);
    }

#ifndef NOTE_DISABLE_USER_AGENT
    SECTION("hub.set with product adds user agent information") {
        J *req = NoteNewRequest("hub.set");
        REQUIRE(req != NULL);
        JAddStringToObject(req, "product", "a.b.c:d");
        NoteJSONTransaction_fake.custom_fake = NoteJSONTransactionValid;
        NoteUserAgent_fake.return_val = JCreateObject();

        J *resp = NoteTransaction(req);
        CHECK(resp != NULL);
        CHECK(NoteUserAgent_fake.call_count > 0);

        JDelete(req);
        JDelete(resp);
    }
#endif // !NOTE_DISABLE_USER_AGENT
}

}

#endif // TEST
