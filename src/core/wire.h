#pragma once

#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

namespace arcade {

constexpr size_t kMaxPacket = 250;  // ESP-NOW payload limit

// Little-endian packet writer; silently stops at the end of the buffer.
struct Writer {
    uint8_t buf[kMaxPacket];
    size_t n = 0;

    void u8(uint8_t v) { if (n < sizeof(buf)) buf[n++] = v; }
    void u16(uint16_t v) { u8(v & 0xFF); u8(v >> 8); }
    void i16(int16_t v) { u16(static_cast<uint16_t>(v)); }
    void u32(uint32_t v) { u16(v & 0xFFFF); u16(v >> 16); }
    void f32(float v) { uint32_t b; memcpy(&b, &v, 4); u32(b); }
};

// Matching reader; reading past the end clears `ok` and returns zeros.
struct Reader {
    const uint8_t* p;
    size_t len;
    size_t pos = 0;
    bool ok = true;

    Reader(const uint8_t* d, size_t n) : p(d), len(n) {}
    uint8_t u8() {
        if (pos >= len) { ok = false; return 0; }
        return p[pos++];
    }
    uint16_t u16() { const uint16_t lo = u8(); return lo | (static_cast<uint16_t>(u8()) << 8); }
    int16_t i16() { return static_cast<int16_t>(u16()); }
    uint32_t u32() { const uint32_t lo = u16(); return lo | (static_cast<uint32_t>(u16()) << 16); }
    float f32() { const uint32_t b = u32(); float v; memcpy(&v, &b, 4); return v; }
};

inline int16_t q16(float v, float scale) {
    float s = v * scale;
    if (s > 32767.0f) s = 32767.0f;
    if (s < -32768.0f) s = -32768.0f;
    return static_cast<int16_t>(lroundf(s));
}

inline uint8_t q8(float v, float scale) {
    float s = v * scale;
    if (s > 255.0f) s = 255.0f;
    if (s < 0.0f) s = 0.0f;
    return static_cast<uint8_t>(lroundf(s));
}

}  // namespace arcade
