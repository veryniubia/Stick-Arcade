#pragma once
#include "core/game_iface.h"

// MidDiff: a sniper duel across a mid-map gap. World units are metres. Player 0 stands on the -z side and faces +z,
// player 1 on the +z side and faces -z. Positions are world coordinates; "forward" and "right" below are always
// relative to the player (the joystick never depends on where the view points). Rules never touch hardware.
class MidDiffGame final : public arcade::IGame {
public:
    // Arena. Each player has a zone of |x| <= kHalfWidth and kNearZ <= |z| <= kFarZ on their own side. The near edge is
    // the line nobody may cross; the low cover stands just beyond it, so the two players are always 11.5 m or more apart.
    static constexpr float kHalfWidth = 3.5f;
    static constexpr float kNearZ = 5.75f, kFarZ = 9.5f;
    static constexpr float kArenaHalfWidth = 4.5f, kWallZ = 10.0f;
    static constexpr float kCoverNearZ = 5.0f, kCoverFarZ = 5.4f, kCoverHalfWidth = 1.3f, kCoverHeight = 1.15f;
    static constexpr float kSpawnZ = 7.5f;

    // Body. A crouching player is 1.0 m tall, which hides the head behind the 1.15 m cover; a standing one shows the
    // head and the top of the torso above it. Crouched, you cannot see over your own cover either: stand up or step aside to shoot.
    static constexpr float kStandHeight = 1.8f, kCrouchHeight = 1.0f;
    static constexpr float kStandEye = 1.65f, kCrouchEye = 0.95f;
    static constexpr float kBodyRadius = 0.42f, kHeadRadius = 0.25f;  // a little generous for gyro aiming
    static constexpr float kWalkSpeed = 2.6f, kCrouchSpeed = 1.3f;
    static constexpr float kCrouchRate = 8.0f;  // crouch amount per second

    // Crouching drains stamina; with none left the player is forced up and may not crouch again until recovered.
    static constexpr float kStaminaDrain = 0.34f, kStaminaRegen = 0.30f, kRegenDelay = 0.5f, kWindedRecover = 0.3f;

    // Combat. Shots are instant; a head shot kills.
    static constexpr float kShotCooldown = 1.1f, kShotBuffer = 0.12f, kRoundStartCooldown = 0.5f;
    static constexpr int kMaxHp = 100, kBodyDamage = 50, kHeadDamage = 100;
    static constexpr float kMaxRange = 40.0f;
    static constexpr float kYawLimit = pad::kLookYawLimit, kPitchLimit = pad::kLookPitchLimit;

    static constexpr float kRoundPause = 1.6f, kRoundTime = 45.0f;
    static constexpr int kWinningScore = 3;

    enum Event : uint32_t {
        ShotP0 = 1, ShotP1 = 2,    // a shot was fired
        HurtP0 = 4, HurtP1 = 8,    // that player took damage
        Round = 16, Finish = 32,
        Headshot = 64,             // accompanies HurtP0/HurtP1
    };

    struct Player {
        float x = 0, z = 0;
        float yaw = 0, pitch = 0;      // view in degrees: yaw positive = right, pitch positive = up
        float crouch = 0;              // 0 standing .. 1 fully down
        float stamina = 1;
        float cooldown = 0, regenWait = 0;
        float flash = 0;               // seconds left of the muzzle flash / tracer
        float hurt = 0;                // seconds left of the damage flash
        float marker = 0;              // seconds left of the hit marker (this player's shot landed)
        float shotX = 0, shotY = 0, shotZ = 0;  // where this player's last shot ended
        uint8_t hp = kMaxHp;
        bool winded = false, ducking = false, markerHead = false;
    };

    struct Trace {
        bool hit = false, head = false, covered = false;
        float t = 0;                   // distance to the end of the ray
        float x = 0, y = 0, z = 0;     // the end of the ray
    };

    Player players[2];
    uint8_t score[2] = {};
    int8_t winner = -1, roundWinner = -1;
    bool solo = false;
    uint32_t seed = 1;
    float elapsed = 0, roundTime = 0, phaseTimer = 0;
    arcade::MatchPhase phase_ = arcade::MatchPhase::Playing;

    void begin(arcade::Mode, uint32_t) override;
    void prime(const PadInput[2]) override;
    uint32_t step(float, const PadInput[2]) override;
    arcade::MatchPhase phase() const override { return phase_; }
    bool idle(int) const override { return phase_ != arcade::MatchPhase::Playing; }
    void encode(arcade::Writer&) const override;
    bool decode(arcade::Reader&) override;
    void localInput(int, const PadInput&) override;
    void draw(int, const arcade::DrawCtx&) override;
    void sfx(uint32_t) override;
#ifdef DEBUG_REMOTE
    bool debugKey(char) override;
#endif

    // Direction a player faces: +1 for player 0 (towards +z), -1 for player 1.
    static float facing(int i) { return i == 0 ? 1.0f : -1.0f; }
    static float bodyHeight(float crouch) { return kStandHeight + (kCrouchHeight - kStandHeight) * crouch; }
    static float eyeHeight(float crouch) { return kStandEye + (kCrouchEye - kStandEye) * crouch; }
    // The view the screen should show for player i. The client shows its own sensor immediately.
    float lookYaw(int i) const { return (mode_ == arcade::Mode::Client && i == localIndex_ && haveLocal_) ? localYaw_ : players[i].yaw; }
    float lookPitch(int i) const { return (mode_ == arcade::Mode::Client && i == localIndex_ && haveLocal_) ? localPitch_ : players[i].pitch; }

    // Fires a ray from player i's eye along their view and reports what it ends on.
    Trace trace(int shooter) const;

private:
    arcade::Mode mode_ = arcade::Mode::Solo;
    uint8_t lastA_[2] = {};
    float fireBuffer_[2] = {};
    uint32_t rng_ = 1;
    int localIndex_ = 0;
    bool haveLocal_ = false;
    float localYaw_ = 0, localPitch_ = 0;

    void startRound();
    // Movement, stance, aim and the trigger of one player. Returns true when the player fires this step.
    bool act(int, const PadInput&, float);
    uint32_t resolveShots(const bool fired[2]);
    uint32_t finishRound(int);
};
extern const arcade::GameInfo kMidDiffInfo;
