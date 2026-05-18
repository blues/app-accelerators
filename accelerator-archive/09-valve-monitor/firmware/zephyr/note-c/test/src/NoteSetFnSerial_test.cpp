/*!
 * @file NoteSetFnSerial_test.cpp
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
FAKE_VALUE_FUNC(bool, serialNoteReset)
FAKE_VALUE_FUNC(const char *, serialNoteTransaction, char *, char **)

namespace
{

uint8_t serialResetCalled = 0;
uint8_t serialTransmitCalled = 0;
uint8_t serialAvailableCalled = 0;
uint8_t serialReceiveCalled = 0;

bool SerialReset()
{
    ++serialResetCalled;
    return true;
}

void SerialTransmit(uint8_t *, size_t, bool)
{
    ++serialTransmitCalled;
}

bool SerialAvailable()
{
    ++serialAvailableCalled;
    return true;
}

char SerialReceive()
{
    ++serialReceiveCalled;
    return 'a';
}


TEST_CASE("NoteSetFnSerial")
{
    RESET_FAKE(serialNoteReset);
    RESET_FAKE(serialNoteTransaction);

    char req[] = "{ \"req\": \"note.add\" }";
    char *resp = NULL;
    serialNoteReset_fake.return_val = true;
    serialNoteTransaction_fake.return_val = NULL;

    NoteSetFnSerial(SerialReset, SerialTransmit, SerialAvailable,
                    SerialReceive);

    CHECK(NoteSerialReset());
    CHECK(serialResetCalled == 1);

    NoteSerialTransmit((uint8_t *)req, strlen(req), false);
    CHECK(serialTransmitCalled == 1);

    CHECK(NoteSerialAvailable());
    CHECK(serialAvailableCalled == 1);

    CHECK(NoteSerialReceive() == 'a');
    CHECK(serialReceiveCalled == 1);

    CHECK(strcmp(NoteActiveInterface(), "serial") == 0);

    CHECK(NoteHardReset());
    CHECK(serialNoteReset_fake.call_count == 1);

    CHECK(NoteJSONTransaction(req, &resp) == NULL);
    CHECK(serialNoteTransaction_fake.call_count == 1);

    // Unset the callbacks and ensure they aren't called again.
    NoteSetFnSerial(NULL, NULL, NULL, NULL);
    NoteSetFnDisabled();

    CHECK(NoteSerialReset());
    CHECK(serialResetCalled == 1);

    NoteSerialTransmit((uint8_t *)req, strlen(req), false);
    CHECK(serialTransmitCalled == 1);

    CHECK(!NoteSerialAvailable());
    CHECK(serialAvailableCalled == 1);

    CHECK(NoteSerialReceive() == 0);
    CHECK(serialReceiveCalled == 1);

    CHECK(strcmp(NoteActiveInterface(), "unknown") == 0);

    CHECK(NoteHardReset());
    CHECK(serialNoteReset_fake.call_count == 1);

    CHECK(NoteJSONTransaction(req, &resp) != NULL);
    CHECK(serialNoteTransaction_fake.call_count == 1);
}

}

#endif // TEST
