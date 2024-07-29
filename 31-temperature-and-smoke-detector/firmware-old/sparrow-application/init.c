// Copyright 2023 Blues Inc.  All rights reserved.
// Use of this source code is governed by licenses granted by the
// copyright holder including that found in the LICENSE file.

// Sparrow Header(s)
#include <framework.h>

// Scheduled App Header(s)
#include "bme/bme.h"
#include "mq2/mq2.h"

void schedAppInit (void) {
    if (!mq2Init()) {
        APP_PRINTF("ERROR: Failed to initialize MQ2 sensor!"
            "\r\n");
    }
}
