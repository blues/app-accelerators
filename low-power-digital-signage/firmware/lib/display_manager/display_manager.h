/*!
 *
 * Written by the Blues Inc. team.
 *
 *
 * Copyright (c) 2019 Blues Inc. MIT License. Use of this source code is
 * governed by licenses granted by the copyright holder including that found in
 * the
 * <a href="https://github.com/blues/app-accelerators/blob/main/LICENSE">LICENSE</a>
 * file.
 *
 */

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