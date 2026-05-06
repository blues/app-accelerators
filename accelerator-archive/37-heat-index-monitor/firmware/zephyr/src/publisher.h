#pragma once

// C standard headers.
#include <stdint.h>
#include <stdbool.h>

// Application headers.
#include "bme280.h"

struct PublisherCtx;
typedef struct PublisherCtx PublisherCtx;

PublisherCtx *publisherInit(const Bme280Ctx* bme280Ctx);
bool publisherStart(PublisherCtx *ctx, uint32_t interval);
bool publisherStop(PublisherCtx *ctx);
