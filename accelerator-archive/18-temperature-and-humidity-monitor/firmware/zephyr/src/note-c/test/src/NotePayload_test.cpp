/*!
 * @file NotePayload_test.cpp
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
// Mocking NoteMalloc so that we can count invocations. See the "Force
// reallocation" test below.
FAKE_VALUE_FUNC(void *, NoteMalloc, size_t)

namespace
{

void *MallocWrapper(size_t size)
{
    return malloc(size);
}

TEST_CASE("NotePayload")
{
    NoteSetFnDefault(NULL, free, NULL, NULL);

    RESET_FAKE(NoteMalloc);

    NoteMalloc_fake.custom_fake = MallocWrapper;

    NotePayloadDesc desc = {0, 0, 0};
    const char* segIds[] = {
        "MYID",
        "ANID"
    };
    unsigned char segDataShort[] = {0xDE, 0xAD, 0xBE, 0xEF};
    unsigned char segDataLong[] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
    uint8_t *findData = NULL;
    uint32_t findLen = 1;

    SECTION("Add and find") {

        CHECK(NotePayloadAddSegment(&desc, segIds[0], segDataShort,
                                    sizeof(segDataShort)));
        CHECK(NotePayloadAddSegment(&desc, segIds[1], segDataLong,
                                    sizeof(segDataLong)));

        SECTION("Add and find segments") {
            CHECK(NotePayloadFindSegment(&desc, segIds[0], &findData,
                                         &findLen));
            CHECK(sizeof(segDataShort) == findLen);
            CHECK(!memcmp(segDataShort, findData, findLen));

            CHECK(NotePayloadFindSegment(&desc, segIds[1], &findData,
                                         &findLen));
            CHECK(sizeof(segDataLong) == findLen);
            CHECK(!memcmp(segDataLong, findData, findLen));
        }

        SECTION("Add and try to find non-existent segment") {
            CHECK(!NotePayloadFindSegment(&desc, "BLUE", &findData,
                                          &findLen));
            CHECK(findData == NULL);
            CHECK(findLen == 0);
        }
    }

    SECTION("Add two segments with the same ID") {
        CHECK(NotePayloadAddSegment(&desc, segIds[0], segDataLong,
                                    sizeof(segDataLong)));
        CHECK(NotePayloadAddSegment(&desc, segIds[0], segDataShort,
                                    sizeof(segDataShort)));

        CHECK(NotePayloadFindSegment(&desc, segIds[0], &findData, &findLen));
        // Ensure that we found the first segment added.
        REQUIRE(sizeof(segDataLong) == findLen);
        CHECK(!memcmp(segDataLong, findData, findLen));
    }

    SECTION("NotePayloadGetSegment") {
        const char segId[] = "MYID";
        unsigned char segData[] = {0xDE, 0xAD, 0xBE, 0xEF};
        unsigned char segCopy[] = {0, 0, 0, 0};
        unsigned char* segCopyOrig = segCopy;

        CHECK(NotePayloadAddSegment(&desc, segId, segData, sizeof(segData)));

        SECTION("Copied") {
            CHECK(NotePayloadGetSegment(&desc, segId, segCopy,
                                        sizeof(segData)));
            // Ensure NotePayloadGetSegment actually returned a copy and didn't
            // just return a pointer to the segment.
            CHECK(segCopyOrig == segCopy);
        }

        SECTION("Incorrect length") {
            CHECK(!NotePayloadGetSegment(&desc, segId, segCopy,
                                         sizeof(segData)) + 1);
        }
    }

    SECTION("Force reallocation") {
        // NotePayloadAddSegment allocates 512 bytes up front to hold data.
        // Adding this 1 kB segment will force allocation of a larger buffer.
        unsigned char segDataOneK[1024];
        for (size_t i = 0; i < sizeof(segDataOneK); ++i) {
            segDataOneK[i] = i;
        }

        CHECK(NotePayloadAddSegment(&desc, segIds[0], segDataLong,
                                    sizeof(segDataLong)));
        CHECK(NotePayloadAddSegment(&desc, segIds[1], segDataOneK,
                                    sizeof(segDataOneK)));

        // Ensure there was a second allocation.
        CHECK(NoteMalloc_fake.call_count == 2);

        CHECK(NotePayloadFindSegment(&desc, segIds[0], &findData,
                                     &findLen));
        REQUIRE(sizeof(segDataLong) == findLen);
        CHECK(!memcmp(segDataLong, findData, findLen));

        CHECK(NotePayloadFindSegment(&desc, segIds[1], &findData,
                                     &findLen));
        REQUIRE(sizeof(segDataOneK) == findLen);
        CHECK(!memcmp(segDataOneK, findData, findLen));
    }

    NotePayloadFree(&desc);
}

}

#endif // TEST
