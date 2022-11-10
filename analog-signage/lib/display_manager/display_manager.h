#pragma once

#include <stdint.h>

struct applicationState {
  int lastUpdate;
  String text;
  bool variablesUpdated;
};