#pragma once
#include "core/game_iface.h"

// Feet coordinates, in the 320x180 world, rendered at 0.75 scale. Rules never touch hardware.
class SamuraiDuelGame final : public arcade::IGame {
public:
    static constexpr float kRoundPause = 1.5f;
    static constexpr float kFloor = 140, kCeiling = 42;
    static constexpr float kLeft = 8, kRight = 312;
    static constexpr float kShotSpeed = 290, kShotWindup = .16f, kShotCooldown = .64f;
    static constexpr float kSlashDuration = .36f, kSlashWindup = .05f;
    static constexpr float kSlashFollowThrough = .19f, kSlashReach = 21;
    static constexpr float kThrustDuration = .54f, kThrustRecovery = .14f;
    static constexpr float kThrustTravelTime = .12f, kThrustDistance = 24, kThrustReach = 26;
    static constexpr float kBurstInterval = .14f;
    static constexpr int kBulletCount = 12, kWinningScore = 5;
    static constexpr float kWorldWidth = 320, kWorldHeight = 180;
    struct Platform { float left, right, y; };
    static const Platform platforms[3];
    struct Fighter {
        float x = 0, y = kFloor, vx = 0, vy = 0;
        float cooldown = 0, action = 0, recoil = 0;
        float jumpBuffer = 0, attackBuffer = 0;
        float shotFx = 0, parryFx = 0, downTime = 0;
        int8_t facing = 1, fallDirection = 1;
        bool grounded = true, struck = false, thrust = false, down = false;
    };
    struct Bullet {
        float x = 0, y = 0;
        int8_t direction = 1;
        uint8_t owner = 0;
        bool active = false, reflected = false;
    };
    enum Event : uint32_t { Shot = 1, Slash = 2, Parry = 4, Hurt = 8, Jump = 16,
                            Round = 32, Finish = 64, Pickup = 128 };
    Fighter fighters[2];
    Bullet bullets[kBulletCount];
    uint32_t score[2] = {};
    uint8_t boostRounds[2] = {};
    bool burgerActive = false;
    float burgerX = 0, burgerY = 0;
    uint8_t swordPlayer = 0;
    int8_t winner = -1, roundWinner = -1;
    bool solo = false;
    uint32_t seed = 1;
    float elapsed = 0, roundTime = 0, phaseTimer = 0;
    float spark = 0, sparkX = 0, sparkY = 0, hitStop = 0;
    arcade::MatchPhase phase_ = arcade::MatchPhase::Playing;

    void begin(arcade::Mode, uint32_t) override;
    void prime(const PadInput[2]) override;
    uint32_t step(float, const PadInput[2]) override;
    arcade::MatchPhase phase() const override { return phase_; }
    bool idle(int) const override { return phase_ == arcade::MatchPhase::MatchEnd; }
    void encode(arcade::Writer&) const override;
    bool decode(arcade::Reader&) override;
    void draw(int, const arcade::DrawCtx&) override;
    void sfx(uint32_t) override;
    bool slashActive(int i) const;
    float attackDuration(int i) const { return fighters[i].thrust ? kThrustDuration : kSlashDuration; }
    float attackRecovery(int i) const { return fighters[i].thrust ? kThrustRecovery : kSlashFollowThrough; }
    float attackReach(int i) const { return fighters[i].thrust ? kThrustReach : kSlashReach; }
    bool isSword(int i) const { return i == swordPlayer; }
#ifdef DEBUG_REMOTE
    bool debugKey(char) override;
#endif
private:
    arcade::Mode mode_ = arcade::Mode::Solo;
    uint8_t lastA_[2] = {}, lastB_[2] = {};
    bool heldA_[2] = {}, heldB_[2] = {};
    int viewLocal_ = 0;
    uint32_t burgerRandom_ = 0;
    uint8_t shotsLeft_ = 0;
    void startRound();
    uint32_t collectBurger();
    uint32_t move(int, const PadInput&, float);
    uint32_t projectiles(float);
    uint32_t damage(int, int);
    uint32_t finishRound(int);
};
extern const arcade::GameInfo kSamuraiDuelInfo;
