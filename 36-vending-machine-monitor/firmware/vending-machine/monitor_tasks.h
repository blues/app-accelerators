// Copyright 2023 Blues Inc.  All rights reserved.
// Use of this source code is governed by licenses granted by the
// copyright holder including that found in the LICENSE file.

#pragma once

/**
 * Handles environment updates to adjust the timing interval of tasks in the app.
 */
#include "TaskScheduler.h"

#include "notecard-env.h"
#include <string>

/**
 * @brief Set the interval that a task repeats at that, as defined by an environment variable and a default value.
 * When the interval is 0 or not defined, the task is disabled.
 *
 * @param update    The current environment
 * @param task      THe task to update the scheduling interval
 * @param envVar    Name of the environment variable to update the interval from.
 * @param multiplier    The multiplier to scale the interval from the environment variable to milliseconds.
 * @param defaultInterval The default interval to use when the environment variable is not defined or is invalid (pre-scaled)
 */
void setTaskIntervalFromEnvironment(EnvironmentUpdate& update, Task& task, const char* envVar, uint32_t multiplier, uint32_t defaultInterval) {
    const char* interval_str = update.get(envVar);
    uint32_t interval = defaultInterval;
    if (interval_str && *interval_str) {    // variable is defined
        char *endptr;
        long value = strtol(interval_str, &endptr, 10);
        if (*endptr || value<0) {
            update.notifyError(envVar, "not a valid whole positive number.", interval_str);
        }
        else {
            interval = value * multiplier;
            if (task.getInterval()!=interval) {
                update.notifyChanged(envVar, task.getInterval()/multiplier, value);
            }
        }
    }
    if (interval) {
        task.setInterval(interval);
        task.enableIfNot();
    }
    else {
        task.disable();
    }
}
