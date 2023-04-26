// Copyright 2023 Blues Inc.  All rights reserved.
// Use of this source code is governed by licenses granted by the
// copyright holder including that found in the LICENSE file.

// App definitions
#define APP_MAIN
#include "app.h"
#include "monitor.h"
#include "monitor_tasks.h"
#include "notecard-sensor.h"
#include "notecard-aux-serial.h"
#include "TaskScheduler.h"

// Sensors:
// ToF sensor for each column
// notecard power
// notecard voltage
// tilt? Not sure it is sensitive enough
// impact (movements)

// disable ToF sensor power when power fails, enable when power resumes
// potentially use periodic mode when power fails and longer delays inbound/outbound

// ToF sensor: report raw values
// detect changes and send a dispensed event
// detect level and send an alert for full, low and empty
/*
Dispensing stack
    I2C address of ToF sensor
    Distance to first can when full (indicates when full)
    Distance to dispenser plate (indicates when empty)
    compute capacity as percentage (based on distance)
    compute number of cans (if can height is given)
    stabilize readings
    look for step change: if distance increases a can has been dispensed. 
    if decreases, cans have been filled.

    low/high alerts not useful here or may need more fine grained alerts
    notecard voltage
 */


NotecardPowerSensor notecardPower(notecard);

class DispensingStack {
    DispensingStack(uint8_t i2cAddress=0x53) {}
};


// The app's sensors that are monitored and reported
Monitor monitor;

// adds app info to each event
AppReporter reporterConfig;
ReportEvents reporter(notecard, monitor, reporterConfig);


/*
 * Create the aux-serial manager over TX/RX
 */
auto auxSerial(newNotecardAuxSerial(notecard, Serial1));

// poll environment and respond to updates
void environmentUpdated(EnvironmentUpdater::timestamp_t modifiedTime, J* environment);
auto environment(EnvironmentUpdater(notecard, environmentUpdated));

NotecardEnvironmentNotifications notifyEnvironmentChanged(auxSerial, environment);

// manages the tasks in the app
Scheduler tasks;

NotecardTiltSensor tiltSensor(auxSerial);

void readSensors() {
    notecardPower.read();
}

void reportToStream(Stream& out, const Report& report) {
    out.println("sensor readings:");
    report.forEachSensor([&](const SensorReport& sr) {
        sr.toStream(out);
        out.println();
    });
}

/**
 * @brief Build a report from the monitored sensor values.
 * 
 * @param alwaysSend
 */
void sendReport(bool alwaysSend=false) {
    const Report report = monitor.checkReadings();
    reportToStream(debug, report);
    if (alwaysSend || report.reportImmediately()) {
        debug.println("sending report");
        reporter.sendReport(report);
    }
}

/**
 * @brief THe monitoring task function. Reads all the sensors in the app and reports alerts.
 */
void task_monitor_sensors() {
    readSensors();
    sendReport();
}

/**
 * @brief The polling environment task's function. Calls the app's environment poller to look for
 * environment updates, and process them. See `environmentUpdated()` below.
 */
void task_poll_environment() {
    debug.println("checking environment...");
    environment.poll();
}

/**
 * @brief The reporting task. Ensures that an event is sent at the configured interval.
 */
void task_send_report() {
    sendReport(true);
}

/**
 * @brief Poll the environment periodically. Always enabled.
 */
Task taskPollEnvironment(DEFAULT_POLL_ENVIRONMENT_INTERVAL, TASK_FOREVER, task_poll_environment, &tasks, true);

/**
 * @brief Poll the sensors for their current readings. Always enabled.
 */
Task taskReadSensors(DEFAULT_POLL_SENSORS_INTERVAL, TASK_FOREVER, task_monitor_sensors, &tasks, true);

/**
 * @brief Send a heartbeat monitoring event. Enabled when `report_mins` environment is non-zero.
 */
Task taskReport(DEFAULT_REPORT_INTERVAL, TASK_FOREVER, task_send_report, &tasks, false);


/**
 * @brief Handles updates to the environment for the app's tasks and report intervals.
 */
void environmentUpdated(EnvironmentUpdater::timestamp_t modifiedTime, J* environment) {
    EnvironmentUpdate update(environment);

    setTaskIntervalFromEnvironment(update, taskPollEnvironment, "environment_update_mins", 1000*60, DEFAULT_POLL_ENVIRONMENT_INTERVAL);
    setTaskIntervalFromEnvironment(update, taskReadSensors, "monitor_secs", 1000, DEFAULT_POLL_SENSORS_INTERVAL);
    setTaskIntervalFromEnvironment(update, taskReport, "report_mins", 1000*60, DEFAULT_REPORT_INTERVAL);

    monitor.environmentUpdated(update);

    update.toStream(debug);
    reporter.notifyUpdate(update);
}

void setup()
{
    // Initialize debug IO
    pinMode(LED_BUILTIN, OUTPUT);
    while (!debug) {};
    debug.begin(115200);
    debug.println("*** " __DATE__ " " __TIME__ " ***");

    // Initialize I2C
    Wire.begin();

    // Initialize Notecard library (without doing any I/O on this task)
    notecard.setDebugOutputStream(debug);
    notecard.begin();

    monitor.addSensors(notecardPower.values);

    // Initialize sensors
    notecardPower.begin();

    J *req = notecard.newRequest("hub.set");
    JAddStringToObject(req, "product", PRODUCT_UID);
    JAddStringToObject(req, "mode", "continuous");
    JAddBoolToObject(req, "sync", true);
    notecard.sendRequest(req);

    environment.begin();     // update the app state from the environment
    reporter.begin();

    auxSerial.initialize();
}

bool sync() {
    J* req = notecard.newRequest("hub.sync");
    return notecard.sendRequest(req);
}

void loop()
{
    auxSerial.poll();

    // allow manual triggering of tasks
    int c = Serial.read();
    switch (c) {
        case 'E': environment.fetchEnvironment(); break;
        case 'e': sync(); task_poll_environment(); break;
        case 'm': task_monitor_sensors(); break;
        case 'r': sendReport(true); break;
        case 's': sync();
    }
//    tasks.execute();
}

