/*!
 * @file NoteUserAgent_test.cpp
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
FAKE_VALUE_FUNC(J *, JCreateObject)

static bool calledNoteUserAgentUpdate = false;
// NoteUserAgentUpdate is a weak function, so we should be able to override it
// here.
void NoteUserAgentUpdate(J *)
{
    calledNoteUserAgentUpdate = true;
}

namespace
{

TEST_CASE("NoteUserAgent")
{
    NoteSetFnDefault(malloc, free, NULL, NULL);

    RESET_FAKE(JCreateObject);
    calledNoteUserAgentUpdate = false;

    SECTION("JCreateObject fails") {
        JCreateObject_fake.return_val = NULL;

        CHECK(NoteUserAgent() == NULL);
    }

    SECTION("Success") {
        J* json = (J *)NoteMalloc(sizeof(J));
        REQUIRE(json != NULL);
        memset(json, 0, sizeof(J));
        json->type = JObject;
        JCreateObject_fake.return_val = json;

        char agent[] = "agent";
        NoteSetUserAgent(agent);

        char os[] = "os";
        char platform[] = "platform";
        char family[] = "family";
        char version[] = "version";
        NoteSetUserAgentOS(os, platform, family, version);

        const int mem = 1;
        const int mhz = 2;
        const int cores = 3;
        char vendor[] = "vendor";
        char cpu[] = "cpu";
        NoteSetUserAgentCPU(mem, mhz, cores, vendor, cpu);

        J *ua = NoteUserAgent();
        REQUIRE(ua != NULL);
        CHECK(strcmp(JGetString(ua, "agent"), agent) == 0);
#if defined(__GNUC__)
        CHECK(strstr(JGetString(ua, "compiler"), "gcc") != NULL);
#endif
        CHECK(strcmp(JGetString(ua, "req_interface"), "unknown") == 0);
        CHECK(JGetInt(ua, "cpu_mem") == mem);
        CHECK(JGetInt(ua, "cpu_mhz") == mhz);
        CHECK(JGetInt(ua, "cpu_cores") == cores);
        CHECK(strcmp(JGetString(ua, "cpu_vendor"), vendor) == 0);
        CHECK(strcmp(JGetString(ua, "cpu_name"), cpu) == 0);
        CHECK(strcmp(JGetString(ua, "os_name"), os) == 0);
        CHECK(strcmp(JGetString(ua, "os_platform"), platform) == 0);
        CHECK(strcmp(JGetString(ua, "os_family"), family) == 0);
        CHECK(strcmp(JGetString(ua, "os_version"), version) == 0);

        CHECK(calledNoteUserAgentUpdate);

        JDelete(json);
    }
}

}

#endif // TEST
