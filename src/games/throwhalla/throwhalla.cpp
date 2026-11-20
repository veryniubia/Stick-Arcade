#include "games/throwhalla/throwhalla.h"

#include <new>

using arcade::MatchPhase;
using arcade::Mode;
using arcade::q16;
using arcade::q8;
using arcade::Reader;
using arcade::Writer;

namespace {

// Player flag bits.
constexpr uint8_t kPlItemUsed = 1;
constexpr uint8_t kPlCharging = 2;
constexpr uint8_t kPlHit = 4;
constexpr uint8_t kPlPowerUp = 8;
constexpr uint8_t kPlFaceRight = 16;

// Projectile flag bits.
constexpr uint8_t kPrExploding = 1;
constexpr uint8_t kPrFuseOn = 2;

float clampAim(float a) {
    if (a < cfg::AIM_MIN_DEG) return cfg::AIM_MIN_DEG;
    if (a > cfg::AIM_MAX_DEG) return cfg::AIM_MAX_DEG;
    return a;
}

}  // namespace

void ThrowhallaGame::begin(Mode mode, uint32_t seed) {
    g_.begin(mode == Mode::Solo ? GameMode::Solo : GameMode::Versus, seed);
    primed_[0] = primed_[1] = false;
}

void ThrowhallaGame::prime(const PadInput in[2]) {
    for (int i = 0; i < 2; ++i) {
        seenB_[i] = in[i].bCount;
        primed_[i] = true;
    }
}

uint32_t ThrowhallaGame::step(float dt, const PadInput in[2]) {
    PlayerInput pi[2];
    for (int i = 0; i < 2; ++i) {
        if (!primed_[i]) {  // Solo and anything else that did not go through prime()
            seenB_[i] = in[i].bCount;
            primed_[i] = true;
        }
        pi[i].aimDeg = in[i].aim;
        pi[i].charge = in[i].a;
        pi[i].jumpHeld = in[i].b;
        pi[i].jump = in[i].bCount != seenB_[i];
        seenB_[i] = in[i].bCount;
    }
    return g_.update(dt, pi);
}

MatchPhase ThrowhallaGame::phase() const {
    switch (g_.phase) {
        case Phase::Playing: return MatchPhase::Playing;
        case Phase::RoundEnd: return MatchPhase::RoundEnd;
        default: return MatchPhase::MatchEnd;
    }
}

void ThrowhallaGame::localInput(int local, const PadInput& in) {
    g_.players[local].aimDeg = clampAim(in.aim);
}

bool ThrowhallaGame::idle(int local) const {
    return g_.phase == Phase::RoundEnd || (g_.phase == Phase::Playing && g_.players[local].itemUsed);
}

void ThrowhallaGame::encode(Writer& w) const {
    w.u8(static_cast<uint8_t>(g_.phase));
    w.u8(static_cast<uint8_t>(static_cast<int8_t>(g_.winner)));
    w.u8(q8(g_.phaseTimer, 100.0f));
    w.u8(q8(g_.turnTimer, 10.0f));
    w.f32(g_.clock());

    for (const Player& p : g_.players) {
        w.i16(q16(p.h, 16.0f));
        w.i16(q16(p.vh, 16.0f));
        w.i16(q16(p.aimDeg, 100.0f));
        w.u8(q8(p.power, 255.0f));
        w.u8(q8(p.hitFlash, 255.0f));
        w.u8(q8(p.cooldown, 255.0f));
        w.u8(p.score);
        w.u8(static_cast<uint8_t>(p.item));
        uint8_t pf = 0;
        if (p.itemUsed) pf |= kPlItemUsed;
        if (p.charging) pf |= kPlCharging;
        if (p.hit) pf |= kPlHit;
        if (p.powerDir > 0) pf |= kPlPowerUp;
        if (p.face > 0) pf |= kPlFaceRight;
        w.u8(pf);
    }

    uint8_t count = 0;
    for (const Projectile& p : g_.proj) count += p.active ? 1 : 0;
    w.u8(count);
    for (const Projectile& p : g_.proj) {
        if (!p.active) continue;
        uint8_t pf = 0;
        if (p.exploding) pf |= kPrExploding;
        if (p.fuseOn) pf |= kPrFuseOn;
        w.u8(pf);
        w.u8(static_cast<uint8_t>(static_cast<uint8_t>(p.type) | (p.owner << 4)));
        w.i16(q16(p.x, 16.0f));
        w.i16(q16(p.y, 16.0f));
        w.i16(q16(p.vx, 8.0f));
        w.i16(q16(p.vy, 8.0f));
        w.i16(q16(p.rot, 256.0f));
        w.u8(q8(p.fuse, 100.0f));
        w.u8(q8(p.blastT, 300.0f));
    }
}

bool ThrowhallaGame::decode(Reader& r) {
    const uint8_t phase = r.u8();
    const int8_t winner = static_cast<int8_t>(r.u8());
    const uint8_t phaseTimer = r.u8();
    const uint8_t turnTimer = r.u8();
    const float clock = r.f32();
    if (!r.ok || phase > static_cast<uint8_t>(Phase::MatchEnd)) return false;

    struct PlayerWire {
        int16_t h, vh, aim;
        uint8_t power, hitFlash, cooldown, score, item, flags;
    } pw[2];
    for (auto& p : pw) {
        p.h = r.i16();
        p.vh = r.i16();
        p.aim = r.i16();
        p.power = r.u8();
        p.hitFlash = r.u8();
        p.cooldown = r.u8();
        p.score = r.u8();
        p.item = r.u8();
        p.flags = r.u8();
    }
    const uint8_t count = r.u8();
    if (!r.ok || count > cfg::MAX_PROJ) return false;

    struct ProjWire {
        uint8_t flags, typeOwner;
        int16_t x, y, vx, vy, rot;
        uint8_t fuse, blastT;
    } prw[cfg::MAX_PROJ];
    for (int i = 0; i < count; ++i) {
        ProjWire& p = prw[i];
        p.flags = r.u8();
        p.typeOwner = r.u8();
        p.x = r.i16();
        p.y = r.i16();
        p.vx = r.i16();
        p.vy = r.i16();
        p.rot = r.i16();
        p.fuse = r.u8();
        p.blastT = r.u8();
    }
    if (!r.ok) return false;

    g_.phase = static_cast<Phase>(phase);
    g_.winner = winner;
    g_.phaseTimer = phaseTimer / 100.0f;
    g_.turnTimer = turnTimer / 10.0f;
    g_.setClock(clock);

    for (int i = 0; i < 2; ++i) {
        Player& p = g_.players[i];
        const PlayerWire& w = pw[i];
        p.h = w.h / 16.0f;
        p.vh = w.vh / 16.0f;
        p.aimDeg = w.aim / 100.0f;
        p.power = w.power / 255.0f;
        p.hitFlash = w.hitFlash / 255.0f;
        p.cooldown = w.cooldown / 255.0f;
        p.score = w.score;
        p.item = static_cast<ItemType>(w.item <= static_cast<uint8_t>(ItemType::Boomerang) ? w.item : 0);
        p.itemUsed = (w.flags & kPlItemUsed) != 0;
        p.charging = (w.flags & kPlCharging) != 0;
        p.hit = (w.flags & kPlHit) != 0;
        p.powerDir = (w.flags & kPlPowerUp) ? 1 : -1;
        p.face = (w.flags & kPlFaceRight) ? 1 : -1;
        p.feetY = g_.platformY(i) - p.h;
    }

    for (auto& p : g_.proj) p = Projectile();
    for (int i = 0; i < count; ++i) {
        const ProjWire& w = prw[i];
        Projectile& p = g_.proj[i];
        p.active = true;
        p.exploding = (w.flags & kPrExploding) != 0;
        p.fuseOn = (w.flags & kPrFuseOn) != 0;
        const uint8_t type = w.typeOwner & 0x0F;
        p.type = static_cast<ItemType>(type <= static_cast<uint8_t>(ItemType::Boomerang) ? type : 0);
        p.owner = (w.typeOwner >> 4) & 1;
        p.x = w.x / 16.0f;
        p.y = w.y / 16.0f;
        p.vx = w.vx / 8.0f;
        p.vy = w.vy / 8.0f;
        p.rot = w.rot / 256.0f;
        p.fuse = w.fuse / 100.0f;
        p.blastT = w.blastT / 300.0f;
    }
    return true;
}

#ifdef DEBUG_REMOTE
bool ThrowhallaGame::debugKey(char key) {
    if (key >= '1' && key <= '4') {
        g_.players[0].item = static_cast<ItemType>(key - '0');
        return true;
    }
    if (key == 'i') {
        auto& item = g_.players[0].item;
        item = static_cast<ItemType>(1 + static_cast<int>(item) % 4);
        return true;
    }
    return false;
}
#endif

namespace {

arcade::IGame* create(void* mem, arcade::Arena&) { return new (mem) ThrowhallaGame(); }

}  // namespace

const arcade::GameInfo kThrowhallaInfo = {"throwhalla", 1, 1, sizeof(ThrowhallaGame), 0, create, 2};
