// Copyright 2022 Blues Inc.  All rights reserved.
// Use of this source code is governed by licenses granted by the
// copyright holder including that found in the LICENSE file.

// App definitions
#define APP_MAIN
#include "app.h"


/**
 * @brief The main app takes care of setting up the tasks that monitor the Dr Watson instances connected
 * to the host.
 * 
 * The code that implements the app is found in `app.cpp`. 
 */

// Task instances
uint32_t tasks = 0;
uint8_t *taskContext;
uint32_t *taskNextRunMs;
uint32_t taskContextSize;

// Arduino entry point
void setup()
{

    // Initialize debug IO
    pinMode(LED_BUILTIN, OUTPUT);
    while (!debug) ;
    debug.begin(115200);
    debug.println("*** " __DATE__ " " __TIME__ " ***");

    // Initialize the RTOS support (see NoteRTOS.h)
    _init();

    // Initialize I2C
    Wire.begin();

    // Initialize Notecard library (without doing any I/O on this task)
    notecard.setDebugOutputStream(debug);
    notecard.begin();

    // Perform setup, including Notefile initialization on the Notecard
    while (!appSetup()) {
        delay(750);
    }

    // Discover the instances of the app.  By contract, the task context
    tasks = appTasks(&taskNextRunMs, &taskContext, &taskContextSize);

    // Setup each individual task
    for (uint32_t i=0; i<tasks; i++) {
        taskNextRunMs[i] = 0;
        taskSetup(&taskContext[i*taskContextSize]);
    }

}

// Poll the app and the task for work to be done
void loop()
{
    uint32_t nowMs = millis();
    static uint32_t prevMs = 0;

    // Run the app's 'loop' handler periodically
    static uint32_t appDueMs = 0;
    if (nowMs > appDueMs || nowMs < prevMs) {
        appDueMs = nowMs + appLoop();
    }

    // Run a single task's 'loop' handler, round-robin
    static uint32_t task = 0;
    if (task < tasks && (nowMs > taskNextRunMs[task] || nowMs < prevMs)) {
        taskNextRunMs[task] = nowMs + taskLoop(&taskContext[task*taskContextSize]);
    }
	task = task+1 >= tasks ? 0 : task+1;

    // Handle system timer wrap
    prevMs = nowMs;
}
