#pragma once

#include <stdint.h>

#include "config.h"

enum class ItemType : uint8_t { None, Knife, Axe, Bomb, Boomerang };
enum class GameMode : uint8_t { Training, Versus };
enum class Phase : uint8_t { Playing, RoundEnd, MatchEnd };

enum GameEvent : uint32_t {
    EV_FIRE = 1u << 0,
    EV_JUMP = 1u << 1,
    EV_HIT = 1u << 2,
    EV_BOUNCE = 1u << 3,
    EV_EXPLODE = 1u << 4,
    EV_ROUND_END = 1u << 5,
    EV_REFRESH = 1u << 6,
    EV_WIN = 1u << 7,
};

struct PlayerInput {
    float aimDeg = 0.0f;
    bool charge = false;    // fire button held
    bool jump = false;      // jump button pressed this tick
    bool jumpHeld = false;  // jump button still held
};

struct Player {
    float x = 0.0f;
    float h = 0.0f;      // height above the platform
    float vh = 0.0f;
    float feetY = 0.0f;  // absolute screen y of the feet
    int8_t dir = 1;      // +1 faces right, -1 faces left
    ItemType item = ItemType::None;
    bool itemUsed = false;
    bool charging = false;
    bool prevCharge = false;
    float power = 0.0f;
    int8_t powerDir = 1;
    float aimDeg = 0.0f;
    int8_t face = 1;     // current facing: equals dir, or -dir while aiming backward
    float cooldown = 0.0f;
    bool hit = false;
    float hitFlash = 0.0f;
    uint8_t score = 0;
};

struct Projectile {
    bool active = false;
    ItemType type = ItemType::None;
    uint8_t owner = 0;
    float x = 0, y = 0, vx = 0, vy = 0;
    float rot = 0.0f;
    float life = 0.0f;
    int bounces = 0;
    bool fuseOn = false;
    float fuse = 0.0f;
    bool exploding = false;
    float blastT = 0.0f;
};

struct WorldOptions {
    bool movePlatforms = true;
    bool obstacles = true;
};

class Game {
public:
    void begin(GameMode mode, uint32_t seed);
    uint32_t update(float dt, const PlayerInput in[2]);

    GameMode mode = GameMode::Training;
    Phase phase = Phase::Playing;
    WorldOptions world;
    Player players[2];
    Projectile proj[cfg::MAX_PROJ];
    int winner = -1;
    float phaseTimer = 0.0f;

    // World clock; online clients copy it from the host so platforms and obstacles line up.
    float clock() const { return time_; }
    void setClock(float t) { time_ = t; }

    float platformY(int i) const;
    float obstacleY(int i) const;
    float obstacleVy(int i) const;
    float obstacleXs[cfg::OBSTACLE_COUNT] = {};

    static float handX(const Player& p);
    static float handY(const Player& p);
    static float blastRadius(const Projectile& p);

    // Index of the player who has lost the match (the 5th point is already decided), or -1.
    int matchLoser() const;

private:
    uint32_t rng_ = 1;
    uint32_t ev_ = 0;
    float time_ = 0.0f;
    bool suddenDeath_ = false;  // 4:4 in versus: the first damage decides the match
    float platPhase_[2] = {0, 0};
    float obsPhase_[cfg::OBSTACLE_COUNT] = {};

    uint32_t rand32();
    float randPhase();
    ItemType randomItem();
    void startRound(bool refresh);
    void refreshItems();
    bool canFire(const Player& p) const;
    void fire(int idx);
    void updatePlayer(int idx, const PlayerInput& in, float dt);
    void updateProjectile(Projectile& p, float dt);
    bool collideWorld(Projectile& p, float radius, float wallRest, float obstacleRest);
    bool bounceOffObstacles(Projectile& p, float radius, float rest);
    bool bounceOffPlayers(Projectile& p, float radius, float rest);
    void detonate(Projectile& p);
    void checkHits(Projectile& p);
    bool anyHit() const;
    bool anyProjectile() const;
    void finishRound();
};
