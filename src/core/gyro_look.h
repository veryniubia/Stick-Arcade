#pragma once

#include <math.h>

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
    static constexpr float kDeadDps = 0.4f;        // residual rate below this is ignored
    static constexpr float kGravityAlpha = 0.15f;  // smoothing of the gravity direction
    static constexpr float kPitchPull = 2.0f;      // per second: how fast the gyro pitch is pulled to the one gravity shows

    // Yaw gains turn device degrees into view degrees and may grow with the view angle: slow near the centre for
    // fine aim, fast near the limits so the player never has to turn the device by the whole view range. Limits are
    // in view degrees. Either way the view is a fixed function of the device angle, so turning back always returns
    // to the centre. Pitch uses the same idea, with gravity as a drift-free reference.
    void configure(float yawGainCenter, float yawGainEdge, float pitchGainCenter, float pitchGainEdge,
                   float yawLimit, float pitchLimit) {
        yawGain0_ = yawGainCenter;
        yawGain1_ = yawGainEdge;
        pitchGain0_ = pitchGainCenter;
        pitchGain1_ = pitchGainEdge;
        yawLimit_ = yawLimit;
        pitchLimit_ = pitchLimit;
        clampAngles();
    }

    // Forget the view and the bias estimate (a new session).
    void reset() {
        yaw_ = 0.0f;
        bx_ = by_ = bz_ = 0.0f;
        stillTime_ = 0.0f;
        settled_ = false;
        haveGravity_ = false;
        haveDev_ = false;
    }

    // The current pose is "straight ahead" again.
    void recenter() { yaw_ = 0.0f; dev0_ = dev_; }

    void update(float ax, float ay, float az, float gx, float gy, float gz, float dt) {
        if (!(dt > 0.0f) || dt > 0.2f) return;
        const float mag = sqrtf(ax * ax + ay * ay + az * az);
        if (mag > 0.5f) {
            const float nx = ax / mag, ny = ay / mag, nz = az / mag;
            if (!haveGravity_) {
                ux_ = nx; uy_ = ny; uz_ = nz;
                haveGravity_ = true;
            } else {
                ux_ += (nx - ux_) * kGravityAlpha;
                uy_ += (ny - uy_) * kGravityAlpha;
                uz_ += (nz - uz_) * kGravityAlpha;
            }
            const float len = sqrtf(ux_ * ux_ + uy_ * uy_ + uz_ * uz_);
            if (len > 0.0f) { ux_ /= len; uy_ /= len; uz_ /= len; }
        }

        float rx = gx - bx_, ry = gy - by_, rz = gz - bz_;
        const bool resting = rx * rx + ry * ry + rz * rz < kStillDps * kStillDps && fabsf(mag - 1.0f) < 0.08f;
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
                rx = gx - bx_; ry = gy - by_; rz = gz - bz_;
            }
        } else {
            stillTime_ = 0.0f;
        }
        rx = dead(rx); ry = dead(ry); rz = dead(rz);

        // Turning about the vertical axis is yaw whatever way the device is held; gravity gives the axis.
        const float up = rx * ux_ + ry * uy_ + rz * uz_;
        yaw_ += -up * kYawSign * gainAt(yaw_, yawLimit_, yawGain0_, yawGain1_) * dt;
        clampAngles();

        // Pitch: the angle at which the screen faces up, from the gyro about the screen's right edge and pulled
        // towards what gravity shows, which cannot drift.
        if (mag > 0.5f) {
            const float elevation = atan2f(az, sqrtf(ax * ax + ay * ay)) * kRadToDeg;
            if (!haveDev_) {
                dev_ = dev0_ = elevation;
                haveDev_ = true;
            } else {
                dev_ += ry * dt;
                if (fabsf(mag - 1.0f) < 0.12f) {
                    const float k = kPitchPull * dt > 1.0f ? 1.0f : kPitchPull * dt;
                    dev_ += (elevation - dev_) * k;
                }
            }
        }
    }

    float yaw() const { return yaw_; }
    float pitch() const { return deviceToView(kPitchSign * (dev_ - dev0_), pitchLimit_, pitchGain0_, pitchGain1_); }
    bool settled() const { return settled_; }

private:
    static constexpr float kRadToDeg = 57.2957795f;

    static float gainAt(float angle, float limit, float center, float edge) {
        const float k = limit > 0.0f ? fabsf(angle) / limit : 0.0f;
        return center + (edge - center) * (k > 1.0f ? 1.0f : k);
    }
    // Closed form of dV/dD = g0 + (g1 - g0) |V| / limit.
    static float deviceToView(float d, float limit, float g0, float g1) {
        const float m = fabsf(d);
        float v = fabsf(g1 - g0) < 1e-4f ? g0 * m : g0 * limit / (g1 - g0) * (expf((g1 - g0) * m / limit) - 1.0f);
        if (v > limit) v = limit;
        return d < 0.0f ? -v : v;
    }
    static float dead(float v) {
        const float m = fabsf(v);
        if (m <= kDeadDps) return 0.0f;
        return v > 0.0f ? m - kDeadDps : kDeadDps - m;
    }
    void clampAngles() {
        if (yaw_ > yawLimit_) yaw_ = yawLimit_;
        if (yaw_ < -yawLimit_) yaw_ = -yawLimit_;
    }

    float yawGain0_ = 1.0f, yawGain1_ = 5.0f, pitchGain0_ = 1.0f, pitchGain1_ = 3.0f;
    float yawLimit_ = 90.0f, pitchLimit_ = 30.0f;
    float yaw_ = 0.0f;
    float dev_ = 0.0f, dev0_ = 0.0f;  // device pitch (screen facing up positive) and the pitch counted as straight ahead
    float bx_ = 0.0f, by_ = 0.0f, bz_ = 0.0f;
    float ux_ = 0.0f, uy_ = 0.0f, uz_ = 1.0f;
    float stillTime_ = 0.0f;
    bool settled_ = false;
    bool haveGravity_ = false;
    bool haveDev_ = false;
};

}  // namespace arcade
