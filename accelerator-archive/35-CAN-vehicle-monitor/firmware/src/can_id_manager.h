#pragma once

#include <Notecard.h>

#ifndef CAN_ID_MANAGER_MAX_IDS
#define CAN_ID_MANAGER_MAX_IDS 20
#endif

class CanIdManager
{

private:
    uint32_t ids[CAN_ID_MANAGER_MAX_IDS];
    size_t numIds;
    Notecard &notecard;
    uint32_t lastEnvVarChangeCheckMs;
    uint32_t lastEnvVarChangeMs;
    uint32_t checkIntervalMs;

    bool envHasChanged();
    bool fetchIds();
    bool updateIds(char *ids);

public:
    CanIdManager(Notecard& notecard, uint32_t checkIntervalMs);

    uint32_t *getIds();
    size_t getNumIds();
    void updateIdsFromEnvironment();
};
