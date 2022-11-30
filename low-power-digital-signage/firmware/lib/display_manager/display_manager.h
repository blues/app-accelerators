#pragma once

#include <stdint.h>
#include <Notecard.h>

struct applicationState {
  int lastUpdate;
  String displayValues;
  J * displayObject;
  int currentDisplayObjectIndex = 0;
  int displayIntervalSec;
  bool variablesUpdated;
  bool displayUpdated;
};