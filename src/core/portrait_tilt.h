#pragma once

#include <cmath>

namespace arcade {

// Fixed-reference screen-plane gravity angle, independent of game entry/calibration.
// M5Unified supplies body X/Y in the display plane; +atan2(X,Y) follows clockwise roll.
class PortraitTilt {
 public:
    void reset() { angle_ = 0; initialized_ = reliable_ = false; }
    float angleDeg() const { return angle_; }

    void update(float x, float y, float z) {
        if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z)) return;
        const float magnitude = std::sqrt(x*x + y*y + z*z);
        if (magnitude < .65f || magnitude > 1.35f) return; // strong acceleration is not a gravity reference
        const float projection = std::sqrt(x*x + y*y) / magnitude;
        // Freeze close to horizontal. Hysteresis prevents noisy entering/leaving of this zone.
        if (projection < .25f) { reliable_ = false; return; }
        if (!reliable_ && projection < .35f) return;
        const float target = std::atan2(x, y) * 57.2957795f;
        if (!initialized_ || !reliable_) {
            angle_ = target;
            initialized_ = reliable_ = true;
            return;
        }
        const float delta = wrap(target - angle_);
        const float size = std::fabs(delta);
        if (size < .15f) return;
        const float alpha = std::fmin(.65f, .25f + size*.02f);
        angle_ = wrap(angle_ + delta*alpha);
    }

 private:
    static float wrap(float angle) {
        if (angle > 180) angle -= 360;
        if (angle < -180) angle += 360;
        return angle;
    }
    float angle_ = 0;
    bool initialized_ = false, reliable_ = false;
};

} // namespace arcade
