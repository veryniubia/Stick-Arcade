#pragma once

#include <stdint.h>

// Constants and helpers shared by every part of the platform. No hardware dependency.
namespace platform {

constexpr int kScreenW = 240;
constexpr int kScreenH = 135;
constexpr int kHintY = 118;  // top of the bottom key-hint strip
constexpr float kTick = 1.0f / 60.0f;  // fixed simulation step of every game

constexpr uint16_t rgb(uint8_t r, uint8_t g, uint8_t b) {
    return static_cast<uint16_t>(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

constexpr const char* kPlatformName = "STICK ARCADE";
constexpr const char* kVersion = "1.0";

}  // namespace platform
