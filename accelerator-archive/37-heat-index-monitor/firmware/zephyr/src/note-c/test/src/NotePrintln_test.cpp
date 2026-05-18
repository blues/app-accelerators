/*!
 * @file NotePrintln_test.cpp
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
FAKE_VALUE_FUNC(bool, NotePrint, const char *)

namespace
{

char printBuf[32];
size_t printBufLen = 0;

bool NotePrintSave(const char* text)
{
    strcpy(printBuf + printBufLen, text);
    printBufLen += strlen(text);
    printBuf[printBufLen] = '\0';

    return true;
}

TEST_CASE("NotePrintln")
{
    NotePrint_fake.custom_fake = NotePrintSave;

    const char msg[] = "Hello world!";
    size_t len = strlen(msg);

    NotePrintln(msg);

    CHECK(!(memcmp(printBuf, msg, len)));
    CHECK(!(memcmp(printBuf + len, c_newline, 2)));
}

}

#endif // TEST
