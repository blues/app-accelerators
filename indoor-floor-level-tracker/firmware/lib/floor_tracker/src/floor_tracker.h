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
  bool baselineChanged;
};

struct sensorReadings {
  float temp;
  float pressure;
  float altitude;
  float floor;
  int currentFloor;   // floor as an integer value
  uint32_t readingTimestamp;
};
