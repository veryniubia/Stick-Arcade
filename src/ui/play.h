#pragma once

#include "games/registry.h"

// Everything that happens after a game card was chosen: the waiting screen (searching for an opponent, B = solo),
// solo play, online play with its READY/countdown/link-lost screens, and the shake-to-exit confirmation.
namespace play {

void begin(const registry::GameEntry& entry);
bool update(float dt);  // returns true when the user should be sent back to the launcher
void draw();
void end();

#ifdef DEBUG_REMOTE
void debugLine();  // prints one status line on the serial port
bool debugKey(char key);
#endif

}  // namespace play
