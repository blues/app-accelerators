/*!
 * @file NoteGetServiceConfig_test.cpp
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
FAKE_VALUE_FUNC(J *, NoteNewRequest, const char *)
FAKE_VALUE_FUNC(J *, NoteRequestResponse, J *)

namespace
{

TEST_CASE("NoteGetServiceConfig")
{
    NoteSetFnDefault(malloc, free, NULL, NULL);

    RESET_FAKE(NoteNewRequest);
    RESET_FAKE(NoteRequestResponse);

    const size_t BUF_SIZE = 64;
    const char zeros[BUF_SIZE] = {0};
    // Set first element of all these buffers to non-zero so we can verify
    // they're zero'd out when they should be.
    char prodBuf[BUF_SIZE] = {1};
    char serviceBuf[BUF_SIZE] = {1};
    char deviceBuf[BUF_SIZE] = {1};
    char snBuf[BUF_SIZE] = {1};

    SECTION("No response") {
        NoteRequestResponse_fake.return_val = NULL;

        CHECK(!NoteGetServiceConfig(prodBuf, sizeof(prodBuf), serviceBuf,
                                    sizeof(serviceBuf), deviceBuf, sizeof(deviceBuf), snBuf,
                                    sizeof(snBuf)));
        CHECK(!memcmp(prodBuf, zeros, sizeof(prodBuf)));
        CHECK(!memcmp(serviceBuf, zeros, sizeof(serviceBuf)));
        CHECK(!memcmp(deviceBuf, zeros, sizeof(deviceBuf)));
        CHECK(!memcmp(snBuf, zeros, sizeof(snBuf)));
    }

    SECTION("Response") {
        const char respProd[] = "com.your-company.your-name:your_product";
        const char respService[] = "a.notefile.net";
        const char respDev[] = "dev:000000000000000";
        const char respSn[] = "your-serial-number";

        J* resp = JCreateObject();
        REQUIRE(resp != NULL);
        JAddStringToObject(resp, "product", respProd);
        JAddStringToObject(resp, "host", respService);
        JAddStringToObject(resp, "device", respDev);
        JAddStringToObject(resp, "sn", respSn);
        NoteRequestResponse_fake.return_val = resp;

        CHECK(NoteGetServiceConfig(prodBuf, sizeof(prodBuf), serviceBuf,
                                   sizeof(serviceBuf), deviceBuf, sizeof(deviceBuf), snBuf,
                                   sizeof(snBuf)));
        CHECK(!strcmp(prodBuf, respProd));
        CHECK(!strcmp(serviceBuf, respService));
        CHECK(!strcmp(deviceBuf, respDev));
        CHECK(!strcmp(snBuf, respSn));

        SECTION("Subsequent no response returns cached values") {
            NoteRequestResponse_fake.return_val = NULL;

            memset(prodBuf, 0, sizeof(prodBuf));
            memset(serviceBuf, 0, sizeof(serviceBuf));
            memset(deviceBuf, 0, sizeof(deviceBuf));
            memset(snBuf, 0, sizeof(snBuf));

            CHECK(!NoteGetServiceConfig(prodBuf, sizeof(prodBuf), serviceBuf,
                                        sizeof(serviceBuf), deviceBuf, sizeof(deviceBuf), snBuf,
                                        sizeof(snBuf)));
            CHECK(!strcmp(prodBuf, respProd));
            CHECK(!strcmp(serviceBuf, respService));
            CHECK(!strcmp(deviceBuf, respDev));
            CHECK(!strcmp(snBuf, respSn));
        }
    }
}

}

#endif // TEST
