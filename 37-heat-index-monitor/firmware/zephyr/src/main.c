// Standard C headers.
#include <stdlib.h>

// Zephyr headers.
#include <zephyr/kernel.h>

// Notecard headers.
#include "note-c/note.h"
#include "note_c_hooks.h"

// Application headers.
#include "alarm_publisher.h"
#include "bme280.h"
#include "env_updater.h"
#include "publisher.h"

// Uncomment this line and replace com.your-company:your-product-name with your
// ProductUID.
// #define PRODUCT_UID "com.your-company:your-product-name"

#ifndef PRODUCT_UID
#define PRODUCT_UID ""
#pragma message "PRODUCT_UID is not defined in this example. Please ensure your Notecard has a product identifier set before running this example or define it in code here. More details at https://bit.ly/product-uid"
#endif

// Notecard locking functions.
static bool notecardLocked = false;

#ifndef NOTECARD_LOCK_POLLS_MS
#define NOTECARD_LOCK_POLLS_MS 10
#endif

// Temperature in Fahrenheit.
#define TEMPERATURE_MIN_DEFAULT 32
#define TEMPERATURE_MAX_DEFAULT 95
// Relative humidity.
#define HUMID_MIN_DEFAULT 5
#define HUMID_MAX_DEFAULT 80
// Heat index in Fahrenheit.
#define HEAT_INDEX_MAX 95

#ifndef HUB_SET_TIMEOUT_SECONDS
#define HUB_SET_TIMEOUT_SECONDS 5
#endif

// Set this value higher to conserve power.
#ifndef OUTBOUND_SYNC_MINS
#define OUTBOUND_SYNC_MINS 1
#endif

// Attempt to get a lock on the I2C bus. This will spin forever until the lock
// is released.
void lockNotecard(void)
{
    while (notecardLocked) {
        NoteDelayMs(NOTECARD_LOCK_POLLS_MS);
    }

    notecardLocked = true;
}

void unlockNotecard(void)
{
    notecardLocked = false;
}

void main(void)
{
    // Initialize note-c references.
    NoteSetFnDefault(malloc, free, platform_delay, platform_millis);
    NoteSetFnDebugOutput(note_log_print);
    NoteSetFnNoteMutex(lockNotecard, unlockNotecard);
    NoteSetFnI2C(NOTE_I2C_ADDR_DEFAULT, NOTE_I2C_MAX_DEFAULT, note_i2c_reset,
                 note_i2c_transmit, note_i2c_receive);

    J *req = NoteNewRequest("hub.set");
    if (PRODUCT_UID[0]) {
       JAddStringToObject(req, "product", PRODUCT_UID);
    }
    JAddStringToObject(req, "mode", "periodic");
    // Sync outbound data every OUTBOUND_SYNC_MINS minutes. Alarm notes will
    // still be synced immediately.
    JAddNumberToObject(req, "outbound", OUTBOUND_SYNC_MINS);
    // The hub.set request may fail if it's sent shortly after power up. We use
    // NoteRequestWithRetry to give it a chance to succeed.
    if (!NoteRequestWithRetry(req, HUB_SET_TIMEOUT_SECONDS)) {
        printk("hub.set failed, aborting.\n");
        return;
    }

    Bme280Ctx* bme280Ctx;
    // Read the sensor every 30 seconds.
    uint32_t interval = 30;
    if ((bme280Ctx = bme280Init()) == NULL) {
        printk("bme280Init failed, aborting.\n");
        return;
    }
    if (!bme280Start(bme280Ctx, interval)) {
        printk("bme280Start failed, aborting.\n");
        return;
    }

    PublisherCtx* publisherCtx;
    // Publish the temperature and humidity every 2 minutes, by default. This
    // can be changed with the monitor_interval environment variable.
    interval = 120;
    if ((publisherCtx = publisherInit(bme280Ctx)) == NULL) {
        printk("publisherInit failed, aborting.\n");
        return;
    }
    if (!publisherStart(publisherCtx, interval)) {
        printk("publisherStart failed, aborting.\n");
        return;
    }

    AlarmPublisherCtx* alarmPublisherCtx;
    // Check for alarms every minute.
    interval = 60;
    if ((alarmPublisherCtx = alarmPublisherInit(bme280Ctx, interval,
        TEMPERATURE_MIN_DEFAULT, TEMPERATURE_MAX_DEFAULT, HUMID_MIN_DEFAULT,
        HUMID_MAX_DEFAULT, HEAT_INDEX_MAX)) == NULL) {
        printk("alarmPublisherInit failed, aborting.\n");
        return;
    }
    if (!alarmPublisherStart(alarmPublisherCtx)) {
        printk("alarmPublisherStart failed, aborting.\n");
        return;
    }

    EnvUpdaterCtx* envUpdaterCtx;
    // Check for environment variable updates every minute.
    interval = 60;
    if ((envUpdaterCtx = envUpdaterInit(publisherCtx,
        alarmPublisherCtx)) == NULL) {
        printk("envUpdaterInit failed, aborting.\n");
        return;
    }
    if (!envUpdaterStart(envUpdaterCtx, interval)) {
        printk("envUpdaterStart failed, aborting.\n");
        return;
    }

    while (true) {
        k_sleep(K_SECONDS(10));
    }
}
