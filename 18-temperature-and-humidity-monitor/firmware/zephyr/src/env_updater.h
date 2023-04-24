#pragma once

// C standard headers.
#include <stdint.h>
#include <stdbool.h>

// Application headers.
#include "alarm_publisher.h"
#include "publisher.h"

struct EnvUpdaterCtx;
typedef struct EnvUpdaterCtx EnvUpdaterCtx;

EnvUpdaterCtx *envUpdaterInit(PublisherCtx* publisherCtx,
    AlarmPublisherCtx* alarmPublisherCtx);
bool envUpdaterStart(EnvUpdaterCtx *ctx, uint32_t interval);
bool envUpdaterStop(EnvUpdaterCtx *ctx);
