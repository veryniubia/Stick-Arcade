#pragma once

#include <math.h>
#include <stdint.h>

// Converts the raw 8-bit axis of the I2C joystick (0..255, about 128 at rest) into -100..100 with a dead zone.
// Hardware independent.
namespace arcade {

struct StickAxis {
    static constexpr float kDeadZone = 0.12f;
    static constexpr float kStartRange = 96.0f;  // counts from the rest value to the end of travel until proven otherwise

    float center = 128.0f;
    float range = kStartRange;

    void seed(uint8_t raw) {
        center = raw;
        range = kStartRange;
    }

    // Learns the real end of travel when the stick goes further than assumed.
    int8_t map(uint8_t raw) {
        const float d = static_cast<float>(raw) - center;
        float m = fabsf(d);
        if (m > range) range = m > 127.0f ? 127.0f : m;
        float n = m / range;
        if (n <= kDeadZone) return 0;
        n = (n - kDeadZone) / (1.0f - kDeadZone);
        if (n > 1.0f) n = 1.0f;
        const int v = static_cast<int>(lroundf(n * 100.0f));
        return static_cast<int8_t>(d < 0.0f ? -v : v);
    }
};

}  // namespace arcade
