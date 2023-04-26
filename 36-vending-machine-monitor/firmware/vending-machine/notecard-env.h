// Copyright 2023 Blues Inc.  All rights reserved.
// Use of this source code is governed by licenses granted by the
// copyright holder including that found in the LICENSE file.

#pragma once

#include <Notecard.h>
#include "notecard-aux-serial.h"

/**
 * @brief Provides a function to poll the environment variables from Notecard that
 * invokes a callback when the environment changes.
 */
struct EnvironmentUpdater {

    using timestamp_t = int64_t;
    using callback_t = std::function<void(timestamp_t, J*)>;

    /**
     * @brief Construct a new Environment Poll object that monitors the Notecard for environment variable changes.
     * 
     * @param notecard  The notecard to poll.
     * @param cb        The callback that is invoked with the environment when it changes
     * @param arg       Optional argument to pass when invoking the callback
     */
    EnvironmentUpdater(Notecard& notecard, callback_t cb)
        : notecard(notecard), cb(cb) {}

    /**
     * @brief Fetches the environment and posts changes. This also has the side effect of
     * updating the last changed timestamp.
     */
    void begin() {
        fetchEnvironment();
    }

    void poll() {
        if (hasEnvironmentChanged()) {
            fetchEnvironment();
        }
    }

    /**
     * @brief External notification that the environment has been updated.
     * 
     * @param modified      The modification time.
     * @param environment   The environment variables that were modified
     */
    void notifyEnvironmentUpdated(timestamp_t modified, J* environment) {
        environmentModifiedTime = modified;
        cb(modified, environment);
    }

    bool hasEnvironmentChanged() {
        return hasEnvironmentChanged(notecard, environmentModifiedTime);
    }

    bool fetchEnvironment() {
        return fetchEnvironment(notecard, environmentModifiedTime, cb);
    }

    static bool hasEnvironmentChanged(Notecard& notecard, int64_t environmentModifiedTime) {
        bool changed = false;
        J *rsp = notecard.requestAndResponse(notecard.newRequest("env.modified"));
        if (rsp != NULL) {
            timestamp_t modified = (timestamp_t) JGetNumber(rsp, "time");
            changed = (!notecard.responseError(rsp) && environmentModifiedTime < modified);
            notecard.deleteResponse(rsp);
        }
        return changed;
    }

    static bool fetchEnvironment(Notecard& notecard, timestamp_t& envModifiedTime, callback_t environmentUpdatedCB) {
        // Read all env vars from the notecard in one transaction
        J* req = NoteNewRequest("env.get");
        // for this to work correctly the
        // if (envModifiedTime) {
        //     JAddNumberToObject(req, "time", envModifiedTime);
        // }
        J *rsp = notecard.requestAndResponse(req);
        if (rsp == NULL) {
            return false;
        }
        if (notecard.responseError(rsp)) {
            notecard.deleteResponse(rsp);
            return false;
        }

        // Update the env modified time
        envModifiedTime = JGetNumber(rsp, "time");

        // Update the environment
        J *body = JGetObject(rsp, "body");
        if (body != NULL) {
            environmentUpdatedCB(envModifiedTime, body);
        }

        // Done
        notecard.deleteResponse(rsp);
        return true;
    }

private:
    Notecard& notecard;
    timestamp_t environmentModifiedTime = 0;
    callback_t cb;
};

template <typename A=void*> inline auto notecardEnvironment(Notecard& notecard, typename EnvironmentUpdater::callback_t cb) {
    return EnvironmentUpdater(notecard, cb);
}

#define NOTE_C_CONST_J(x)   ((J*)x)     // workaround for non-const functions in note-c
/*
 * @brief Contains the details to communicate an environment update to the app.
 * Provides the environment, and captures notifications of changes and errors.
 */
class EnvironmentUpdate {
    const J* environment;
    J* changed;
    J* errors;

    bool hasChildren(J* item) const {
        return item->child != NULL;
    }

public:
    EnvironmentUpdate(const J* env) : environment(env), changed(JCreateObject()), errors(JCreateObject()) {}

    ~EnvironmentUpdate() {
        JDelete(changed);
        JDelete(errors);
    }

    /**
     * @brief Fetches the value of an environment variable.
     * 
     * @tparam T 
     * @param name 
     * @return T 
     */
    template<typename T> T get(const char* name);

    const char* get(const char* name) {
        return JGetString(NOTE_C_CONST_J(environment), name);
    }

    bool get(const char* name, double& currentValue, double defaultValue) {
        return _getNumber(name, currentValue, defaultValue, strtod, [](double a, double b) { return equals_double(a,b,0.01);});
    }

    bool get(const char* name, float& currentValue, float defaultValue) {
        return _getNumber(name, currentValue, defaultValue, strtof, [](double a, double b) { return equals_float(a,b,0.01);});
    }

    bool get(const char* name, long& currentValue, long defaultValue) {
        return _getNumber(name, currentValue, defaultValue, [](const char* s, char** e) {return strtol(s,e,10);}, equals_long);
    }



    /**
     * @brief 
     * 
     * @param name  The name of the environment variable to fetch as a double
     * @param currentValue The current value.
     * @param defaultValue The value to use when the environment variable is not defined.
     * @return true When the environment variable is not defined, or when it is defined and successfully parsed.
     * @return false When the environment variable is not defined
     */
    template<typename T, typename parser=std::function<T(const char*, const char**)>, typename equality=std::function<bool(T,T)>>
    bool _getNumber(const char* name, T& currentValue, T defaultValue, parser parse, equality is_same) {
        bool success = false;
        const char* value = get(name);
        double r = currentValue;
        if (value && *value) {
            char* end;
            r = parse(value, &end);
            if (end-value==strlen(value)) { // all characters parsed
                if (!is_same(currentValue, r)) {
                    notifyChanged(name, currentValue, r);
                    currentValue = r;
                }
                success = true;
            }
            else {
                notifyError(name, "Value is not a number.", value);
            }
        }
        else {      // environment variable removed
            currentValue = defaultValue;
            success = true;
        }
        return success;
    }

    bool notifyChanged(const char* name, double oldValue, double newValue) {
        J* item = JCreateObject();
        if (!isnan(oldValue))
            JAddNumberToObject(item, "old_value", oldValue);
        if (!isnan(newValue))
            JAddNumberToObject(item, "new_value", newValue);
        JAddItemToObject(changed, name, item);
        return item;
    }

    bool notifyError(const char* name, const char* text, const char* value) {
        J* item = JCreateObject();
        JAddStringToObject(item, "error", text);
        JAddStringToObject(item, "value", value);
        JAddItemToObject(errors, name, item);
        return item;
    }

    const J* reportedChanges() const {
        return this->changed;
    }

    const J* reportedErrors() const {
        return this->errors;
    }

    bool hasChanges() const {
        return hasChildren(changed);
    }

    bool hasErrors() const {
        return hasChildren(errors);
    }

    /**
     * @brief Adds the updates and errors to a json object.
     * 
     * @param body The parent JSON node to receive the changes and errors.
     */
    bool addNote(J* body) {
        bool added = false;
        if (hasChanges()) {
            JAddItemToObject(body, "updates", changed);
            changed = nullptr;
            added = true;
        }
        if (hasErrors()) {
            JAddItemToObject(body, "errors", errors);
            errors = nullptr;
            added = true;
        }
        return added;
    }

    bool hasContent() const {
        return hasChanges() || hasErrors();
    }

    template <typename S> void toStream(S& s) {
        s.println("environment updates:");
        toStream(s, changed);
        s.println("environment errors:");
        toStream(s, errors);
        s.println();
    }

    template <typename S> void toStream(S& s, J* item) {
        char buf[256];
        if (hasChildren(item)) {
            JPrintPreallocated(item, buf, sizeof(buf), false);
            s.println(buf);
        }
        else {
            s.println("none");
        }
    }

    static bool equals_double(double a, double b, double epsilon=0.01) {
        return (fabs(a - b) < epsilon);
    }

    static bool equals_float(float a, float b, float epsilon=0.01) {
        return (fabs(a - b) < epsilon);
    }

    static bool equals_long(long a, long b) {
        return a==b;
    }
};


/**
 * @brief Implemented by classes that respond to changes in the Notecard environment.
 */
class Environmental {
    /**
     * @brief Updates the alert intervals from the environment
     * 
     * @param environment JSON object of keys to values for environment variables.
     * @param updated     Adds keys for the environment variables that changed and their values.
     * @param errors      keys and string messages for environment errors.
     * @param dryRun      when true, no updates are performed but the environment is checked and `updates` and `errors` populated with
     * the environment variables that have actually changed, and any errors that would result.
     * @return The number of variables that changed. -1 if there is an error.
     */
    int updateFromEnvironment(EnvironmentUpdate& update, bool dryRun=false);
};

class NotecardEnvironmentNotifications : public AuxSerialHandler {
    EnvironmentUpdater& updater;

public:
    NotecardEnvironmentNotifications(AuxSerialRegistry& registry, EnvironmentUpdater& updater)
    : AuxSerialHandler(registry, "env"), updater(updater) {}

    virtual void handle(J* notification) override {
        // Update the env modified time
        EnvironmentUpdater::timestamp_t environmentModifiedTime = JGetNumber(notification, "modified");

        // Update the environment
        J *body = JGetObject(notification, "body");
        if (body != NULL) {
            updater.notifyEnvironmentUpdated(environmentModifiedTime, body);
        }
    }
};
