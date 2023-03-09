// Standard C headers.
#include <stdlib.h>
#include <string.h>

// Zephyr headers.
#include <zephyr/kernel.h>

// Notecard headers.
#include "note.h"

// Application headers.
#include "env_updater.h"
#include "publisher.h"

struct EnvUpdaterCtx {
    PublisherCtx *publisherCtx;
    AlarmPublisherCtx *alarmPublisherCtx;
    struct k_work envUpdateWorkItem;
    struct k_timer envUpdateTimer;
    uint32_t envLastModTime;
    uint32_t interval;
};

// Forward declarations.
static void envUpdateTimerCb(struct k_timer *timer);
static void envUpdateWorkCb(struct k_work *item);

static bool envUpdate(EnvUpdaterCtx* ctx)
{
    if (ctx == NULL) {
        printk("envUpdate: error: Called with NULL ctx.\n");
        return false;
    }
    if (ctx->publisherCtx == NULL) {
        printk("envUpdate: error: Called with NULL ctx->publisherCtx.\n");
        return false;
    }

    J *rsp = NoteRequestResponse(NoteNewRequest("env.modified"));
    if (rsp == NULL) {
        printk("envUpdate: error: NULL response to env.modified.\n");
        return false;
    }

    uint32_t modifiedTime = JGetInt(rsp, "time");
    NoteDeleteResponse(rsp);
    // If the last modified timestamp is the same as the one we've got saved,
    // there have been no changes.
    if (ctx->envLastModTime == modifiedTime) {
        return true;
    }

    ctx->envLastModTime = modifiedTime;

    printk("envUpdate: Environment variable changed detected.\n");

    bool updated = false;
    J *req = NoteNewRequest("env.get");
    if (req == NULL) {
        printk("envUpdate: error: Failed to allocate env.get Note.\n");
        return false;
    }

    J *names = JAddArrayToObject(req, "names");
    JAddItemToArray(names, JCreateString("monitor_interval"));
    JAddItemToArray(names, JCreateString("temperature_threshold_min"));
    JAddItemToArray(names, JCreateString("temperature_threshold_max"));
    JAddItemToArray(names, JCreateString("humidity_threshold_min"));
    JAddItemToArray(names, JCreateString("humidity_threshold_max"));

    rsp = NoteRequestResponse(req);
    if (rsp != NULL) {
        if (NoteResponseError(rsp)) {
            NoteDeleteResponse(rsp);
            printk("envUpdate: error: Error in env.get response.\n");
            return false;
        }
        else {
            J *body = JGetObject(rsp, "body");
            if (body != NULL) {
                char *valueStr = JGetString(body, "monitor_interval");
                float value;
                char *endPtr;

                if (valueStr != NULL && strlen(valueStr) > 0) {
                    value = strtof(valueStr, &endPtr);
                    if ((value == 0 && valueStr == endPtr) || value < 0) {
                        printk("envUpdate: error: Failed to convert %s to "
                            "positive float for monitor interval.\n", valueStr);
                    }
                    else {
                        uint32_t interval = (uint32_t)(value);
                        updated = true;
                        printk("envUpdate: Monitor interval set to %u seconds."
                            "\n", interval);

                        // If the monitor interval changed, we need to restart
                        // the publisher using the new interval value.
                        publisherStop(ctx->publisherCtx);
                        publisherStart(ctx->publisherCtx, interval);
                    }
                }

                valueStr = JGetString(body, "temperature_threshold_min");
                if (valueStr != NULL && strlen(valueStr) > 0) {
                    value = strtof(valueStr, &endPtr);
                    if ((value == 0 && valueStr == endPtr) || value < 0) {
                        printk("envUpdate: error: Failed to convert %s to "
                            "positive float for temperature min.\n", valueStr);
                    }
                    else {
                        alarmPublisherUpdateTempMin(ctx->alarmPublisherCtx,
                            value);
                        updated = true;
                        printk("envUpdate: Temperature min set to %sC.\n",
                            valueStr);
                    }
                }

                valueStr = JGetString(body, "temperature_threshold_max");
                if (valueStr != NULL && strlen(valueStr) > 0) {
                    value = strtof(valueStr, &endPtr);
                    if ((value == 0 && valueStr == endPtr) || value < 0) {
                        printk("envUpdate: error: Failed to convert %s to "
                            "positive float for temperature max.\n", valueStr);
                    }
                    else {
                        alarmPublisherUpdateTempMax(ctx->alarmPublisherCtx,
                            value);
                        updated = true;
                        printk("envUpdate: Temperature max set to %sC.\n",
                            valueStr);
                    }
                }

                valueStr = JGetString(body, "humidity_threshold_min");
                if (valueStr != NULL && strlen(valueStr) > 0) {
                    value = strtof(valueStr, &endPtr);
                    if ((value == 0 && valueStr == endPtr) || value < 0) {
                        printk("envUpdate: error: Failed to convert %s to "
                            "positive float for humidity min.\n", valueStr);
                    }
                    else {
                        alarmPublisherUpdateHumidMin(ctx->alarmPublisherCtx,
                            value);
                        updated = true;
                        printk("envUpdate: Humidity min set to %s%%.\n",
                            valueStr);
                    }
                }

                valueStr = JGetString(body, "humidity_threshold_max");
                if (valueStr != NULL && strlen(valueStr) > 0) {
                    value = strtof(valueStr, &endPtr);
                    if ((value == 0 && valueStr == endPtr) || value < 0) {
                        printk("envUpdate: error: Failed to convert %s to "
                            "positive float for humidity max.\n", valueStr);
                    }
                    else {
                        alarmPublisherUpdateHumidMax(ctx->alarmPublisherCtx,
                            value);
                        updated = true;
                        printk("envUpdate: Humidity max set to %s%%.\n",
                            valueStr);
                    }
                }
            }
            else {
                printk("envUpdate: error: NULL body in response to env.get "
                    "request.\n");
                NoteDeleteResponse(rsp);
                return false;
            }
        }

        NoteDeleteResponse(rsp);
    }
    else {
        printk("envUpdate: error: NULL response to env.get request.\n");
        return false;
    }

    if (updated) {
        // Acknowledge the update.
        req = NoteNewRequest("note.add");
        if (req != NULL) {
            JAddStringToObject(req, "file", "notify.qo");
            JAddBoolToObject(req, "sync", true);

            J *body = JCreateObject();
            if (body != NULL) {
                JAddStringToObject(body, "message", "environment variable "
                    "update received");
                JAddItemToObject(req, "body", body);
                if (!NoteRequest(req)) {
                    printk("envUpdate: error: Failed to send update "
                        "acknowledgment.\n");
                    return false;
                }
            }
            else {
                JDelete(req);
                printk("envUpdate: error: Failed to create note body for update"
                    " acknowledgment.\n");
                return false;
            }
        }
        else {
            printk("envUpdate: error: Failed to create note.add request for "
                "update acknowledgment.\n");
            return false;
        }
    }

    return true;
}

/**
 * Allocate and initialize a new environment variable updater context.
 *
 * @param publisherCtx      A publisher context object.
 * @param alarmPublisherCtx An alarm publisher context object.
 *
 * @return A valid EnvUpdaterCtx pointer on success and NULL on failure.
 */
EnvUpdaterCtx *envUpdaterInit(PublisherCtx* publisherCtx,
    AlarmPublisherCtx* alarmPublisherCtx)
{
    if (publisherCtx == NULL) {
        printk("envUpdaterInit: error: Called with NULL publisherCtx.\n");
        return NULL;
    }

    EnvUpdaterCtx *ctx = (EnvUpdaterCtx *)malloc(sizeof(EnvUpdaterCtx));
    if (ctx == NULL) {
        printk("envUpdaterInit: error: failed to allocate EnvUpdaterCtx.\n");
        return NULL;
    }
    memset(ctx, 0, sizeof(*ctx));

    ctx->publisherCtx = publisherCtx;
    ctx->alarmPublisherCtx = alarmPublisherCtx;

    k_work_init(&ctx->envUpdateWorkItem, envUpdateWorkCb);
    k_timer_init(&ctx->envUpdateTimer, envUpdateTimerCb, NULL);

    printk("envUpdaterInit: Initialized environment variable updater.\n");

    return ctx;
}

/**
 * Start the environment variable update timer. Notehub will be checked
 * periodically for environment variable updates according to the interval.
 *
 * @param ctx      The environment variable updater context object.
 * @param interval The interval to check for updates, in seconds.
 *
 * @return True on success and false on failure.
 */
bool envUpdaterStart(EnvUpdaterCtx *ctx, uint32_t interval)
{
    if (ctx == NULL) {
        printk("envUpdaterStart: error: Called with NULL ctx.\n");
        return false;
    }
    if (interval == 0) {
        printk("envUpdaterStart: error: Called with 0 interval. Must be > 0."
            "\n");
        return false;
    }

    k_timer_start(&ctx->envUpdateTimer, K_SECONDS(0), K_SECONDS(interval));

    return true;
}

/**
 * Stop the environment variable update timer. Notehub won't be checked for
 * environment variable updates until envUpdaterStart is called again.
 *
 * @param ctx The environment variable updater context object.
 *
 * @return True on success and false on failure.
 */
bool envUpdaterStop(EnvUpdaterCtx *ctx)
{
    if (ctx == NULL) {
        printk("envUpdaterStop: error: Called with NULL ctx.\n");
        return false;
    }

    k_timer_stop(&ctx->envUpdateTimer);

    return true;
}

/**
 * Callback that will be executed when the environment variable update timer
 * expires.
 *
 * @param timer The timer handle.
 */
static void envUpdateTimerCb(struct k_timer *timer)
{
    EnvUpdaterCtx *ctx = CONTAINER_OF(timer, EnvUpdaterCtx, envUpdateTimer);
    k_work_submit(&ctx->envUpdateWorkItem);
}

/**
 * Callback that will be executed by the system workqueue when it's time to
 * check for environment variable updates.
 *
 * @param item The environment variable update work item.
 */
static void envUpdateWorkCb(struct k_work *item)
{
    EnvUpdaterCtx *ctx = CONTAINER_OF(item, EnvUpdaterCtx, envUpdateWorkItem);
    envUpdate(ctx);
}
