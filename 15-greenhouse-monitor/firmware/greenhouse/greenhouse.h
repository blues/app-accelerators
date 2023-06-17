// Copyright 2023 Blues Inc.  All rights reserved.
// Use of this source code is governed by licenses granted by the
// copyright holder including that found in the LICENSE file.

#pragma once

#include <cmath>    // for NAN, isnan

// User-defined types are best defined in header files for compatibility with the Arduino IDE
// Otherwise the .ino preprocessor generates function prototypes in the wrong place,
// causing compiler errors.
// See https://www.visualmicro.com/forums/YaBB.pl?num=1562000582

/**
 * @brief Classification of the alert status of a reading.
 */
enum AlertLevel {
    ALERT_LEVEL_NORMAL,
    ALERT_LEVEL_WARNING,
    ALERT_LEVEL_CRITICAL
};

const char* alertLevelString(AlertLevel e) {
    switch (e) {
        case ALERT_LEVEL_NORMAL: return "normal";
        case ALERT_LEVEL_WARNING: return "warning";
        case ALERT_LEVEL_CRITICAL: return "critical";
        default: return "unknown";
    }
}

inline bool isAlertLevel(AlertLevel a) {
    return a!=ALERT_LEVEL_NORMAL;
}

inline enum AlertLevel highestAlertLevel(enum AlertLevel a1, enum AlertLevel a2) {
    return a1 > a2 ? a1 : a2;
}

enum AlertSequence {
    ALERT_SEQ_NONE = 0,           /* regular report */
    ALERT_SEQ_FIRST = 1,          /* set for the first alert */
    ALERT_SEQ_SUBSEQUENT = 2,     /* set for subsequent alerts */
    ALERT_SEQ_LAST = 3            /* set for the first regular reading after a series of alerts */
};

const char* alertSequenceString(enum AlertSequence e) {
    switch (e) {
        case ALERT_SEQ_NONE: return "none";
        case ALERT_SEQ_FIRST: return "first";
        case ALERT_SEQ_SUBSEQUENT: return "ongoing";
        case ALERT_SEQ_LAST: return "cleared";
        default: return "unknown";
    }
};

inline bool isAlertSequence(enum AlertSequence a) {
    return a!=ALERT_SEQ_NONE;
}

inline bool isAlertSequenceImmediate(enum AlertSequence a) {
    return a==ALERT_SEQ_FIRST || a==ALERT_SEQ_LAST;
}

inline bool isAlertSequenceOngoing(enum AlertSequence a) {
    return a==ALERT_SEQ_FIRST || a==ALERT_SEQ_SUBSEQUENT;
}

enum Threshold {
    THRESHOLD_NONE,
    THRESHOLD_LOWER,
    THRESHOLD_UPPER
};

const char* thresholdString(Threshold threshold) {
    switch(threshold) {
        case THRESHOLD_NONE: return "none";
        case THRESHOLD_LOWER: return "lower";
        case THRESHOLD_UPPER: return "upper";
        default: return "unknown";
    }
}

const char* thresholdStatusString(Threshold threshold) {
    switch(threshold) {
        case THRESHOLD_NONE: return "ok";
        case THRESHOLD_LOWER: return "low";
        case THRESHOLD_UPPER: return "high";
        default: return "unknown";
    }
}

#define SENSOR_VALUE_UNDEFINED (NAN)

bool inline isValueSet(float v) {
    return !isnan(v);
}

struct Interval {
    float vmin;
    float vmax;

    Interval() : vmin(SENSOR_VALUE_UNDEFINED), vmax(SENSOR_VALUE_UNDEFINED) {}
};

struct AlertIntervals {
    Interval normal;
    Interval warning;
};

Threshold checkInterval(double vmin, double vmax, double v) {
    if (isValueSet(vmin) && vmin > v) {
        return THRESHOLD_LOWER;
    }
    if (isValueSet(vmax) && vmax < v) {
        return THRESHOLD_UPPER;
    }
    return THRESHOLD_NONE;
}

struct ThresholdAlert {
    Threshold threshold;
    AlertLevel alertLevel;
};

const constexpr ThresholdAlert NORMAL = { .threshold = THRESHOLD_NONE, .alertLevel = ALERT_LEVEL_NORMAL };
