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
  // env.modified and env.get return a time value that is updated when ANY env var
  // is modified. Use this variable to specify whether a var we care about has
  // changed.
  bool variablesUpdated;
};

struct sensorReadings {
  float temp;
  float pressure;
  float altitude;
  float floor;
  int currentFloor;   // floor as an integer value
  uint32_t readingTimestamp;
};
