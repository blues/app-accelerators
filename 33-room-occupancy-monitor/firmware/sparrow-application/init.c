// Copyright 2023 Blues Inc.  All rights reserved.
// Use of this source code is governed by licenses granted by the
// copyright holder including that found in the LICENSE file.

// Sparrow Header(s)
#include <framework.h>

// Scheduled App Header(s)
#include "pir/pir.h"
#include "dsm/dsm.h"

void schedAppInit (void) {
    if (!pirInit()) {
        APP_PRINTF("ERROR: Failed to initialize PIR motion detector!"
            "\r\n");
    }
    if (!dsmInit()) {
        APP_PRINTF("ERROR: Failed to initialize door state monitor!"
            "\r\n");
    }
}
