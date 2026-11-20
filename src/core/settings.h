#pragma once

#include "core/settings_data.h"

namespace settings {

void begin();  // loads from NVS (defaults if missing or damaged) and applies the values
const Data& get();
void set(const Data& d);  // applies and stores
void resetDefaults();

float aimGain();
uint8_t brightness();

}  // namespace settings
