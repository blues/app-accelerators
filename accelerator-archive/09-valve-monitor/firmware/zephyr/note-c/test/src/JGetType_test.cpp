/*!
 * @file JGetType_test.cpp
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

#include "n_lib.h"

namespace
{

TEST_CASE("JGetType")
{
    NoteSetFnDefault(malloc, free, NULL, NULL);

    const char field[] = "field";
    J *json = JCreateObject();

    SECTION("NULL JSON") {
        CHECK(JGetType(NULL, field) == JTYPE_NOT_PRESENT);
    }

    SECTION("NULL field") {
        CHECK(JGetType(json, NULL) == JTYPE_NOT_PRESENT);
    }

    SECTION("Wrong field") {
        CHECK(JGetType(json, "wrong_field") == JTYPE_NOT_PRESENT);
    }

    SECTION("JTrue") {
        JAddBoolToObject(json, field, true);
        CHECK(JGetType(json, field) == JTYPE_BOOL_TRUE);
    }

    SECTION("JFalse") {
        JAddBoolToObject(json, field, false);
        CHECK(JGetType(json, field) == JTYPE_BOOL_FALSE);
    }

    SECTION("JNull") {
        JAddNullToObject(json, field);
        CHECK(JGetType(json, field) == JTYPE_NULL);
    }

    SECTION("JNumber") {
        SECTION("Zero") {
            JAddNumberToObject(json, field, 0);
            CHECK(JGetType(json, field) == JTYPE_NUMBER_ZERO);
        }

        SECTION("Non-zero") {
            JAddNumberToObject(json, field, 1234);
            CHECK(JGetType(json, field) == JTYPE_NUMBER);
        }
    }

    SECTION("Strings") {
        typedef J *(*AddStringFn)(J *, const char * const, const char * const);
        // Run this set of tests with both JAddStringToObject and
        // JAddRawToObject.
        AddStringFn addStringFns[] = {JAddStringToObject, JAddRawToObject};

        for (size_t i=0; i < sizeof(addStringFns)/sizeof(*addStringFns); ++i) {
            // Sections have to have unique names, hence we incorporate the loop
            // counter into the name. See https://github.com/catchorg/Catch2/blob/devel/docs/limitations.md#sections-nested-in-loops.
            DYNAMIC_SECTION("Regular string " << i) {
                addStringFns[i](json, field, "string");
                CHECK(JGetType(json, field) == JTYPE_STRING);
            }

            DYNAMIC_SECTION("Empty string " << i) {
                addStringFns[i](json, field, "");
                CHECK(JGetType(json, field) == JTYPE_STRING_BLANK);
            }

            DYNAMIC_SECTION("NULL valuestring " << i) {
                addStringFns[i](json, field, "string");
                J *string = JGetObjectItem(json, field);
                char *tmp = string->valuestring;
                string->valuestring = NULL;
                CHECK(JGetType(json, field) == JTYPE_STRING_BLANK);
                string->valuestring = tmp;
            }

            DYNAMIC_SECTION("Number string " << i) {
                DYNAMIC_SECTION("Zero " << i) {
                    addStringFns[i](json, field, "0.0");
                    CHECK(JGetType(json, field) == JTYPE_STRING_ZERO);
                }

                DYNAMIC_SECTION("Non-zero " << i) {
                    addStringFns[i](json, field, "123.45");
                    CHECK(JGetType(json, field) == JTYPE_STRING_NUMBER);
                }
            }

            DYNAMIC_SECTION("Boolean string " << i) {
                DYNAMIC_SECTION("True " << i) {
                    DYNAMIC_SECTION("Lowercase " << i) {
                        addStringFns[i](json, field, "true");
                    }

                    DYNAMIC_SECTION("Uppercase " << i) {
                        addStringFns[i](json, field, "TRUE");
                    }

                    CHECK(JGetType(json, field) == JTYPE_STRING_BOOL_TRUE);
                }

                DYNAMIC_SECTION("False " << i) {
                    DYNAMIC_SECTION("Lowercase " << i) {
                        addStringFns[i](json, field, "false");
                    }

                    DYNAMIC_SECTION("Uppercase " << i) {
                        addStringFns[i](json, field, "FALSE");
                    }

                    CHECK(JGetType(json, field) == JTYPE_STRING_BOOL_FALSE);
                }
            }
        }
    }

    SECTION("JObject") {
        JAddObjectToObject(json, field);
        CHECK(JGetType(json, field) == JTYPE_OBJECT);
    }

    SECTION("JArray") {
        JAddArrayToObject(json, field);
        CHECK(JGetType(json, field) == JTYPE_ARRAY);
    }

    SECTION("JInvalid") {
        J *invalid = (J*)NoteMalloc(sizeof(J));
        memset(invalid, 0, sizeof(J));
        invalid->type = JInvalid;
        JAddItemToObject(json, field, invalid);

        CHECK(JGetType(json, field) == JTYPE_NOT_PRESENT);
    }

    JDelete(json);
}

}

#endif // TEST
