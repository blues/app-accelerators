// Standard C headers.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Zephyr headers.
#include <zephyr/kernel.h>

// Notecard headers.
#include "note.h"

// Application headers.
#include "alarm_publisher.h"

struct AlarmPublisherCtx {
    const Bme280Ctx *bme280Ctx;
    struct k_timer alarmCheckTimer;
    struct k_timer alarmCooldownTimer;
    struct k_work alarmCheckWorkItem;
    double tempMin;
    double tempMax;
    double humidMin;
    double humidMax;
    uint32_t checkInterval;
    bool started;
    volatile bool coolingDown;
};

// Forward declarations.
static void checkAlarm(AlarmPublisherCtx *ctx);
static bool publishAlarm(const char *tempStatus,
    const char *humidStatus, double temp, double humid);

static const char statusOk[] = "ok";
static const char statusLow[] = "low";
static const char statusHigh[] = "high";

// The user can define ALARM_COOLDOWN_SECONDS to override the cooldown timer
// interval.
#ifndef ALARM_COOLDOWN_SECONDS
#define ALARM_COOLDOWN_SECONDS (60 * 5) // 5 minutes
#endif

/**
 * Callback that will be executed when the alarm check timer expires.
 *
 * @param timer The timer handle.
 */
static void alarmCheckTimerCb(struct k_timer *timer)
{
    AlarmPublisherCtx *ctx = CONTAINER_OF(timer, AlarmPublisherCtx,
        alarmCheckTimer);
    k_work_submit(&ctx->alarmCheckWorkItem);
}

/**
 * Callback that will be executed by the system workqueue when it's time to
 * check for alarm conditions.
 *
 * @param item The alarm check work item.
 */
static void alarmCheckWorkCb(struct k_work *item)
{
    AlarmPublisherCtx *ctx = CONTAINER_OF(item, AlarmPublisherCtx,
        alarmCheckWorkItem);
    checkAlarm(ctx);
}

/**
 * Callback that will be executed when the alarm cooldown timer expires.
 *
 * @param timer The timer handle.
 */
static void alarmCooldownTimerCb(struct k_timer *timer)
{
    AlarmPublisherCtx *ctx = CONTAINER_OF(timer, AlarmPublisherCtx,
        alarmCooldownTimer);
    ctx->coolingDown = false;
}

/**
 * Allocate and initialize a new alarm publisher context.
 *
 * @param bme280Ctx     A BME280 context object.
 * @param checkInterval The interval to check for alarm conditions, in seconds.
 * @param tempMin       The minimum allowed temperature.
 * @param tempMax       The maximum allowed temperature.
 * @param humidMin      The minimum allowed humidity.
 * @param humidMax      The maximum allowed humidity.
 *
 * @return A valid AlarmPublisherCtx pointer on success and NULL on failure.
 */
AlarmPublisherCtx *alarmPublisherInit(const Bme280Ctx* bme280Ctx,
    uint32_t checkInterval, double tempMin, double tempMax, double humidMin,
    double humidMax)
{
    if (bme280Ctx == NULL) {
        printk("alarmPublisherInit: error: Called with NULL bme280Ctx.\n");
        return NULL;
    }
    if (checkInterval == 0) {
        printk("alarmPublisherInit: error: Called with 0 check interval. Must "
            "be > 0.\n");
        return NULL;
    }

    AlarmPublisherCtx *ctx = (AlarmPublisherCtx *)malloc(
        sizeof(AlarmPublisherCtx));
    if (ctx == NULL) {
        printk("alarmPublisherInit: error: failed to allocate PublisherCtx.\n");
        return NULL;
    }
    memset(ctx, 0, sizeof(*ctx));

    ctx->bme280Ctx = bme280Ctx;
    ctx->checkInterval = checkInterval;
    ctx->tempMin = tempMin;
    ctx->tempMax = tempMax;
    ctx->humidMin = humidMin;
    ctx->humidMax = humidMax;

    k_timer_init(&ctx->alarmCheckTimer, alarmCheckTimerCb, NULL);
    k_timer_init(&ctx->alarmCooldownTimer, alarmCooldownTimerCb, NULL);
    k_work_init(&ctx->alarmCheckWorkItem, alarmCheckWorkCb);

    printk("alarmPublisherInit: Initialized alarm publisher.\n");

    return ctx;
}

/**
 * Start the alarm check timer. Alarm conditions will be checked according to
 * the interval passed to alarmPublisherInit.
 *
 * @param ctx The publisher context object.
 *
 * @return True on success and false on failure.
 */
bool alarmPublisherStart(AlarmPublisherCtx *ctx)
{
    if (ctx == NULL) {
        printk("alarmPublisherStart: error: Called with NULL ctx.\n");
        return false;
    }

    k_timer_start(&ctx->alarmCheckTimer, K_SECONDS(0),
        K_SECONDS(ctx->checkInterval));
    ctx->started = true;

    return true;
}

/**
 * Stop the alarm check timer. Alarm conditions won't be checked until
 * alarmPublisherStart is called again.
 *
 * @param ctx The publisher context object.
 *
 * @return True on success and false on failure.
 */
bool alarmPublisherStop(AlarmPublisherCtx *ctx)
{
    if (ctx == NULL) {
        printk("alarmPublisherStop: error: Called with NULL ctx.\n");
        return false;
    }

    k_timer_stop(&ctx->alarmCheckTimer);
    ctx->started = false;

    return true;
}

/**
 * Update one of the bounds used for triggering alarms.
 *
 * @param ctx    An alarm publisher context object.
 * @param bound  The bound to update. See alarm_publisher.h's AlarmBound enum
 *               for possible values.
 * @param newVal The new value for the bound.
 *
 * @return True on success and false on failure.
 */
bool alarmPublisherUpdateBound(AlarmPublisherCtx* ctx, AlarmBound bound,
    double newVal)
{
    if (ctx == NULL) {
        printk("alarmPublisherUpdateTempMax: error: Called with NULL ctx.\n");
        return false;
    }

    double *field;
    switch (bound) {
        case TEMP_MIN:
            field = &ctx->tempMin;
            break;
        case TEMP_MAX:
            field = &ctx->tempMax;
            break;
        case HUMID_MIN:
            field = &ctx->humidMin;
            break;
        case HUMID_MAX:
            field = &ctx->humidMax;
            break;
        default:
            printk("alarmPublisherUpdateBound: error: Unrecognized bound.\n");
            return false;
    }

    k_timer_stop(&ctx->alarmCheckTimer);
    k_timer_stop(&ctx->alarmCooldownTimer);
    ctx->coolingDown = false;

    *field = newVal;

    if (ctx->started) {
        k_timer_start(&ctx->alarmCheckTimer, K_SECONDS(0),
            K_SECONDS(ctx->checkInterval));
    }

    return true;
}

/**
 * Check to see if temperature or humidity are out of bounds and, if so, publish
 * an alarm.
 *
 * @param ctx An alarm publisher context object.
 */
static void checkAlarm(AlarmPublisherCtx *ctx)
{
    // If we're in the alarm cooldown period, don't check alarm conditions.
    if (ctx->coolingDown) {
        return;
    }

    double temp, humid;
    if (!bme280GetTempAndHumidity(ctx->bme280Ctx, &temp, &humid)) {
        printk("checkAlarm: error: bme280GetTempAndHumidity failed.\n");
        return;
    }

    const char *tempStatus = statusOk;
    const char *humidStatus = statusOk;

    // We use printf for these alarm logs because printk doesn't support
    // printing doubles.
    if (temp < ctx->tempMin) {
        tempStatus = statusLow;
        printf("checkAlarm: temp is low @ %.2f, min is %.2f.\n", temp,
            ctx->tempMin);
    }
    else if (temp > ctx->tempMax) {
        tempStatus = statusHigh;
        printf("checkAlarm: temp is high @ %.2f, max is %.2f.\n", temp,
            ctx->tempMax);
    }

    if (humid < ctx->humidMin) {
        humidStatus = statusLow;
        printf("checkAlarm: humid is low @ %.2f, min is %.2f.\n", humid,
            ctx->humidMin);
    }
    else if (humid > ctx->humidMax) {
        humidStatus = statusHigh;
        printf("checkAlarm: humid is high @ %.2f, max is %.2f.\n", humid,
            ctx->humidMax);
    }

    // If either status is not "ok".
    if (strcmp(tempStatus, statusOk) || strcmp(humidStatus, statusOk)) {
        if (!publishAlarm(tempStatus, humidStatus, temp, humid)) {
            printk("checkAlarm: error: publishAlarm failed.\n");
            return;
        }

        // To avoid spamming Notehub about the same alarm condition over and
        // over, we use a cooldown timer. Once we publish an alarm, this timer
        // counts down from ALARM_COOLDOWN_SECONDS. Only once that timer hits
        // 0 are we allowed to publish another alarm.
        ctx->coolingDown = true;
        k_timer_start(&ctx->alarmCooldownTimer,
            K_SECONDS(ALARM_COOLDOWN_SECONDS), K_FOREVER);
    }
}

/**
 * Publish an alarm note to Notehub.
 *
 * @param tempStatus  A string with the temperature alarm status.
 * @param humidStatus A string with the humidity alarm status.
 * @param temp        The temperature at the time the alarm was triggered.
 * @param humid       The humidity at the time the alarm was triggered.
 *
 * @return True on success and false on failure.
 */
static bool publishAlarm(const char *tempStatus,
    const char *humidStatus, double temp, double humid)
{
    J *req = NoteNewRequest("note.add");
    J *body = JCreateObject();
    J *temperatureBody = JCreateObject();
    J *humidityBody = JCreateObject();
    if (req == NULL || body == NULL || temperatureBody == NULL ||
        humidityBody == NULL) {
        printk("publishAlarm: error: Failed to allocate memory for alarm note."
            "\n");
        return false;
    }

    JAddStringToObject(req, "file", "alarm.qo");
    JAddBoolToObject(req, "sync", true);
    JAddNumberToObject(temperatureBody, "value", temp);
    JAddStringToObject(temperatureBody, "status",
        tempStatus);
    JAddItemToObject(body, "temperature", temperatureBody);
    JAddNumberToObject(humidityBody, "value", humid);
    JAddStringToObject(humidityBody, "status", humidStatus);
    JAddItemToObject(body, "humidity", humidityBody);
    JAddItemToObject(req, "body", body);

    if (!NoteRequest(req)) {
        printk("publishAlarm: error: Failed to send alarm note.\n");
        return false;
    }

    printk("publishAlarm: Sent alarm note.\n");

    return true;
}
