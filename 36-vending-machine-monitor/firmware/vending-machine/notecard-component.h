#pragma once

#include <Notecard.h>

class NotecardComponent {

protected:
    Notecard& notecard;
    const char* const name;

public:

    NotecardComponent(Notecard& notecard, const char* name) : notecard(notecard), name(name) {}

    void initialize() {

    }

    void update() {

    }

    void notification() {

    }
};