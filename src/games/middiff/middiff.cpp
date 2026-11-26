#include "games/middiff/middiff.h"
#include <algorithm>
#include <cmath>
#include <new>

namespace {

constexpr float kDeg = 0.017453292f;

// By-value helpers: the firmware builds as C++11, where binding a constexpr static member to std::min's reference
// parameter needs an out-of-class definition.
float clampf(float x, float lo, float hi) { return x < lo ? lo : (x > hi ? hi : x); }
float minf(float a, float b) { return a < b ? a : b; }
float maxf(float a, float b) { return a > b ? a : b; }
void decay(float& t, float dt) { t = t > dt ? t - dt : 0.f; }
arcade::IGame* create(void* mem, arcade::Arena&) { return new (mem) MidDiffGame(); }

bool hitSphere(const float o[3], const float d[3], float cx, float cy, float cz, float r, float& t) {
    const float lx = o[0] - cx, ly = o[1] - cy, lz = o[2] - cz;
    const float b = lx * d[0] + ly * d[1] + lz * d[2];
    const float c = lx * lx + ly * ly + lz * lz - r * r;
    const float disc = b * b - c;
    if (disc < 0) return false;
    const float s = std::sqrt(disc);
    float t0 = -b - s;
    if (t0 < 0) t0 = -b + s;
    if (t0 < 0) return false;
    t = t0;
    return true;
}

// Vertical cylinder between heights y0 and y1.
bool hitCylinder(const float o[3], const float d[3], float cx, float cz, float r, float y0, float y1, float& t) {
    const float a = d[0] * d[0] + d[2] * d[2];
    if (a < 1e-9f) return false;
    const float lx = o[0] - cx, lz = o[2] - cz;
    const float b = lx * d[0] + lz * d[2];
    const float c = lx * lx + lz * lz - r * r;
    const float disc = b * b - a * c;
    if (disc < 0) return false;
    const float t0 = (-b - std::sqrt(disc)) / a;
    if (t0 < 0) return false;
    const float y = o[1] + d[1] * t0;
    if (y < y0 || y > y1) return false;
    t = t0;
    return true;
}

bool hitBox(const float o[3], const float d[3], const float lo[3], const float hi[3], float& t) {
    float tMin = 0, tMax = 1e9f;
    for (int k = 0; k < 3; ++k) {
        if (std::fabs(d[k]) < 1e-9f) {
            if (o[k] < lo[k] || o[k] > hi[k]) return false;
            continue;
        }
        float t0 = (lo[k] - o[k]) / d[k], t1 = (hi[k] - o[k]) / d[k];
        if (t0 > t1) std::swap(t0, t1);
        tMin = maxf(tMin, t0);
        tMax = minf(tMax, t1);
        if (tMin > tMax) return false;
    }
    t = tMin;
    return true;
}

}  // namespace

const arcade::GameInfo kMidDiffInfo = {"middiff", 6, 5, sizeof(MidDiffGame), 0, create, 2, false, true, true};

using K = MidDiffGame::Kind;
// Mid wall (0..10): a doorway on each side of the centre arch, 4 m high, doorways 2.9 m. Left doorway: iron lift gate.
// Right doorway: barrels (one stack of two). Then the covers of each half, laid out a little differently.
const MidDiffGame::Box MidDiffGame::boxes[MidDiffGame::kBoxCount] = {
    /* 0 */ {-4.6f, -2.0f, 0.0f, 2.9f, -0.15f, 0.15f, K::Gate},
    /* 1 */ {-6.0f, -4.6f, 0.0f, 4.0f, -0.45f, 0.45f, K::Stone},
    /* 2 */ {-2.0f, -1.0f, 0.0f, 4.0f, -0.45f, 0.45f, K::Stone},
    /* 3 */ {1.0f, 2.0f, 0.0f, 4.0f, -0.45f, 0.45f, K::Stone},
    /* 4 */ {4.6f, 6.0f, 0.0f, 4.0f, -0.45f, 0.45f, K::Stone},
    /* 5 */ {-4.6f, -2.0f, 2.9f, 4.0f, -0.45f, 0.45f, K::Lintel},
    /* 6 */ {-1.0f, 1.0f, 2.9f, 4.0f, -0.45f, 0.45f, K::Lintel},
    /* 7 */ {2.0f, 4.6f, 2.9f, 4.0f, -0.45f, 0.45f, K::Lintel},
    /* 8 */ {2.4f, 3.04f, 0.0f, 2.0f, -0.32f, 0.32f, K::Barrel},
    /* 9 */ {3.18f, 3.82f, 0.0f, 1.0f, -0.32f, 0.32f, K::Barrel},
    /* 10 */ {3.9f, 4.54f, 0.0f, 1.0f, -0.32f, 0.32f, K::Barrel},
    // Team 0 (z < 0): two 3 m high walls with a 3 m gap, a long low wall further back, a crate on the right.
    /* 11 */ {-4.8f, -1.8f, 0.0f, 1.5f, -6.8f, -6.2f, K::HighCover},
    /* 12 */ {1.2f, 4.2f, 0.0f, 1.5f, -6.8f, -6.2f, K::HighCover},
    /* 13 */ {-3.6f, 1.4f, 0.0f, 1.1f, -11.7f, -11.1f, K::LowCover},
    /* 14 */ {2.8f, 4.0f, 0.0f, 1.1f, -10.0f, -9.4f, K::Crate},
    // Team 1 (z > 0): one wide and one short high wall, two low walls with a gap between them.
    /* 15 */ {-4.6f, -0.8f, 0.0f, 1.5f, 6.2f, 6.8f, K::HighCover},
    /* 16 */ {2.2f, 3.8f, 0.0f, 1.5f, 6.2f, 6.8f, K::HighCover},
    /* 17 */ {-4.2f, -1.0f, 0.0f, 1.1f, 11.1f, 11.7f, K::LowCover},
    /* 18 */ {0.4f, 3.4f, 0.0f, 1.1f, 11.1f, 11.7f, K::LowCover},
    // Scenery in the closed middle: crate stacks and sandbags that hide legs and hips.
    /* 19 */ {-5.6f, -4.2f, 0.0f, 1.4f, -3.6f, -2.2f, K::Crate},
    /* 20 */ {0.6f, 3.0f, 0.0f, 0.7f, -3.0f, -2.4f, K::Sandbag},
    /* 21 */ {4.2f, 5.6f, 0.0f, 1.4f, 2.2f, 3.6f, K::Crate},
    /* 22 */ {-3.0f, -0.6f, 0.0f, 0.7f, 2.4f, 3.0f, K::Sandbag},
};

float MidDiffGame::jumpHeight(const Player& p) {
    if (p.jumpT <= 0) return 0.0f;
    const float s = 1.0f - p.jumpT / kJumpTime;
    return kJumpHeight * 4.0f * s * (1.0f - s);
}

float MidDiffGame::gateLift() const {
    const float p = gate.pos;
    return p * p * (3.0f - 2.0f * p);
}

float MidDiffGame::boxBottom(int index) const {
    return boxes[index].kind == Kind::Gate ? kGateHeight * gateLift() : boxes[index].y0;
}

bool MidDiffGame::blockedAt(float x, float z, float margin) {
    for (int i = kWalkFirst; i < kWalkEnd; ++i) {
        const Box& b = boxes[i];
        if (x > b.x0 - margin && x < b.x1 + margin && z > b.z0 - margin && z < b.z1 + margin) return true;
    }
    return false;
}

float MidDiffGame::random01() {
    rng_ = rng_ * 1664525u + 1013904223u;
    return static_cast<float>((rng_ >> 8) & 0xFFFFFF) / 16777216.0f;
}

void MidDiffGame::begin(arcade::Mode mode, uint32_t s) {
    const bool rematch = mode == arcade::Mode::Host && mode_ == arcade::Mode::Host && phase_ == arcade::MatchPhase::MatchEnd;
    mode_ = mode;
    heardBlast_ = false;
    solo = mode == arcade::Mode::Solo;
    seed = s;
    rng_ = s ? s : 1;
    // Teams swap on every rematch; a solo run starts on a random team.
    swap_ = solo ? static_cast<uint8_t>((s >> 1) & 1) : rematch ? static_cast<uint8_t>(swap_ ^ 1) : 0;
    score[0] = score[1] = 0;
    winner = -1;
    elapsed = 0;
    haveLocal_ = false;
    for (int i = 0; i < 2; ++i) { lastA_[i] = lastJump_[i] = 0; fireBuffer_[i] = 0; }
    players[0] = Player();
    players[1] = Player();
    burger = Burger();
    gate = Gate();
    gate.timer = randomBetween(kGateClosedMin, kGateClosedMax);
    respawn(0);
    respawn(1);
    phase_ = arcade::MatchPhase::Playing;
}

void MidDiffGame::prime(const PadInput p[2]) {
    for (int i = 0; i < 2; ++i) {
        const PadInput in = solo && i == 1 ? PadInput() : p[i];
        lastA_[i] = in.aCount;
        lastJump_[i] = in.jumpCount;
    }
}

// Random spot in the team's own half that is clear of the covers. The solo target also has to be in plain sight of
// the player, so practice is never spent hunting for it behind a wall.
void MidDiffGame::respawn(int i) {
    if (burger.state != BurgerState::Flying && burger.state != BurgerState::Blast) {
        burger = Burger();
        burger.timer = randomBetween(kBurgerWaitMin, kBurgerWaitMax);
    }
    const float side = face(i) > 0 ? -1.0f : 1.0f;
    float px = 0.0f, pz = side * kStartDepth;
    for (int k = 0; k < 32; ++k) {
        const float x = randomBetween(-kHalfWidth, kHalfWidth);
        const float z = side * randomBetween(kNearZ + 0.4f, kFarZ);
        if (blockedAt(x, z, kCollideRadius + 0.25f)) continue;
        if (solo && i == 1 &&
            !segmentClear(players[0].x, kEye + jumpHeight(players[0]), players[0].z, x, kStandHeight - 0.2f, z)) continue;
        px = x;
        pz = z;
        break;
    }
    Player& p = players[i];
    const float yaw = p.yaw, pitch = p.pitch;
    p = Player();
    p.x = px;
    p.z = pz;
    p.yaw = yaw;
    p.pitch = pitch;
    p.invuln = kSpawnInvuln;
    p.cooldown = kSpawnCooldown;
    fireBuffer_[i] = 0;
}

void MidDiffGame::updateLife(int i, float dt) {
    Player& p = players[i];
    if (p.dead <= 0) return;
    p.dead -= dt;
    if (p.dead <= 0) respawn(i);
}

uint32_t MidDiffGame::updateGate(float dt) {
    uint32_t events = 0;
    switch (gate.state) {
        case GateState::Closed:
        case GateState::Open:
            gate.timer -= dt;
            if (!gate.warned && gate.timer <= kGateWarn) {
                gate.warned = true;
                events |= GateMove;
            }
            if (gate.timer <= 0) {
                gate.state = gate.state == GateState::Closed ? GateState::Opening : GateState::Closing;
                gate.timer = kGateMoveTime;
                gate.warned = false;
            }
            break;
        case GateState::Opening:
            gate.timer -= dt;
            gate.pos = 1.0f - maxf(0.0f, gate.timer) / kGateMoveTime;
            if (gate.timer <= 0) {
                gate.pos = 1.0f;
                gate.state = GateState::Open;
                gate.timer = randomBetween(kGateOpenMin, kGateOpenMax);
            }
            break;
        case GateState::Closing:
            gate.timer -= dt;
            gate.pos = maxf(0.0f, gate.timer) / kGateMoveTime;
            if (gate.timer <= 0) {
                gate.pos = 0.0f;
                gate.state = GateState::Closed;
                gate.timer = randomBetween(kGateClosedMin, kGateClosedMax);
            }
            break;
    }
    return events;
}

void MidDiffGame::moveBy(int i, float dx, float dz) {
    Player& me = players[i];
    const float side = face(i) > 0 ? -1.0f : 1.0f;
    const float nx = clampf(me.x + dx, -kHalfWidth, kHalfWidth);
    if (!blockedAt(nx, me.z, kCollideRadius)) me.x = nx;
    const float depth = clampf(std::fabs(me.z + dz), kNearZ, kFarZ);
    const float nz = side * depth;
    if (!blockedAt(me.x, nz, kCollideRadius)) me.z = nz;
}

bool MidDiffGame::act(int i, const PadInput& p, float dt) {
    Player& me = players[i];
    const bool pressA = p.aCount != lastA_[i];
    const bool pressJump = p.jumpCount != lastJump_[i];
    lastA_[i] = p.aCount;
    lastJump_[i] = p.jumpCount;
    if (me.dead > 0) {
        fireBuffer_[i] = 0;
        return false;
    }
    decay(me.cooldown, dt);
    decay(me.invuln, dt);
    decay(fireBuffer_[i], dt);
    if (pressA) fireBuffer_[i] = kShotBuffer;

    me.yaw = clampf(std::isfinite(p.aim) ? p.aim : 0.0f, -kYawLimit, kYawLimit);
    me.pitch = clampf(std::isfinite(p.pitch) ? p.pitch : 0.0f, -kPitchLimit, kPitchLimit);
    me.stamina = minf(1.0f, me.stamina + kStaminaRegen * dt);

    const float f = face(i);
    if (pressJump && me.jumpT <= 0 && me.stamina >= kJumpCost - 1e-3f) {
        me.stamina = maxf(0.0f, me.stamina - kJumpCost);
        me.jumpT = kJumpTime;
        const float jx = clampf(static_cast<float>(p.jumpX), -100.0f, 100.0f) / 100.0f;
        const float jy = clampf(static_cast<float>(p.jumpY), -100.0f, 100.0f) / 100.0f;
        const float mag = std::sqrt(jx * jx + jy * jy);
        if (mag > kJumpMinStick) {
            const float k = kJumpDistance / kJumpTime / mag;
            me.jumpVX = jx * k * f;
            me.jumpVZ = jy * k * f;
        } else {
            me.jumpVX = me.jumpVZ = 0.0f;
        }
    }

    if (me.jumpT > 0) {
        moveBy(i, me.jumpVX * dt, me.jumpVZ * dt);
        decay(me.jumpT, dt);
    } else {
        // The stick moves the player in their own frame whatever way the view points.
        float sx = clampf(static_cast<float>(p.stickX), -100.0f, 100.0f) / 100.0f;
        float sy = clampf(static_cast<float>(p.stickY), -100.0f, 100.0f) / 100.0f;
        const float mag = std::sqrt(sx * sx + sy * sy);
        if (mag > 1.0f) { sx /= mag; sy /= mag; }
        moveBy(i, sx * kWalkSpeed * dt * f, sy * kWalkSpeed * dt * f);
    }

    if (fireBuffer_[i] > 0 && me.cooldown <= 0) {
        fireBuffer_[i] = 0;
        me.cooldown = kShotCooldown;
        me.invuln = 0;  // firing gives the protection up
        return true;
    }
    return false;
}

MidDiffGame::Trace MidDiffGame::trace(int s) const {
    const Player& me = players[s];
    const Player& foe = players[1 - s];
    const float f = face(s);
    const float yaw = me.yaw * kDeg, pitch = me.pitch * kDeg;
    const float o[3] = {me.x, kEye + jumpHeight(me), me.z};
    const float d[3] = {std::sin(yaw) * std::cos(pitch) * f, std::sin(pitch), std::cos(yaw) * std::cos(pitch) * f};

    float tWorld = kMaxRange;
    bool covered = false;
    if (d[1] < -1e-6f) tWorld = minf(tWorld, -o[1] / d[1]);
    if (d[0] > 1e-6f) tWorld = minf(tWorld, (kArenaHalfWidth - o[0]) / d[0]);
    if (d[0] < -1e-6f) tWorld = minf(tWorld, (-kArenaHalfWidth - o[0]) / d[0]);
    if (d[2] > 1e-6f) tWorld = minf(tWorld, (kWallZ - o[2]) / d[2]);
    if (d[2] < -1e-6f) tWorld = minf(tWorld, (-kWallZ - o[2]) / d[2]);
    for (int i = 0; i < kBoxCount; ++i) {
        const Box& b = boxes[i];
        const float y0 = boxBottom(i);
        if (y0 >= b.y1 - 1e-4f) continue;  // a fully lifted gate is out of the way
        const float lo[3] = {b.x0, y0, b.z0}, hi[3] = {b.x1, b.y1, b.z1};
        float t;
        if (hitBox(o, d, lo, hi, t) && t < tWorld) {
            tWorld = t;
            covered = true;
        }
    }

    Trace r;
    if (foe.dead <= 0) {
        const float jy = jumpHeight(foe);
        float tHead = 1e9f, tBody = 1e9f;
        const bool head = hitSphere(o, d, foe.x, jy + kStandHeight - 0.2f, foe.z, kHeadRadius, tHead);
        const bool body = hitCylinder(o, d, foe.x, foe.z, kBodyRadius, jy, jy + kStandHeight - 0.35f, tBody);
        const float tTarget = minf(head ? tHead : 1e9f, body ? tBody : 1e9f);
        if ((head || body) && tTarget < tWorld) {
            r.hit = true;
            r.head = head && tHead <= tBody;
            r.t = tTarget;
        }
    }
    if (!r.hit) {
        r.t = tWorld;
        r.covered = covered;
    }
    r.x = o[0] + d[0] * r.t;
    r.y = o[1] + d[1] * r.t;
    r.z = o[2] + d[2] * r.t;
    return r;
}

bool MidDiffGame::segmentClear(float ox, float oy, float oz, float tx, float ty, float tz) const {
    float d[3] = {tx - ox, ty - oy, tz - oz};
    const float len = std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
    if (len < 1e-4f) return true;
    for (float& v : d) v /= len;
    const float o[3] = {ox, oy, oz};
    for (int i = 0; i < kBoxCount; ++i) {
        const Box& b = boxes[i];
        const float y0 = boxBottom(i);
        if (y0 >= b.y1 - 1e-4f) continue;
        const float lo[3] = {b.x0, y0, b.z0}, hi[3] = {b.x1, b.y1, b.z1};
        float t;
        if (hitBox(o, d, lo, hi, t) && t < len - 0.02f) return false;
    }
    return true;
}

uint32_t MidDiffGame::kill(int victim, int killer, bool head) {
    (void)head;
    Player& v = players[victim];
    if (holdingBurger(victim)) { burger.state = BurgerState::Waiting; burger.timer = randomBetween(kBurgerWaitMin, kBurgerWaitMax); }
    v.hp = 0;
    v.dead = kRespawnTime;
    v.jumpT = 0;
    v.hurt = 0.35f;
    fireBuffer_[victim] = 0;
    if (score[killer] < 255) ++score[killer];
    return victim ? DieP1 : DieP0;
}

uint32_t MidDiffGame::resolveShots(const bool fired[2]) {
    uint32_t events = 0;
    int damage[2] = {};
    bool head[2] = {};
    // Both shots are traced against the positions of this step before anyone loses health: a double hit is a double hit.
    for (int i = 0; i < 2; ++i) {
        if (!fired[i]) continue;
        if (holdingBurger(i)) { throwBurger(i); events |= BurgerThrow; continue; }
        const Trace t = trace(i);
        Player& me = players[i];
        me.flash = 0.12f;
        me.shotX = t.x;
        me.shotY = t.y;
        me.shotZ = t.z;
        events |= i ? ShotP1 : ShotP0;
        if (burger.state == BurgerState::Pickup) {
            const float o[3] = {me.x, kEye + jumpHeight(me), me.z};
            const float yaw = me.yaw * kDeg, pitch = me.pitch * kDeg;
            const float d[3] = {std::sin(yaw)*std::cos(pitch)*face(i), std::sin(pitch), std::cos(yaw)*std::cos(pitch)*face(i)};
            float distance;
            if (hitSphere(o, d, burger.x, burger.y, burger.z, kBurgerRadius, distance) && distance < t.t) {
                burger.state = BurgerState::Held; burger.owner = i; burger.timer = 0;
                me.shotX = o[0]+d[0]*distance; me.shotY = o[1]+d[1]*distance; me.shotZ = o[2]+d[2]*distance;
                me.marker = .25f; me.markerHead = me.markerShield = false;
                events |= BurgerGet;
                continue;
            }
        }
        if (!t.hit) continue;
        me.marker = 0.25f;
        me.markerHead = t.head;
        me.markerShield = players[1 - i].invuln > 0;
        if (me.markerShield) continue;  // the shot is absorbed
        damage[1 - i] += t.head ? kHeadDamage : kBodyDamage;
        head[1 - i] |= t.head;
    }
    for (int j = 0; j < 2; ++j) {
        if (!damage[j]) continue;
        Player& v = players[j];
        v.hp = static_cast<uint8_t>(std::max(0, static_cast<int>(v.hp) - damage[j]));
        v.hurt = 0.35f;
        events |= j ? HurtP1 : HurtP0;
        if (head[j]) events |= Headshot;
        if (v.hp == 0) events |= kill(j, 1 - j, head[j]);
    }
    return events;
}

uint32_t MidDiffGame::step(float dt, const PadInput in[2]) {
    for (auto& p : players) {
        decay(p.flash, dt);
        decay(p.hurt, dt);
        decay(p.marker, dt);
    }
    uint32_t events = updateGate(dt);
    if (phase_ == arcade::MatchPhase::MatchEnd) return events;
    elapsed += dt;
    const PadInput none = PadInput();
    const PadInput* pads[2] = {&in[0], solo ? &none : &in[1]};
    for (int i = 0; i < 2; ++i) updateLife(i, dt);
    bool fired[2];
    for (int i = 0; i < 2; ++i) fired[i] = act(i, *pads[i], dt);
    events |= updateBurger(dt);
    // Do not let a blast victim fire on the same frame.
    for (int i = 0; i < 2; ++i) if (players[i].dead > 0) fired[i] = false;
    events |= resolveShots(fired);
    if (!solo) {
        const int top = std::max(score[0], score[1]);
        if (top >= kWinningScore && score[0] != score[1]) {
            phase_ = arcade::MatchPhase::MatchEnd;
            winner = score[0] > score[1] ? 0 : 1;
        }
    }
    return events;
}


void MidDiffGame::throwBurger(int i) {
    const Player& p = players[i];
    burger.state = BurgerState::Flying;
    burger.timer = kBurgerFuse;
    burger.x = p.x; burger.y = kEye + jumpHeight(p); burger.z = p.z;
    const float yaw = p.yaw*kDeg, pitch = p.pitch*kDeg;
    burger.vx = std::sin(yaw)*std::cos(pitch)*face(i)*kThrowSpeed;
    burger.vz = std::cos(yaw)*std::cos(pitch)*face(i)*kThrowSpeed;
    burger.vy = std::sin(pitch)*kThrowSpeed + 2.0f;
}

uint32_t MidDiffGame::updateBurger(float dt) {
    if (burger.state == BurgerState::Pickup || burger.state == BurgerState::Held) return 0;
    const float before = burger.timer;
    decay(burger.timer, dt);
    if (burger.state == BurgerState::Waiting) {
        if (burger.timer == 0) {
            // Central arch or above the two low barrels: reachable by shots from both halves.
            static const float spots[][3] = {{-.45f,1.9f,0}, {.45f,2.3f,0}, {0,1.1f,0}, {3.5f,1.6f,0}, {4.15f,1.7f,0}};
            const int n = static_cast<int>(random01()*5);
            burger.x=spots[n][0]; burger.y=spots[n][1]; burger.z=spots[n][2];
            burger.state = BurgerState::Pickup;
            burger.timer = 0;
        }
        return 0;
    }
    if (burger.state == BurgerState::Blast) {
        if (burger.timer == 0) { burger.state = BurgerState::Waiting; burger.timer = randomBetween(kBurgerWaitMin, kBurgerWaitMax); }
        return 0;
    }
    // A closing gate can move onto a resting grenade; eject it to the nearest side.
    const Box& gateBox = boxes[0];
    if (boxBottom(0)<gateBox.y1-1e-4f && burger.x>gateBox.x0-kGrenadeRadius && burger.x<gateBox.x1+kGrenadeRadius &&
        burger.y>boxBottom(0)-kGrenadeRadius && burger.y<gateBox.y1+kGrenadeRadius &&
        burger.z>gateBox.z0-kGrenadeRadius && burger.z<gateBox.z1+kGrenadeRadius) {
        const float side=burger.z<0 ? -1.f : 1.f;
        burger.z=side*(gateBox.z1+kGrenadeRadius+.001f);
        burger.vz=side*maxf(1.f,std::fabs(burger.vz)*.55f);
    }
    // Small steps prevent a fast projectile tunnelling through the thin moving gate.
    float remaining = minf(dt, before);
    while (remaining > 0) {
        const float h = minf(remaining, .004f); remaining -= h;
        burger.vy -= kGravity*h;
        float* pos[3] = {&burger.x,&burger.y,&burger.z};
        float* vel[3] = {&burger.vx,&burger.vy,&burger.vz};
        for (int a=0;a<3;++a) {
            const float old = *pos[a];
            *pos[a] += *vel[a]*h;
            bool blocked = burger.y < kGrenadeRadius || std::fabs(burger.x)>kArenaHalfWidth-kGrenadeRadius || std::fabs(burger.z)>kWallZ-kGrenadeRadius;
            for (int b=0;b<kBoxCount && !blocked;++b) {
                const Box& box=boxes[b]; const float bottom=boxBottom(b);
                if (bottom >= box.y1-1e-4f) continue;
                blocked = burger.x>box.x0-kGrenadeRadius && burger.x<box.x1+kGrenadeRadius &&
                    burger.y>bottom-kGrenadeRadius && burger.y<box.y1+kGrenadeRadius &&
                    burger.z>box.z0-kGrenadeRadius && burger.z<box.z1+kGrenadeRadius;
            }
            if (blocked) { *pos[a]=old; *vel[a]*=-.55f;
                if (a==1) { burger.vx*=.82f; burger.vz*=.82f; if (std::fabs(burger.vy)<.25f) burger.vy=0; }
            }
        }
    }
    // A direct hit drops at the avatar's feet with its original fuse.
    const Player& foe = players[1-burger.owner];
    const float foot = jumpHeight(foe);
    const float reach = kBodyRadius + kGrenadeRadius;
    const float gapX = burger.x-foe.x, gapZ = burger.z-foe.z;
    if (foe.dead <= 0 && gapX*gapX+gapZ*gapZ <= reach*reach &&
        burger.y >= foot-kGrenadeRadius && burger.y <= foot+kStandHeight+kGrenadeRadius &&
        segmentClear(burger.x,burger.y,burger.z,foe.x,foot+.9f,foe.z)) {
        burger.vx = burger.vz = 0;
        burger.vy = minf(burger.vy,0);
    }
    uint32_t events = static_cast<int>(before*4) != static_cast<int>(burger.timer*4) ? BurgerTick : 0;
    if (burger.timer > 0) return events;
    burger.state = BurgerState::Blast; burger.timer = .45f;
    events |= BurgerBoom;
    for (int i=0;i<2;++i) {
        const Player& p=players[i];
        if (p.dead>0 || p.invuln>0) continue;
        const float y=jumpHeight(p)+.9f;
        const float dx=p.x-burger.x, dy=y-burger.y, dz=p.z-burger.z;
        if (dx*dx+dy*dy+dz*dz <= kBlastRadius*kBlastRadius && segmentClear(burger.x,burger.y,burger.z,p.x,y,p.z)) {
            events |= (i ? HurtP1 : HurtP0) | kill(i,1-i,false);
        }
    }
    return events;
}

void MidDiffGame::localInput(int local, const PadInput& in) {
    localIndex_ = local;
    haveLocal_ = true;
    localYaw_ = clampf(std::isfinite(in.aim) ? in.aim : 0.0f, -kYawLimit, kYawLimit);
    localPitch_ = clampf(std::isfinite(in.pitch) ? in.pitch : 0.0f, -kPitchLimit, kPitchLimit);
}

void MidDiffGame::encode(arcade::Writer& w) const {
    w.u8(static_cast<uint8_t>(burger.state)); w.u8(burger.owner);
    w.i16(arcade::q16(burger.timer, 1000));
    w.i16(arcade::q16(burger.x, 1024)); w.i16(arcade::q16(burger.y, 1024)); w.i16(arcade::q16(burger.z, 1024));
    w.u32(seed);
    w.u8(static_cast<uint8_t>(phase_));
    w.u8(static_cast<uint8_t>(winner));
    w.u8(solo ? 1 : 0);
    w.u8(swap_);
    w.u8(score[0]);
    w.u8(score[1]);
    w.f32(elapsed);
    w.u8(static_cast<uint8_t>(gate.state));
    w.u8(arcade::q8(gate.pos, 255.0f));
    w.u8(arcade::q8(gate.timer, 20.0f));
    for (const auto& p : players) {
        w.i16(arcade::q16(p.x, 1024.0f));
        w.i16(arcade::q16(p.z, 1024.0f));
        w.i16(arcade::q16(p.yaw, 100.0f));
        w.i16(arcade::q16(p.pitch, 100.0f));
        w.u8(arcade::q8(p.stamina, 255.0f));
        w.u8(p.hp);
        w.u8(arcade::q8(p.cooldown, 100.0f));
        w.u8(arcade::q8(p.flash, 1000.0f));
        w.u8(arcade::q8(p.hurt, 500.0f));
        w.u8(arcade::q8(p.marker, 1000.0f));
        w.u8(arcade::q8(p.dead, 100.0f));
        w.u8(arcade::q8(p.invuln, 100.0f));
        w.u8(arcade::q8(p.jumpT, 400.0f));
        w.u8((p.markerHead ? 1 : 0) | (p.markerShield ? 2 : 0));
        w.i16(arcade::q16(p.shotX, 512.0f));
        w.i16(arcade::q16(p.shotY, 512.0f));
        w.i16(arcade::q16(p.shotZ, 512.0f));
    }
}

bool MidDiffGame::decode(arcade::Reader& r) {
    MidDiffGame n = *this;
    const uint8_t bs = r.u8(); n.burger.owner = r.u8();
    n.burger.state = static_cast<BurgerState>(bs);
    n.burger.timer = r.i16()/1000.f;
    n.burger.x = r.i16()/1024.f; n.burger.y = r.i16()/1024.f; n.burger.z = r.i16()/1024.f;
    if (bs>4 || n.burger.owner>1 || n.burger.timer<0 || n.burger.timer>kBurgerWaitMax+.001f ||
        std::fabs(n.burger.x)>kArenaHalfWidth || std::fabs(n.burger.z)>kWallZ || n.burger.y<0 || n.burger.y>12 ||
        (bs==3 && n.burger.timer>kBurgerFuse+.001f) || (bs==4 && n.burger.timer>.451f)) return false;
    n.seed = r.u32();
    const uint8_t phase = r.u8();
    n.winner = static_cast<int8_t>(r.u8());
    const uint8_t soloFlag = r.u8();
    n.solo = soloFlag != 0;
    n.swap_ = r.u8();
    n.score[0] = r.u8();
    n.score[1] = r.u8();
    n.elapsed = r.f32();
    const uint8_t gateState = r.u8();
    n.gate.pos = r.u8() / 255.0f;
    n.gate.timer = r.u8() / 20.0f;
    if ((phase != 0 && phase != 2) || soloFlag > 1 || n.swap_ > 1 || gateState > 3 || n.winner < -1 || n.winner > 1 ||
        !std::isfinite(n.elapsed) || n.elapsed < 0) return false;
    n.gate.state = static_cast<GateState>(gateState);
    n.phase_ = static_cast<arcade::MatchPhase>(phase);
    const int top = std::max(n.score[0], n.score[1]);
    const bool over = !n.solo && top >= kWinningScore && n.score[0] != n.score[1];
    if (over != (phase == 2) || (over && n.winner != (n.score[0] > n.score[1] ? 0 : 1)) || (!over && n.winner != -1)) return false;
    for (int i = 0; i < 2; ++i) {
        Player& p = n.players[i];
        p.x = r.i16() / 1024.0f;
        p.z = r.i16() / 1024.0f;
        p.yaw = r.i16() / 100.0f;
        p.pitch = r.i16() / 100.0f;
        p.stamina = r.u8() / 255.0f;
        p.hp = r.u8();
        p.cooldown = r.u8() / 100.0f;
        p.flash = r.u8() / 1000.0f;
        p.hurt = r.u8() / 500.0f;
        p.marker = r.u8() / 1000.0f;
        p.dead = r.u8() / 100.0f;
        p.invuln = r.u8() / 100.0f;
        p.jumpT = r.u8() / 400.0f;
        const uint8_t bits = r.u8();
        p.markerHead = bits & 1;
        p.markerShield = bits & 2;
        p.shotX = r.i16() / 512.0f;
        p.shotY = r.i16() / 512.0f;
        p.shotZ = r.i16() / 512.0f;
        const float side = n.face(i) > 0 ? -1.0f : 1.0f;
        if (bits > 3 || std::fabs(p.x) > kHalfWidth + 0.01f || p.z * side < kNearZ - 0.01f || p.z * side > kFarZ + 0.01f ||
            std::fabs(p.yaw) > kYawLimit + 0.02f || std::fabs(p.pitch) > kPitchLimit + 0.02f || p.hp > kMaxHp ||
            p.cooldown > kShotCooldown + 0.01f || p.flash > 0.121f || p.hurt > 0.351f || p.marker > 0.251f ||
            p.dead > kRespawnTime + 0.01f || p.invuln > kSpawnInvuln + 0.01f || p.jumpT > kJumpTime + 0.01f ||
            (p.dead > 0 && p.hp != 0) || std::fabs(p.shotX) > kMaxRange + 0.1f || std::fabs(p.shotY) > kMaxRange + 0.1f ||
            std::fabs(p.shotZ) > kMaxRange + 0.1f) return false;
    }
    if (!r.ok || r.pos != r.len) return false;
    *this = n;
    return true;
}

#ifdef DEBUG_REMOTE
bool MidDiffGame::debugKey(char key) {
    if (key == 'c') { respawn(0); respawn(1); return true; }
    if ((key == 'h' || key == 'K') && phase_ == arcade::MatchPhase::Playing) {
        const int victim = key == 'h' ? 1 : 0;
        if (players[victim].dead > 0) return false;
        players[victim].hp = 0;
        kill(victim, 1 - victim, true);
        return true;
    }
    return false;
}
#endif
