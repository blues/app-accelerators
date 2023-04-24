#pragma once

struct Bme280Ctx;
typedef struct Bme280Ctx Bme280Ctx;

Bme280Ctx *bme280Init(void);
bool bme280GetTempAndHumidity(const Bme280Ctx *ctx, double *temp,
    double *humidity);
bool bme280Start(Bme280Ctx *ctx, uint32_t interval);
bool bme280Stop(Bme280Ctx *ctx);
