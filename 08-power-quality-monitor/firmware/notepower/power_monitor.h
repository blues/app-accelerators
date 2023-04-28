#pragma once

#include <stdint.h>
#include "Notecard.h"

typedef struct {
    uint8_t taskID;
    float lastApparentPower;
    float lastReactivePower;
    float lastPowerFactor;
    float lastVoltage;
    float lastCurrent;
    float lastPower;
    float lastFrequency;
    float maxVoltage;
    float maxCurrent;
    float maxPower;
    float startup;              // duration in seconds
    float shutdown;             // duration in seconds
    bool first;
} mcpContext;

typedef struct {
  uint32_t envHeartbeatMins;
  float envVoltageUnder;
  float envVoltageOver;
  float envVoltageChange;
  float envCurrentUnder;
  float envCurrentOver;
  float envCurrentChange;
  bool envSendPowerAlarms;
  float envPowerUnder;
  float envPowerOver;
  float envPowerChange;
} applicationState;
