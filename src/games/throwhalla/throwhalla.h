#pragma once

#include "core/game_iface.h"
#include "games/throwhalla/game.h"

// Throwhalla: Vikings throw knives, axes, bombs and boomerangs across a lava pit.
// Platform adapter around the rules in game.h/game.cpp. draw() and sfx() live in throwhalla_view.cpp.
class ThrowhallaGame final : public arcade::IGame {
public:
    void begin(arcade::Mode mode, uint32_t seed) override;
    uint32_t step(float dt, const PadInput in[2]) override;
    void prime(const PadInput in[2]) override;
    arcade::MatchPhase phase() const override;
    void encode(arcade::Writer& w) const override;
    bool decode(arcade::Reader& r) override;
    void localInput(int local, const PadInput& in) override;
    bool idle(int local) const override;
    void draw(int local, const arcade::DrawCtx& ctx) override;
    void sfx(uint32_t events) override;
#ifdef DEBUG_REMOTE
    bool debugKey(char key) override;
#endif

    Game& core() { return g_; }
    const Game& core() const { return g_; }

private:
    Game g_;
    bool primed_[2] = {false, false};
    uint8_t seenB_[2] = {0, 0};
};

extern const arcade::GameInfo kThrowhallaInfo;
