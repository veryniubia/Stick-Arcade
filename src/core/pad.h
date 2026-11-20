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
};
