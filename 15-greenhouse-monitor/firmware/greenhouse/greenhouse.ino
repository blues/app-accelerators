// Copyright 2023 Blues Inc.  All rights reserved.
// Use of this source code is governed by licenses granted by the
// copyright holder including that found in the LICENSE file.

// App definitions
#define APP_MAIN
#include "app.h"
#include "monitor.h"
#include "monitor_tasks.h"
#include "Adafruit_BME280.h"
#include "Adafruit_seesaw.h"
#include "analog_sensor.h"
#include "TaskScheduler.h"

/**
 * @brief Provides the readings from a Seesaw soil sensor.
 */
struct SeesawPeripheral : public MultiSensorPeripheral<2, SeesawPeripheral> {

    SeesawPeripheral() : base({
        Sensor("soil_temp", "C"),
        Sensor("soil_moisture", "")
        }) {};

    bool readValues() {
        // unfortunately the seesaw library doesn't include error checking.
        setValue(0, ss.getTemp());
        setValue(1, ss.touchRead(0));
        return true;
    }

    bool initialize(bool first) {
        bool started = ss.begin(0x36);
        if (started) {
            debug.print("seesaw started! version: ");
            debug.println(ss.getVersion(), HEX);
        }
        else if (first) {
            debug.println("ERROR! seesaw not found");
        }
        return started;
    }

private:
    Adafruit_seesaw ss;

    using base = MultiSensorPeripheral<2, SeesawPeripheral>;
};

/**
 * @brief Reads temperature, humidity and pressure from a BME280
 */
struct BME280Peripheral : public MultiSensorPeripheral<3, BME280Peripheral> {

    BME280Peripheral() : base(
        {
        Sensor("air_temp", "C"),
        Sensor("air_humidity", "%"),
        Sensor("air_pressure", "hPa")
        }) {};

    bool readValues() {
        setValue(0, bme.readTemperature());
        setValue(1, bme.readHumidity());
        setValue(2, bme.readPressure()/100.0);
        return true;
    }

    bool initialize(bool first) {
        unsigned status = bme.begin();
        if (!status && first) {
            messaged = true;
            debug.println("Could not find a valid BME280 sensor, check wiring, address, sensor ID!");
            debug.print("SensorID was: 0x"); debug.println(bme.sensorID(),16);
            debug.print("        ID of 0xFF probably means a bad address, a BMP 180 or BMP 085\n");
            debug.print("   ID of 0x56-0x58 represents a BMP 280,\n");
            debug.print("        ID of 0x60 represents a BME 280.\n");
            debug.print("        ID of 0x61 represents a BME 680.\n");
        }
        return status;
    }

private:
    Adafruit_BME280 bme;
    bool started;
    bool messaged;

    using base = MultiSensorPeripheral<3, BME280Peripheral>;
};


// The app's sensors that are monitored and reported
Monitor monitor;
SeesawPeripheral seesaw;
BME280Peripheral bme280;
AnalogSensor light("light_level", "V", A0);


// adds app info to each event
AppReporter reporterConfig;
ReportEvents reporter(notecard, monitor, reporterConfig);

// poll the environment and respond to updates
void environmentUpdated(void* arg, J* environment);
auto pollEnvironment = create_environment_poller(notecard, environmentUpdated);

// manages the tasks in the app
Scheduler tasks;

void readSensors() {
    bme280.read();
    seesaw.read();
    light.read();
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
    pollEnvironment.poll();
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
void environmentUpdated(void* arg, J* environment) {
    EnvironmentUpdate update(environment);

    setTaskIntervalFromEnvironment(update, taskPollEnvironment, "environment_update_mins", 1000*60, DEFAULT_POLL_ENVIRONMENT_INTERVAL);
    setTaskIntervalFromEnvironment(update, taskReadSensors, "monitor_secs", 1000, DEFAULT_POLL_SENSORS_INTERVAL);
    setTaskIntervalFromEnvironment(update, taskReport, "report_mins", 1000*60, DEFAULT_POLL_SENSORS_INTERVAL);

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

    // Initialize sensors
    seesaw.begin();
    bme280.begin();
    light.begin();

    // add the sensor values to the monitor
    monitor.addSensor(light);
    monitor.addSensors(bme280.values);
    monitor.addSensors(seesaw.values);

    J *req = notecard.newRequest("hub.set");
    JAddStringToObject(req, "product", PRODUCT_UID);
    JAddStringToObject(req, "mode", "continuous");
    notecard.sendRequest(req);

    pollEnvironment.begin();     // update the app state from the environment
    reporter.begin();
}

bool sync() {
    J* req = notecard.newRequest("hub.sync");
    return notecard.sendRequest(req);
}

void loop()
{
    // allow manual triggering of tasks
    int c = Serial.read();
    switch (c) {
        case 'E': pollEnvironment.fetchEnvironment(); break;
        case 'e': sync(); task_poll_environment(); break;
        case 'm': task_monitor_sensors(); break;
        case 'r': sendReport(true); break;
        case 's': sync();
    }
    tasks.execute();
}

