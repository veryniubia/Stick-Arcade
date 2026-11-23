#include "core/joystick.h"

#include <M5Unified.h>

#include "core/stick_history.h"
#include "core/stick_map.h"

namespace joystick {

namespace {

constexpr uint8_t kAddr = 0x63;
constexpr uint8_t kRegAdc8 = 0x10;    // X then Y, 8-bit ADC, about 128 at rest
constexpr uint8_t kRegButton = 0x20;  // 0 = pressed, 1 = released
constexpr uint32_t kBusHz = 400000;
constexpr uint32_t kRetryMs = 200;    // while the unit does not answer, probe this rarely so the frame is not stalled
constexpr uint32_t kLostMs = 800;     // no good read for this long means the unit is gone

// How the unit sits on the device. Adjust these if the stick moves the wrong way on the real hardware.
constexpr bool kSwapAxes = true;       // the unit's first axis is the player's forward axis (measured: stick up -> axis 0 high)
constexpr bool kInvertRight = false;   // the unit's reading grows to the left
constexpr bool kInvertForward = false; // the unit's reading grows backwards

bool g_power = false;
bool g_busUp = false;
bool g_seeded = false;
uint32_t g_lastOkMs = 0;
uint32_t g_lastTryMs = 0;
bool g_everOk = false;
arcade::StickAxis g_axisA, g_axisB;
int8_t g_x = 0, g_y = 0;
uint8_t g_rawA = 0, g_rawB = 0;
bool g_held = false;
uint8_t g_count = 0;
arcade::StickHistory g_history;
int8_t g_jumpX = 0, g_jumpY = 0;  // direction latched for the latest press

}  // namespace

void setPower(bool on) {
    if (on == g_power) return;
    g_power = on;
    if (on) {
        if (!g_busUp) g_busUp = M5.Ex_I2C.begin();
        M5.Power.setExtOutput(true);
        g_seeded = false;
        g_lastTryMs = 0;
    } else {
        M5.Power.setExtOutput(false);
    }
    g_everOk = false;
    g_x = g_y = 0;
    g_held = false;
    g_history.clear();
}

bool powered() { return g_power; }

void update() {
    if (!g_power || !g_busUp) return;
    const uint32_t now = millis();
    const bool offline = !ok();
    if (offline && now - g_lastTryMs < kRetryMs) return;
    g_lastTryMs = now;

    uint8_t axes[2] = {};
    uint8_t button = 1;
    const bool good = M5.Ex_I2C.readRegister(kAddr, kRegAdc8, axes, 2, kBusHz) &&
                      M5.Ex_I2C.readRegister(kAddr, kRegButton, &button, 1, kBusHz);
    if (!good) {
        if (!ok()) { g_x = g_y = 0; g_held = false; }
        return;
    }
    g_lastOkMs = now;
    g_everOk = true;
    g_rawA = axes[0];
    g_rawB = axes[1];
    if (!g_seeded) {
        g_axisA.seed(axes[0]);
        g_axisB.seed(axes[1]);
        g_seeded = true;
    }
    int8_t a = g_axisA.map(axes[0]);
    int8_t b = g_axisB.map(axes[1]);
    int8_t right = kSwapAxes ? b : a;
    int8_t forward = kSwapAxes ? a : b;
    g_x = kInvertRight ? static_cast<int8_t>(-right) : right;
    g_y = kInvertForward ? static_cast<int8_t>(-forward) : forward;
    const bool held = button == 0;
    if (held && !g_held) {
        ++g_count;
        g_history.latch(now, g_jumpX, g_jumpY);
    }
    g_held = held;
    // The press itself nudges the stick, so the history only gets positions of an unpressed stick.
    if (!held) g_history.push(now, g_x, g_y);
}

bool ok() { return g_power && g_everOk && millis() - g_lastOkMs < kLostMs; }

int8_t x() { return g_x; }
int8_t y() { return g_y; }
bool buttonHeld() { return g_held; }
uint8_t pressCount() { return g_count; }
int8_t pressX() { return g_jumpX; }
int8_t pressY() { return g_jumpY; }
void raw(uint8_t& a, uint8_t& b) { a = g_rawA; b = g_rawB; }

}  // namespace joystick
