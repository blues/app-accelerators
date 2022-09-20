#pragma once

#include <stdint.h>


struct applicationState {
  bool live;
  double floorHeight;
  int baselineFloor;
  float baselineFloorPressure;
  int noMovementThreshold;
  int lastFloor;
  uint32_t lastFloorChangeAt;
  bool alarmSent;
};

struct sensorReadings {
  float temp;
  float pressure;
  float altitude;
  int currentFloor;
  uint32_t readingTimestamp;
};
