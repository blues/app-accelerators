/*!
 * @file NotecardEnvVarManager_setEnvVarCb_test.cpp
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

#include "note-c/note.h"

#include "NotecardEnvVarManager.h"

namespace
{

void userCb(const char *var, const char *val, void *ctx)
{
}
uint32_t userCtx = 42;

TEST_CASE("NotecardEnvVarManager_setEnvVarCb")
{
    NoteSetFnDefault(malloc, free, NULL, NULL);

    NotecardEnvVarManager *man = NotecardEnvVarManager_alloc();
    REQUIRE(man != NULL);

    SECTION("NULL manager") {
        CHECK(NotecardEnvVarManager_setEnvVarCb(NULL, userCb, &userCtx)
              == NEVM_FAILURE);
    }

    SECTION("Valid manager") {
        CHECK(NotecardEnvVarManager_setEnvVarCb(man, userCb, &userCtx)
              == NEVM_SUCCESS);
    }

    NotecardEnvVarManager_free(man);
}

}

#endif // NEVM_TEST
