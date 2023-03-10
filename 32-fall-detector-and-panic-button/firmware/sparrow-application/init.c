// Copyright 2023 Blues Inc.  All rights reserved.
// Use of this source code is governed by licenses granted by the
// copyright holder including that found in the LICENSE file.

// Sparrow Header(s)
#include <framework.h>

// Scheduled App Header(s)
#include "fall_detector/fall_detector.h"
#include "panic/panic.h"

void schedAppInit (void) {
    if (!fallDetectorInit()) {
        APP_PRINTF("ERROR: Failed to initialize fall detector!\r\n");
    }
    if (!panicInit()) {
        APP_PRINTF("ERROR: Failed to initialize panic button!\r\n");
    }
}
