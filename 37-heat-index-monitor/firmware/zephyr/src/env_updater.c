// Standard C headers.
#include <stdlib.h>
#include <string.h>

// Zephyr headers.
#include <zephyr/kernel.h>

// Notecard headers.
#include "NotecardEnvVarManager.h"

// Application headers.
#include "env_updater.h"
#include "publisher.h"

struct EnvUpdaterCtx {
    PublisherCtx *publisherCtx;
    AlarmPublisherCtx *alarmPublisherCtx;
    NotecardEnvVarManager *envVarManager;
    struct k_work envUpdateWorkItem;
    struct k_timer envUpdateTimer;
    uint32_t envLastModTime;
    uint32_t interval;
};

// Forward declarations.
static void envUpdateTimerCb(struct k_timer *timer);
static void envUpdateWorkCb(struct k_work *item);

static const char *watchVars[] = {
    "monitor_interval",
    "temperature_min",
    "temperature_max",
    "humidity_min",
    "humidity_max",
    "heat_index_max"
};
static const size_t numWatchVars = sizeof(watchVars) / sizeof(watchVars[0]);

static void envVarManagerCb(const char *var, const char *val, void *userCtx)
{
    EnvUpdaterCtx* envUpdaterCtx = (EnvUpdaterCtx*)userCtx;
    float value;
    char *endPtr;

    value = strtof(val, &endPtr);
    if ((value == 0 && val == endPtr) || value < 0) {
        printk("envVarManagerCb: error: Failed to convert %s to positive float "
            "for %s.\n", val, var);
        return;
    }

    if (strcmp(var, "monitor_interval") == 0) {
        uint32_t interval = (uint32_t)(value);
        printk("envVarManagerCb: Monitor interval set to %u seconds.\n",
            interval);
        // If the monitor interval changed, we need to restart the publisher
        // using the new interval value.
        publisherStop(envUpdaterCtx->publisherCtx);
        publisherStart(envUpdaterCtx->publisherCtx, interval);
    }
    else if (strcmp(var, "temperature_min") == 0) {
        alarmPublisherUpdateTempMin(envUpdaterCtx->alarmPublisherCtx, value);
        printk("envVarManagerCb: Temperature min set to %sF.\n", val);
    }
    else if (strcmp(var, "temperature_max") == 0) {
        alarmPublisherUpdateTempMax(envUpdaterCtx->alarmPublisherCtx, value);
        printk("envVarManagerCb: Temperature max set to %sF.\n", val);
    }
    else if (strcmp(var, "humidity_min") == 0) {
        alarmPublisherUpdateHumidMin(envUpdaterCtx->alarmPublisherCtx, value);
        printk("envVarManagerCb: Humidity min set to %s%%.\n", val);
    }
    else if (strcmp(var, "humidity_max") == 0) {
        alarmPublisherUpdateHumidMax(envUpdaterCtx->alarmPublisherCtx, value);
        printk("envVarManagerCb: Humidity max set to %s%%.\n", val);
    }
    else if (strcmp(var, "heat_index_max") == 0) {
        alarmPublisherUpdateHeatIndexMax(envUpdaterCtx->alarmPublisherCtx,
            value);
        printk("envVarManagerCb: Heat index max set to %sF.\n", val);
    }
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

    ctx->envVarManager = NotecardEnvVarManager_alloc();
    if (ctx->envVarManager == NULL) {
        printk("envUpdaterInit: error: NotecardEnvVarManager_new failed.\n");
        free(ctx);
        return NULL;
    }
    if (NotecardEnvVarManager_setEnvVarCb(ctx->envVarManager, envVarManagerCb,
            ctx) != NEVM_SUCCESS) {
        printk("envUpdaterInit: error: NotecardEnvVarManager_setEnvVarCb "
            "failed.\n");
        NotecardEnvVarManager_free(ctx->envVarManager);
        free(ctx);
        return NULL;
    }
    if (NotecardEnvVarManager_setWatchVars(ctx->envVarManager, watchVars,
            numWatchVars) != NEVM_SUCCESS) {
        printk("envUpdaterInit: error: NotecardEnvVarManager_setWatchVars "
            "failed.\n");
        NotecardEnvVarManager_free(ctx->envVarManager);
        free(ctx);
        return NULL;
    }

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
    NotecardEnvVarManager_process(ctx->envVarManager);
}
