#include "can_id_manager.h"

// Check for environment variable changes. Returns true if there are changes
// and false otherwise.
bool CanIdManager::envHasChanged()
{
    uint32_t currentMs = millis();

    // Don't check for changes unless its time to do so.
    if (currentMs - this->lastEnvVarChangeCheckMs < this->checkIntervalMs) {
        return false;
    }

    this->lastEnvVarChangeCheckMs = currentMs;

    J *rsp = notecard.requestAndResponse(
                notecard.newRequest("env.modified"));
    if (rsp == NULL) {
        notecard.logDebug("NULL response to env.modified.\n\r");
        return false;
    }

    uint32_t modifiedTime = JGetInt(rsp, "time");
    notecard.deleteResponse(rsp);
    // If the last modified timestamp is the same as the one we've got saved,
    // there have been no changes.
    if (this->lastEnvVarChangeMs == modifiedTime) {
        return false;
    }

    this->lastEnvVarChangeMs = modifiedTime;

    notecard.logDebug("Environment variable changed detected.\n\r");

    return true;
}

// Returns true if ID fetch was successful.
bool CanIdManager::fetchIds()
{
    bool success = false;
    J *req = notecard.newRequest("env.get");
    J *names = JAddArrayToObject(req, "names");
    JAddItemToArray(names, JCreateString("ids"));

    J *rsp = notecard.requestAndResponse(req);
    if (rsp != NULL) {
        if (notecard.responseError(rsp)) {
            notecard.logDebug("Error in env.get response.\n\r");
        }
        else {
            J *body = JGetObject(rsp, "body");
            if (body != NULL) {
                char *ids = JGetString(body, "ids");
                if (ids != NULL) {
                    if (!updateIds(ids)) {
                        notecard.logDebug("Failed to update IDs.\r\n");
                        success = false;
                    }
                }
                else {
                    notecard.logDebug("No IDs in env.get response.\n\r");
                }
            }
        }

        notecard.deleteResponse(rsp);
    }
    else {
        notecard.logDebug("NULL response to env.get request.\n\r");
    }

    return success;
}

bool CanIdManager::updateIds(char *ids)
{
    uint32_t newIds[CAN_ID_MANAGER_MAX_IDS];
    char* p = ids;
    long int val;
    size_t i = 0;
    bool success = true;

    do {
        val = strtoul(p, &p, 16);
        if (val == 0) {
            notecard.logDebug("Failed to convert ID to integer."
                " Aborting ID update.\r\n");
            success = false;
            break;
        }

        newIds[i] = val;
        ++i;

        if (*p == ';') {
            ++p;
        }
    } while (i < CAN_ID_MANAGER_MAX_IDS && *p != '\0');

    if (success) {
        this->numIds = i;

        // We only change this->ids to reflect the new set of IDs once we're
        // sure the entire list was valid (i.e. there was no parsing failure).
        notecard.logDebug("New ID list: ");
        for (i = 0; i < this->numIds; ++i) {
            this->ids[i] = newIds[i];

            notecard.logDebugf("0x%x", newIds[i]);
            if (i != this->numIds - 1) {
                notecard.logDebug(", ");
            }
            else {
                notecard.logDebug("\r\n");
            }
        }
    }

    return success;
}

CanIdManager::CanIdManager(Notecard& notecard, uint32_t checkIntervalMs) :
    ids{},
    numIds{0},
    notecard{notecard},
    lastEnvVarChangeCheckMs{0},
    lastEnvVarChangeMs{0},
    checkIntervalMs{checkIntervalMs}
{
}

uint32_t *CanIdManager::getIds()
{
    return this->ids;
}

size_t CanIdManager::getNumIds()
{
    return this->numIds;
}

void CanIdManager::updateIdsFromEnvironment()
{
    if (envHasChanged()) {
        if (fetchIds()) {
            J *req = notecard.newRequest("note.add");
            if (req != NULL) {
                JAddStringToObject(req, "file", "notify.qo");
                JAddBoolToObject(req, "sync", true);

                J *body = JCreateObject();
                if (body != NULL) {
                    JAddStringToObject(body, "message", "ID update "
                        "received");
                    JAddItemToObject(req, "body", body);
                    if (!notecard.sendRequest(req)) {
                        notecard.logDebug("Failed to send ID change update "
                            "ack.\n\r");
                    }
                }
                else {
                    JDelete(req);
                    notecard.logDebug("Failed to create note body for ID "
                        "update ack.\n\r");
                }
            }
            else {
                notecard.logDebug("Failed to create note.add request for ID"
                    " update ack.\n\r");
            }
        }
    }
}
