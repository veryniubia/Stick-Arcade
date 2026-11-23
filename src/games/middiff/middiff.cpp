#include "games/middiff/middiff.h"
#include <algorithm>
#include <cmath>
#include <new>

namespace {

constexpr float kDeg = 0.017453292f;

float clampf(float x, float lo, float hi) { return std::max(lo, std::min(x, hi)); }
void decay(float& t, float dt) { t = std::max(0.f, t - dt); }
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

// Vertical cylinder standing on the floor.
bool hitCylinder(const float o[3], const float d[3], float cx, float cz, float r, float top, float& t) {
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
    if (y < 0 || y > top) return false;
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
        tMin = std::max(tMin, t0);
        tMax = std::min(tMax, t1);
        if (tMin > tMax) return false;
    }
    t = tMin;
    return true;
}

}  // namespace

const arcade::GameInfo kMidDiffInfo = {"middiff", 6, 1, sizeof(MidDiffGame), 0, create, 2, false, true};

void MidDiffGame::begin(arcade::Mode mode, uint32_t s) {
    mode_ = mode;
    solo = mode == arcade::Mode::Solo;
    seed = s;
    rng_ = s ? s : 1;
    score[0] = score[1] = 0;
    winner = -1;
    elapsed = 0;
    haveLocal_ = false;
    for (int i = 0; i < 2; ++i) { lastA_[i] = 0; fireBuffer_[i] = 0; }
    startRound();
}

void MidDiffGame::startRound() {
    for (int i = 0; i < 2; ++i) {
        Player& p = players[i];
        p = Player();
        p.z = -facing(i) * kSpawnZ;
        p.cooldown = kRoundStartCooldown;
        fireBuffer_[i] = 0;
    }
    // The practice target stays where it spawned but not always behind the cover.
    if (solo) {
        rng_ = 1664525u * rng_ + 1013904223u;
        players[1].x = (static_cast<int>((rng_ >> 16) % 61) - 30) / 10.0f;
    }
    roundWinner = -1;
    phase_ = arcade::MatchPhase::Playing;
    roundTime = phaseTimer = 0;
}

void MidDiffGame::prime(const PadInput p[2]) {
    for (int i = 0; i < 2; ++i) lastA_[i] = solo && i == 1 ? 0 : p[i].aCount;
}

bool MidDiffGame::act(int i, const PadInput& p, float dt) {
    Player& me = players[i];
    decay(me.cooldown, dt);
    decay(fireBuffer_[i], dt);
    const bool pressA = p.aCount != lastA_[i];
    lastA_[i] = p.aCount;
    if (pressA) fireBuffer_[i] = kShotBuffer;

    me.yaw = clampf(std::isfinite(p.aim) ? p.aim : 0, -kYawLimit, kYawLimit);
    me.pitch = clampf(std::isfinite(p.pitch) ? p.pitch : 0, -kPitchLimit, kPitchLimit);

    // Stance and stamina.
    if (p.b && !me.winded && me.stamina > 0) {
        me.ducking = true;
        me.stamina -= kStaminaDrain * dt;
        me.regenWait = kRegenDelay;
        if (me.stamina <= 0) { me.stamina = 0; me.winded = true; me.ducking = false; }
    } else {
        me.ducking = false;
    }
    if (!me.ducking) {
        if (me.regenWait > 0) decay(me.regenWait, dt);
        else me.stamina = std::min(1.f, me.stamina + kStaminaRegen * dt);
    }
    if (me.winded && !p.b && me.stamina >= kWindedRecover) me.winded = false;
    const float target = me.ducking ? 1.f : 0.f;
    me.crouch += clampf(target - me.crouch, -kCrouchRate * dt, kCrouchRate * dt);

    // The stick moves the player in their own frame whatever way the view points.
    float sx = clampf(p.stickX, -100, 100) / 100.f, sy = clampf(p.stickY, -100, 100) / 100.f;
    const float mag = std::sqrt(sx * sx + sy * sy);
    if (mag > 1.f) { sx /= mag; sy /= mag; }
    const float speed = kWalkSpeed + (kCrouchSpeed - kWalkSpeed) * me.crouch;
    const float f = facing(i);
    me.x = clampf(me.x + sx * speed * dt * f, -kHalfWidth, kHalfWidth);
    const float side = -f;  // the side of the arena this player owns
    const float depth = clampf(std::fabs(me.z + sy * speed * dt * f), kNearZ, kFarZ);
    me.z = side * depth;

    if (fireBuffer_[i] > 0 && me.cooldown <= 0) {
        fireBuffer_[i] = 0;
        me.cooldown = kShotCooldown;
        return true;
    }
    return false;
}

MidDiffGame::Trace MidDiffGame::trace(int s) const {
    const Player& me = players[s];
    const Player& foe = players[1 - s];
    const float f = facing(s);
    const float yaw = me.yaw * kDeg, pitch = me.pitch * kDeg;
    const float o[3] = {me.x, eyeHeight(me.crouch), me.z};
    const float d[3] = {std::sin(yaw) * std::cos(pitch) * f, std::sin(pitch), std::cos(yaw) * std::cos(pitch) * f};

    float tWorld = kMaxRange;
    bool covered = false;
    if (d[1] < -1e-6f) {
        const float tFloor = -o[1] / d[1];
        if (tFloor < tWorld) tWorld = tFloor;
    }
    for (int c = 0; c < 2; ++c) {
        const float lo[3] = {-kCoverHalfWidth, 0, c == 0 ? -kCoverFarZ : kCoverNearZ};
        const float hi[3] = {kCoverHalfWidth, kCoverHeight, c == 0 ? -kCoverNearZ : kCoverFarZ};
        float t;
        if (hitBox(o, d, lo, hi, t) && t < tWorld) { tWorld = t; covered = true; }
    }

    const float height = bodyHeight(foe.crouch);
    float tHead = 1e9f, tBody = 1e9f;
    const bool head = hitSphere(o, d, foe.x, height - 0.2f, foe.z, kHeadRadius, tHead);
    const bool body = hitCylinder(o, d, foe.x, foe.z, kBodyRadius, height - 0.35f, tBody);

    Trace r;
    const float tTarget = std::min(head ? tHead : 1e9f, body ? tBody : 1e9f);
    if ((head || body) && tTarget < tWorld) {
        r.hit = true;
        r.head = head && tHead <= tBody;
        r.t = tTarget;
    } else {
        r.t = tWorld;
        r.covered = covered;
    }
    r.x = o[0] + d[0] * r.t;
    r.y = o[1] + d[1] * r.t;
    r.z = o[2] + d[2] * r.t;
    return r;
}

uint32_t MidDiffGame::resolveShots(const bool fired[2]) {
    uint32_t events = 0;
    int damage[2] = {};
    bool head[2] = {};
    for (int i = 0; i < 2; ++i) {
        if (!fired[i]) continue;
        const Trace t = trace(i);
        Player& me = players[i];
        me.flash = 0.12f;
        me.shotX = t.x; me.shotY = t.y; me.shotZ = t.z;
        events |= i ? ShotP1 : ShotP0;
        if (!t.hit) continue;
        damage[1 - i] += t.head ? kHeadDamage : kBodyDamage;
        head[1 - i] |= t.head;
        me.marker = 0.25f;
        me.markerHead = t.head;
    }
    // Both shots are traced against the positions of this step before anyone loses health: a double hit is a double hit.
    for (int j = 0; j < 2; ++j) {
        if (!damage[j]) continue;
        players[j].hp = static_cast<uint8_t>(std::max(0, players[j].hp - damage[j]));
        players[j].hurt = 0.35f;
        events |= j ? HurtP1 : HurtP0;
        if (head[j]) events |= Headshot;
    }
    const bool dead0 = players[0].hp == 0, dead1 = players[1].hp == 0;
    if (dead0 && dead1) events |= finishRound(-1);
    else if (dead1) events |= finishRound(0);
    else if (dead0) events |= finishRound(1);
    return events;
}

uint32_t MidDiffGame::finishRound(int win) {
    roundWinner = static_cast<int8_t>(win);
    phaseTimer = kRoundPause;
    if (win >= 0) ++score[win];
    if (!solo && win >= 0 && score[win] >= kWinningScore) {
        phase_ = arcade::MatchPhase::MatchEnd;
        winner = static_cast<int8_t>(win);
        return Finish;
    }
    phase_ = arcade::MatchPhase::RoundEnd;
    return Round;
}

uint32_t MidDiffGame::step(float dt, const PadInput in[2]) {
    for (auto& p : players) {
        decay(p.flash, dt); decay(p.hurt, dt); decay(p.marker, dt);
    }
    if (phase_ == arcade::MatchPhase::MatchEnd) return 0;
    if (phase_ == arcade::MatchPhase::RoundEnd) {
        phaseTimer -= dt;
        if (phaseTimer <= 0) startRound();
        return 0;
    }
    elapsed += dt;
    roundTime += dt;
    const PadInput p[2] = {in[0], solo ? PadInput{} : in[1]};
    bool fired[2];
    for (int i = 0; i < 2; ++i) fired[i] = act(i, p[i], dt);
    uint32_t events = resolveShots(fired);
    if (phase_ == arcade::MatchPhase::Playing && !solo && roundTime >= kRoundTime) {
        const int a = players[0].hp, b = players[1].hp;
        events |= finishRound(a > b ? 0 : b > a ? 1 : -1);
    }
    return events;
}

void MidDiffGame::localInput(int local, const PadInput& in) {
    localIndex_ = local;
    haveLocal_ = true;
    localYaw_ = clampf(std::isfinite(in.aim) ? in.aim : 0, -kYawLimit, kYawLimit);
    localPitch_ = clampf(std::isfinite(in.pitch) ? in.pitch : 0, -kPitchLimit, kPitchLimit);
}

void MidDiffGame::encode(arcade::Writer& w) const {
    w.u32(seed); w.u8(uint8_t(phase_)); w.u8(uint8_t(winner)); w.u8(uint8_t(roundWinner));
    w.u8(solo); w.u8(score[0]); w.u8(score[1]);
    w.f32(elapsed);
    w.u16(uint16_t(lroundf(roundTime * 100))); w.u16(uint16_t(lroundf(phaseTimer * 1000)));
    for (const auto& p : players) {
        w.i16(arcade::q16(p.x, 1024)); w.i16(arcade::q16(p.z, 1024));
        w.i16(arcade::q16(p.yaw, 100)); w.i16(arcade::q16(p.pitch, 100));
        w.u8(arcade::q8(p.crouch, 255)); w.u8(arcade::q8(p.stamina, 255)); w.u8(p.hp);
        w.u8(arcade::q8(p.cooldown, 100));
        w.u8(arcade::q8(p.flash, 1000)); w.u8(arcade::q8(p.hurt, 500)); w.u8(arcade::q8(p.marker, 1000));
        w.u8((p.winded ? 1 : 0) | (p.ducking ? 2 : 0) | (p.markerHead ? 4 : 0));
        w.i16(arcade::q16(p.shotX, 512)); w.i16(arcade::q16(p.shotY, 512)); w.i16(arcade::q16(p.shotZ, 512));
    }
}

bool MidDiffGame::decode(arcade::Reader& r) {
    MidDiffGame n = *this;
    n.seed = r.u32();
    const uint8_t phase = r.u8();
    n.winner = int8_t(r.u8()); n.roundWinner = int8_t(r.u8());
    const uint8_t soloFlag = r.u8();
    n.solo = soloFlag != 0;
    n.score[0] = r.u8(); n.score[1] = r.u8();
    n.elapsed = r.f32();
    n.roundTime = r.u16() / 100.f; n.phaseTimer = r.u16() / 1000.f;
    if (phase > 2 || soloFlag > 1 || n.winner < -1 || n.winner > 1 || n.roundWinner < -1 || n.roundWinner > 1 ||
        (!n.solo && (n.score[0] > kWinningScore || n.score[1] > kWinningScore)) ||
        !std::isfinite(n.elapsed) || n.elapsed < 0 || n.roundTime > kRoundTime + .02f || n.phaseTimer > kRoundPause + .01f) return false;
    n.phase_ = arcade::MatchPhase(phase);
    const bool end = !n.solo && (n.score[0] == kWinningScore || n.score[1] == kWinningScore);
    if (end != (phase == 2) || (end && n.score[0] == kWinningScore && n.score[1] == kWinningScore) ||
        (end && (n.winner != (n.score[0] == kWinningScore ? 0 : 1) || n.roundWinner != n.winner)) ||
        (!end && n.winner != -1) || (phase == 0 && (n.roundWinner != -1 || n.phaseTimer != 0))) return false;
    for (int i = 0; i < 2; ++i) {
        Player& p = n.players[i];
        p.x = r.i16() / 1024.f; p.z = r.i16() / 1024.f;
        p.yaw = r.i16() / 100.f; p.pitch = r.i16() / 100.f;
        p.crouch = r.u8() / 255.f; p.stamina = r.u8() / 255.f; p.hp = r.u8();
        p.cooldown = r.u8() / 100.f;
        p.flash = r.u8() / 1000.f; p.hurt = r.u8() / 500.f; p.marker = r.u8() / 1000.f;
        const uint8_t bits = r.u8();
        p.winded = bits & 1; p.ducking = bits & 2; p.markerHead = bits & 4;
        p.shotX = r.i16() / 512.f; p.shotY = r.i16() / 512.f; p.shotZ = r.i16() / 512.f;
        const float side = -facing(i);
        if (bits > 7 || std::fabs(p.x) > kHalfWidth + .01f || p.z * side < kNearZ - .01f || p.z * side > kFarZ + .01f ||
            std::fabs(p.yaw) > kYawLimit + .02f || std::fabs(p.pitch) > kPitchLimit + .02f || p.hp > kMaxHp ||
            p.cooldown > kShotCooldown + .01f || p.flash > .121f || p.hurt > .351f || p.marker > .251f ||
            std::fabs(p.shotX) > kMaxRange + .1f || std::fabs(p.shotY) > kMaxRange + .1f || std::fabs(p.shotZ) > kMaxRange + .1f) return false;
    }
    if (!r.ok || r.pos != r.len) return false;
    *this = n;
    return true;
}

#ifdef DEBUG_REMOTE
bool MidDiffGame::debugKey(char key) {
    if (key == 'c') { startRound(); return true; }
    if (key == 'h' && phase_ == arcade::MatchPhase::Playing) { players[1].hp = 0; finishRound(0); return true; }
    return false;
}
#endif
