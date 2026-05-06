// Standard C headers.
#include <stdlib.h>

// Zephyr headers.
#include <zephyr/kernel.h>

// Notecard headers.
#include "note.h"
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

// Temperature in C.
#ifndef TEMPERATURE_MIN_DEFAULT
#define TEMPERATURE_MIN_DEFAULT 5
#endif

#ifndef TEMPERATURE_MAX_DEFAULT
#define TEMPERATURE_MAX_DEFAULT 33
#endif

// Relative humidity.
#ifndef HUMID_MIN_DEFAULT
#define HUMID_MIN_DEFAULT 20
#endif

#ifndef HUMID_MAX_DEFAULT
#define HUMID_MAX_DEFAULT 70
#endif

// Read the sensor every 30 seconds.
#ifndef BME280_READ_INTERVAL
#define BME280_READ_INTERVAL 30
#endif

// Publish the temperature and humidity every 2 minutes, by default. This can be
// changed with the monitor_interval environment variable.
#ifndef PUBLISH_INTERVAL
#define PUBLISH_INTERVAL 120
#endif

// Check for alarm conditions every minute.
#ifndef ALARM_CHECK_INTERVAL
#define ALARM_CHECK_INTERVAL 60
#endif

// Check for environment variable updates every minute.
#ifndef ENV_VAR_CHECK_INTERVAL
#define ENV_VAR_CHECK_INTERVAL 60
#endif

#ifndef HUB_SET_TIMEOUT
#define HUB_SET_TIMEOUT 5
#endif

#ifndef OUTBOUND_SYNC_INTERVAL
#define OUTBOUND_SYNC_INTERVAL 5
#endif

#ifndef NOTECARD_LOCK_POLL_INTERVAL
#define NOTECARD_LOCK_POLL_INTERVAL 10
#endif

// Notecard locking functions.
static bool notecardLocked = false;

// Attempt to get a lock on the I2C bus. This will spin forever until the lock
// is released.
void lockNotecard(void)
{
    while (notecardLocked) {
        NoteDelayMs(NOTECARD_LOCK_POLL_INTERVAL);
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
    NoteSetFnDebugOutput(noteLogPrint);
    NoteSetFnNoteMutex(lockNotecard, unlockNotecard);
    NoteSetFnI2C(NOTE_I2C_ADDR_DEFAULT, NOTE_I2C_MAX_DEFAULT, noteI2cReset,
                 noteI2cTransmit, noteI2cReceive);

    J *req = NoteNewRequest("hub.set");
    if (PRODUCT_UID[0]) {
       JAddStringToObject(req, "product", PRODUCT_UID);
    }
    JAddStringToObject(req, "mode", "periodic");
    // Sync outbound data every OUTBOUND_SYNC_INTERVAL minutes. Alarm notes will
    // still be synced immediately.
    JAddNumberToObject(req, "outbound", OUTBOUND_SYNC_INTERVAL);
    // The hub.set request may fail if it's sent shortly after power up. We use
    // NoteRequestWithRetry to give it a chance to succeed.
    if (!NoteRequestWithRetry(req, HUB_SET_TIMEOUT)) {
        printk("hub.set failed, aborting.\n");
        return;
    }

    Bme280Ctx* bme280Ctx;
    if ((bme280Ctx = bme280Init()) == NULL) {
        printk("bme280Init failed, aborting.\n");
        return;
    }
    if (!bme280Start(bme280Ctx, BME280_READ_INTERVAL)) {
        printk("bme280Start failed, aborting.\n");
        return;
    }

    PublisherCtx* publisherCtx;
    if ((publisherCtx = publisherInit(bme280Ctx)) == NULL) {
        printk("publisherInit failed, aborting.\n");
        return;
    }
    if (!publisherStart(publisherCtx, PUBLISH_INTERVAL)) {
        printk("publisherStart failed, aborting.\n");
        return;
    }

    AlarmPublisherCtx* alarmPublisherCtx;
    // Check for alarms every minute.
    if ((alarmPublisherCtx = alarmPublisherInit(bme280Ctx, ALARM_CHECK_INTERVAL,
        TEMPERATURE_MIN_DEFAULT, TEMPERATURE_MAX_DEFAULT, HUMID_MIN_DEFAULT,
        HUMID_MAX_DEFAULT)) == NULL) {
        printk("alarmPublisherInit failed, aborting.\n");
        return;
    }
    if (!alarmPublisherStart(alarmPublisherCtx)) {
        printk("alarmPublisherStart failed, aborting.\n");
        return;
    }

    EnvUpdaterCtx* envUpdaterCtx;
    // Check for environment variable updates every minute.
    if ((envUpdaterCtx = envUpdaterInit(publisherCtx, alarmPublisherCtx))
        == NULL) {
        printk("envUpdaterInit failed, aborting.\n");
        return;
    }
    if (!envUpdaterStart(envUpdaterCtx, ENV_VAR_CHECK_INTERVAL)) {
        printk("envUpdaterStart failed, aborting.\n");
        return;
    }

    while (true) {
        k_sleep(K_SECONDS(10));
    }
}

void NoteUserAgentUpdate(J *ua) {
    JAddStringToObject(ua, "app", "nf18");
}
