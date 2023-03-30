// Standard C headers.
#include <math.h>
#include <stdlib.h>
#include <string.h>

// Zephyr headers.
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/sensor.h>

// Application headers.
#include "bme280.h"

struct Bme280Ctx {
    const struct device *dev;
    struct k_work readWorkItem;
    struct k_timer readTimer;
    double temperature;
    double humidity;
    double heatIndex;
};

// Forward declarations.
static const struct device *getDevice(void);
static void readTimerCb(struct k_timer *timer);
static void readWorkCb(struct k_work *item);

/**
 * Get the last recorded temperature and humidity values.
 *
 * @param ctx       The sensor context object.
 * @param temp      A pointer to a double to store the temperature value in. Can
 *                  be NULL.
 * @param humidity  A pointer to a double to store the humidity value in. Can be
 *                  NULL.
 * @param heatIndex A pointer to a double to store the heat index value in. Can
 *                  be NULL.
 *
 * @return True on success and false on failure.
 */
bool bme280GetData(const Bme280Ctx *ctx, double *temp, double *humidity,
    double *heatIndex)
{
    if (ctx == NULL) {
        printk("bme280GetData: error: Called with NULL ctx.\n");
        return false;
    }

    if (temp != NULL) {
        *temp = ctx->temperature;
    }

    if (humidity != NULL) {
        *humidity = ctx->humidity;
    }

    if (heatIndex != NULL) {
        *heatIndex = ctx->heatIndex;
    }

    return true;
}

/**
 * Allocate and initialize a new sensor context.
 *
 * @return A valid Bme280Ctx pointer on success and NULL on failure.
 */
Bme280Ctx *bme280Init(void)
{
    Bme280Ctx *ctx = (Bme280Ctx *)malloc(sizeof(Bme280Ctx));
    if (ctx == NULL) {
        printk("bme280Init: error: failed to allocate Bme280Ctx.\n");
        return NULL;
    }
    memset(ctx, 0, sizeof(*ctx));

    ctx->dev = getDevice();
    if (ctx->dev == NULL) {
        printk("bme280Init: error: getDevice failed.\n");
        free(ctx);
        return NULL;
    }

    k_work_init(&ctx->readWorkItem, readWorkCb);
    k_timer_init(&ctx->readTimer, readTimerCb, NULL);

    printk("bme280Init: Initialized sensor.\n");

    return ctx;
}

/**
 * Start the sensor reading timer. The sensor will be read periodically
 * according to the interval.
 *
 * @param ctx      The sensor context object.
 * @param interval The sensor reading interval, in seconds.
 *
 * @return True on success and false on failure.
 */
bool bme280Start(Bme280Ctx *ctx, uint32_t interval)
{
    if (ctx == NULL) {
        printk("bme280Start: error: Called with NULL ctx.\n");
        return false;
    }
    if (interval == 0) {
        printk("bme280Start: error: Called with 0 interval. Must be > 0.\n");
        return false;
    }

    k_timer_start(&ctx->readTimer, K_SECONDS(0), K_SECONDS(interval));

    return true;
}

/**
 * Stop the sensor reading timer. No new readings will be performed until
 * bme280Start is called again.
 *
 * @param ctx The sensor context object.
 *
 * @return True on success and false on failure.
 */
bool bme280Stop(Bme280Ctx *ctx)
{
    if (ctx == NULL) {
        printk("bme280Stop: error: Called with NULL ctx.\n");
        return false;
    }

    k_timer_stop(&ctx->readTimer);

    return true;
}

/**
 * Compute the heat index based on the temperature and relative humidity. Uses
 * the algorithm described here:
 * https://www.wpc.ncep.noaa.gov/html/heatindex_equation.shtml
 *
 * @param temp  The temperature, in Fahrenheit.
 * @param humid The relative humidity.
 *
 * @return The computed heat index, in Fahrenheit.
 */
static double calcHeatIndex(double temp, double humid)
{
    double heatIndex = 0.5 * (temp + 61.0 + ((temp-68.0)*1.2) + (humid*0.094));

    if ((heatIndex + temp) / 2 >= 80) {
        heatIndex = -42.379 + 2.04901523*temp + 10.14333127*humid
                    - .22475541*temp*humid - .00683783*temp*temp
                    - .05481717*humid*humid + .00122874*temp*temp*humid
                    + .00085282*temp*humid*humid
                    - .00000199*temp*temp*humid*humid;

        if (humid < 13 && temp > 80 && temp < 112) {
            heatIndex -=  ((13-humid)/4)*sqrt((17-abs(temp-95.))/17);
        }
        else if (humid > 85 && temp > 80 && temp < 87) {
            heatIndex += ((humid-85)/10) * ((87-temp)/5);
        }
    }

    return heatIndex;
}

/**
 * Get a device handle for the BME280.
 *
 * @return A valid device pointer on success and NULL on failure.
 */
static const struct device *getDevice(void)
{
    const struct device *const dev = DEVICE_DT_GET_ANY(bosch_bme280);

    if (dev == NULL) {
        // No such node, or the node does not have status "okay".
        printk("getDevice: error: no BME280 found.\n");
        return NULL;
    }

    if (!device_is_ready(dev)) {
        printk("getDevice: error: Device \"%s\" is not ready; "
               "check the driver initialization logs for errors.\n",
               dev->name);
        return NULL;
    }

    printk("getDevice: Found device \"%s\".\n", dev->name);

    return dev;
}

/**
 * Read the temperature and humidity from the sensor and store the results in
 * the sensor context;
 *
 * @param ctx The sensor context object.
 *
 * @return True on success and false on failure.
 */
static bool readSensor(Bme280Ctx* ctx)
{
    if (ctx == NULL) {
        printk("readSensor: error: Called with NULL ctx.\n");
        return false;
    }

    struct sensor_value temp, humid;

    int rc;
    if ((rc = sensor_sample_fetch(ctx->dev)) != 0) {
        printk("readSensor: error: sensor_sample_fetch failed (%d).\n", rc);
        return false;
    }

    sensor_channel_get(ctx->dev, SENSOR_CHAN_AMBIENT_TEMP, &temp);
    sensor_channel_get(ctx->dev, SENSOR_CHAN_HUMIDITY, &humid);

    // Multiply sensor value by 1.8 and add 32 to convert Celsius to Fahrenheit.
    ctx->temperature = sensor_value_to_double(&temp) * 1.8 + 32;
    ctx->humidity = sensor_value_to_double(&humid);
    ctx->heatIndex = calcHeatIndex(ctx->temperature, ctx->humidity);

    printk("readSensor: temp: %dF, humidity: %d%%, heat index: %dF.\n",
           (int)ctx->temperature, (int)ctx->humidity, (int)ctx->heatIndex);

    return true;
}

/**
 * Callback that will be executed when the sensor reading timer expires.
 *
 * @param timer The timer handle.
 */
static void readTimerCb(struct k_timer *timer)
{
    Bme280Ctx *ctx = CONTAINER_OF(timer, Bme280Ctx, readTimer);
    k_work_submit(&ctx->readWorkItem);
}

/**
 * Callback that will be executed by the system workqueue when it's time to do a
 * sensor reading.
 *
 * @param item The sensor reading work item.
 */
static void readWorkCb(struct k_work *item)
{
    Bme280Ctx *ctx = CONTAINER_OF(item, Bme280Ctx, readWorkItem);
    readSensor(ctx);
}
