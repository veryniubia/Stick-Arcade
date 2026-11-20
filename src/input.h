#pragma once

namespace input {

void begin();
void update();  // call right after M5.update()

// Elevation of the screen's right edge above the horizontal plane, in degrees (filtered).
// Aim angle in degrees relative to the facing direction (0 = forward, 90 = up, 180 = backward).
float aimDeg();
bool imuOk();

bool aHeld();
bool aPressed();
bool bHeld();
bool bPressed();

}  // namespace input
