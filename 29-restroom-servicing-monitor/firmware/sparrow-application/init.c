// Copyright 2023 Blues Inc.  All rights reserved.
// Use of this source code is governed by licenses granted by the
// copyright holder including that found in the LICENSE file.

// Sparrow Header(s)
#include <framework.h>

// Scheduled App Header(s)
#include "rsm/rsm.h"

void schedAppInit (void) {
    if (!rsmInit()) {
        APP_PRINTF("ERROR: Failed to initialize restroom servicing monitor!"
            "\r\n");
    }
}
