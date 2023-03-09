// Standard C headers.
#include <stdlib.h>
#include <string.h>

// Zephyr headers.
#include <zephyr/kernel.h>

// Notecard headers.
#include "note.h"

// Application headers.
#include "publisher.h"
#include "bme280.h"

struct PublisherCtx {
    const Bme280Ctx *bme280Ctx;
    struct k_work publishWorkItem;
    struct k_timer publishTimer;
};

// Forward declarations.
static void publishTimerCb(struct k_timer *timer);
static void publishWorkCb(struct k_work *item);

static bool publish(PublisherCtx* ctx)
{
    if (ctx == NULL) {
        printk("publish: error: Called with NULL ctx.\n");
        return false;
    }
    if (ctx->bme280Ctx == NULL) {
        printk("publish: error: Called with NULL ctx->bme280Ctx.\n");
        return false;
    }

    double temp, humidity;
    if (!bme280GetTempAndHumidity(ctx->bme280Ctx, &temp, &humidity)) {
        printk("publish: error: bme280GetTempAndHumidity failed.\n");
        return false;
    }

    J *req = NoteNewRequest("note.add");
    if (req != NULL) {
        JAddStringToObject(req, "file", "data.qo");

        J *body = JCreateObject();
        if (body != NULL) {
            JAddNumberToObject(body, "temperature", temp);
            JAddNumberToObject(body, "humidity", humidity);
            JAddItemToObject(req, "body", body);

            if (!NoteRequest(req)) {
                printk("publish: error: NoteRequest failed.\n");
                return false;
            }
        }
        else {
            JDelete(req);
            printk("publish: error: Failed to create Note body.\n");
            return false;
        }
    }
    else {
        printk("publish: error: Failed to create note.add request.\n");
        return false;
    }

    return true;
}

/**
 * Allocate and initialize a new publisher context.
 *
 * @param bme280Ctx A BME280 context object.
 *
 * @return A valid PublisherCtx pointer on success and NULL on failure.
 */
PublisherCtx *publisherInit(const Bme280Ctx *bme280Ctx)
{
    if (bme280Ctx == NULL) {
        printk("publisherInit: error: Called with NULL bme280Ctx.\n");
        return NULL;
    }

    PublisherCtx *ctx = (PublisherCtx *)malloc(sizeof(PublisherCtx));
    if (ctx == NULL) {
        printk("publisherInit: error: failed to allocate PublisherCtx.\n");
        return NULL;
    }
    memset(ctx, 0, sizeof(*ctx));

    ctx->bme280Ctx = bme280Ctx;

    k_work_init(&ctx->publishWorkItem, publishWorkCb);
    k_timer_init(&ctx->publishTimer, publishTimerCb, NULL);

    printk("publisherInit: Initialized publisher.\n");

    return ctx;
}

/**
 * Start the publish timer. A Note will be published periodically according to
 * the interval.
 *
 * @param ctx      The publisher context object.
 * @param interval The publish interval, in seconds.
 *
 * @return True on success and false on failure.
 */
bool publisherStart(PublisherCtx *ctx, uint32_t interval)
{
    if (ctx == NULL) {
        printk("publisherStart: error: Called with NULL ctx.\n");
        return false;
    }
    if (interval == 0) {
        printk("publisherStart: error: Called with 0 interval. Must be > 0.\n");
        return false;
    }

    k_timer_start(&ctx->publishTimer, K_SECONDS(0), K_SECONDS(interval));

    return true;
}

/**
 * Stop the publish timer. No new Notes will be published until publisherStart
 * is called again.
 *
 * @param ctx The publisher context object.
 *
 * @return True on success and false on failure.
 */
bool publisherStop(PublisherCtx *ctx)
{
    if (ctx == NULL) {
        printk("publisherStop: error: Called with NULL ctx.\n");
        return false;
    }

    k_timer_stop(&ctx->publishTimer);

    return true;
}

/**
 * Callback that will be executed when the publish timer expires.
 *
 * @param timer The timer handle.
 */
static void publishTimerCb(struct k_timer *timer)
{
    PublisherCtx *ctx = CONTAINER_OF(timer, PublisherCtx, publishTimer);
    k_work_submit(&ctx->publishWorkItem);
}

/**
 * Callback that will be executed by the system workqueue when it's time to
 * publish.
 *
 * @param item The publish work item.
 */
static void publishWorkCb(struct k_work *item)
{
    PublisherCtx *ctx = CONTAINER_OF(item, PublisherCtx, publishWorkItem);
    publish(ctx);
}
