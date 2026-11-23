#pragma once

#include <stdint.h>

// What one player's hands are doing. Every game receives this, whether the player is local, remote or absent.
// Key convention shared by all games: A = main action (confirm, charge, fire), B = secondary (next, jump).
struct PadInput {
    float aim = 0.0f;       // device tilt in degrees times the AIM setting (not clamped)
    bool a = false;         // A held
    bool b = false;         // B held
    uint8_t aCount = 0;     // number of A presses so far (wraps); a changed count means "pressed since last step"
    uint8_t bCount = 0;     // same for B

    // Only filled for games that need the joystick (GameInfo::needsJoystick); zero for every other game.
    // There `aim` is the view yaw in degrees (positive = right) and `pitch` the view pitch (positive = up),
    // both already multiplied by the AIM setting and limited by the platform.
    float pitch = 0.0f;
    int8_t stickX = 0;      // -100..100, positive = right of the player
    int8_t stickY = 0;      // -100..100, positive = forward (away from the player)
    uint8_t jumpCount = 0;  // joystick presses so far (wraps); a changed count means "pressed since last step"
    int8_t jumpX = 0;       // stick direction latched for that press (read just before the press, which nudges the stick)
    int8_t jumpY = 0;
};

namespace pad {
// The view range the platform allows for games that use PadInput::aim as yaw and PadInput::pitch (degrees, +/-).
constexpr float kLookYawLimit = 90.0f;
constexpr float kLookPitchLimit = 30.0f;
}  // namespace pad
