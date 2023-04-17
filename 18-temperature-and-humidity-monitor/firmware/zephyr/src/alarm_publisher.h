#pragma once

// C standard headers.
#include <stdint.h>
#include <stdbool.h>

// Application headers.
#include "bme280.h"

struct AlarmPublisherCtx;
typedef struct AlarmPublisherCtx AlarmPublisherCtx;

typedef enum {
    TEMP_MIN,
    TEMP_MAX,
    HUMID_MIN,
    HUMID_MAX
} AlarmBound;

AlarmPublisherCtx *alarmPublisherInit(const Bme280Ctx* bme280Ctx,
    uint32_t checkInterval, double tempMin, double tempMax, double humidMin,
    double humidMax);
bool alarmPublisherUpdateBound(AlarmPublisherCtx* ctx, AlarmBound bound,
    double newVal);
bool alarmPublisherStart(AlarmPublisherCtx *ctx);
bool alarmPublisherStop(AlarmPublisherCtx *ctx);

#define alarmPublisherUpdateTempMin(ctx, newVal) alarmPublisherUpdateBound(ctx, TEMP_MIN, newVal)
#define alarmPublisherUpdateTempMax(ctx, newVal) alarmPublisherUpdateBound(ctx, TEMP_MAX, newVal)
#define alarmPublisherUpdateHumidMin(ctx, newVal) alarmPublisherUpdateBound(ctx, HUMID_MIN, newVal)
#define alarmPublisherUpdateHumidMax(ctx, newVal) alarmPublisherUpdateBound(ctx, HUMID_MAX, newVal)
