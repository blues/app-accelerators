// Copyright 2023 Blues Inc.  All rights reserved.
// Use of this source code is governed by licenses granted by the
// copyright holder including that found in the LICENSE file.

#pragma once

// Standard Libraries
#include <stdbool.h>
#include <stdint.h>

// Forward Declaration(s)
typedef struct J J;

#ifdef __cplusplus
extern "C" {
#endif
bool aqiInit(void);
void aqiPoll(int appID, int state, void *appContext);
void aqiResponse(int appID, J *rsp, void *appContext);

#ifdef __cplusplus
}
#endif