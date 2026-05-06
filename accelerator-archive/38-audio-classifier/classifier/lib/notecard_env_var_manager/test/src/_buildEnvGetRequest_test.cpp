/*!
 * @file _buildEnvGetRequest_test.cpp
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
#include "test_static.h"

#include "note-c/note.h"

#include "NotecardEnvVarManager.h"

DEFINE_FFF_GLOBALS
FAKE_VALUE_FUNC(J *, NoteNewRequest, const char *)
FAKE_VALUE_FUNC(J *, JCreateStringReference, const char *)
FAKE_VALUE_FUNC(J *, JCreateArray)

namespace
{

J *JCreateArray_real(void)
{
    J* item = (J*)NoteMalloc(sizeof(J));
    if (item) {
        memset(item, '\0', sizeof(J));
        item->type=JArray;
    }

    return item;
}

J *JCreateStringReference_real(const char *string)
{
    J* item = (J*)NoteMalloc(sizeof(J));
    if (item) {
        memset(item, '\0', sizeof(J));
        item->type = JString | JIsReference;
        item->valuestring = (char*)string;
    }

    return item;
}

const char *vars[] = {
    "var_a",
    "var_b",
    "var_c"
};
static const size_t numVars = sizeof(vars) / sizeof(vars[0]);

TEST_CASE("_buildEnvGetRequest")
{
    RESET_FAKE(NoteNewRequest);
    RESET_FAKE(JCreateStringReference);
    RESET_FAKE(JCreateArray);

    NoteSetFnDefault(malloc, free, NULL, NULL);

    J *req = JCreateObject();
    REQUIRE(req != NULL);

    SECTION("Errors") {
        SECTION("vars NULL and numVars is not NEVM_ENV_VAR_ALL") {
            CHECK(_buildEnvGetRequest(NULL, numVars) == NULL);
            JDelete(req);
        }

        SECTION("NoteNewRequest fails") {
            NoteNewRequest_fake.return_val = NULL;

            CHECK(_buildEnvGetRequest(vars, numVars) == NULL);
            JDelete(req);
        }

        SECTION("JCreateArray fails") {
            NoteNewRequest_fake.return_val = req;
            JCreateArray_fake.return_val = NULL;

            CHECK(_buildEnvGetRequest(vars, numVars) == NULL);
        }

        SECTION("JCreateStringReference fails") {
            NoteNewRequest_fake.return_val = req;
            JCreateArray_fake.custom_fake = JCreateArray_real;
            JCreateStringReference_fake.return_val = NULL;

            CHECK(_buildEnvGetRequest(vars, numVars) == NULL);
        }
    }

    SECTION("Success") {
        NoteNewRequest_fake.return_val = req;
        JCreateArray_fake.custom_fake = JCreateArray_real;
        JCreateStringReference_fake.custom_fake = JCreateStringReference_real;

        SECTION("NEVM_ENV_VAR_ALL") {
            CHECK(_buildEnvGetRequest(NULL, NEVM_ENV_VAR_ALL) != NULL);
            CHECK(strcmp(NoteNewRequest_fake.arg0_history[0], "env.get") == 0);
            // NEVM_ENV_VAR_ALL should cause the request to have no "names"
            // field, which is how we request all env vars.
            CHECK(JGetObjectItem(req, "names") == NULL);
        }

        SECTION("Finite env vars") {
            CHECK(_buildEnvGetRequest(vars, numVars) != NULL);
            CHECK(strcmp(NoteNewRequest_fake.arg0_history[0], "env.get") == 0);
            // Make sure all the variables are in the "names" array.
            J *names = JGetObjectItem(req, "names");
            REQUIRE(names != NULL);
            J *name = NULL;
            REQUIRE(JGetArraySize(names) == numVars);
            for (size_t i = 0; i < numVars; ++i) {
                J *elem = JGetArrayItem(names, i);
                REQUIRE(elem != NULL);
                CHECK(strcmp(vars[i], elem->valuestring) == 0);
            }
        }

        JDelete(req);
    }
}

}

#endif // NEVM_TEST
