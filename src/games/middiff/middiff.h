#pragma once
#include "core/game_iface.h"
#include "core/pad.h"

// MidDiff: a first-person sniper duel across a walled mid-map. World units are metres, z runs between the two
// teams and x across. Team 0 (T) holds the -z half and faces +z, team 1 (CT) holds the +z half and faces -z; which
// player is which team swaps on every rematch. "Forward" and "right" in the input are always relative to the
// player, never to where the view points. Rules never touch hardware.
//
//   z=+15  end wall, double doors
//   z=+11  low cover (CT)           <- far: ~24 m to the other team's low cover
//   z=+6.2 high cover (CT)          <- near: ~13 m
//   z= 0   mid wall: left doorway with an iron lift gate, arch in the centre, right doorway filled with barrels
class MidDiffGame final : public arcade::IGame {
public:
    // Arena. Each team walks in |x| <= kHalfWidth, kNearZ <= depth <= kFarZ on its own side; the mid area is closed.
    static constexpr float kArenaHalfWidth = 6.0f, kWallZ = 15.0f, kWallHeight = 4.0f;
    static constexpr float kHalfWidth = 5.0f, kNearZ = 6.2f, kFarZ = 14.3f, kStartDepth = 8.5f;
    static constexpr float kHighCover = 1.5f, kLowCover = 1.1f, kGateHeight = 2.9f;

    // Body. Nobody can duck: the head is always over the high cover and the chest over the low one.
    static constexpr float kStandHeight = 1.8f, kEye = 1.65f;
    static constexpr float kBodyRadius = 0.42f, kHeadRadius = 0.25f;  // generous for gyro aiming
    static constexpr float kCollideRadius = 0.36f;
    static constexpr float kWalkSpeed = 3.6f;

    // The jump is a short hop in the direction the stick pointed when it was pressed. It costs a third of the
    // stamina, which refills a third per second.
    static constexpr float kJumpTime = 0.55f, kJumpDistance = 2.5f, kJumpHeight = 0.7f;
    static constexpr float kJumpCost = 1.0f / 3.0f, kStaminaRegen = 1.0f / 3.0f;
    static constexpr float kJumpMinStick = 0.35f;  // below this the press is a hop on the spot

    // Combat. Shots are instant; a head shot kills.
    static constexpr float kShotCooldown = 1.1f, kShotBuffer = 0.12f, kSpawnCooldown = 0.3f;
    static constexpr int kMaxHp = 100, kBodyDamage = 50, kHeadDamage = 100;
    static constexpr float kMaxRange = 40.0f;
    static constexpr float kYawLimit = pad::kLookYawLimit, kPitchLimit = pad::kLookPitchLimit;

    // A kill scores a point; the dead respawn at random after kRespawnTime and cannot be hurt for kSpawnInvuln
    // (firing ends that early). First to kWinningScore with a lead wins; there are no rounds.
    static constexpr float kRespawnTime = 1.5f, kSpawnInvuln = 1.5f;
    static constexpr int kWinningScore = 5;

    // The lift gate moves for kGateMoveTime, then waits a random time. It rattles kGateWarn before it moves.
    static constexpr float kGateMoveTime = 0.9f, kGateWarn = 0.6f;
    static constexpr float kGateClosedMin = 2.5f, kGateClosedMax = 7.0f, kGateOpenMin = 2.0f, kGateOpenMax = 5.0f;

    static constexpr float kBurgerWaitMin = 8.0f, kBurgerWaitMax = 15.0f;
    static constexpr float kBurgerFuse = 2.0f, kBlastRadius = 2.0f;
    static constexpr float kBurgerRadius = 0.26f, kGrenadeRadius = 0.15f;
    static constexpr float kThrowSpeed = 18.0f, kGravity = 9.8f;
    enum class BurgerState : uint8_t { Waiting, Pickup, Held, Flying, Blast };
    struct Burger {
        BurgerState state = BurgerState::Waiting;
        uint8_t owner = 0;
        float timer = 8, x = 0, y = 0, z = 0;
        float vx = 0, vy = 0, vz = 0; // host physics only
    } burger;
    bool holdingBurger(int i) const { return burger.state == BurgerState::Held && burger.owner == i; }

    enum Event : uint32_t {
        ShotP0 = 1, ShotP1 = 2,    // a shot was fired
        HurtP0 = 4, HurtP1 = 8,    // that player took damage
        DieP0 = 16, DieP1 = 32,    // that player died (with phase MatchEnd it was the last point)
        Headshot = 64,             // accompanies HurtP0/HurtP1
        BurgerGet = 256, BurgerThrow = 512, BurgerBoom = 1024, BurgerTick = 2048,
        GateMove = 128,            // the gate is about to move
    };

    enum class Kind : uint8_t { Gate, Stone, Lintel, Barrel, HighCover, LowCover, Crate, Sandbag };
    // Everything solid in the arena, in world coordinates. Boxes kWalkFirst..kWalkEnd-1 are the covers the teams
    // walk around; the others cannot be reached but stop bullets and sight. The gate (index 0) is lifted at run time.
    struct Box { float x0, x1, y0, y1, z0, z1; Kind kind; };
    static constexpr int kBoxCount = 23, kWalkFirst = 11, kWalkEnd = 19;
    static const Box boxes[kBoxCount];

    enum class GateState : uint8_t { Closed, Opening, Open, Closing };
    struct Gate {
        GateState state = GateState::Closed;
        float pos = 0;      // 0 closed .. 1 fully lifted
        float timer = 3;    // seconds left of the wait or of the movement
        bool warned = false;
    };

    struct Player {
        float x = 0, z = 0;
        float yaw = 0, pitch = 0;      // view in degrees: yaw positive = right, pitch positive = up
        float stamina = 1;
        float cooldown = 0;
        float flash = 0;               // seconds left of the muzzle flash / tracer
        float hurt = 0;                // seconds left of the damage flash
        float marker = 0;              // seconds left of the hit marker (this player's shot landed)
        float dead = 0;                // > 0 while dead: seconds until the respawn
        float invuln = 0;              // > 0 while spawn protected
        float jumpT = 0;               // > 0 while airborne: seconds left
        float jumpVX = 0, jumpVZ = 0;  // hop velocity (host only)
        float shotX = 0, shotY = 0, shotZ = 0;  // where this player's last shot ended
        uint8_t hp = kMaxHp;
        bool markerHead = false, markerShield = false;
    };

    struct Trace {
        bool hit = false, head = false, covered = false;
        float t = 0;                   // distance to the end of the ray
        float x = 0, y = 0, z = 0;     // the end of the ray
    };

    Player players[2];
    uint8_t score[2] = {};
    int8_t winner = -1;
    bool solo = false;
    uint8_t swap_ = 0;                 // 1: player 0 is CT, player 1 is T
    uint32_t seed = 1;
    float elapsed = 0;
    Gate gate;
    arcade::MatchPhase phase_ = arcade::MatchPhase::Playing;

    void begin(arcade::Mode, uint32_t) override;
    void prime(const PadInput[2]) override;
    uint32_t step(float, const PadInput[2]) override;
    arcade::MatchPhase phase() const override { return phase_; }
    bool idle(int i) const override { return phase_ != arcade::MatchPhase::Playing || players[i].dead > 0; }
    void encode(arcade::Writer&) const override;
    bool decode(arcade::Reader&) override;
    void localInput(int, const PadInput&) override;
    void draw(int, const arcade::DrawCtx&) override;
    void sfx(uint32_t) override;
#ifdef DEBUG_REMOTE
    bool debugKey(char) override;
#endif

    int team(int i) const { return (i ^ swap_) & 1; }
    // Direction player i faces: +1 for team 0 (towards +z), -1 for team 1.
    float face(int i) const { return team(i) == 0 ? 1.0f : -1.0f; }
    static float jumpHeight(const Player& p);
    float gateLift() const;                   // 0..1 eased
    float boxBottom(int index) const;         // the lifted gate leaves room underneath
    // The view the screen should show for player i. The client shows its own sensor immediately.
    float lookYaw(int i) const { return (mode_ == arcade::Mode::Client && i == localIndex_ && haveLocal_) ? localYaw_ : players[i].yaw; }
    float lookPitch(int i) const { return (mode_ == arcade::Mode::Client && i == localIndex_ && haveLocal_) ? localPitch_ : players[i].pitch; }

    // Fires a ray from player i's eye along their view and reports what it ends on.
    Trace trace(int shooter) const;
    // True if nothing solid is between the two points.
    bool segmentClear(float ox, float oy, float oz, float tx, float ty, float tz) const;
    // Is (x, z) inside a walkable cover, grown by `margin`?
    static bool blockedAt(float x, float z, float margin);

private:
    arcade::Mode mode_ = arcade::Mode::Solo;
    uint8_t lastA_[2] = {}, lastJump_[2] = {};
    float fireBuffer_[2] = {};
    uint32_t rng_ = 1;
    int localIndex_ = 0;
    bool haveLocal_ = false;
    float localYaw_ = 0, localPitch_ = 0;

    float random01();
    float randomBetween(float lo, float hi) { return lo + (hi - lo) * random01(); }
    void respawn(int);
    void updateLife(int, float);
    uint32_t updateGate(float);
    uint32_t updateBurger(float);
    void throwBurger(int);
    bool act(int, const PadInput&, float);
    void moveBy(int, float, float);
    uint32_t resolveShots(const bool fired[2]);
    uint32_t kill(int victim, int killer, bool head);
};
extern const arcade::GameInfo kMidDiffInfo;
