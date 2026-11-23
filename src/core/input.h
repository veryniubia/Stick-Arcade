#pragma once

#include <stdint.h>

// Buttons and the motion sensor. Call update() once per loop right after M5.update().
namespace input {

void begin();
void update();

// Device tilt in degrees: elevation of the screen's right edge above the horizontal plane, filtered,
// relative to the calibrated level pose.
float tiltDeg();
float driveTiltDeg();             // fixed-reference screen-plane rotation; holds last angle near horizontal
float rawTiltDeg();               // same without the calibration offset
void setTiltOffset(float deg);    // pose that counts as level
bool imuOk();

bool aHeld();
bool aPressed();    // went down since the previous update()
bool bHeld();
bool bPressed();
uint8_t aCount();   // total presses so far (wraps); games compare it to see "pressed since last step"
uint8_t bCount();

// True once after the device was shaken twice in a row (the exit gesture). Cleared by reading.
bool shaken();

// Gyro view control for games that need the joystick. beginLook() starts the integration (see GyroLook::configure
// for the gains and limits); keep the device still for a moment afterwards so the gyro bias can settle. Pressing
// the joystick recentres the view.
void beginLook(float yawGainCenter, float yawGainEdge, float pitchGainCenter, float pitchGainEdge,
               float yawLimit, float pitchLimit);
void endLook();
void recenterLook();
float lookYawDeg();    // positive = right
float lookPitchDeg();  // positive = up

}  // namespace input
