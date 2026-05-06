/*!
 * @file JGetBinaryFromObject_test.cpp
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

namespace
{

TEST_CASE("JGetBinaryFromObject")
{
    RESET_FAKE(NoteMalloc);

    NoteSetFnDefault(NULL, free, NULL, NULL);
    NoteMalloc_fake.custom_fake = malloc;

    const char field[] = "req";
    const char val[] = "Here's a string to base64 encode";
    uint8_t *binData;
    uint32_t binDataLen;
    J *json = JCreateObject();
    REQUIRE(json != NULL);

    SECTION("NULL JSON") {
        CHECK(!JGetBinaryFromObject(NULL, field, &binData, &binDataLen));
    }

    SECTION("Empty string") {
        JAddStringToObject(json, field, "");

        CHECK(!JGetBinaryFromObject(json, field, &binData, &binDataLen));
    }

    SECTION("NoteMalloc fails") {
        JAddStringToObject(json, field, "string");

        NoteMalloc_fake.custom_fake = NULL;
        NoteMalloc_fake.return_val = NULL;

        CHECK(!JGetBinaryFromObject(json, field, &binData, &binDataLen));
    }

    SECTION("Success") {
        REQUIRE(JAddBinaryToObject(json, field, val, sizeof(val)));

        SECTION("With field specifier") {
            REQUIRE(JGetBinaryFromObject(json, field, &binData, &binDataLen));
        }

        SECTION("Without field specifier") {
            char *str = JGetString(json, field);
            REQUIRE(JGetBinaryFromObject((J *)str, NULL, &binData, &binDataLen));
        }

        REQUIRE(binDataLen == sizeof(val));
        CHECK(memcmp(binData, val, sizeof(val)) == 0);

        NoteFree(binData);
    }

    JDelete(json);
}

}

#endif // TEST
