#pragma once

#include <stdint.h>

#ifdef DEBUG_REMOTE
#include <Arduino.h>
#endif

// Development-only serial remote (build with -DDEBUG_REMOTE). In a release build everything here is a no-op.
// Keys: v = open the first game (online lobby), g = open it and go solo, o = press B, a = press A, x = shake twice,
// m = back to the launcher, f = charge A for 0.45 s, j = press B, u/b/n = force aim up/backward/off, s = screenshot,
// l/k/L = drop 20% / 0% / 100% of received packets, 1-4 and i = game specific (Throwhalla: pick/next item).
namespace debug {

#ifdef DEBUG_REMOTE

struct State {
    uint32_t chargeUntil = 0;
    float aim = -1000.0f;
    uint8_t extraB = 0;
    bool aPress = false;
    bool bPress = false;
    bool shake = false;
    bool shot = false;
    int lossPct = 0;
};
inline State g;

inline float aim(float imu) { return g.aim > -999.0f ? g.aim : imu; }
inline bool charging() { return millis() < g.chargeUntil; }
inline uint8_t extraB() { return g.extraB; }
inline bool aPress() { const bool v = g.aPress; g.aPress = false; return v; }
inline bool bPress() { const bool v = g.bPress; g.bPress = false; return v; }
inline bool shake() { const bool v = g.shake; g.shake = false; return v; }

#else

inline float aim(float imu) { return imu; }
inline bool charging() { return false; }
inline uint8_t extraB() { return 0; }
inline bool aPress() { return false; }
inline bool bPress() { return false; }
inline bool shake() { return false; }

#endif

}  // namespace debug
