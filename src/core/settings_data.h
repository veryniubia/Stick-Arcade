#pragma once

#include <stddef.h>
#include <stdint.h>

// Persistent user settings: plain data plus the byte format used in NVS. No hardware dependency.
namespace settings {

constexpr uint8_t kFormat = 1;  // bump when the layout changes; unknown formats fall back to the defaults
constexpr int kVolumeLevels = 5;
constexpr int kBrightLevels = 5;
constexpr int kAimLevels = 5;

constexpr uint8_t kVolumeValue[kVolumeLevels] = {0, 50, 95, 140, 190};   // speaker volume (190 = 75% of max)
constexpr uint8_t kBrightValue[kBrightLevels] = {30, 60, 90, 110, 160};  // backlight
constexpr float kAimValue[kAimLevels] = {2.0f, 2.5f, 3.0f, 3.5f, 4.0f};  // tilt to aim multiplier

struct Data {
    uint8_t volume = 3;         // index into kVolumeValue, 0 = off
    uint8_t bright = 3;         // index into kBrightValue
    uint8_t aim = 2;            // index into kAimValue
    int16_t tiltOffsetX10 = 0;  // level pose, tenths of a degree (from CALIBRATE)

    bool operator==(const Data& o) const {
        return volume == o.volume && bright == o.bright && aim == o.aim && tiltOffsetX10 == o.tiltOffsetX10;
    }
};

constexpr size_t kBlobSize = 8;  // format, volume, bright, aim, offset lo/hi, reserved, crc8

inline uint8_t crc8(const uint8_t* d, size_t n) {
    uint8_t c = 0xA5;
    for (size_t i = 0; i < n; ++i) {
        c ^= d[i];
        for (int k = 0; k < 8; ++k) c = (c & 0x80) ? static_cast<uint8_t>((c << 1) ^ 0x07) : static_cast<uint8_t>(c << 1);
    }
    return c;
}

inline void encode(const Data& s, uint8_t out[kBlobSize]) {
    out[0] = kFormat;
    out[1] = s.volume;
    out[2] = s.bright;
    out[3] = s.aim;
    out[4] = static_cast<uint8_t>(static_cast<uint16_t>(s.tiltOffsetX10) & 0xFF);
    out[5] = static_cast<uint8_t>(static_cast<uint16_t>(s.tiltOffsetX10) >> 8);
    out[6] = 0;
    out[7] = crc8(out, kBlobSize - 1);
}

// Returns false (and leaves `s` alone) for a wrong size, checksum, format or out-of-range value.
inline bool decode(const uint8_t* in, size_t len, Data& s) {
    if (len != kBlobSize || in[0] != kFormat || crc8(in, kBlobSize - 1) != in[7]) return false;
    if (in[1] >= kVolumeLevels || in[2] >= kBrightLevels || in[3] >= kAimLevels) return false;
    s.volume = in[1];
    s.bright = in[2];
    s.aim = in[3];
    s.tiltOffsetX10 = static_cast<int16_t>(static_cast<uint16_t>(in[4]) | (static_cast<uint16_t>(in[5]) << 8));
    return true;
}

}  // namespace settings
