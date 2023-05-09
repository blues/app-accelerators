// Copyright 2023 Blues Inc.  All rights reserved.
// Use of this source code is governed by licenses granted by the
// copyright holder including that found in the LICENSE file.

#pragma once

/**
 * @file notecard-env.h
 * @author Matthew McGowan (mat@blues.com)
 * @brief Utility classes based around the Notecard/note-c API to make building applications simpler.
 * @version 0.1
 * @date 2023-03-30
 *
 * @copyright Copyright (c) 2023 Blues Inc.  All rights reserved.
 */

#include <Notecard.h>


/**
 * @brief Provides a function to poll the environment variables from Notecard that
 * invokes a callback when the environment changes.
 *
 * @param A The type of argument passed to the callback
 */
template <typename A=void*>
struct EnvironmentPoll {

    /**
     * @brief Callback for handling the retrieved environment variables.
     */
    using callback_t = void (*)(A arg, J* j);

    /**
     * @brief Construct a new Environment Poll object that monitors the Notecard for environment variable changes.
     * 
     * @param notecard  The notecard to poll.
     * @param cb        The callback that is invoked with the environment when it changes
     * @param arg       Optional argument to pass when invoking the callback
     */
    EnvironmentPoll(Notecard& notecard, callback_t cb, A arg=A())
        : notecard(notecard), cb(cb), arg(arg) {}

    /**
     * @brief Fetches the environment and posts changes. This also has the side effect of
     * update the last changed timestamp.
     */
    void begin() {
        fetchEnvironment();
    }

    void poll() {
        if (hasEnvironmentChanged()) {
            fetchEnvironment();
        }
    }

    bool hasEnvironmentChanged() {
        return hasEnvironmentChanged(notecard, environmentModifiedTime);
    }

    bool fetchEnvironment() {
        return fetchEnvironment(notecard, environmentModifiedTime, cb, arg);
    }

    bool fetchEnvironment(A arg) {
        return fetchEnvironment(notecard, environmentModifiedTime, cb, arg);
    }

    static bool hasEnvironmentChanged(Notecard& notecard, int64_t environmentModifiedTime) {
        bool changed = false;
        J *rsp = notecard.requestAndResponse(notecard.newRequest("env.modified"));
        if (rsp != NULL) {
            int64_t modified = (int64_t) JGetNumber(rsp, "time");
            changed = (!notecard.responseError(rsp) && environmentModifiedTime != modified);
            notecard.deleteResponse(rsp);
        }
        return changed;
    }

    static bool fetchEnvironment(Notecard& notecard, int64_t& envModifiedTime, callback_t environmentUpdated, A arg) {
        // Read all env vars from the notecard in one transaction
        J *rsp = notecard.requestAndResponse(notecard.newRequest("env.get"));
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
            environmentUpdated(arg, body);
        }

        // Done
        notecard.deleteResponse(rsp);
        return true;
    }

private:
    Notecard& notecard;
    int64_t environmentModifiedTime = 0;
    callback_t cb;
    A arg;
};

template <typename A=void*> inline auto create_environment_poller(Notecard& notecard, typename EnvironmentPoll<A>::callback_t cb, A arg=A()) {
    return EnvironmentPoll<A>(notecard, cb, arg);
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

