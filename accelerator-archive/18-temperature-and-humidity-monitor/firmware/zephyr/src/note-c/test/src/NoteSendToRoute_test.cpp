/*!
 * @file NoteSendToRoute_test.cpp
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
FAKE_VALUE_FUNC(bool, NoteRequest, J *)

namespace
{

TEST_CASE("NoteSendToRoute")
{
    NoteSetFnDefault(malloc, free, NULL, NULL);

    RESET_FAKE(NoteNewRequest);
    RESET_FAKE(NoteRequestResponse);
    RESET_FAKE(NoteRequest);

    const char method[] = "post";
    const char routeAlias[] = "alias";
    char file[] = "data.qo";
    J *body = JCreateObject();
    REQUIRE(body != NULL);

    SECTION("NoteNewRequest for note.event fails") {
        NoteNewRequest_fake.return_val = NULL;

        CHECK(!NoteSendToRoute(method, routeAlias, file, body));
    }

    SECTION("NoteRequestResponse") {
        J *firstReq = JCreateObject();
        NoteNewRequest_fake.return_val = firstReq;

        SECTION("Fails") {
            NoteRequestResponse_fake.return_val = NULL;

            CHECK(!NoteSendToRoute(method, routeAlias, file, body));
        }

        SECTION("Succeeds") {
            J *rsp = JCreateObject();
            NoteRequestResponse_fake.return_val = rsp;

            SECTION("Response has error") {
                JAddStringToObject(rsp, "err", "an error");

                CHECK(!NoteSendToRoute(method, routeAlias, file, body));
            }

            SECTION("NoteNewRequest for web.post fails") {
                J *noteNewRequestRetVals[] = {firstReq, NULL};
                SET_RETURN_SEQ(NoteNewRequest, noteNewRequestRetVals, 2);

                CHECK(!NoteSendToRoute(method, routeAlias, file, body));
            }

            SECTION("NoteNewRequest for web.post succeeds") {
                J *secondReq = JCreateObject();
                REQUIRE(secondReq != NULL);
                J *noteNewRequestRetVals[] = {firstReq, secondReq};
                SET_RETURN_SEQ(NoteNewRequest, noteNewRequestRetVals, 2);

                SECTION("NoteRequest for web.post fails") {
                    NoteRequest_fake.return_val = false;

                    CHECK(!NoteSendToRoute(method, routeAlias, file, body));
                }

                SECTION("NoteRequest for web.post succeeds") {
                    NoteRequest_fake.return_val = true;

                    CHECK(NoteSendToRoute(method, routeAlias, file, body));
                }

                CHECK(strcmp(JGetString(secondReq, "route"), routeAlias) == 0);

                JDelete(secondReq);
            }
        }

        JDelete(firstReq);
    }
}

}

#endif // TEST
