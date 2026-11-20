#pragma once

#include <stdint.h>

// Online versus screen: owns the radio, the net::Session and everything drawn while it runs.
namespace versus {

struct Input {
    float aimDeg = 0.0f;
    bool charge = false;
    bool jump = false;      // B pressed this frame
    bool jumpHeld = false;
    bool again = false;     // A pressed this frame
    bool exitHeld = false;  // A+B held
};

void begin();
// Returns true when the user should be sent back to the menu.
bool update(float dt, const Input& in);
void draw();
void end();

#ifdef DEBUG_REMOTE
void debugLine();               // prints one status line on the serial port
void debugSetRxLoss(int pct);   // drops this percentage of received packets
#endif

}  // namespace versus
