#include "game.h"

#include <math.h>

namespace {

constexpr float kTwoPi = 6.28318530718f;

struct ItemSpec {
    float vMin;
    float vMax;
    float radius;
    float wallRest;
    float obstacleRest;
};

constexpr ItemSpec kSpecs[] = {
    {0, 0, 0, 0, 0},                  // None
    {156, 360, 2.5f, 0.9f, 0.9f},     // Knife (x1.2)
    {150, 360, 5.0f, 0.6f, 0.7f},     // Axe (x1.5)
    {150, 345, 4.5f, 0.6f, 0.6f},     // Bomb (x1.5)
    {180, 360, 5.0f, 0.9f, 0.9f},     // Boomerang (x1.5)
};

const ItemSpec& specOf(ItemType t) { return kSpecs[static_cast<int>(t)]; }

float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

bool circleRect(float cx, float cy, float r, float l, float t, float rt, float b) {
    const float nx = clampf(cx, l, rt);
    const float ny = clampf(cy, t, b);
    const float dx = cx - nx;
    const float dy = cy - ny;
    return dx * dx + dy * dy <= r * r;
}

// Bounce off the left/right walls and the ceiling.
bool reflectWalls(Projectile& p, float r, float rest) {
    bool bounced = false;
    if (p.x < r) {
        p.x = r;
        if (p.vx < 0) { p.vx = -p.vx * rest; bounced = true; }
    } else if (p.x > cfg::SCREEN_W - r) {
        p.x = cfg::SCREEN_W - r;
        if (p.vx > 0) { p.vx = -p.vx * rest; bounced = true; }
    }
    if (p.y < r) {
        p.y = r;
        if (p.vy < 0) { p.vy = -p.vy * rest; bounced = true; }
    }
    return bounced;
}

}  // namespace

float Game::platformY(int i) const {
    if (!world.movePlatforms) return cfg::PLATFORM_CENTER_Y;
    return cfg::PLATFORM_CENTER_Y +
           cfg::MOTION_AMP * sinf(kTwoPi * time_ / cfg::MOTION_PERIOD_SEC + platPhase_[i]);
}

float Game::obstacleY(int i) const {
    if (!world.movePlatforms) return cfg::OBSTACLE_CENTER_Y;
    return cfg::OBSTACLE_CENTER_Y +
           cfg::OBSTACLE_AMP * sinf(kTwoPi * time_ / cfg::OBSTACLE_PERIOD_SEC + obsPhase_[i]);
}

float Game::obstacleVy(int i) const {
    if (!world.movePlatforms) return 0.0f;
    const float w = kTwoPi / cfg::OBSTACLE_PERIOD_SEC;
    return cfg::OBSTACLE_AMP * w * cosf(w * time_ + obsPhase_[i]);
}

float Game::handX(const Player& p) { return p.x + p.face * cfg::HAND_DX; }
float Game::handY(const Player& p) { return p.feetY - cfg::HAND_DY; }

float Game::projectileRadius(const Projectile& p) { return specOf(p.type).radius * p.scale; }

float Game::blastRadius(const Projectile& p) {
    const float elapsed = cfg::BOMB_BLAST_SEC - p.blastT;
    const float grow = clampf(elapsed / cfg::BOMB_BLAST_GROW_SEC, 0.0f, 1.0f);
    const float ease = 1.0f - (1.0f - grow) * (1.0f - grow);
    float r = cfg::BOMB_BLAST_R * (0.25f + 0.75f * ease);
    if (p.blastT < cfg::BOMB_BLAST_FADE_SEC) r *= clampf(p.blastT / cfg::BOMB_BLAST_FADE_SEC, 0.0f, 1.0f);
    return r * p.scale;
}

uint32_t Game::rand32() {
    rng_ ^= rng_ << 13;
    rng_ ^= rng_ >> 17;
    rng_ ^= rng_ << 5;
    return rng_;
}

float Game::randPhase() { return (rand32() % 6283u) / 1000.0f; }

ItemType Game::randomItem() { return static_cast<ItemType>(1 + rand32() % 4); }

void Game::begin(GameMode m, uint32_t seed) {
    mode = m;
    rng_ = seed ? seed : 0x9E3779B9u;
    winner = -1;
    time_ = 0.0f;
    for (auto& ph : platPhase_) ph = randPhase();
    for (auto& ph : obsPhase_) ph = randPhase();
    const float slot = (cfg::OBSTACLE_X_MAX - cfg::OBSTACLE_X_MIN) / cfg::OBSTACLE_COUNT;
    for (int i = 0; i < cfg::OBSTACLE_COUNT; ++i) {
        const float jitter = ((rand32() % 1000u) / 1000.0f - 0.5f) * slot * cfg::OBSTACLE_JITTER;
        obstacleXs[i] = cfg::OBSTACLE_X_MIN + slot * (i + 0.5f) + jitter;
    }
    for (int i = 0; i < 2; ++i) {
        players[i] = Player();
        players[i].x = cfg::PLAYER_X[i];
        players[i].dir = i == 0 ? 1 : -1;
        players[i].face = players[i].dir;
        players[i].feetY = platformY(i);
    }
    players[0].item = randomItem();
    players[1].item = (m == GameMode::Versus) ? randomItem() : ItemType::None;
    startRound(false);
    if (m == GameMode::Solo) players[1].itemUsed = true;  // the dummy never throws
}

float Game::turnLeft() const {
    const float left = cfg::TURN_TIMEOUT_SEC - turnTimer;
    return left > 0.0f ? left : 0.0f;
}

// Rounds: everybody throws once, the round ends when everybody has thrown and nothing is flying, and
// the items are refreshed for the next round.
void Game::startRound(bool refresh) {
    phase = Phase::Playing;
    phaseTimer = 0.0f;
    turnTimer = 0.0f;
    for (auto& p : proj) p = Projectile();
    for (auto& p : players) {
        p.hit = false;
        p.hitSources = 0;
        p.hitFlash = 0.0f;
        p.h = 0.0f;
        p.vh = 0.0f;
        p.charging = false;
        p.power = 0.0f;
        p.cooldown = 0.0f;
    }
    if (refresh) refreshItems();
    spawnPowerups();
}

void Game::spawnPowerups() {
    for (auto& p : powerups) p = Powerup();
    if (!world.powerups) return;
    const int count = rand32() % (cfg::MAX_POWERUPS + 1);
    int lanes[cfg::MAX_POWERUPS] = {0, 1, 2};
    for (int i = cfg::MAX_POWERUPS - 1; i > 0; --i) {
        const int j = rand32() % (i + 1);
        const int temp = lanes[i]; lanes[i] = lanes[j]; lanes[j] = temp;
    }
    for (int i = 0; i < count; ++i) {
        Powerup& p = powerups[i];
        p.type = static_cast<PowerupType>(1 + rand32() % 4);
        // Separate horizontal lanes keep icons apart and clear of both players.
        p.x = 78.0f + lanes[i] * 40.0f + (rand32() % 21);
        p.y = 22.0f + (rand32() % 61);
        if (world.obstacles) {
            for (int attempt = 0; attempt < 32; ++attempt) {
                bool clear = true;
                for (int j = 0; j < cfg::OBSTACLE_COUNT; ++j) {
                    const float dx = p.x - obstacleXs[j], dy = p.y - obstacleY(j);
                    if (dx * dx + dy * dy < 196.0f) clear = false;
                }
                if (clear) break;
                p.y = 22.0f + (rand32() % 61);
            }
        }
    }
}

void Game::collectPowerups(Projectile& p) {
    if (!world.powerups || phase != Phase::Playing || p.exploding) return;
    for (auto& item : powerups) {
        if (item.type == PowerupType::None) continue;
        const float dx = p.x - item.x, dy = p.y - item.y;
        const float r = projectileRadius(p) + cfg::POWERUP_R;
        if (dx * dx + dy * dy > r * r) continue;
        if (item.type == PowerupType::Split) {
            Projectile* child = nullptr;
            for (auto& slot : proj) if (!slot.active) { child = &slot; break; }
            if (!child) continue;  // do not consume the core unless both halves can exist
            *child = p;  // preserve type, effects, age, fuse and original owner
            const float c = cosf(cfg::SPLIT_ANGLE), s = sinf(cfg::SPLIT_ANGLE);
            const float vx = p.vx, vy = p.vy;
            p.vx = vx * c - vy * s; p.vy = vx * s + vy * c;
            child->vx = vx * c + vy * s; child->vy = -vx * s + vy * c;
            // Start the children just apart so their first contact isn't a mutual bounce.
            const float speed = sqrtf(vx * vx + vy * vy);
            const float nx = speed > 0.001f ? -vy / speed : 0.0f;
            const float ny = speed > 0.001f ? vx / speed : 1.0f;
            const float offset = projectileRadius(p) + 0.1f;
            p.x += nx * offset; p.y += ny * offset;
            child->x -= nx * offset; child->y -= ny * offset;
            if (p.type == ItemType::Knife) child->rot = atan2f(child->vy, child->vx);
        } else if (item.type == PowerupType::Burger) {
            p.scale *= cfg::BURGER_SCALE;
        } else {
            const float factor = item.type == PowerupType::Lightning ? cfg::LIGHTNING_SPEED : cfg::ICE_SPEED;
            p.vx *= factor; p.vy *= factor;
            p.speedScale *= factor;
        }
        item = Powerup();
    }
}

bool Game::isDraw() const {
    return mode == GameMode::Versus && phase != Phase::Playing &&
           players[0].score >= cfg::WIN_SCORE && players[1].score >= cfg::WIN_SCORE;
}

int Game::matchLoser() const {
    if (mode != GameMode::Versus) return -1;
    if (phase == Phase::MatchEnd) return winner >= 0 ? 1 - winner : -1;
    if (phase == Phase::RoundEnd) {
        const int s0 = players[0].score;
        const int s1 = players[1].score;
        if ((s0 >= cfg::WIN_SCORE || s1 >= cfg::WIN_SCORE) && s0 != s1) return s0 > s1 ? 1 : 0;
    }
    return -1;
}

void Game::refreshItems() {
    for (auto& p : players) {
        p.item = randomItem();
        p.itemUsed = false;
    }
    if (mode == GameMode::Solo) {
        players[1].item = ItemType::None;
        players[1].itemUsed = true;
    }
    ev_ |= EV_REFRESH;
}

bool Game::canFire(const Player& p) const {
    return p.item != ItemType::None && !p.itemUsed && p.cooldown <= 0.0f;
}

void Game::fire(int idx) {
    Player& p = players[idx];
    p.charging = false;

    Projectile* slot = nullptr;
    for (auto& s : proj) {
        if (!s.active) { slot = &s; break; }
    }
    if (!slot) return;

    const ItemSpec& spec = specOf(p.item);
    const float speed = spec.vMin + (spec.vMax - spec.vMin) * p.power;
    const float ang = p.aimDeg * 3.14159265f / 180.0f;

    *slot = Projectile();
    slot->active = true;
    slot->type = p.item;
    slot->owner = static_cast<uint8_t>(idx);
    slot->x = handX(p);
    slot->y = handY(p);
    slot->vx = p.dir * cosf(ang) * speed;
    slot->vy = -sinf(ang) * speed;

    p.cooldown = cfg::FIRE_COOLDOWN_SEC;
    ev_ |= EV_FIRE;
    p.itemUsed = true;
}

void Game::updatePlayer(int idx, const PlayerInput& in, float dt) {
    Player& p = players[idx];
    p.aimDeg = clampf(in.aimDeg, cfg::AIM_MIN_DEG, cfg::AIM_MAX_DEG);
    if (p.aimDeg > cfg::AIM_FLIP_UP_DEG) p.face = -p.dir;
    else if (p.aimDeg < cfg::AIM_FLIP_DOWN_DEG) p.face = p.dir;
    p.hitFlash = clampf(p.hitFlash - dt, 0.0f, 1.0f);
    if (p.cooldown > 0.0f) p.cooldown -= dt;

    const bool grounded = p.h <= 0.0f && p.vh <= 0.0f;
    if (grounded && in.jump && phase == Phase::Playing) {
        p.vh = cfg::JUMP_V;
        ev_ |= EV_JUMP;
    }
    if (p.vh > cfg::JUMP_CUT_V && !in.jumpHeld) p.vh = cfg::JUMP_CUT_V;
    if (p.h > 0.0f || p.vh > 0.0f) {
        p.vh -= cfg::JUMP_G * dt;
        p.h += p.vh * dt;
        if (p.h <= 0.0f) {
            p.h = 0.0f;
            p.vh = 0.0f;
        }
    }
    p.feetY = platformY(idx) - p.h;

    if (phase == Phase::Playing) {
        if (!p.charging && in.charge && !p.prevCharge && canFire(p)) {
            p.charging = true;
            p.power = 0.0f;
            p.powerDir = 1;
        }
        if (p.charging) {
            if (in.charge) {
                p.power += p.powerDir * dt / cfg::CHARGE_HALF_SEC;
                if (p.power >= 1.0f) { p.power = 1.0f; p.powerDir = -1; }
                if (p.power <= 0.0f) { p.power = 0.0f; p.powerDir = 1; }
            } else {
                fire(idx);
            }
        }
    } else {
        p.charging = false;
    }
    p.prevCharge = in.charge;
}

void Game::detonate(Projectile& p) {
    p.exploding = true;
    p.blastT = cfg::BOMB_BLAST_SEC;
    p.vx = 0;
    p.vy = 0;
    ev_ |= EV_EXPLODE;
}

void Game::checkHits(Projectile& p) {
    if (phase != Phase::Playing) return;  // damage after the round is settled does not count
    for (int i = 0; i < 2; ++i) {
        Player& pl = players[i];
        const uint8_t source = static_cast<uint8_t>(1u << p.owner);
        if (p.hitPlayers & (1u << i)) continue;
        if (p.type == ItemType::Bomb && !p.exploding) continue;  // a bomb only hurts through its blast
        if (!p.exploding && i == p.owner && p.life < cfg::OWNER_IMMUNE_SEC) continue;

        const float bottom = pl.feetY;
        const float top = bottom - cfg::PLAYER_H;
        const float radius = p.exploding ? blastRadius(p) : projectileRadius(p);
        if (!circleRect(p.x, p.y, radius, pl.x - cfg::PLAYER_HALF_W, top, pl.x + cfg::PLAYER_HALF_W, bottom))
            continue;

        pl.hit = true;
        pl.hitSources |= source;
        p.hitPlayers |= static_cast<uint8_t>(1u << i);
        ++pl.hitSerial;
        pl.hitFlash = 1.0f;
        if (mode == GameMode::Versus || i == 1) {
            Player& scorer = players[1 - i];
            if (mode == GameMode::Solo || scorer.score < cfg::WIN_SCORE) ++scorer.score;
        }
        pl.charging = false;
        ev_ |= EV_HIT;
        if (!p.exploding) {
            p.active = false;
            return;
        }
    }
}

bool Game::bounceOffObstacles(Projectile& p, float r, float rest) {
    if (!world.obstacles) return false;
    bool bounced = false;
    for (int i = 0; i < cfg::OBSTACLE_COUNT; ++i) {
        const float ox = obstacleXs[i];
        const float oy = obstacleY(i);
        const float minD = r + cfg::OBSTACLE_R;
        const float dx = p.x - ox;
        const float dy = p.y - oy;
        const float d2 = dx * dx + dy * dy;
        if (d2 >= minD * minD || d2 < 1e-6f) continue;

        const float d = sqrtf(d2);
        const float nx = dx / d;
        const float ny = dy / d;
        const float ovy = obstacleVy(i);
        const float rvx = p.vx;
        const float rvy = p.vy - ovy;
        const float vn = rvx * nx + rvy * ny;
        if (vn < 0.0f) {
            p.vx = rvx - (1.0f + rest) * vn * nx;
            p.vy = rvy - (1.0f + rest) * vn * ny + ovy;
            bounced = true;
        }
        p.x = ox + nx * minD;
        p.y = oy + ny * minD;
    }
    return bounced;
}

bool Game::bounceOffPlayers(Projectile& p, float r, float rest) {
    bool bounced = false;
    for (int i = 0; i < 2; ++i) {
        if (i == p.owner && p.life < cfg::OWNER_IMMUNE_SEC) continue;
        const Player& pl = players[i];
        const float left = pl.x - cfg::PLAYER_HALF_W;
        const float right = pl.x + cfg::PLAYER_HALF_W;
        const float bottom = pl.feetY;
        const float top = bottom - cfg::PLAYER_H;

        const float nearX = clampf(p.x, left, right);
        const float nearY = clampf(p.y, top, bottom);
        const float dx = p.x - nearX;
        const float dy = p.y - nearY;
        const float d2 = dx * dx + dy * dy;
        if (d2 >= r * r) continue;

        float nx = 0.0f;
        float ny = -1.0f;
        if (d2 > 1e-6f) {
            const float d = sqrtf(d2);
            nx = dx / d;
            ny = dy / d;
            p.x += nx * (r - d);
            p.y += ny * (r - d);
        } else {
            p.y = top - r;
        }
        const float vn = p.vx * nx + p.vy * ny;
        if (vn < 0.0f) {
            p.vx -= (1.0f + rest) * vn * nx;
            p.vy -= (1.0f + rest) * vn * ny;
            bounced = true;
        }
    }
    return bounced;
}

bool Game::collideWorld(Projectile& p, float r, float wallRest, float obstacleRest) {
    bool bounced = reflectWalls(p, r, wallRest);
    bounced = bounceOffObstacles(p, r, obstacleRest) || bounced;
    if (p.type == ItemType::Bomb) bounced = bounceOffPlayers(p, r, obstacleRest) || bounced;
    return bounced;
}

void Game::updateProjectile(Projectile& p, float dt) {
    if (!p.active) return;
    p.life += dt;

    if (p.exploding) {
        p.blastT -= dt;
        checkHits(p);
        if (p.blastT <= 0.0f) p.active = false;
        return;
    }

    const ItemSpec& spec = specOf(p.type);
    const float accelScale = p.speedScale * p.speedScale;

    switch (p.type) {
        case ItemType::Knife:
            p.x += p.vx * dt;
            p.y += p.vy * dt;
            break;
        case ItemType::Axe:
            p.vy += cfg::WORLD_G * accelScale * dt;
            p.x += p.vx * dt;
            p.y += p.vy * dt;
            p.rot += (p.vx >= 0 ? 14.0f : -14.0f) * dt;
            break;
        case ItemType::Bomb:
            p.vy += cfg::WORLD_G * accelScale * dt;
            p.x += p.vx * dt;
            p.y += p.vy * dt;
            p.rot += p.vx * dt * 0.1f;
            break;
        case ItemType::Boomerang: {
            const Player& owner = players[p.owner];
            const float dx = handX(owner) - p.x;
            const float dy = handY(owner) - p.y;
            const float len = sqrtf(dx * dx + dy * dy);
            if (len > 1.0f) {
                p.vx += dx / len * cfg::BOOMERANG_PULL * accelScale * dt;
                p.vy += dy / len * cfg::BOOMERANG_PULL * accelScale * dt;
            }
            p.x += p.vx * dt;
            p.y += p.vy * dt;
            p.rot += 18.0f * dt;
            break;
        }
        default:
            p.active = false;
            return;
    }

    collectPowerups(p);
    const float r = projectileRadius(p);
    if (collideWorld(p, r, spec.wallRest, spec.obstacleRest)) registerBounce(p);
    if (!p.active) return;
    if (p.type == ItemType::Knife) p.rot = atan2f(p.vy, p.vx);

    if (p.y + r >= cfg::LAVA_Y) {
        if (p.type == ItemType::Bomb) {
            p.y = cfg::LAVA_Y - 2.0f;
            detonate(p);
            checkHits(p);
        } else {
            p.active = false;
        }
        return;
    }

    if (p.type == ItemType::Bomb) {
        if (p.fuseOn) p.fuse -= dt;
        if ((p.fuseOn && p.fuse <= 0.0f) || p.life >= cfg::PROJ_LIFE_SEC) {
            detonate(p);
            checkHits(p);
            return;
        }
    } else if (p.life >= cfg::PROJ_LIFE_SEC) {
        p.active = false;
        return;
    }
    checkHits(p);
}

bool Game::anyProjectile() const {
    for (const auto& p : proj) {
        if (p.active) return true;
    }
    return false;
}

void Game::registerBounce(Projectile& p) {
    ev_ |= EV_BOUNCE;
    if (p.type == ItemType::Bomb) {
        if (!p.fuseOn) {
            p.fuseOn = true;
            p.fuse = cfg::BOMB_FUSE_SEC;
        }
    } else if (++p.bounces > cfg::MAX_BOUNCES) {
        p.active = false;
    }
}

void Game::collideProjectiles() {
    for (int i = 0; i < cfg::MAX_PROJ; ++i) {
        Projectile& a = proj[i];
        if (!a.active || a.exploding) continue;
        for (int j = i + 1; j < cfg::MAX_PROJ && a.active; ++j) {
            Projectile& b = proj[j];
            if (!b.active || b.exploding) continue;
            const float radius = projectileRadius(a) + projectileRadius(b);
            const float dx = b.x - a.x, dy = b.y - a.y;
            const float d2 = dx * dx + dy * dy;
            if (d2 >= radius * radius) continue;
            const float d = sqrtf(d2);
            const float nx = d > 0.0001f ? dx / d : 1.0f;
            const float ny = d > 0.0001f ? dy / d : 0.0f;
            const float overlap = (radius - d + 0.01f) * 0.5f;
            a.x -= nx * overlap; a.y -= ny * overlap;
            b.x += nx * overlap; b.y += ny * overlap;
            const float vn = (b.vx - a.vx) * nx + (b.vy - a.vy) * ny;
            if (vn >= 0.0f) continue;  // separating contacts must not bounce again
            const float impulse = -(1.0f + 0.8f) * vn * 0.5f;
            a.vx -= impulse * nx; a.vy -= impulse * ny;
            b.vx += impulse * nx; b.vy += impulse * ny;
            registerBounce(a);
            registerBounce(b);
            if (a.type == ItemType::Knife) a.rot = atan2f(a.vy, a.vx);
            if (b.type == ItemType::Knife) b.rot = atan2f(b.vy, b.vx);
        }
    }
}

void Game::finishRound() {
    phase = Phase::RoundEnd;
    phaseTimer = cfg::ROUND_END_SEC;
    ev_ |= EV_ROUND_END;
}

uint32_t Game::update(float dt, const PlayerInput in[2]) {
    ev_ = 0;
    if (phase == Phase::MatchEnd) return 0;

    time_ += dt;
    for (int i = 0; i < 2; ++i) updatePlayer(i, in[i], dt);
    // Bound travel to two pixels per substep, including fast head-on knives.
    float maxSpeed = 0.0f;
    float maxAccelScale = 1.0f;
    for (const auto& p : proj) {
        if (p.active && !p.exploding) {
            const float speed = sqrtf(p.vx * p.vx + p.vy * p.vy);
            if (speed > maxSpeed) maxSpeed = speed;
            if (p.speedScale * p.speedScale > maxAccelScale) maxAccelScale = p.speedScale * p.speedScale;
        }
    }
    // Account for collecting up to three speed boosts during this update too.
    float maxBoost = 1.0f;
    if (world.powerups) for (const auto& item : powerups) {
        if (item.type == PowerupType::Lightning) maxBoost *= cfg::LIGHTNING_SPEED;
    }
    const int steps = 1 + static_cast<int>((maxSpeed * maxBoost +
        (cfg::WORLD_G + cfg::BOOMERANG_PULL) * maxAccelScale * maxBoost * maxBoost * dt) * dt / 2.0f);
    for (int step = 0; step < steps; ++step) {
        for (auto& p : proj) updateProjectile(p, dt / steps);
        collideProjectiles();
    }

    if (phase == Phase::Playing) {
        if (mode == GameMode::Versus) {
            turnTimer += dt;
            if (turnTimer >= cfg::TURN_TIMEOUT_SEC) {
                for (auto& p : players) {
                    if (p.itemUsed) continue;
                    p.itemUsed = true;
                    p.charging = false;
                }
            }
        }
        const bool allThrown = players[0].itemUsed && players[1].itemUsed;
        const bool won = mode == GameMode::Versus &&
                         (players[0].score >= cfg::WIN_SCORE || players[1].score >= cfg::WIN_SCORE);
        if (won || (allThrown && !anyProjectile())) finishRound();
    } else if (phase == Phase::RoundEnd) {
        phaseTimer -= dt;
        if (phaseTimer <= 0.0f) {
            const int s0 = players[0].score;
            const int s1 = players[1].score;
            if (mode == GameMode::Versus && (s0 >= cfg::WIN_SCORE || s1 >= cfg::WIN_SCORE)) {
                winner = s0 == s1 ? -1 : (s0 > s1 ? 0 : 1);
                phase = Phase::MatchEnd;
                ev_ |= EV_WIN;
            } else {
                startRound(true);
            }
        }
    }
    return ev_;
}
