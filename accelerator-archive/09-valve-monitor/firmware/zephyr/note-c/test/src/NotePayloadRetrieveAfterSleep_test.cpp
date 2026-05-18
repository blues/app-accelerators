/*!
 * @file NotePayloadRetrieveAfterSleep_test.cpp
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
FAKE_VALUE_FUNC(void *, NoteMalloc, size_t)
FAKE_VALUE_FUNC(J *, NoteNewRequest, const char *)
FAKE_VALUE_FUNC(J *, NoteRequestResponse, J *)

namespace
{

TEST_CASE("NotePayloadRetrieveAfterSleep")
{
    NoteSetFnDefault(NULL, free, NULL, NULL);

    RESET_FAKE(NoteMalloc);
    RESET_FAKE(NoteNewRequest);
    RESET_FAKE(NoteRequestResponse);

    NoteMalloc_fake.custom_fake = malloc;

    NotePayloadDesc payload = {0};

    SECTION("No payload") {
        J *req = JCreateObject();
        REQUIRE(req != NULL);
        J *resp = JCreateObject();
        REQUIRE(resp != NULL);
        NoteNewRequest_fake.return_val = req;
        NoteRequestResponse_fake.return_val = resp;

        CHECK(NotePayloadRetrieveAfterSleep(NULL));

        JDelete(req);
    }

    SECTION("With payload") {

        SECTION("Errors") {

            SECTION("NoteNewRequest fails") {
                NoteNewRequest_fake.return_val = NULL;

                CHECK(!NotePayloadRetrieveAfterSleep(&payload));
            }

            SECTION("NoteRequestResponse fails") {
                J *req = JCreateObject();
                REQUIRE(req != NULL);
                NoteNewRequest_fake.return_val = req;
                NoteRequestResponse_fake.return_val = NULL;

                CHECK(!NotePayloadRetrieveAfterSleep(&payload));

                JDelete(req);
            }

            SECTION("Response has an error") {
                J *req = JCreateObject();
                REQUIRE(req != NULL);
                J *resp = JCreateObject();
                REQUIRE(resp != NULL);
                JAddStringToObject(resp, "err", "an error");
                NoteNewRequest_fake.return_val = req;
                NoteRequestResponse_fake.return_val = resp;

                CHECK(!NotePayloadRetrieveAfterSleep(&payload));

                JDelete(req);
            }

            SECTION("Response has no payload") {
                J *req = JCreateObject();
                REQUIRE(req != NULL);
                J *resp = JCreateObject();
                REQUIRE(resp != NULL);
                NoteNewRequest_fake.return_val = req;
                NoteRequestResponse_fake.return_val = resp;

                CHECK(!NotePayloadRetrieveAfterSleep(&payload));

                JDelete(req);
            }

            SECTION("Payload output buffer allocation fails") {
                J *req = JCreateObject();
                REQUIRE(req != NULL);
                J *resp = JCreateObject();
                REQUIRE(resp != NULL);
                JAddStringToObject(resp, "payload", "placeholder payload");
                NoteNewRequest_fake.return_val = req;
                NoteRequestResponse_fake.return_val = resp;
                NoteMalloc_fake.custom_fake = NULL;
                NoteMalloc_fake.return_val = NULL;

                CHECK(!NotePayloadRetrieveAfterSleep(&payload));

                JDelete(req);
            }
        }

        SECTION("Success") {
            J *req = JCreateObject();
            REQUIRE(req != NULL);
            J *resp = JCreateObject();
            REQUIRE(resp != NULL);
            const char decodedText[] = "it worked";
            const char encodedText[] = "aXQgd29ya2Vk";
            JAddStringToObject(resp, "payload", encodedText);
            JAddNumberToObject(resp, "time", 1599769214);
            NoteNewRequest_fake.return_val = req;
            NoteRequestResponse_fake.return_val = resp;

            CHECK(NotePayloadRetrieveAfterSleep(&payload));
            CHECK(memcmp(payload.data, decodedText, payload.length) == 0);

            JDelete(req);
        }
    }

    NotePayloadFree(&payload);
}

}

#endif // TEST
