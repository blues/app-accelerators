/*!
 * @file NotecardEnvVarManager_alloc_test.cpp
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
FAKE_VALUE_FUNC(void *, NoteMalloc, size_t);

namespace
{

TEST_CASE("NotecardEnvVarManager_alloc")
{
    RESET_FAKE(NoteMalloc);

    SECTION("NoteMalloc fails") {
        NoteMalloc_fake.return_val = NULL;

        CHECK(NotecardEnvVarManager_alloc() == NULL);
    }

    SECTION("NoteMalloc succeeds") {
        NoteMalloc_fake.custom_fake = malloc;

        NotecardEnvVarManager *man = NotecardEnvVarManager_alloc();
        CHECK(man != NULL);

        NotecardEnvVarManager_free(man);
    }
}

}

#endif // NEVM_TEST
