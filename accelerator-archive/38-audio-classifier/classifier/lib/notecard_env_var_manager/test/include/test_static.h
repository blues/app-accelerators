#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include "note-c/note.h"

// If we're building the tests, NEVM_STATIC is defined to nothing. This allows
// the tests to access the static functions in the library. Among other things,
// this let's us mock these normally static functions.
#define NEVM_STATIC

// Make these normally static functions externally visible if building tests.
J *_buildEnvGetRequest(const char **vars, size_t numVars);

#ifdef __cplusplus
}
#endif
