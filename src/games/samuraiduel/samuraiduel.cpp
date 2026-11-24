#include "games/samuraiduel/samuraiduel.h"
#include <algorithm>
#include <cmath>
#include <new>

namespace {
float clamp(float x, float lo, float hi) { return std::max(lo, std::min(x, hi)); }
void decay(float& t, float dt) { t = std::max(0.f, t - dt); }
arcade::IGame* create(void* mem, arcade::Arena&) { return new (mem) SamuraiDuelGame(); }
}
const arcade::GameInfo kSamuraiDuelInfo = {"samuraiduel", 5, 6, sizeof(SamuraiDuelGame), 0, create, 2, false};
const SamuraiDuelGame::Platform SamuraiDuelGame::platforms[3] = {
    {24, 126, 106}, {90, 230, 72}, {194, 296, 106}
};

void SamuraiDuelGame::begin(arcade::Mode mode, uint32_t s) {
    const bool rematch = mode == arcade::Mode::Host && mode_ == arcade::Mode::Host &&
                         phase_ == arcade::MatchPhase::MatchEnd;
    mode_ = mode; solo = mode == arcade::Mode::Solo;
    seed = s;
    burgerRandom_ = s;
    boostRounds[0] = boostRounds[1] = 0;
    // Use separate random bits for the starting side and solo loadout.
    swordPlayer = solo ? (seed >> 1) & 1 : rematch ? 1 - swordPlayer : 0;
    score[0] = score[1] = 0; winner = -1; elapsed = 0;
    for (int i = 0; i < 2; ++i) {
        lastA_[i] = lastB_[i] = 0; heldA_[i] = heldB_[i] = false;
    }
    startRound();
}
void SamuraiDuelGame::startRound() {
    for (int i = 0; i < 2; ++i) {
        fighters[i] = Fighter();
        bool right = (i ^ (seed & 1)) != 0;
        fighters[i].x = right ? 276 : 44;
        fighters[i].facing = right ? -1 : 1;
    }
    for (auto& rounds : boostRounds) if (rounds > 0) --rounds;
    shotsLeft_ = 0;
    burgerActive = true;
    burgerRandom_ = 1664525u * burgerRandom_ + 1013904223u;
    burgerX = (fighters[0].x + fighters[1].x) * .5f;
    if (score[0] != score[1]) {
        const int behind = score[0] < score[1] ? 0 : 1;
        const float bias = .16f + ((burgerRandom_ >> 8) % 9) * .01f;
        burgerX += (fighters[behind].x - burgerX) * bias;
    }
    // Both the bridge and the upper deck span the whole spawn band.
    const float deck = (burgerRandom_ >> 31) ? platforms[1].y : kFloor;
    burgerY = deck - 9 - ((burgerRandom_ >> 16) % 23);
    for (auto& b : bullets) b = Bullet();
    roundWinner = -1; phase_ = arcade::MatchPhase::Playing;
    roundTime = phaseTimer = spark = hitStop = 0;
}
uint32_t SamuraiDuelGame::collectBurger() {
    if (!burgerActive) return 0;
    float nearest = 13.f * 13.f;
    int collector = -1;
    for (int i = 0; i < 2; ++i) {
        if (fighters[i].down) continue;
        const float dx = fighters[i].x - burgerX, dy = fighters[i].y - 9 - burgerY;
        const float distance = dx * dx + dy * dy;
        if (distance < nearest || (distance == nearest && collector >= 0 && (burgerRandom_ & 1))) {
            nearest = distance;
            collector = i;
        }
    }
    if (collector < 0) return 0;
    burgerActive = false;
    boostRounds[collector] = 2;
    return Pickup;
}
void SamuraiDuelGame::prime(const PadInput p[2]) {
    for (int i = 0; i < 2; ++i) {
        const PadInput input = solo && i == 1 ? PadInput{} : p[i];
        lastA_[i] = input.aCount; lastB_[i] = input.bCount;
        heldA_[i] = input.a; heldB_[i] = input.b;
    }
}
bool SamuraiDuelGame::slashActive(int i) const {
    const auto& f = fighters[i];
    // Every sword attack shares this window; bullets have no attack-specific exceptions.
    return isSword(i) && !f.down && f.grounded && f.action > attackRecovery(i) &&
           f.action <= attackDuration(i) - kSlashWindup;
}
uint32_t SamuraiDuelGame::move(int i, const PadInput& p, float dt) {
    auto& f = fighters[i]; uint32_t events = 0;
    bool pressA = p.aCount != lastA_[i] || (p.a && !heldA_[i]);
    bool pressB = p.bCount != lastB_[i] || (p.b && !heldB_[i]);
    lastA_[i] = p.aCount; lastB_[i] = p.bCount; heldA_[i] = p.a; heldB_[i] = p.b;
    decay(f.cooldown, dt); decay(f.recoil, dt);
    decay(f.shotFx, dt); decay(f.parryFx, dt);
    if (f.down) f.downTime = std::min(3.f, f.downTime + dt);
    decay(f.jumpBuffer, dt); decay(f.attackBuffer, dt);
    if (pressB && !f.down) f.jumpBuffer = .11f;
    if (pressA && !f.down) f.attackBuffer = .09f;
    // A grounded slash cannot be carried into the air by jumping midway through it.
    if (!f.down && f.jumpBuffer > 0 && f.grounded && (!isSword(i) || f.action <= 0) && f.recoil <= 0) {
        f.vy = -171; f.grounded = false; f.jumpBuffer = 0; events |= Jump;
    }
    float oldY = f.y;
    float aim = std::isfinite(p.aim) ? clamp(p.aim, -45, 45) : 0;
    float axis = fabsf(aim) < 5 ? 0 : std::copysign(clamp((fabsf(aim)-5)/27, 0, 1), aim);
    if (f.down) axis = 0;
    float target = axis * (isSword(i) ? 74 : 64);
    if (isSword(i) && f.action > 0) target *= .18f;
    if (f.down) f.vx *= std::max(0.f, 1.f-dt*9);
    else if (f.recoil <= 0) f.vx += (target - f.vx) * std::min(1.f, dt * (f.grounded ? 22 : 9));
    float stepX = 0;
    if (isSword(i) && f.thrust && !f.down && f.grounded && f.action > 0) {
        const float travelStart = kThrustDuration - kSlashWindup;
        const float overlap = std::min(f.action, travelStart) - std::max(f.action - dt, travelStart-kThrustTravelTime);
        stepX = f.facing * kThrustDistance * std::max(0.f, overlap) / kThrustTravelTime;
    }
    f.x = clamp(f.x + f.vx * dt + stepX, kLeft, kRight);
    if (!f.down && !f.grounded && !p.b && f.vy < -78) f.vy = -78;
    f.vy = std::min(260.f, f.vy + 340 * dt); f.y += f.vy * dt;
    f.grounded = false;
    float landing = kFloor;
    for (const auto& platform : platforms) if (f.x >= platform.left && f.x <= platform.right &&
        f.vy >= 0 && oldY <= platform.y + .01f && f.y >= platform.y) landing = std::min(landing, platform.y);
    if (f.y >= landing) { f.y = landing; f.vy = 0; f.grounded = true; }
    if (f.y < kCeiling) { f.y = kCeiling; f.vy = std::max(0.f, f.vy); }

    float before = f.action; decay(f.action, dt);
    if (isSword(i) && !f.grounded) f.action = 0;
    // Fire after visible windup, at current height; facing was locked when A was pressed.
    if (!isSword(i) && !f.down && before > 0 && f.action <= 0) {
        for (auto& b : bullets) if (!b.active) {
            b.active = true; b.reflected = false; b.owner = uint8_t(i);
            b.direction = f.facing; b.x = f.x + f.facing * 10; b.y = f.y - 8;
            f.shotFx = .18f;
            events |= Shot; break;
        }
        if (shotsLeft_ > 0) {
            --shotsLeft_;
            f.action = kBurstInterval + before - dt;
        }
    }
    // Keep the attack heading through the exact tick that releases a shot.
    if (!f.down && f.action <= 0 && f.recoil <= 0 && axis != 0) f.facing = axis > 0 ? 1 : -1;
    if (!f.down && f.attackBuffer > 0 && f.cooldown <= 0 && f.recoil <= 0 && (!isSword(i) || f.grounded)) {
        f.attackBuffer = 0; f.struck = false;
        f.thrust = isSword(i) && boostRounds[i] > 0;
        f.action = isSword(i) ? attackDuration(i) : kShotWindup;
        f.cooldown = isSword(i) ? attackDuration(i) : kShotCooldown;
        if (isSword(i)) events |= Slash;
        else {
            shotsLeft_ = boostRounds[i] ? 2 : 0;
        }
    }
    return events;
}
uint32_t SamuraiDuelGame::damage(int victim, int direction) {
    auto& f = fighters[victim];
    if (phase_ == arcade::MatchPhase::MatchEnd || f.down) return 0;
    f.vx = direction * 83; f.action = 0; f.attackBuffer = 0;
    f.jumpBuffer = 0; f.down = true; f.downTime = 0; f.fallDirection = int8_t(direction);
    f.shotFx = f.parryFx = 0;
    if (!isSword(victim)) shotsLeft_ = 0;
    if (score[1-victim] != UINT32_MAX) ++score[1-victim];
    sparkX = f.x; sparkY = f.y - 8; spark = .20f; hitStop = .045f;
    return Hurt | finishRound(1-victim);
}
uint32_t SamuraiDuelGame::projectiles(float dt) {
    uint32_t events = 0;
    // Swept horizontal collision: fast bullets cannot skip a blade/body between ticks.
    for (auto& b : bullets) {
        if (!b.active) continue;
        float end = b.x + b.direction * kShotSpeed * dt;
        float lo = std::min(b.x, end), hi = std::max(b.x, end);
        int target = 1 - b.owner; auto& f = fighters[target];
        float front = f.x + f.facing * attackReach(target);
        bool blade = slashActive(target) && b.direction == -f.facing &&
            fabsf(b.y - (f.y - 8)) <= 8 &&
            hi >= std::min(f.x, front) && lo <= std::max(f.x, front);
        if (blade) {
            b.direction = -b.direction; b.owner = uint8_t(target); b.reflected = true;
            b.x = front + f.facing * 2;
            f.vx = -f.facing * 8; f.recoil = .055f; f.parryFx = .16f;
            sparkX = front; sparkY = b.y; spark = .18f; hitStop = .025f;
            events |= Parry;
            continue;
        }
        if (!f.down && hi >= f.x - 5 && lo <= f.x + 5 && b.y >= f.y - 17 && b.y <= f.y - 1) {
            events |= damage(target, b.direction); b.active = false;
        } else { b.x = end; if (b.x < -10 || b.x > kWorldWidth+10) b.active = false; }
    }
    return events;
}
uint32_t SamuraiDuelGame::finishRound(int result) {
    if (phase_ == arcade::MatchPhase::MatchEnd) return 0;
    roundWinner = fighters[0].down && fighters[1].down ? -1 : int8_t(result);
    // A later death gets its own full animation time. Keep inputs and projectiles alive.
    phaseTimer = kRoundPause;
    phase_ = arcade::MatchPhase::RoundEnd; return Round;
}
uint32_t SamuraiDuelGame::step(float dt, const PadInput in[2]) {
    if (mode_ == arcade::Mode::Client || !std::isfinite(dt) || dt <= 0 || phase_ == arcade::MatchPhase::MatchEnd) return 0;
    dt = std::min(dt, .05f); elapsed += dt; decay(spark, dt);
    if (phase_ == arcade::MatchPhase::RoundEnd) decay(phaseTimer, dt);
    if (hitStop > 0) { decay(hitStop, dt); return 0; }
    if (phase_ == arcade::MatchPhase::Playing) roundTime += dt;
    const PadInput p[2] = {in[0], solo ? PadInput{} : in[1]};
    uint32_t events = 0;
    for (int i = 0; i < 2; ++i) events |= move(i, p[i], dt);
    events |= collectBurger();
    events |= projectiles(dt); // An active blade parries before body damage is evaluated.
    int s = swordPlayer, g = 1-s; auto& sword = fighters[s]; auto& gun = fighters[g];
    float dx = (gun.x - sword.x) * sword.facing;
    if (!gun.down && slashActive(s) && !sword.struck && dx >= -2 && dx <= attackReach(s)+4 && fabsf(gun.y - sword.y) <= 12) {
        sword.struck = true; events |= damage(g, sword.facing);
    }
    if (phase_ == arcade::MatchPhase::Playing && roundTime >= 45) events |= finishRound(-1);
    if (phase_ == arcade::MatchPhase::RoundEnd && phaseTimer <= 0) {
        if (!solo && (score[0] >= kWinningScore || score[1] >= kWinningScore)) {
            winner = score[0] == score[1] ? -1 : score[0] > score[1] ? 0 : 1;
            phase_ = arcade::MatchPhase::MatchEnd;
            events |= Finish;
        } else {
            prime(in); // Do not replay buttons from the preceding exchange at the new spawn.
            startRound();
        }
    }
    return events;
}

void SamuraiDuelGame::encode(arcade::Writer& w) const {
    w.u32(seed); w.u8(uint8_t(phase_)); w.u8(uint8_t(winner)); w.u8(uint8_t(roundWinner));
    w.u8(solo); w.u8(swordPlayer); w.u32(score[0]); w.u32(score[1]);
    w.f32(elapsed); w.u16(uint16_t(lroundf(roundTime*1000)));
    w.u16(uint16_t(lroundf(phaseTimer*1000))); w.u8(arcade::q8(spark, 250));
    w.i16(arcade::q16(sparkX, 32)); w.i16(arcade::q16(sparkY, 32));
    w.u8(arcade::q8(hitStop, 250));
    for (const auto& f : fighters) {
        w.i16(arcade::q16(f.x, 64)); w.i16(arcade::q16(f.y, 64));
        w.i16(arcade::q16(f.vx, 64)); w.i16(arcade::q16(f.vy, 64));
        w.u16(uint16_t(lroundf(f.cooldown*1000))); w.u16(uint16_t(lroundf(f.action*1000)));
        w.u8(arcade::q8(f.recoil, 250));
        w.u8((f.facing > 0 ? 1 : 0) | (f.grounded ? 2 : 0) | (f.struck ? 4 : 0) |
             (f.thrust ? 8 : 0) | (f.down ? 16 : 0) | (f.fallDirection > 0 ? 32 : 0));
        w.u8(arcade::q8(f.shotFx, 250)); w.u8(arcade::q8(f.parryFx, 250));
        w.u16(uint16_t(lroundf(f.downTime*1000)));
    }
    for (const auto& b : bullets) {
        w.u8((b.active ? 1 : 0) | (b.reflected ? 2 : 0) | (b.direction > 0 ? 4 : 0) | (b.owner << 3));
        w.i16(arcade::q16(b.x, 32)); w.i16(arcade::q16(b.y, 32));
    }
    w.i16(arcade::q16(burgerX, 32)); w.i16(arcade::q16(burgerY, 32));
    w.u8(burgerActive); w.u8(boostRounds[0]); w.u8(boostRounds[1]);
}
bool SamuraiDuelGame::decode(arcade::Reader& r) {
    SamuraiDuelGame n = *this;
    n.seed = r.u32(); uint8_t phase = r.u8(); n.winner = int8_t(r.u8()); n.roundWinner = int8_t(r.u8());
    uint8_t soloFlag = r.u8(); n.solo = soloFlag != 0;
    n.swordPlayer = r.u8(); n.score[0] = r.u32(); n.score[1] = r.u32();
    n.elapsed = r.f32(); n.roundTime = r.u16()/1000.f; n.phaseTimer = r.u16()/1000.f;
    n.spark = r.u8()/250.f; n.sparkX = r.i16()/32.f; n.sparkY = r.i16()/32.f; n.hitStop = r.u8()/250.f;
    if (phase > 2 || soloFlag > 1 || n.swordPlayer > 1 ||
        n.winner < -1 || n.winner > 1 || n.roundWinner < -1 || n.roundWinner > 1 ||
        (!n.solo && (n.score[0] > kWinningScore || n.score[1] > kWinningScore)) || !std::isfinite(n.elapsed) || n.elapsed < 0 ||
        n.roundTime > 45.06f || n.phaseTimer > kRoundPause+.001f || n.spark > .204f || n.hitStop > .048f ||
        n.sparkX < -16 || n.sparkX > kWorldWidth+16 || n.sparkY < 0 || n.sparkY > kFloor) return false;
    n.phase_ = arcade::MatchPhase(phase);
    const bool target = !n.solo && (n.score[0] == kWinningScore || n.score[1] == kWinningScore);
    const int expected = n.score[0] == n.score[1] ? -1 : n.score[0] > n.score[1] ? 0 : 1;
    if ((phase == 2 && (!target || n.winner != expected || n.phaseTimer != 0)) ||
        (phase != 2 && n.winner != -1) ||
        (phase == 0 && (target || n.roundWinner != -1 || n.phaseTimer != 0))) return false;
    for (int i = 0; i < 2; ++i) {
        auto& f = n.fighters[i];
        f.x = r.i16()/64.f; f.y = r.i16()/64.f; f.vx = r.i16()/64.f; f.vy = r.i16()/64.f;
        f.cooldown = r.u16()/1000.f; f.action = r.u16()/1000.f;
        f.recoil = r.u8()/250.f; uint8_t bits = r.u8();
        f.facing = bits & 1 ? 1 : -1; f.grounded = bits & 2; f.struck = bits & 4;
        f.thrust = bits & 8; f.down = bits & 16; f.fallDirection = bits & 32 ? 1 : -1;
        f.shotFx = r.u8()/250.f; f.parryFx = r.u8()/250.f; f.downTime = r.u16()/1000.f;
        if (bits > 63 || f.x < kLeft || f.x > kRight || f.y < kCeiling || f.y > kFloor ||
            fabsf(f.vx) > 84 || f.vy < -172 || f.vy > 261 || f.cooldown > .641f ||
            f.action > (n.isSword(i) ? n.attackDuration(i)+.001f : .161f) || f.recoil > .164f ||
            f.shotFx > .184f || f.parryFx > .164f || f.downTime > 3.001f ||
            (f.thrust && !n.isSword(i)) || (!f.down && f.downTime != 0) ||
            (f.down && (f.action > 0 || phase == 0)) ||
            (n.isSword(i) && !f.grounded && f.action > 0)) return false;
        if (f.grounded) {
            bool supported = fabsf(f.y-kFloor) < .02f;
            for (const auto& p : platforms) if (fabsf(f.y-p.y)<.02f && f.x >= p.left-.02f && f.x <= p.right+.02f) supported = true;
            if (!supported || f.vy != 0) return false;
        }
    }
    for (auto& b : n.bullets) {
        uint8_t bits = r.u8(); b.active = bits & 1; b.reflected = bits & 2;
        b.direction = bits & 4 ? 1 : -1; b.owner = (bits >> 3) & 1;
        b.x = r.i16()/32.f; b.y = r.i16()/32.f;
        if (bits > 15 || b.x < -25 || b.x > kWorldWidth+25 || b.y < 0 || b.y > kFloor ||
            (b.active && (b.y < kCeiling-8 || (b.reflected ? b.owner != n.swordPlayer : b.owner == n.swordPlayer)))) return false;
    }
    n.burgerX = r.i16()/32.f; n.burgerY = r.i16()/32.f;
    const uint8_t active = r.u8(); n.burgerActive = active != 0;
    n.boostRounds[0] = r.u8(); n.boostRounds[1] = r.u8();
    if (active > 1 || n.boostRounds[0] > 2 || n.boostRounds[1] > 2 ||
        n.burgerX < kLeft || n.burgerX > kRight || n.burgerY < kCeiling-9 || n.burgerY > kFloor) return false;
    if (!r.ok || r.pos != r.len) return false;
    *this = n; return true;
}
#ifdef DEBUG_REMOTE
bool SamuraiDuelGame::debugKey(char key) {
    // The shared lowercase 'a' is a menu press, not a PadInput attack count.
    if (key == 'A') {
        if(!fighters[0].down)fighters[0].attackBuffer=.09f;
        return true;
    }
    // Deterministic on-device visual checks. These prepare state, never opponent AI.
    // 1/2 = sword/gun practice, 3/4 = mutual death / match draw, 6 = approaching burst.
    if ((key >= '1' && key <= '4') || key == '6') {
        PadInput pads[2] = {};
        for(int i=0;i<2;++i) {
            pads[i].aCount=lastA_[i];pads[i].bCount=lastB_[i];
            pads[i].a=heldA_[i];pads[i].b=heldB_[i];
        }
        begin(arcade::Mode::Solo,key=='2' ? 2:0);prime(pads);
        fighters[0].x=84;fighters[1].x=230;
        burgerActive=false;
        if(key=='3' || key=='4') {
            if(key=='4') { solo=false;score[0]=score[1]=kWinningScore-1; }
            for(int owner=0;owner<2;++owner) {
                auto& b=bullets[owner];b.active=true;b.owner=uint8_t(owner);
                b.direction=owner==0 ? 1:-1;b.reflected=isSword(owner);
                b.x=fighters[1-owner].x-b.direction*7;b.y=kFloor-8;
            }
        } else if(key=='6') {
            boostRounds[0]=2;
            for(int n=0;n<3;++n) {
                auto& b=bullets[n];b.active=true;b.owner=1;b.direction=-1;
                b.x=fighters[0].x+64+n*kShotSpeed*kBurstInterval;b.y=kFloor-8;
            }
        }
        return true;
    }
    if (key == '5') {
        burgerActive=true;burgerX=fighters[0].x;burgerY=fighters[0].y-9;
        return true;
    }
    if (key == 'c') { startRound(); return true; }
    if (key == 'h') { damage(1, 1); return true; }
    if (key == 'w' && !solo && phase_ == arcade::MatchPhase::Playing) {
        score[0] = kWinningScore-1; damage(1, 1); return true;
    }
    return false;
}
#endif
