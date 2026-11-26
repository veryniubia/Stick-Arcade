#pragma once
#include "core/game_iface.h"

namespace springring {
constexpr int kMat = 111, kPadY = 107, kPadLeft = 107, kPadRight = 133;
struct Point { float x, y; };
struct Leg {
    float length = 0, reach = 0; // extension in [0,1], steering in screen pixels
    bool spent = false, planted = false;
};
struct Clinch {
    // A locked two-arm grip fixes both torsos to one rotating frame. A contact
    // at either player's boot therefore acts on the mass and inertia of both.
    float x = 120, y = 91, angle = 0, vx = 0, vy = 0, spin = 0;
    Leg legs[2];
    Point joint(float side, float down) const;
    Point hip(int i) const;
    Point head(int i) const;
    Point boot(int i) const;
};
class Match : public arcade::IGame {
public:
    enum Event { Kick = 1, Spring = 2, Impact = 4 };
    Clinch pair;
    uint8_t score[2] = {}, hitMask = 0, freeze = 0, flash = 0;
    void begin(arcade::Mode, uint32_t) override;
    uint32_t step(float, const PadInput[2]) override;
    arcade::MatchPhase phase() const override;
    bool idle(int) const override { return hitMask != 0; }
    void encode(arcade::Writer&) const override;
    bool decode(arcade::Reader&) override;
    void draw(int, const arcade::DrawCtx&) override;
    void sfx(uint32_t) override;
private:
    arcade::Mode mode_ = arcade::Mode::Solo;
};
void drawCard(int, int, int, int, uint32_t);
void drawLobby(uint32_t);
}
extern const arcade::GameInfo kSpringRingInfo;
