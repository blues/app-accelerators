/*!
 * @file NotecardEnvVarManager_test.cpp
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
#include <cstring>
#include "fff.h"

#include "note-c/note.h"

#include "NotecardEnvVarManager.h"

DEFINE_FFF_GLOBALS
FAKE_VALUE_FUNC(J *, NoteTransaction, J *)

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
bool changeCbCalled[numVars] = {false, false, false};

void userCb(const char *var, const char *val, void *ctx)
{
    int ret = NEVM_FAILURE;

    for (size_t i = 0; i < sizeof(vars)/sizeof(*vars); ++i) {
        if (strcmp(var, vars[i]) == 0 && strcmp(val, vals[i]) == 0 &&
                userCtx == *((uint32_t *)ctx)) {
            changeCbCalled[i] = true;
            break;
        }
    }
}

J *savedReq = NULL;
J *rsp = NULL;

J *NoteTransactionSaveRequest(J *req)
{
    if (savedReq != NULL) {
        JDelete(savedReq);
    }

    savedReq = JDuplicate(req, true);

    return rsp;
}

TEST_CASE("NotecardEnvVarManager")
{
    RESET_FAKE(NoteTransaction);

    NoteSetFnDefault(malloc, free, NULL, NULL);
    char rawRsp[128];
    char val[16];
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
    NoteTransaction_fake.return_val = rsp;

    NotecardEnvVarManager *man = NotecardEnvVarManager_alloc();
    REQUIRE(man != NULL);
    CHECK(NotecardEnvVarManager_setEnvVarCb(man, userCb, &userCtx) ==
          NEVM_SUCCESS);

    SECTION("Fetch") {
        CHECK(NotecardEnvVarManager_fetch(man, vars, numVars)
              == NEVM_SUCCESS);
    }

    SECTION("Fetch with NEVM_ENV_VAR_ALL") {
        // Fetch with NEVM_ENV_VAR_ALL should result in a request for all
        // environment variables (i.e. env.get with no "names" field).
        NoteTransaction_fake.custom_fake = NoteTransactionSaveRequest;
        J *expectedReq = JParse("{\"req\": \"env.get\"}");

        CHECK(NotecardEnvVarManager_fetch(man, NULL, NEVM_ENV_VAR_ALL)
              == NEVM_SUCCESS);
        CHECK(JCompare(savedReq, expectedReq, false));

        JDelete(expectedReq);
    }

    for (size_t i = 0; i < sizeof(changeCbCalled)/sizeof(*changeCbCalled); ++i) {
        CHECK(changeCbCalled[i]);
        changeCbCalled[i] = false;
    }

    NotecardEnvVarManager_free(man);
    JDelete(savedReq);
    savedReq = NULL;
}

}

#endif // NEVM_TEST
