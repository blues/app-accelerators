/*!
 * @file JAddBinaryToObject_test.cpp
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
FAKE_VALUE_FUNC(void *, NoteMalloc, size_t)
FAKE_VALUE_FUNC(J *, JCreateStringValue, const char *)

namespace
{

J *JCreateStringValueCustom(const char *string)
{
    J *node = (J*)_Malloc(sizeof(J));
    if (node) {
        memset(node, '\0', sizeof(J));
        node->type = JString;
        node->valuestring = (char*)string;
    }

    return node;
}

TEST_CASE("JAddBinaryToObject")
{
    RESET_FAKE(NoteMalloc);
    RESET_FAKE(JCreateStringValue);

    NoteSetFnDefault(NULL, free, NULL, NULL);
    NoteMalloc_fake.custom_fake = malloc;

    const char field[] = "req";
    const char val[] = "Here's a string to base64 encode";
    J *json = JCreateObject();
    REQUIRE(json != NULL);

    SECTION("NULL JSON") {
        CHECK(!JAddBinaryToObject(NULL, field, val, sizeof(val)));
    }

    SECTION("NoteMalloc fails") {
        NoteMalloc_fake.custom_fake = NULL;
        NoteMalloc_fake.return_val = NULL;

        CHECK(!JAddBinaryToObject(json, field, val, sizeof(val)));
    }

    SECTION("JCreateStringValue fails") {
        JCreateStringValue_fake.return_val = NULL;

        CHECK(!JAddBinaryToObject(json, field, val, sizeof(val)));
    }

    SECTION("Success") {
        JCreateStringValue_fake.custom_fake = JCreateStringValueCustom;

        CHECK(JAddBinaryToObject(json, field, val, sizeof(val)));
        char *b64 = JGetString(json, field);
        REQUIRE(b64 != NULL);
        char decoded[sizeof(val)];
        REQUIRE(JB64Decode(decoded, b64) == sizeof(val));
        CHECK(strcmp(decoded, val) == 0);
    }

    JDelete(json);
}

}

#endif // TEST
