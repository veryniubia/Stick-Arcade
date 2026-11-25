#pragma once
#include <cmath>
#include "core/gyro_look.h"

namespace arcade {
// Device heading from a north-referenced attitude sensor, in clockwise degrees.
class HeadingLook {
public:
    void sample(float degrees, uint32_t now, bool turning) {
        if (!std::isfinite(degrees)) return;
        if (seen_) unwrapped_ += std::remainder(degrees - last_, 360.f);
        last_ = degrees;
        stamp_ = now;
        seen_ = true;
        if (active_) {
            if (turning) target_ = unwrapped_ - center_;
            offset_ += .4f * (target_ - offset_);
        }
    }
    void center(uint32_t now) {
        active_ = seen_ && now - stamp_ < 250;
        center_ = unwrapped_;
        target_ = offset_ = 0;
    }
    bool active() const { return active_; }
    float yaw() const {
        return fminf(pad::kLookYawLimit, fmaxf(-pad::kLookYawLimit,
            offset_ * pad::kLookYawLimit / GyroLook::kDeviceYawAtLimit));
    }
private:
    bool seen_ = false, active_ = false;
    uint32_t stamp_ = 0;
    float last_ = 0, unwrapped_ = 0, center_ = 0, target_ = 0, offset_ = 0;
};
}
