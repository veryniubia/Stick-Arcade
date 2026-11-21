#include "core/input.h"

#include <M5Unified.h>
#include <math.h>

#include "core/shake.h"
#include "core/portrait_tilt.h"

namespace {

// Screen's right edge points up: sign of the accelerometer axis along the long side of the device.
constexpr float kImuAimSign = 1.0f;

arcade::PortraitTilt g_portraitTilt;
float g_tilt = 0.0f;  // filtered device tilt in degrees
float g_offset = 0.0f;
bool g_primed = false;
bool g_imuOk = false;
uint8_t g_aCount = 0;
uint8_t g_bCount = 0;
bool g_shaken = false;
arcade::ShakeDetector g_shake;

constexpr float kRadToDeg = 57.2957795f;
// Tilt filter: tiny changes are ignored, small ones are smoothed heavily, fast moves follow quickly.
// Everything here is in tilt degrees (before the AIM gain), so hand tremor is amplified by the gain in the aim.
constexpr float kHoldDeg = 0.35f;
constexpr float kAlphaMin = 0.10f;
constexpr float kAlphaMax = 0.60f;
constexpr float kAlphaPerDeg = 0.03f;

}  // namespace

namespace input {

void begin() {
    g_imuOk = M5.Imu.isEnabled();
    g_primed = false;
    g_portraitTilt.reset();
}

void update() {
    if (M5.BtnA.wasPressed()) ++g_aCount;
    if (M5.BtnB.wasPressed()) ++g_bCount;
    if (!g_imuOk) return;
    M5.Imu.update();
    float ax = 0, ay = 0, az = 0;
    if (!M5.Imu.getAccel(&ax, &ay, &az)) return;

    const float magnitude = sqrtf(ax * ax + ay * ay + az * az);
    if (g_shake.update(millis(), fabsf(magnitude - 1.0f))) {
        g_shaken = true;
#ifdef DEBUG_REMOTE
        Serial.println("SHAKE");
#endif
    }

    g_portraitTilt.update(ax, ay, az);
    const float along = kImuAimSign * ay;
    const float across = sqrtf(ax * ax + az * az);
    if (along == 0.0f && across == 0.0f) return;

    const float deg = atan2f(along, across) * kRadToDeg;
    if (!g_primed) {
        g_tilt = deg;
        g_primed = true;
        return;
    }
    const float delta = deg - g_tilt;
    const float mag = fabsf(delta);
    if (mag < kHoldDeg) return;
    float alpha = kAlphaMin + kAlphaPerDeg * mag;
    if (alpha > kAlphaMax) alpha = kAlphaMax;
    g_tilt += delta * alpha;
}

float driveTiltDeg() { return g_portraitTilt.angleDeg(); }
float rawTiltDeg() { return g_tilt; }
float tiltDeg() { return g_tilt - g_offset; }
void setTiltOffset(float deg) { g_offset = deg; }
bool imuOk() { return g_imuOk; }

bool aHeld() { return M5.BtnA.isPressed(); }
bool aPressed() { return M5.BtnA.wasPressed(); }
bool bHeld() { return M5.BtnB.isPressed(); }
bool bPressed() { return M5.BtnB.wasPressed(); }
uint8_t aCount() { return g_aCount; }
uint8_t bCount() { return g_bCount; }

bool shaken() {
    const bool s = g_shaken;
    g_shaken = false;
    return s;
}

}  // namespace input
