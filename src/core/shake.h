#pragma once

#include <stdint.h>

namespace arcade {

// Recognises "shake the device twice" from the accelerometer, the platform's exit gesture.
// Only the deviation of the acceleration magnitude from 1 g is used: measured on a StickS3, normal play
// (fast tilting, button mashing) stays below 1.2 g while a deliberate shake reaches 5 g or more. The gyroscope
// is deliberately ignored because quick aiming reaches the same angular rate as a shake.
class ShakeDetector {
public:
    static constexpr float kThresholdG = 2.5f;   // spike: |magnitude - 1 g| above this (sampled at 60 Hz, so a bit under the 3 g midpoint)
    static constexpr float kRearmG = 1.0f;       // back below this before the next spike can count
    static constexpr uint32_t kWindowMs = 800;   // both spikes within this time

    // Feed one sample. Returns true once when the second spike arrives in time.
    bool update(uint32_t nowMs, float deviationG) {
        if (deviationG < kRearmG) armed_ = true;
        if (!armed_ || deviationG < kThresholdG) return false;
        armed_ = false;
        if (spikes_ == 1 && nowMs - firstMs_ <= kWindowMs) {
            spikes_ = 0;
            return true;
        }
        spikes_ = 1;
        firstMs_ = nowMs;
        return false;
    }

    void reset() { spikes_ = 0; armed_ = false; }

private:
    uint8_t spikes_ = 0;
    uint32_t firstMs_ = 0;
    bool armed_ = true;
};

}  // namespace arcade
