#pragma once

#include <math.h>
#include "core/pad.h"

// Turns the gyroscope into a view direction that can be limited and amplified: a small turn of the device
// becomes a large turn of the view, so the player never has to rotate the device by the full view range.
// Hardware independent; the platform feeds it accelerometer (g) and gyroscope (deg/s) samples in the M5Unified
// device frame, where +Y points along the screen's right edge when the device is held in landscape.
namespace arcade {

class GyroLook {
public:
    // Flip these if a turn of the device moves the view the wrong way on the real hardware.
    static constexpr float kYawSign = 1.0f;    // positive device turn to the right gives positive yaw
    // The device is a window onto the world: raising its top edge towards the player (the screen turns to face down)
    // looks up. The pitch angle of the device is "screen facing up" positive, hence the minus.
    static constexpr float kPitchSign = -1.0f;

    static constexpr float kStillDps = 3.0f;       // below this the device counts as resting (hands tremble a little)
    static constexpr float kStillHold = 0.3f;      // seconds of rest before the bias estimate follows the gyro
    // After bias calibration, protect deliberate slow pans from the wider
    // three-axis tremor gate. Allow small stationary bias changes (~1 deg/s).
    static constexpr float kStillYawDps = 1.25f;
    static constexpr float kDeadDps = 0.4f;        // residual rate below this is ignored

    static constexpr float kDeviceYawAtLimit = 30.0f;

    void configurePitch(float pitchGainCenter, float pitchGainEdge) {
        pitchGain0_ = pitchGainCenter;
        pitchGain1_ = pitchGainEdge;
    }

    // Forget the view and the bias estimate (a new session).
    void reset() {
        deviceYaw_ = 0.0f;
        sampling_ = false;
        sampleTime_ = 0;
        bx_ = by_ = bz_ = 0.0f;
        stillTime_ = 0.0f;
        settled_ = false;
        haveGravity_ = false;
        haveDev_ = false;
    }

    // The current pose is "straight ahead" again.
    void recenter() { deviceYaw_ = 0.0f; dev0_ = dev_; }

    void beginCalibration() { sampleTime_ = biasTime_ = 0; sampling_ = true; }
    float calibrationProgress() const { return sampling_ ? fminf(sampleTime_, 0.99f) : sampleTime_; }

    void update(float ax, float ay, float az, float gx, float gy, float gz, float dt) {
        if (!(dt > 0.0f) || dt > 0.2f) return;
        const float mag = sqrtf(ax * ax + ay * ay + az * az);
        if (mag > 0.65f && mag < 1.35f) {
            const float nx = ax / mag, ny = ay / mag, nz = az / mag;
            if (!haveGravity_) {
                ux_ = nx; uy_ = ny; uz_ = nz;
                haveGravity_ = true;
            } else {
                const float alpha = dt / (0.045f + dt);
                ux_ += (nx - ux_) * alpha;
                uy_ += (ny - uy_) * alpha;
                uz_ += (nz - uz_) * alpha;
            }
            const float len = sqrtf(ux_ * ux_ + uy_ * uy_ + uz_ * uz_);
            if (len > 0.0f) { ux_ /= len; uy_ /= len; uz_ /= len; }
        }

        // Elevation has an absolute reference. A residual gyro rate must never move
        // the vertical view when the measured gravity direction stays the same.
        if (haveGravity_) {
            dev_ = atan2f(uz_, sqrtf(ux_ * ux_ + uy_ * uy_)) * kRadToDeg;
            if (!haveDev_) { dev0_ = dev_; haveDev_ = true; }
        }

        if (sampling_) {
            // A one-second window after release. Discard the button transient and
            // obvious bumps from the bias estimate without restarting the clock.
            sampleTime_ += dt;
            if (sampleTime_ > 0.12f && fabsf(mag - 1) < 0.3f &&
                gx * gx + gy * gy + gz * gz < 40.f * 40.f) {
                biasTime_ += dt;
                const float weight = dt / biasTime_;
                mean_[0] += (gx - mean_[0]) * weight;
                mean_[1] += (gy - mean_[1]) * weight;
                mean_[2] += (gz - mean_[2]) * weight;
            }
            if (sampleTime_ >= 1.0f && haveDev_) {
                if (biasTime_ < 0.5f) {
                    beginCalibration();
                    return;
                }
                bx_ = mean_[0]; by_ = mean_[1]; bz_ = mean_[2];
                settled_ = true;
                stillTime_ = 0.0f;
                sampleTime_ = 1;
                sampling_ = false;
                recenter();
            }
            return;
        }

        const float rx = gx - bx_, ry = gy - by_, rz = gz - bz_;
        const float up = rx * ux_ + ry * uy_ + rz * uz_;
        const bool resting = rx * rx + ry * ry + rz * rz < kStillDps * kStillDps &&
                             fabsf(mag - 1.0f) < 0.08f && (!settled_ || fabsf(up) < kStillYawDps);
        if (resting) {
            stillTime_ += dt;
            if (stillTime_ >= kStillHold) {
                // Learn the bias quickly at first, slowly afterwards.
                const float tau = settled_ ? 2.0f : 0.25f;
                const float k = dt / tau > 1.0f ? 1.0f : dt / tau;
                bx_ += (gx - bx_) * k;
                by_ += (gy - by_) * k;
                bz_ += (gz - bz_) * k;
                if (stillTime_ >= kStillHold + 0.4f) settled_ = true;
            }
            return;  // Hold the current heading while the bias estimate catches up.
        }
        stillTime_ = 0.0f;

        // Turning about the vertical axis is yaw whatever way the device is held; gravity gives the axis.
        if (fabsf(up) > kDeadDps) deviceYaw_ -= up * kYawSign * dt;
    }

    float yaw() const {
        // Keep tracking the device past the stops so the calibrated centre survives an overshoot.
        const float view = deviceYaw_ * (pad::kLookYawLimit / kDeviceYawAtLimit);
        return fmaxf(-pad::kLookYawLimit, fminf(view, pad::kLookYawLimit));
    }
    float pitch() const { return deviceToView(kPitchSign * (dev_ - dev0_), pad::kLookPitchLimit, pitchGain0_, pitchGain1_); }
    bool settled() const { return settled_; }

private:
    bool sampling_ = false;
    float sampleTime_ = 0, biasTime_ = 0, mean_[3] = {};
    static constexpr float kRadToDeg = 57.2957795f;

    // Closed form of dV/dD = g0 + (g1 - g0) |V| / limit.
    static float deviceToView(float d, float limit, float g0, float g1) {
        const float m = fabsf(d);
        float v = fabsf(g1 - g0) < 1e-4f ? g0 * m : g0 * limit / (g1 - g0) * (expf((g1 - g0) * m / limit) - 1.0f);
        if (v > limit) v = limit;
        return d < 0.0f ? -v : v;
    }

    float pitchGain0_ = 1.0f, pitchGain1_ = 3.0f;
    float deviceYaw_ = 0.0f;
    float dev_ = 0.0f, dev0_ = 0.0f;  // device pitch (screen facing up positive) and the pitch counted as straight ahead
    float bx_ = 0.0f, by_ = 0.0f, bz_ = 0.0f;
    float ux_ = 0.0f, uy_ = 0.0f, uz_ = 1.0f;
    float stillTime_ = 0.0f;
    bool settled_ = false;
    bool haveGravity_ = false;
    bool haveDev_ = false;
};

}  // namespace arcade
