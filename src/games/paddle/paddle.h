#pragma once

#include "core/game_iface.h"

// PADDLE: two players tilt their devices to move a paddle and keep the ball in play; first to 5 points wins.
// Solo: the right side is a wall and the score is the best rally. A small game that also serves as the
// reference for the platform interface (see the README, "Adding a game").
class PaddleGame final : public arcade::IGame {
public:
    static constexpr int kFieldW = 240;
    static constexpr int kFieldH = 135;
    static constexpr float kPaddleH = 28.0f;
    static constexpr float kPaddleW = 5.0f;
    static constexpr float kPaddleX[2] = {10.0f, 230.0f};  // centres
    static constexpr float kBallR = 3.0f;
    static constexpr int kWinScore = 5;
    static constexpr float kServeSec = 1.2f;
    static constexpr float kAimRange = 60.0f;  // aim of +-60 degrees moves the paddle over the whole field

    enum Event : uint32_t { EV_PADDLE = 1, EV_WALL = 2, EV_SCORE = 4, EV_WIN = 8, EV_SERVE = 16 };

    void begin(arcade::Mode mode, uint32_t seed) override;
    uint32_t step(float dt, const PadInput in[2]) override;
    arcade::MatchPhase phase() const override { return phase_; }
    void encode(arcade::Writer& w) const override;
    bool decode(arcade::Reader& r) override;
    void localInput(int local, const PadInput& in) override;
    bool idle(int) const override { return phase_ != arcade::MatchPhase::Playing; }
    void draw(int local, const arcade::DrawCtx& ctx) override;
    void sfx(uint32_t events) override;

    // State is public: the tests and the renderer read it.
    float paddleY[2] = {kFieldH / 2.0f, kFieldH / 2.0f};
    float bx = kFieldW / 2.0f, by = kFieldH / 2.0f, bvx = 0, bvy = 0;
    uint8_t score[2] = {0, 0};
    uint8_t rally = 0;
    int8_t winner = -1;
    arcade::MatchPhase phase_ = arcade::MatchPhase::RoundEnd;  // RoundEnd while waiting to serve
    float serveTimer = kServeSec;
    bool solo = false;

private:
    uint32_t rng_ = 1;
    int serveDir_ = 1;
    uint32_t rand32();
    void serve();
    static float paddleTarget(float aim);
};

extern const arcade::GameInfo kPaddleInfo;
