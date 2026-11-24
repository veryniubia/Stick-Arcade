#pragma once
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cmath>
#include <algorithm>
using std::min;
using std::max;
#define PROGMEM
uint32_t millis();
uint32_t micros();
void delay(uint32_t ms);
struct SimSerial { void begin(int) {} };
inline SimSerial Serial;
