#include "games/paddle/paddle.h"

#include <math.h>
#include <new>

using arcade::MatchPhase;
using arcade::Mode;
using arcade::q16;
using arcade::q8;

namespace {

constexpr float kStartSpeed = 110.0f;
constexpr float kMaxSpeed = 260.0f;
constexpr float kSpeedUp = 1.06f;
constexpr float kPaddleSpeed = 320.0f;  // px/s the paddle may move

float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

}  // namespace

uint32_t PaddleGame::rand32() {
    rng_ ^= rng_ << 13;
    rng_ ^= rng_ >> 17;
    rng_ ^= rng_ << 5;
    return rng_;
}

float PaddleGame::paddleTarget(float aim) {
    const float f = clampf(aim / kAimRange, -1.0f, 1.0f);  // +1 = tilted up
    const float half = kPaddleH / 2.0f;
    return (kFieldH / 2.0f) - f * (kFieldH / 2.0f - half);
}

void PaddleGame::begin(Mode mode, uint32_t seed) {
    solo = mode == Mode::Solo;
    rng_ = seed ? seed : 0x9E3779B9u;
    score[0] = score[1] = 0;
    rally = 0;
    winner = -1;
    paddleY[0] = paddleY[1] = kFieldH / 2.0f;
    serveDir_ = (rand32() & 1) ? 1 : -1;
    serve();
}

void PaddleGame::serve() {
    phase_ = MatchPhase::RoundEnd;
    serveTimer = kServeSec;
    bx = kFieldW / 2.0f;
    by = kFieldH / 2.0f;
    bvx = bvy = 0.0f;
}

void PaddleGame::localInput(int local, const PadInput& in) { paddleY[local] = paddleTarget(in.aim); }

uint32_t PaddleGame::step(float dt, const PadInput in[2]) {
    uint32_t ev = 0;
    if (phase_ == MatchPhase::MatchEnd) return 0;

    for (int i = 0; i < 2; ++i) {
        if (solo && i == 1) continue;
        const float target = paddleTarget(in[i].aim);
        const float maxMove = kPaddleSpeed * dt;
        paddleY[i] += clampf(target - paddleY[i], -maxMove, maxMove);
    }

    if (phase_ == MatchPhase::RoundEnd) {
        serveTimer -= dt;
        if (serveTimer <= 0.0f) {
            phase_ = MatchPhase::Playing;
            bvx = serveDir_ * kStartSpeed;
            bvy = (static_cast<int>(rand32() % 100) - 50) * 0.9f;
            ev |= EV_SERVE;
        }
        return ev;
    }

    bx += bvx * dt;
    by += bvy * dt;
    if (by < kBallR) { by = kBallR; bvy = -bvy; ev |= EV_WALL; }
    if (by > kFieldH - kBallR) { by = kFieldH - kBallR; bvy = -bvy; ev |= EV_WALL; }

    const float half = kPaddleH / 2.0f;
    const float leftFace = kPaddleX[0] + kPaddleW / 2.0f;
    const float rightFace = kPaddleX[1] - kPaddleW / 2.0f;
    auto bounce = [&](int who, float face, float dir) {
        bx = face + dir * kBallR;
        bvx = -bvx * kSpeedUp;
        if (fabsf(bvx) > kMaxSpeed) bvx = bvx < 0 ? -kMaxSpeed : kMaxSpeed;
        bvy += (by - paddleY[who]) / half * 90.0f;
        bvy = clampf(bvy, -kMaxSpeed, kMaxSpeed);
        ev |= EV_PADDLE;
    };
    if (bvx < 0 && bx - kBallR <= leftFace && bx > kPaddleX[0] - kPaddleW && fabsf(by - paddleY[0]) <= half + kBallR) {
        bounce(0, leftFace, 1.0f);
        if (solo && rally < 255) ++rally;
    } else if (bvx > 0 && bx + kBallR >= rightFace) {
        if (solo) {
            bx = rightFace - kBallR;  // the wall
            bvx = -bvx;
            ev |= EV_WALL;
        } else if (bx < kPaddleX[1] + kPaddleW && fabsf(by - paddleY[1]) <= half + kBallR) {
            bounce(1, rightFace, -1.0f);
        }
    }

    if (bx < -kBallR) {
        if (solo) {
            if (rally > score[0]) score[0] = rally;
            rally = 0;
        } else {
            ++score[1];
        }
        serveDir_ = 1;
        ev |= EV_SCORE;
        serve();
    } else if (bx > kFieldW + kBallR) {
        ++score[0];
        serveDir_ = -1;
        ev |= EV_SCORE;
        serve();
    }

    if (!solo && (score[0] >= kWinScore || score[1] >= kWinScore)) {
        winner = score[0] > score[1] ? 0 : 1;
        phase_ = MatchPhase::MatchEnd;
        ev |= EV_WIN;
    }
    return ev;
}

void PaddleGame::encode(arcade::Writer& w) const {
    w.u8(static_cast<uint8_t>(phase_));
    w.u8(static_cast<uint8_t>(winner));
    w.u8(q8(serveTimer, 100.0f));
    w.u8(score[0]);
    w.u8(score[1]);
    w.u8(rally);
    w.i16(q16(paddleY[0], 16.0f));
    w.i16(q16(paddleY[1], 16.0f));
    w.i16(q16(bx, 16.0f));
    w.i16(q16(by, 16.0f));
    w.i16(q16(bvx, 8.0f));
    w.i16(q16(bvy, 8.0f));
}

bool PaddleGame::decode(arcade::Reader& r) {
    const uint8_t phase = r.u8();
    const int8_t win = static_cast<int8_t>(r.u8());
    const uint8_t timer = r.u8();
    const uint8_t s0 = r.u8();
    const uint8_t s1 = r.u8();
    const uint8_t rl = r.u8();
    const int16_t y0 = r.i16();
    const int16_t y1 = r.i16();
    const int16_t x = r.i16();
    const int16_t y = r.i16();
    const int16_t vx = r.i16();
    const int16_t vy = r.i16();
    if (!r.ok || phase > static_cast<uint8_t>(MatchPhase::MatchEnd)) return false;

    phase_ = static_cast<MatchPhase>(phase);
    winner = win;
    serveTimer = timer / 100.0f;
    score[0] = s0;
    score[1] = s1;
    rally = rl;
    paddleY[0] = y0 / 16.0f;
    paddleY[1] = y1 / 16.0f;
    bx = x / 16.0f;
    by = y / 16.0f;
    bvx = vx / 8.0f;
    bvy = vy / 8.0f;
    return true;
}

namespace {

arcade::IGame* create(void* mem, arcade::Arena&) { return new (mem) PaddleGame(); }

}  // namespace

const arcade::GameInfo kPaddleInfo = {"paddle", 2, 1, sizeof(PaddleGame), 0, create, 2};
