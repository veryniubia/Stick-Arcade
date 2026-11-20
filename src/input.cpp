#include "input.h"

#include <M5Unified.h>
#include <math.h>

#include "config.h"

namespace {

float g_tilt = 0.0f;  // filtered device tilt in degrees
bool g_primed = false;
bool g_imuOk = false;

constexpr float kRadToDeg = 57.2957795f;
// Tilt filter: tiny changes are ignored, small ones are smoothed heavily, fast moves follow quickly.
// Everything here is in tilt degrees (before AIM_GAIN), so hand tremor is amplified 3x in the aim.
constexpr float kHoldDeg = 0.35f;
constexpr float kAlphaMin = 0.10f;
constexpr float kAlphaMax = 0.60f;
constexpr float kAlphaPerDeg = 0.03f;

}  // namespace

namespace input {

void begin() {
    g_imuOk = M5.Imu.isEnabled();
    g_primed = false;
}

void update() {
    if (!g_imuOk) return;
    M5.Imu.update();
    float ax = 0, ay = 0, az = 0;
    if (!M5.Imu.getAccel(&ax, &ay, &az)) return;

    const float along = cfg::IMU_AIM_SIGN * ay;
    const float across = sqrtf(ax * ax + az * az);
    if (along == 0.0f && across == 0.0f) return;

    const float deg = atan2f(along, across) * kRadToDeg;
#ifdef IMU_DEBUG
    static uint32_t lastPrint = 0;
    if (millis() - lastPrint > 250) {
        lastPrint = millis();
        Serial.printf("IMU ax=%.2f ay=%.2f az=%.2f tilt=%.1f\n", ax, ay, az, deg);
    }
#endif

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

float aimDeg() {
    const float aim = g_tilt * cfg::AIM_GAIN;
    if (aim < cfg::AIM_MIN_DEG) return cfg::AIM_MIN_DEG;
    if (aim > cfg::AIM_MAX_DEG) return cfg::AIM_MAX_DEG;
    return aim;
}
bool imuOk() { return g_imuOk; }

bool aHeld() { return M5.BtnA.isPressed(); }
bool aPressed() { return M5.BtnA.wasPressed(); }
bool bHeld() { return M5.BtnB.isPressed(); }
bool bPressed() { return M5.BtnB.wasPressed(); }

}  // namespace input
