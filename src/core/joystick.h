#pragma once

#include <stdint.h>

// M5Stack Unit Joystick2 on the Grove port (I2C 0x63). Required by games with GameInfo::needsJoystick.
// The unit needs the 5V output of the Grove port, which M5Unified leaves off; setPower() switches it.
namespace joystick {

void setPower(bool on);
bool powered();

// Polls the unit; call once per loop (input::update does). Does nothing while unpowered.
void update();

// A read succeeded recently. False while unpowered, while the unit is missing, or after it was unplugged.
bool ok();

// Stick position in the player's frame: x positive = right, y positive = forward. -100..100, 0 at rest.
int8_t x();
int8_t y();

bool buttonHeld();
uint8_t pressCount();  // stick presses so far (wraps)
// Stick direction to attach to the latest press: the position just before it (-100..100, same frame as x()/y()).
int8_t pressX();
int8_t pressY();

// Raw 8-bit readings of the unit's two axes, for diagnostics.
void raw(uint8_t& a, uint8_t& b);

}  // namespace joystick
