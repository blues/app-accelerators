#pragma once

#include <stdint.h>

struct applicationState {
  int lastUpdate;
  String text;
  String imageBytes;
  bool variablesUpdated;
};