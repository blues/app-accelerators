/*!
 * @file NotecardEnvVarManager_fetch_test.cpp
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

#ifdef NEVM_TEST

#include <catch2/catch_test_macros.hpp>
#include "fff.h"

#include "note-c/note.h"

#include "NotecardEnvVarManager.h"

DEFINE_FFF_GLOBALS
FAKE_VALUE_FUNC(J *, NoteRequestResponse, J *)

namespace
{

const size_t numVars = 3;
const char *vars[numVars] = {
    "var_a",
    "var_b",
    "var_c"
};
const char *vals[] = {
    "val_a",
    "val_b",
    "val_c"
};
uint32_t userCtx = 42;
bool userCbCalled[numVars] = {false, false, false};

void userCb(const char *var, const char *val, void *ctx)
{
    for (size_t i = 0; i < sizeof(vars)/sizeof(*vars); ++i) {
        if (strcmp(var, vars[i]) == 0 && strcmp(val, vals[i]) == 0 &&
                userCtx == *((uint32_t *)ctx)) {
            userCbCalled[i] = true;
            break;
        }
    }
}

J *rsp;
J *NoteRequestResponse_deleteReq(J *req)
{
    JDelete(req);
    return rsp;
}

TEST_CASE("NotecardEnvVarManager_fetch")
{
    RESET_FAKE(NoteRequestResponse);
    rsp = NULL;

    NoteSetFnDefault(malloc, free, NULL, NULL);

    NotecardEnvVarManager *man = NotecardEnvVarManager_alloc();
    REQUIRE(man != NULL);
    CHECK(NotecardEnvVarManager_setEnvVarCb(man, userCb, &userCtx) ==
          NEVM_SUCCESS);
    NoteRequestResponse_fake.custom_fake = NoteRequestResponse_deleteReq;

    SECTION("Errors") {
        SECTION("NULL manager") {
            CHECK(NotecardEnvVarManager_fetch(NULL, vars, numVars) ==
                  NEVM_FAILURE);
        }

        SECTION("NoteRequestResponse fails") {
            NoteRequestResponse_fake.return_val = NULL;

            CHECK(NotecardEnvVarManager_fetch(man, vars, numVars) ==
                  NEVM_FAILURE);
        }

        SECTION("Response has an error") {
            rsp = JCreateObject();
            REQUIRE(rsp != NULL);
            JAddStringToObject(rsp, "err", "an error");

            CHECK(NotecardEnvVarManager_fetch(man, vars, numVars) ==
                  NEVM_FAILURE);
        }

        SECTION("Malformed response (no \"body\" field)") {
            rsp = JCreateObject();
            REQUIRE(rsp != NULL);

            CHECK(NotecardEnvVarManager_fetch(man, vars, numVars) ==
                  NEVM_FAILURE);
        }
    }

    SECTION("Success") {
        SECTION("NULL user callback") {
            // This doesn't cause an error, but it should just return
            // immediately without fetching anything.
            CHECK(NotecardEnvVarManager_setEnvVarCb(man, NULL, NULL) ==
                  NEVM_SUCCESS);
            CHECK(NotecardEnvVarManager_fetch(man, vars, numVars) ==
                  NEVM_SUCCESS);
            // Ensure no request was made.
            CHECK(NoteRequestResponse_fake.call_count == 0);
        }

        SECTION("Valid user callback") {
            char rawRsp[128];
            snprintf(rawRsp, sizeof(rawRsp),
                     "{"
                     "\"body\": {"
                     "\"%s\": \"%s\","
                     "\"%s\": \"%s\","
                     "\"%s\": \"%s\""
                     "}"
                     "}", vars[0], vals[0], vars[1], vals[1], vars[2], vals[2]
                    );
            rsp = JParse(rawRsp);
            REQUIRE(rsp != NULL);

            CHECK(NotecardEnvVarManager_fetch(man, vars, numVars) ==
                  NEVM_SUCCESS);

            for (size_t i = 0; i < sizeof(userCbCalled)/sizeof(*userCbCalled);
                    ++i) {
                CHECK(userCbCalled[i]);
            }
        }
    }

    NotecardEnvVarManager_free(man);
}

}

#endif // NEVM_TEST
