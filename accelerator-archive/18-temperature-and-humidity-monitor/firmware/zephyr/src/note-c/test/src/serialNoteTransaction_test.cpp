/*!
 * @file serialNoteTransaction_test.cpp
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
FAKE_VALUE_FUNC(bool, NoteSerialAvailable)
FAKE_VALUE_FUNC(char, NoteSerialReceive)
FAKE_VALUE_FUNC(long unsigned int, NoteGetMs)
FAKE_VOID_FUNC(NoteSerialTransmit, uint8_t *, size_t, bool)

namespace
{

char transmitBuf[CARD_REQUEST_SERIAL_SEGMENT_MAX_LEN * 2];
size_t transmitBufLen = 0;
bool resetTransmitBufLen = false;

void NoteSerialTransmitAppend(uint8_t *buf, size_t len, bool)
{
    if (resetTransmitBufLen) {
        transmitBufLen = 0;
        resetTransmitBufLen = false;
    }

    if (buf[len - 1] == '\n') {
        resetTransmitBufLen = true;
    }

    if (transmitBufLen + len > sizeof(transmitBuf)) {
        return;
    }

    memcpy(transmitBuf + transmitBufLen, buf, len);
    transmitBufLen += len;
}

#define SERIAL_MULTI_CHUNK_RECV_BYTES (ALLOC_CHUNK * 2)

char NoteSerialReceiveMultiChunk()
{
    static uint32_t left = SERIAL_MULTI_CHUNK_RECV_BYTES;

    if (left-- > 1) {
        return 1;
    } else {
        left = SERIAL_MULTI_CHUNK_RECV_BYTES;
        return '\n';
    }
}

void *MallocNull(size_t)
{
    return NULL;
}

TEST_CASE("serialNoteTransaction")
{
    NoteSetFnDefault(NULL, free, NULL, NULL);

    RESET_FAKE(NoteMalloc);
    RESET_FAKE(NoteSerialAvailable);
    RESET_FAKE(NoteSerialTransmit);
    RESET_FAKE(NoteSerialReceive);
    RESET_FAKE(NoteGetMs);

    char noteAddReq[] = "{\"req\": \"note.add\"}";

    SECTION("Transmit buffer allocation fails") {
        NoteMalloc_fake.return_val = NULL;

        CHECK(serialNoteTransaction(noteAddReq, NULL) != NULL);
        CHECK(NoteMalloc_fake.call_count == 1);
    }

    SECTION("No response expected") {
        NoteMalloc_fake.custom_fake = malloc;
        NoteSerialAvailable_fake.return_val = true;
        NoteSerialTransmit_fake.custom_fake = NoteSerialTransmitAppend;
        char *request = NULL;
        uint32_t reqLen;

        SECTION("One transmission") {
            reqLen = CARD_REQUEST_SERIAL_SEGMENT_MAX_LEN - 2;
            request = (char*)malloc(reqLen + 1);
            REQUIRE(request != NULL);
            memset(request, 1, reqLen);
            request[reqLen] = '\0';

            CHECK(serialNoteTransaction(request, NULL) == NULL);
            // The request length is less than
            // CARD_REQUEST_SERIAL_SEGMENT_MAX_LEN, so it should all be sent in
            // one call to NoteSerialTransmit.
            CHECK(NoteSerialTransmit_fake.call_count == 1);
            CHECK(!memcmp(transmitBuf, request, reqLen - 2));
            CHECK(!memcmp(transmitBuf + reqLen, c_newline, c_newline_len));
        }

        SECTION("Multiple transmissions") {
            reqLen = CARD_REQUEST_SERIAL_SEGMENT_MAX_LEN;
            request = (char*)malloc(reqLen + 1);
            REQUIRE(request != NULL);
            memset(request, 1, reqLen);
            request[reqLen] = '\0';

            CHECK(serialNoteTransaction(request, NULL) == NULL);
            // The request is 1 byte greater than
            // CARD_REQUEST_SERIAL_SEGMENT_MAX_LEN, so it should require two
            // calls to NoteSerialTransmit.
            CHECK(NoteSerialTransmit_fake.call_count == 2);
            CHECK(!memcmp(transmitBuf, request, reqLen - 2));
            CHECK(!memcmp(transmitBuf + reqLen, c_newline, c_newline_len));
        }

        free(request);
    }

    SECTION("Response expected") {
        char* resp = NULL;

        SECTION("Response buffer allocation fails") {
            NoteSerialAvailable_fake.return_val = true;
            uint8_t *transmitBuf = (uint8_t *)malloc(strlen(noteAddReq) +
                                   c_newline_len);
            REQUIRE(transmitBuf != NULL);
            void* mallocReturnVals[2] = {transmitBuf, NULL};
            SET_RETURN_SEQ(NoteMalloc, mallocReturnVals, 2);
            const char* err;

            CHECK((err = serialNoteTransaction(noteAddReq, &resp)) != NULL);
            CHECK(NoteSerialTransmit_fake.call_count == 1);
            CHECK(NoteSerialReceive_fake.call_count == 0);
            CHECK(NoteMalloc_fake.call_count == 2);
        }

        SECTION("NoteSerialReceive fails") {
            NoteSerialAvailable_fake.return_val = true;
            NoteMalloc_fake.custom_fake = malloc;
            NoteSerialReceive_fake.return_val = 0;

            CHECK(serialNoteTransaction(noteAddReq, &resp) != NULL);
            CHECK(NoteSerialTransmit_fake.call_count == 1);
            CHECK(NoteSerialReceive_fake.call_count == 1);
        }

        SECTION("Force timeout before receive") {
            NoteSerialAvailable_fake.return_val = false;
            NoteMalloc_fake.custom_fake = malloc;
            NoteSerialReceive_fake.return_val = '{';
            long unsigned int getMsReturnVals[3];

            SECTION("No millisecond overflow") {
                getMsReturnVals[0] = 0;
                getMsReturnVals[1] = 100;
                getMsReturnVals[2] = NOTECARD_TRANSACTION_TIMEOUT_SEC * 1000
                                     + 1;
            }

            SECTION("Millisecond overflow") {
                getMsReturnVals[0] = UINT32_MAX -
                                     NOTECARD_TRANSACTION_TIMEOUT_SEC * 1000;
                getMsReturnVals[1] = UINT32_MAX -
                                     (NOTECARD_TRANSACTION_TIMEOUT_SEC - 1) *
                                     1000;
                getMsReturnVals[2] = 0;
            }

            SET_RETURN_SEQ(NoteGetMs, getMsReturnVals, 3);
            const char* err;

            CHECK((err = serialNoteTransaction(noteAddReq, &resp)) != NULL);
            // Make sure we actually timed out by checking the error message.
            CHECK(strstr(err, "timeout") != NULL);
            CHECK(NoteSerialTransmit_fake.call_count == 1);
            CHECK(NoteSerialReceive_fake.call_count == 0);
        }

        SECTION("Check response") {
            SECTION("One receipt") {
                NoteSerialAvailable_fake.return_val = true;
                NoteMalloc_fake.custom_fake = malloc;
                NoteSerialReceive_fake.return_val = '\n';

                CHECK(serialNoteTransaction(noteAddReq, &resp) == NULL);
                CHECK(NoteSerialReceive_fake.call_count == 1);
            }

            SECTION("Multiple chunks") {
                NoteSerialAvailable_fake.return_val = true;
                NoteMalloc_fake.custom_fake = malloc;
                NoteSerialReceive_fake.custom_fake = NoteSerialReceiveMultiChunk;

                CHECK(serialNoteTransaction(noteAddReq, &resp) == NULL);
                CHECK(NoteSerialReceive_fake.call_count == SERIAL_MULTI_CHUNK_RECV_BYTES);
            }

            // The response should be all 1s followed by a newline.
            size_t respSz = strlen(resp);
            for (size_t i = 0; i < respSz; ++i) {
                if (i != respSz - 1) {
                    CHECK(resp[i] == 1);
                } else {
                    CHECK(resp[i] == '\n');
                }
            }
        }

        SECTION("Growing response buffer fails") {
            NoteSerialAvailable_fake.return_val = true;
            NoteSerialReceive_fake.custom_fake = NoteSerialReceiveMultiChunk;
            void *(*mallocFns[])(size_t) = {malloc, malloc, MallocNull};
            SET_CUSTOM_FAKE_SEQ(NoteMalloc, mallocFns, 3);

            CHECK(serialNoteTransaction(noteAddReq, &resp) != NULL);
        }

        SECTION("Partial response timeout") {
            bool availReturnVals[] = {true, true, false};
            SET_RETURN_SEQ(NoteSerialAvailable, availReturnVals, 3);
            NoteMalloc_fake.custom_fake = malloc;
            NoteSerialReceive_fake.return_val = 'a';
            long unsigned int getMsReturnVals[] = {
                0, 0, NOTECARD_TRANSACTION_TIMEOUT_SEC * 1000 + 1
            };
            SET_RETURN_SEQ(NoteGetMs, getMsReturnVals, 3);
            const char *err;

            CHECK((err = serialNoteTransaction(noteAddReq, &resp)) != NULL);
            // Make sure we hit the partial response error.
            CHECK(strstr(err, "incomplete") != NULL);
        }

        free(resp);
    }
}

}

#endif // TEST
