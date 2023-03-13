// Copyright 2023 Blues Inc.  All rights reserved.
// Use of this source code is governed by licenses granted by the
// copyright holder including that found in the LICENSE file.

// Sparrow Header(s)
#include <framework.h>

// Scheduled App Header(s)
#include "aqi/aqi.h"

void schedAppInit (void) {
    if (!aqiInit()) {
        APP_PRINTF("ERROR: Failed to initialize air quality monitor!"
            "\r\n");
    }
}
