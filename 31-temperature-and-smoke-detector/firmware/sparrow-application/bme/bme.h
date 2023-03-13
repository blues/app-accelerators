// Copyright 2022 Blues Inc.  All rights reserved.
// Use of this source code is governed by licenses granted by the
// copyright holder including that found in the LICENSE file.

#pragma once

// Standard Libraries
#include <stdbool.h>

// Forward Declaration(s)
typedef struct J J;

typedef struct {
    double temperature;
    double pressure;
    double humidity;
} BMESample;

/**
 * @brief Initialize the BME280 app. 
 * 
 * @return true 
 * @return false 
 */
bool bmeInit(void);

/**
 * @brief An alternative to calling bmeInit() that doesn't start the app but initializes the sensor
 * for subsequent calls to bmeUpdate().
 * 
 * @return true     The sensor was detected
 * @return false    The sensor was not detected
 */
bool bmePresent();
bool bmeUpdate(BMESample* sample);
