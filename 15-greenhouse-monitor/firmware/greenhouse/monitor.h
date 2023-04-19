// Copyright 2023 Blues Inc.  All rights reserved.
// Use of this source code is governed by licenses granted by the
// copyright holder including that found in the LICENSE file.


#pragma once

#include <string>
#include <cmath>
#include <vector>
#include "notecard-env.h"

#define STR_INTERVAL_LOW "low"
#define STR_INTERVAL_HIGH "high"

/**
 * @brief Classification of the alert status of a reading.
 */
struct AlertLevel {
    enum Enum { NORMAL, WARNING, CRITICAL };

    static const char* toString(Enum e) {
        switch (e) {
            case NORMAL: return "normal";
            case WARNING: return "warning";
            case CRITICAL: return "critical";
            default: return "unknown";
        }
    }

    static inline bool isAlert(Enum e) {
        return e!=NORMAL;
    }

    static inline Enum highest(Enum e1, Enum e2) {
        return e1 > e2 ? e1 : e2;
    }
};

/**
 * @brief Describes which alert is being sent.
 */
struct AlertSequence {
    enum Enum {
        NONE = 0,           /* regular report */
        FIRST = 1,          /* set for the first alert */
        SUBSEQUENT = 2,     /* set for subsequent alerts */
        LAST = 3            /* set for the first regular reading after a series of alerts */
    };

    static const char* toString(Enum e) {
        switch (e) {
            case NONE: return "none";
            case FIRST: return "first";
            case SUBSEQUENT: return "ongoing";
            case LAST: return "cleared";
            default: return "unknown";
        }
    }

    static inline bool isImmediate(Enum e) {
        return e==FIRST || e==LAST;
    }

    static inline bool isAlerting(Enum e) {
        return e!=NONE;
    }

    static inline bool isActiveAlert(Enum e) {
        return e==FIRST || e==SUBSEQUENT;
    }
};


class Sensor {
    const char* name; /* The name of the reading */
    const char* unit; /* The unit, may be null */
    double value;

public:
    static constexpr double UNDEFINED = NAN;

    Sensor(const char* name, const char* unit=nullptr, double value=UNDEFINED)
    : name(name), unit(unit), value(value) {}

    const char* getName() const {
        return name;
    }

    const char* getUnit() const {
        return unit;
    }

    double getValue() const {
        return value;
    }

    void setValue(double newValue) {
        value = newValue;
    }

    bool hasValue() const {
        return value != UNDEFINED;
    }

    template <typename S> void toStream(S& s) const {
        s.print(name);
        s.print(": ");
        if (hasValue()) {
            s.print(value);
        }
        else {
            s.print("N/A");
        }
    }
};

struct IntervalReport {
    enum Threshold {
        NONE,
        LOWER,
        UPPER
    };

    Threshold threshold;
    AlertLevel::Enum alertLevel;

    static const char* toString(Threshold threshold) {
        switch(threshold) {
            case NONE: return "none";
            case LOWER: return "lower";
            case UPPER: return "upper";
            default: return "unknown";
        };
    }

    static const char* toStatusString(Threshold threshold) {
        switch(threshold) {
            case NONE: return "ok";
            case LOWER: return "low";
            case UPPER: return "high";
            default: return "unknown";
        };
    }


    /**
     * @brief Indicates if this represents a threshold that has been crossed
     */
    operator bool() {
        return threshold;
    }

    template <typename S> void toStream(S& s) const {
        s.print(AlertLevel::toString(alertLevel));
        if (threshold!=NONE) {
            s.print(": threshold ");
            s.print(toString(threshold));
        }
    }
};

/**
 * @brief A range of values.
 */
struct Interval {

    double vmin;
    double vmax;

    static constexpr auto UNDEFINED = Sensor::UNDEFINED;

    Interval() : Interval(UNDEFINED, UNDEFINED) {};
    Interval(double vmin, double vmax) : vmin(vmin), vmax(vmax) {}

    IntervalReport::Threshold checkValue(double v) const {
        if (vmin != UNDEFINED && vmin > v) {
            return IntervalReport::LOWER;
        }
        if (vmax != UNDEFINED && vmax < v) {
            return IntervalReport::UPPER;
        }
        return IntervalReport::NONE;
    }

    template <typename S> void toStream(S& s) const {
        s.print('[');
        if (!isnan(vmin)) s.print(vmin);
        s.print(',');
        if (!isnan(vmax)) s.print(vmax);
        s.print(']');
    }

    bool isEmpty() const {
        return vmin==vmax || (isnan(vmin) && isnan(vmax));
    }
};

/**
 * @brief Describes the intervals that generate different levels of alerts.
 * todo - generify this
 */
struct AlertIntervals {
    Interval normal;
    Interval warning;

    static constexpr IntervalReport NORMAL = { .threshold = IntervalReport::NONE, .alertLevel = AlertLevel::NORMAL };

    static inline IntervalReport checkInterval(IntervalReport& report, const Interval& interval, AlertLevel::Enum level, double v) {
        return report = { interval.checkValue(v), level };
    }

    IntervalReport reportValue(double v) const {
        IntervalReport result;
        checkInterval(result, warning, AlertLevel::CRITICAL, v)
            || checkInterval(result, normal, AlertLevel::WARNING, v)
            || (result=NORMAL);
        return result;
    }

    template <typename Stream> void toStream(Stream& s) const {
        if (!normal.isEmpty()) {
            s.print(AlertLevel::toString(AlertLevel::NORMAL));
            normal.toStream(s);
            s.print(' ');
        }
        if (!warning.isEmpty()) {
            s.print(AlertLevel::toString(AlertLevel::WARNING));
            warning.toStream(s);
        }
    }
};


/**
 * @brief Applies alerts to sensor values and updates the alert intervals from the environment.
 */
class SensorMonitor: public Environmental {

    inline bool compare_float(double a, double b, double epsilon=0.01) {
        return (fabs(a - b) < epsilon);
    }

    /**
     * @brief Retrieve the value for an internal limit
     * 
     * @param update The details of the environment update
     * @param name  The name of the interval to update
     * @param limit The limit (min/max) of the interval to update
     * @param result The updated value - only valid if the function returns true.
     * @return true If the variable was retrieved successfully
     */
    bool getIntervalLimitFromEnvrionment(EnvironmentUpdate& update, const char* name, const char* limit, double& result) {
        char varname[256];
        snprintf(varname, sizeof(varname), "%s_%s_%s", sensor.getName(), name, limit);
        const char* value = update.get(varname);
        bool success = false;
        double r = Interval::UNDEFINED;
        if (value && *value) {
            char* end;
            r = strtod(value, &end);
            if (end-value==strlen(value)) { // all characters parsed
                if (!compare_float(result, r)) {
                    update.notifyChanged(varname, result, r);
                    result = r;
                }
                success = true;
            }
            else {
                update.notifyError(varname, "Value is not a number.", value);
            }
        }
        else {      // environment variable removed
            result = r;
            success = true;
        }
        return success;
    }

    void updateIntervalFromEnvironment(EnvironmentUpdate& update, Interval& interval, const char* name, bool dryRun=false) {
        double tmp;
        getIntervalLimitFromEnvrionment(update, name, STR_INTERVAL_LOW, dryRun ? (tmp=interval.vmin) : interval.vmin);
        getIntervalLimitFromEnvrionment(update, name, STR_INTERVAL_HIGH, dryRun ? (tmp=interval.vmax) : interval.vmax);
    }

public:

    SensorMonitor(const Sensor& sensor) : sensor(sensor) {}

    void environmentUpdated(EnvironmentUpdate& update, bool dryRun=false) {
        updateIntervalFromEnvironment(update, alertIntervals.normal, "normal", dryRun);
        updateIntervalFromEnvironment(update, alertIntervals.warning, "warning", dryRun);
    }

    IntervalReport alertLevel() const {
        return alertIntervals.reportValue(sensor.getValue());
    }

    template<typename S> void toStream(S& out) const {
        sensor.toStream(out);
        out.print(' ');
        alertIntervals.toStream(out);
    }

    /**
     * @brief The sensor reading to monitor and report
     */
    const Sensor& sensor;

    /**
     * @brief The value intervals that define the value ranges for warning and critical alerts.
     */
    AlertIntervals alertIntervals;
};


/**
 * @brief A sensor that provides multiple sensor values.
 *
 * @tparam N The number of values provided.
 */
template<int N>
struct MultiSensor {

    using Values = std::array<Sensor, N>;

    MultiSensor(const Values&& sensors) : values(sensors) {}

    void setValue(size_t index, double value) {
        values[index].setValue(value);
    }

    void clearValues() {
        for (int i=0; i<N; i++) {
            setValue(i, Sensor::UNDEFINED);
        }
    }

    Values values;
};

/**
 * @brief Manages the startup of the sensor, so that diagnostic messages are only sent on the 
 * first failure, or subsequent failure after a successful read.
 *
 * @tparam AppSensor The name of the subclass. This is used to avoid runtime polymorphism.
 */
template<typename AppClass>
struct SensorPeripheral {

    bool begin() {
        if (!started) {
            started = peripheral().initialize(first);
            if (started) {
                started = peripheral().readValues();
                if (!started) {
                    peripheral().reset();
                }
            }
            first = false;
        }
        return started;
    }

    bool read() {
        if (!begin()) {
            peripheral().clearValues();
            return false;
        }
        bool success = peripheral().readValues();
        if (!success) {
            peripheral().clearValues();
            peripheral().reset();
            started = false;
            first = true;
        }
        return success;
    }

protected:
    /**
     * @brief Can be implemented by subclasses to reset the sensor so it is ready for reinitialization.
     */
    void reset() {}

    AppClass& peripheral() {
        return *((AppClass*)this);
    }

private:
    bool started;   // has the sensor been successfully started
    bool first;     // when not started is this the first attempt or first attempt after unsuccessful read
};

template <int N, class Peripheral>
struct MultiSensorPeripheral : public MultiSensor<N>, public SensorPeripheral<Peripheral> {
    using baseSensor = MultiSensor<N>;
    using Values = typename baseSensor::Values;

    MultiSensorPeripheral(const Values&& sensors) : baseSensor(std::forward<const Values>(sensors)) {}
};

struct SensorReport {
    const SensorMonitor& monitor;
    const IntervalReport interval;

    template <typename S> void toStream(S& out) const {
        monitor.sensor.toStream(out);
        out.print(' ');
        interval.toStream(out);
        out.print(' ');
        monitor.alertIntervals.toStream(out);
    }
};

/**
 * @brief The details of an alert.
 */
struct Report {
    /**
     * @brief
     */
    std::vector<SensorReport> checks;

    /**
     * @brief The sequence for this alert.
     */
    AlertSequence::Enum alertSequence = AlertSequence::NONE;

    /**
     * @brief The alert level for this alert. This is the highest alert level found across all sensors.
     */
    AlertLevel::Enum alertLevel;


    template <typename Callback> void forEachSensor(Callback callback) const {
        std::for_each(checks.begin(), checks.end(), callback);
    }

    void beginUpdate() {
        alertLevel = AlertLevel::NORMAL;
        checks.clear();
    }

    const SensorReport& updateSensorReading(const SensorMonitor& sensor) {
        const SensorReport report = { sensor, sensor.alertLevel() };
        // find the most serious alert level
        alertLevel = AlertLevel::highest(alertLevel, report.interval.alertLevel);
        checks.push_back(report);
        return checks.back();
    }

    void endUpdate() {
        bool wasAlerting = AlertSequence::isActiveAlert(alertSequence);
        alertSequence = AlertLevel::isAlert(alertLevel)
            ? (wasAlerting ? AlertSequence::SUBSEQUENT : AlertSequence::FIRST)
            : (wasAlerting ? AlertSequence::LAST : AlertSequence::NONE);
    }

    inline bool reportImmediately() const {
        return AlertSequence::isImmediate(alertSequence);
    }
};

/**
 * @brief Monitors the sensors in the app, checking their current values against
 * the configured limits.
 */
class Monitor: public Environmental {

    Report report;
    std::vector<SensorMonitor> sensors;

public:

    template <typename Callback> void forEachSensor(Callback callback) {
        std::for_each(sensors.begin(), sensors.end(), callback);
    }

    /**
     * @brief Notifies the monitor that the environment was updated.
     *
     * @param environment The environment keys and values.
     */
    void environmentUpdated(EnvironmentUpdate& update, bool dryRun=false) {
        // iterate through monitored sensors and update their environment
        forEachSensor([&](SensorMonitor& sensor) {
            sensor.environmentUpdated(update, dryRun); 
        });
    }

    /**
     * @brief Checks the latest readings for alerts and returns the alert reasons and status.
     *
     * @return const Alert 
     */
    const Report checkReadings() {
        report.beginUpdate();
        forEachSensor([&](const SensorMonitor& sensor){
            report.updateSensorReading(sensor);
        });
        report.endUpdate();
        return report;
    }

    template <size_t N> void addSensors(const std::array<Sensor, N>& sensors) {
        std::for_each(sensors.begin(), sensors.end(), [&](const Sensor& s) {addSensor(s);});
    }

    /**
     * @brief Adds a sensor to this monitor.
     * 
     * @param sensor The sensor to add.
     */
    void addSensor(const Sensor& sensor) {
        sensors.push_back(sensor);
    }
};

struct ReportEventsConfig {
    virtual const char* monitorNotefile() { return "data.qo"; }
    virtual const char* alertNotefile() { return "alert.qo"; }
    virtual const char* notifytNotefile() { return "notify.qo"; }
    virtual void updateNoteTemplate(J* noteTemplate) {};
    virtual void updateNote(J* note) {};
    virtual bool syncMonitoringNotes() { return false; }
};

class ReportEvents {

    Notecard& notecard;
    Monitor& monitor;
    ReportEventsConfig& config;
    bool templateRegistered = false;

    static constexpr const char* DATA_FIELD_ALERT_SEQUENCE = "alert_seq";
    static constexpr const char* DATA_FIELD_ALERT_LEVEL = "alert";

    bool registerTemplate() {
        if (templateRegistered)
            return true;

         // Add the notefile template
        J *body = JCreateObject();
        JAddStringToObject(body, DATA_FIELD_ALERT_SEQUENCE, TSTRINGV);
        JAddStringToObject(body, DATA_FIELD_ALERT_LEVEL, TSTRINGV);

        monitor.forEachSensor([=](const SensorMonitor& as) {
            JAddNumberToObject(body, as.sensor.getName(), TFLOAT32);
        });

        config.updateNoteTemplate(body);

        J* req = notecard.newCommand("note.template");
        JAddStringToObject(req, "file", config.monitorNotefile());
        JAddItemToObject(req, "body", body);
        templateRegistered = notecard.sendRequest(req);
        if (!templateRegistered) {
            notecard.logDebug("Unable to register note template.\n");
        }
        return templateRegistered;
    }

    bool buildAndSendEvent(const Report& report) {
        bool success = sendMonitorEvent(report);
        if (AlertSequence::isAlerting(report.alertSequence)) {
            success = sendAlert(report);
        }
        return success;
    }

    J* buildMonitorNote(const Report& report, J* req, bool addSensorValues=true) {
        J* body = NoteNewBody();
        if (report.alertSequence) {
            JAddStringToObject(body, DATA_FIELD_ALERT_SEQUENCE, AlertSequence::toString(report.alertSequence));
        }
        if (report.alertLevel) {
            JAddStringToObject(body, DATA_FIELD_ALERT_LEVEL, AlertLevel::toString(report.alertLevel));
        }
        if (addSensorValues) {
            monitor.forEachSensor([&](const SensorMonitor& m){
                if (m.sensor.hasValue()) {
                    JAddNumberToObject(body, m.sensor.getName(), m.sensor.getValue());
                }
            });
        }
        config.updateNote(body);

        if (AlertSequence::isImmediate(report.alertSequence)) {
            JAddBoolToObject(req, "sync", true);
        }
        NoteAddBodyToObject(req, body);
        return body;
    }

    bool sendMonitorEvent(const Report& report) {
        J *req = notecard.newRequest("note.add");
        JAddStringToObject(req, "file", config.monitorNotefile());
        if (AlertSequence::isImmediate(report.alertSequence) || config.syncMonitoringNotes()) {
            JAddBoolToObject(req, "sync", true);
        }
        buildMonitorNote(report, req);
        return notecard.sendRequest(req);
    }

    void buildAlerts(const Report& report, J* body) {
        report.forEachSensor([&](const SensorReport& sr) {
            J* r = JAddObjectToObject(body, sr.monitor.sensor.getName());
            if (sr.interval.alertLevel) {
                JAddStringToObject(r, DATA_FIELD_ALERT_LEVEL, AlertLevel::toString(sr.interval.alertLevel));
            }
            JAddStringToObject(r, "status", IntervalReport::toStatusString(sr.interval.threshold));
            JAddNumberToObject(r, "value", sr.monitor.sensor.getValue());
        });
    }

    bool sendAlert(const Report& report) {
        J *req = notecard.newRequest("note.add");
        JAddStringToObject(req, "file", config.alertNotefile());
        if (AlertSequence::isImmediate(report.alertSequence)) {
            JAddBoolToObject(req, "sync", true);
        }
        J* body = buildMonitorNote(report, req, false);
        buildAlerts(report, body);
        return notecard.sendRequest(req);
    }

public:

    ReportEvents(Notecard& card, Monitor& monitor, ReportEventsConfig& config)
    : notecard(notecard), monitor(monitor), config(config) {}

    bool begin() {
        return registerTemplate();
    }

    bool sendReport(const Report& report) {
        return registerTemplate() && buildAndSendEvent(report);
    }

    bool notifyUpdate(EnvironmentUpdate& update) {
        if (update.hasContent()) {
            J* req = notecard.newRequest("note.add");
            JAddStringToObject(req, "file", config.notifytNotefile());
            J* body = NoteNewBody();
            bool added = update.addNote(body);
            NoteAddBodyToObject(req, body);
            if (added) {
                return notecard.sendRequest(req);
            }
            else {
                JDelete(req);
            }
        }
        return true;
    }
};
