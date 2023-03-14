// Copyright 2022 Blues Inc.  All rights reserved.
// Use of this source code is governed by licenses granted by the
// copyright holder including that found in the LICENSE file.

#include "bme.h"

// Standard Header(s)
#include <math.h>

// ST Header(s)
#include <main.h>

// Blues Header(s)
#include <framework.h>
#include <note.h>

// 3rd-party Libraries
#include <bme280/bme280.h>

// An instance of an env sample
static BMESample envSamples[5];
static BMESample lastBME = {0};

// Which I2C device we are using
extern I2C_HandleTypeDef hi2c2;

// Device address
static uint8_t bme_dev_addr = 0;

// Forwards
static bool bme280_read(struct bme280_dev *dev, struct bme280_data *comp_data);
static void bme280_delay_us(uint32_t period, void *intf_ptr);
static int8_t bme280_i2c_read(uint8_t reg_addr, uint8_t *reg_data, uint32_t len, void *intf_ptr);
static int8_t bme280_i2c_write(uint8_t reg_addr, const uint8_t *reg_data, uint32_t len, void *intf_ptr);

bool bmePresent() {
    // Power on the sensor to see if it's here
    GPIO_InitTypeDef init = {0};
    init.Speed = GPIO_SPEED_FREQ_HIGH;
    init.Pin = BME_POWER_Pin;
    init.Mode = GPIO_MODE_OUTPUT_PP;
    init.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(BME_POWER_GPIO_Port, &init);

    return bmeUpdate(&lastBME);
}


// Update the static temp/humidity/pressure values with the most accurate
// values that we can by averaging several samples.
static bool bmeAverageSamples(BMESample* sample)
{
    bool success = false;
    // Determine whether it's on primary or secondary address
    struct bme280_dev dev;
    dev.intf = BME280_I2C_INTF;
    dev.read = bme280_i2c_read;
    dev.write = bme280_i2c_write;
    dev.delay_us = bme280_delay_us;
    dev.intf_ptr = &bme_dev_addr;
    bme_dev_addr = BME280_I2C_ADDR_PRIM;
    if (bme280_init(&dev) != BME280_INTF_RET_SUCCESS) {
        return success;
    }

    // Allocate a sample buffer
    sample->temperature = sample->humidity = sample->pressure = 0.0;

    // Ignore the first two readings for settling purposes
    struct bme280_data comp_data;
    bme280_read(&dev, &comp_data);
    bme280_read(&dev, &comp_data);

    // Take a set of measurements, discarding data that is very high or very low.  We
    // do this to get the most accurate sample possible.
    int validSamples = 0;
    int totalRetries = 0;
    int maxRetries = (sizeof(envSamples) / sizeof(envSamples[0]))*2;
    struct bme280_data prev_data = {0};
    int samples = sizeof(envSamples) / sizeof(envSamples[0]);
    for (int i=0; i<samples; i++) {
        // Read the sample, and retry if I2C read failure
        if (!bme280_read(&dev, &comp_data)) {
            if (totalRetries++ < maxRetries) {
                --i;
            }
            continue;
        }

        // If we haven't yet converged, retry
        if (totalRetries < maxRetries && i > 0) {
            double tempPct = fabs((comp_data.temperature-prev_data.temperature)/prev_data.temperature);
            double pressPct = fabs((comp_data.pressure-prev_data.pressure)/prev_data.pressure);
            double humidPct = fabs((comp_data.humidity-prev_data.humidity)/prev_data.humidity);
            bool retry = false;
            if (tempPct >= 0.001) {
                retry = true;
            }
            if (pressPct >= 0.0001) {
                retry = true;
            }
            if (humidPct >= 0.005) {
                retry = true;
            }
            if (retry) {
                totalRetries++;
                i = -1;
                validSamples = 0;
                continue;
            }
        }
        memcpy(&prev_data, &comp_data, sizeof(struct bme280_data));

        // Store the sample
        envSamples[validSamples].temperature = comp_data.temperature;
        envSamples[validSamples].pressure = comp_data.pressure;
        envSamples[validSamples].humidity = comp_data.humidity;
        validSamples++;
    }

    // Average the samples (assuming the sample is already zero'ed)
    if (validSamples) {
        for (int i=0; i<validSamples; i++) {
            sample->temperature += envSamples[i].temperature;
            sample->pressure += envSamples[i].pressure;
            sample->humidity += envSamples[i].humidity;
        }
        sample->temperature /= validSamples;
        sample->pressure /= validSamples;
        sample->humidity /= validSamples;
        success = true;
    }

    // Put the sensor to sleep, to save power if we're leaving it on
    bme280_set_sensor_mode(BME280_SLEEP_MODE, &dev);

    // Done
    return success;
}

bool bmeUpdate(BMESample* sample) {
    HAL_GPIO_WritePin(BME_POWER_GPIO_Port, BME_POWER_Pin, GPIO_PIN_SET);
    MX_I2C2_Init();
    bool success = bmeAverageSamples(sample);
    MX_I2C2_DeInit();
    HAL_GPIO_WritePin(BME_POWER_GPIO_Port, BME_POWER_Pin, GPIO_PIN_RESET);
    return success;
}

// BME280 sensor read
bool bme280_read(struct bme280_dev *dev, struct bme280_data *comp_data)
{
    int8_t rslt;
    uint8_t settings_sel;

    dev->settings.osr_h = BME280_OVERSAMPLING_1X;
    dev->settings.osr_p = BME280_OVERSAMPLING_16X;
    dev->settings.osr_t = BME280_OVERSAMPLING_2X;
    dev->settings.filter = BME280_FILTER_COEFF_16;
    dev->settings.standby_time = BME280_STANDBY_TIME_62_5_MS;

    settings_sel = BME280_OSR_PRESS_SEL;
    settings_sel |= BME280_OSR_TEMP_SEL;
    settings_sel |= BME280_OSR_HUM_SEL;
    settings_sel |= BME280_STANDBY_SEL;
    settings_sel |= BME280_FILTER_SEL;
    rslt = bme280_set_sensor_settings(settings_sel, dev);
    if (rslt != BME280_INTF_RET_SUCCESS) {
        return false;
    }
    rslt = bme280_set_sensor_mode(BME280_NORMAL_MODE, dev);
    if (rslt != BME280_INTF_RET_SUCCESS) {
        return false;
    }

    // Delay while the sensor completes a measurement
    dev->delay_us(70000, dev->intf_ptr);
    memset(comp_data, 0, sizeof(struct bme280_data));
    rslt = bme280_get_sensor_data(BME280_ALL, comp_data, dev);
    if (rslt != BME280_INTF_RET_SUCCESS) {
        return false;
    }

    // If the data looks bad, don't accept it.  (Humidity does operate
    // at the extremes, but these do not and we've seen these failures
    // concurrently, where temp == -40 and press == 110000 && humid == 100%)
    if (comp_data->temperature == -40           // temperature_min
            || comp_data->pressure == 30000.0       // pressure_min
            || comp_data->pressure == 110000.0) {   // pressure_max
        return false;
    }

    return true;
}

// Delay
void bme280_delay_us(uint32_t period, void *intf_ptr)
{
    // Unused parameter(s)
    (void)intf_ptr;

    HAL_DelayUs(period);
}

// Read from sensor
int8_t bme280_i2c_read(uint8_t reg_addr, uint8_t *reg_data, uint32_t len, void *intf_ptr)
{
    // Unused parameter(s)
    (void)intf_ptr;

    bool success = MY_I2C2_ReadRegister(bme_dev_addr, reg_addr, reg_data, len, 5000);
    return (success ? BME280_INTF_RET_SUCCESS : !BME280_INTF_RET_SUCCESS);
}

// Write to sensor
int8_t bme280_i2c_write(uint8_t reg_addr, const uint8_t *reg_data, uint32_t len, void *intf_ptr)
{
    // Unused parameter(s)
    (void)intf_ptr;

    bool success = MY_I2C2_WriteRegister(bme_dev_addr, reg_addr, (uint8_t *) reg_data, len, 5000);
    return (success ? BME280_INTF_RET_SUCCESS : !BME280_INTF_RET_SUCCESS);
}
