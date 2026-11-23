// Host-side tests for MidDiff rules, its state codec, and the platform helpers it depends on (gyro look, stick map).
#include "games/middiff/middiff.h"
#include "core/gyro_look.h"
#include "core/stick_map.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>

void MidDiffGame::draw(int, const arcade::DrawCtx&) {}
void MidDiffGame::sfx(uint32_t) {}
static int failures = 0;
#define CHECK(c) do { if (!(c)) { printf("FAIL %d: %s\n", __LINE__, #c); ++failures; } } while (0)
using G = MidDiffGame;
constexpr float tick = 1.f / 60;
constexpr float rad = 57.2957795f;

static void run(G& g, PadInput p[2], int frames) { for (int i = 0; i < frames; ++i) g.step(tick, p); }
static G fresh(arcade::Mode m = arcade::Mode::Host) {
    G g;
    g.begin(m, 7);
    PadInput p[2] = {};
    g.prime(p);
    return g;
}
static bool same(const G& a, const G& b) {
    arcade::Writer x, y;
    a.encode(x);
    b.encode(y);
    return x.n == y.n && memcmp(x.buf, y.buf, x.n) == 0;
}
// Points shooter i's view at a world point.
static void aimAt(PadInput& p, const G& g, int i, float tx, float ty, float tz) {
    const float f = G::facing(i);
    const auto& me = g.players[i];
    const float dx = (tx - me.x) * f, dz = (tz - me.z) * f;
    p.aim = std::atan2(dx, dz) * rad;
    p.pitch = std::atan2(ty - G::eyeHeight(me.crouch), std::hypot(dx, dz)) * rad;
}
static void fire(PadInput& p) { ++p.aCount; }
static void waitReady(G& g, PadInput p[2]) { run(g, p, int(G::kRoundStartCooldown / tick) + 2); }

static void movement() {
    G g = fresh();
    PadInput p[2] = {};
    // Forward for both players is towards the middle and stops at the line nobody may cross.
    p[0].stickY = 100; p[1].stickY = 100;
    run(g, p, 300);
    CHECK(std::fabs(g.players[0].z + G::kNearZ) < 1e-3f);
    CHECK(std::fabs(g.players[1].z - G::kNearZ) < 1e-3f);
    CHECK(g.players[1].z - g.players[0].z >= 2 * G::kNearZ - 1e-3f);
    // Backwards stops at the back of the zone.
    p[0].stickY = p[1].stickY = -100;
    run(g, p, 400);
    CHECK(std::fabs(g.players[0].z + G::kFarZ) < 1e-3f && std::fabs(g.players[1].z - G::kFarZ) < 1e-3f);
    // Right is the player's right: +x for player 0, -x for player 1.
    p[0].stickY = p[1].stickY = 0;
    p[0].stickX = p[1].stickX = 100;
    run(g, p, 30);
    CHECK(g.players[0].x > 1.0f && g.players[1].x < -1.0f);
    run(g, p, 300);
    CHECK(std::fabs(g.players[0].x - G::kHalfWidth) < 1e-3f && std::fabs(g.players[1].x + G::kHalfWidth) < 1e-3f);
    // The view direction never changes where the stick takes the player.
    G a = fresh(), b = fresh();
    PadInput pa[2] = {}, pb[2] = {};
    pa[0].stickX = pb[0].stickX = 60; pa[0].stickY = pb[0].stickY = 80;
    pb[0].aim = 85; pb[0].pitch = -20;
    run(a, pa, 40); run(b, pb, 40);
    CHECK(a.players[0].x == b.players[0].x && a.players[0].z == b.players[0].z);
    // Diagonals are not faster than straight lines.
    G c = fresh(), d = fresh();
    PadInput pc[2] = {}, pd[2] = {};
    pc[0].stickX = 100; pc[0].stickY = 0;
    pd[0].stickX = 100; pd[0].stickY = 100;
    run(c, pc, 20); run(d, pd, 20);
    CHECK(std::hypot(d.players[0].x, d.players[0].z + G::kSpawnZ) <= std::hypot(c.players[0].x, c.players[0].z + G::kSpawnZ) + 1e-3f);
    // Garbage input is harmless.
    PadInput bad[2] = {};
    bad[0].aim = std::numeric_limits<float>::quiet_NaN();
    bad[0].pitch = std::numeric_limits<float>::infinity();
    bad[0].stickX = -128; bad[0].stickY = 127;
    run(c, bad, 10);
    CHECK(std::isfinite(c.players[0].x) && c.players[0].yaw == 0 && c.players[0].pitch == 0);
}

static void viewLimits() {
    G g = fresh();
    PadInput p[2] = {};
    p[0].aim = 500; p[0].pitch = -500;
    g.step(tick, p);
    CHECK(g.players[0].yaw == G::kYawLimit && g.players[0].pitch == -G::kPitchLimit);
}

static void staminaAndCrouch() {
    G g = fresh();
    PadInput p[2] = {};
    p[0].b = true;
    run(g, p, 12);
    CHECK(g.players[0].crouch > 0.9f && g.players[0].ducking);
    const float walk = G::kCrouchSpeed;
    CHECK(walk < G::kWalkSpeed);
    // Crouching drains stamina until the player is forced up.
    run(g, p, int(1.0f / G::kStaminaDrain / tick) + 10);
    CHECK(g.players[0].winded && !g.players[0].ducking && g.players[0].stamina == 0);
    run(g, p, 60);
    CHECK(g.players[0].crouch < 0.05f);
    // Still holding B: no way back down, even after stamina returns.
    run(g, p, 600);
    CHECK(g.players[0].stamina > G::kWindedRecover && g.players[0].winded && !g.players[0].ducking);
    p[0].b = false;
    run(g, p, 2);
    CHECK(!g.players[0].winded);
    p[0].b = true;
    run(g, p, 12);
    CHECK(g.players[0].ducking);
    // Regeneration waits a moment after the last crouch.
    p[0].b = false;
    const float s0 = g.players[0].stamina;
    run(g, p, 20);
    CHECK(g.players[0].stamina == s0);
    run(g, p, 120);
    CHECK(g.players[0].stamina > s0);
    run(g, p, 60 * 5);
    CHECK(g.players[0].stamina == 1.0f);
    // Crouched movement is slower.
    G m = fresh();
    PadInput pm[2] = {};
    pm[0].b = true; pm[0].stickY = 100;
    run(m, pm, 30);
    const float crouched = m.players[0].z + G::kSpawnZ;
    G n = fresh();
    PadInput pn[2] = {};
    pn[0].stickY = 100;
    run(n, pn, 30);
    CHECK(crouched < n.players[0].z + G::kSpawnZ);
}

static void shooting() {
    // A standing opponent's head above their cover is hit, and a head shot kills.
    G g = fresh();
    PadInput p[2] = {};
    waitReady(g, p);
    aimAt(p[0], g, 0, 0, 1.6f, g.players[1].z);
    fire(p[0]);
    uint32_t ev = g.step(tick, p);
    CHECK(ev & G::ShotP0);
    CHECK((ev & G::HurtP1) && (ev & G::Headshot));
    CHECK(g.players[1].hp == 0 && g.score[0] == 1 && g.roundWinner == 0);
    CHECK(g.phase() == arcade::MatchPhase::RoundEnd && (ev & G::Round));
    // A body shot costs half the health.
    g = fresh();
    p[0] = p[1] = PadInput();
    waitReady(g, p);
    aimAt(p[0], g, 0, 0, 1.2f, g.players[1].z);
    fire(p[0]);
    ev = g.step(tick, p);
    CHECK((ev & G::HurtP1) && !(ev & G::Headshot) && g.players[1].hp == G::kMaxHp - G::kBodyDamage);
    CHECK(g.players[0].marker > 0 && !g.players[0].markerHead && g.players[1].hurt > 0);
    // The shot has a cooldown. A press that comes too early is lost; one just before the gun is ready is buffered.
    PadInput idle[2] = {};
    idle[0] = p[0];
    run(g, idle, 30);
    CHECK(g.players[0].cooldown > 0.5f);
    fire(idle[0]);
    run(g, idle, 10);
    CHECK(g.players[1].hp == G::kMaxHp - G::kBodyDamage);
    run(g, idle, 22);
    CHECK(g.players[0].cooldown < 0.1f && g.players[0].cooldown > 0);
    fire(idle[0]);
    bool fired = false;
    for (int i = 0; i < 10 && !fired; ++i) fired = (g.step(tick, idle) & G::ShotP0) != 0;
    CHECK(fired);
    // Player 1 shoots the other way.
    g = fresh();
    p[0] = p[1] = PadInput();
    waitReady(g, p);
    aimAt(p[1], g, 1, 0, 1.6f, g.players[0].z);
    fire(p[1]);
    ev = g.step(tick, p);
    CHECK((ev & G::ShotP1) && (ev & G::HurtP0) && g.score[1] == 1);
}

static void coverAndPeeking() {
    G g = fresh();
    PadInput p[2] = {};
    waitReady(g, p);
    // The cover hides a standing opponent's torso below the cover line but not their head.
    aimAt(p[0], g, 0, 0, 0.85f, g.players[1].z);
    g.players[0].yaw = p[0].aim; g.players[0].pitch = p[0].pitch;
    CHECK(g.trace(0).covered && !g.trace(0).hit);
    // A crouched opponent directly behind the cover cannot be hit anywhere.
    g.players[1].crouch = 1;
    bool any = false;
    for (float y = 0.1f; y < 1.2f; y += 0.05f) {
        aimAt(p[0], g, 0, 0, y, g.players[1].z);
        g.players[0].yaw = p[0].aim; g.players[0].pitch = p[0].pitch;
        any |= g.trace(0).hit;
    }
    CHECK(!any);
    // Peeking out to the side makes the same crouched opponent a target.
    g.players[1].x = G::kCoverHalfWidth + 0.8f;
    aimAt(p[0], g, 0, g.players[1].x, 0.5f, g.players[1].z);
    g.players[0].yaw = p[0].aim; g.players[0].pitch = p[0].pitch;
    CHECK(g.trace(0).hit && !g.trace(0).head);
    // The shooter's own cover stops shots aimed into it.
    g.players[0].crouch = 1;
    g.players[0].yaw = 0; g.players[0].pitch = 0;
    CHECK(g.trace(0).covered);
    // Shots into the floor end on it (away from the cover).
    g.players[0].crouch = 0; g.players[0].pitch = -30; g.players[0].x = 3;
    const auto t = g.trace(0);
    CHECK(!t.hit && !t.covered && std::fabs(t.y) < 1e-3f);
    // Aiming above the opponent misses and ends at maximum range or the arena.
    g.players[0].pitch = 10; g.players[0].yaw = 0; g.players[0].x = 0;
    CHECK(!g.trace(0).hit);
}

static void doubleKnockout() {
    G g = fresh();
    PadInput p[2] = {};
    waitReady(g, p);
    aimAt(p[0], g, 0, 0, 1.6f, g.players[1].z);
    aimAt(p[1], g, 1, 0, 1.6f, g.players[0].z);
    fire(p[0]); fire(p[1]);
    const uint32_t ev = g.step(tick, p);
    CHECK((ev & G::HurtP0) && (ev & G::HurtP1));
    CHECK(g.roundWinner == -1 && g.score[0] == 0 && g.score[1] == 0);
    CHECK(g.phase() == arcade::MatchPhase::RoundEnd);
    // The next round starts clean.
    run(g, p, int(G::kRoundPause / tick) + 2);
    CHECK(g.phase() == arcade::MatchPhase::Playing);
    CHECK(g.players[0].hp == G::kMaxHp && g.players[1].hp == G::kMaxHp);
    CHECK(g.players[0].x == 0 && g.players[0].z == -G::kSpawnZ && g.players[1].z == G::kSpawnZ);
}

static void matchFlow() {
    G g = fresh();
    PadInput p[2] = {};
    for (int round = 0; round < G::kWinningScore; ++round) {
        waitReady(g, p);
        aimAt(p[0], g, 0, 0, 1.6f, g.players[1].z);
        fire(p[0]);
        const uint32_t ev = g.step(tick, p);
        CHECK(g.score[0] == round + 1);
        if (round < G::kWinningScore - 1) {
            CHECK(g.phase() == arcade::MatchPhase::RoundEnd && (ev & G::Round) && !(ev & G::Finish));
            run(g, p, int(G::kRoundPause / tick) + 2);
            CHECK(g.phase() == arcade::MatchPhase::Playing);
        } else {
            CHECK(g.phase() == arcade::MatchPhase::MatchEnd && (ev & G::Finish) && g.winner == 0);
        }
    }
    // The match stays over.
    const G frozen = g;
    run(g, p, 120);
    CHECK(g.phase() == arcade::MatchPhase::MatchEnd && g.score[0] == G::kWinningScore);
    (void)frozen;
}

static void timeout() {
    G g = fresh();
    PadInput p[2] = {};
    waitReady(g, p);
    aimAt(p[0], g, 0, 0, 1.2f, g.players[1].z);
    fire(p[0]);
    g.step(tick, p);
    p[0] = PadInput();
    run(g, p, int(G::kRoundTime / tick) + 5);
    CHECK(g.roundWinner == 0 && g.score[0] == 1 && g.phase() != arcade::MatchPhase::Playing);
    G h = fresh();
    run(h, p, int(G::kRoundTime / tick) + 5);
    CHECK(h.roundWinner == -1 && h.score[0] == 0 && h.score[1] == 0 && h.phase() == arcade::MatchPhase::RoundEnd);
}

static void soloPractice() {
    G g = fresh(arcade::Mode::Solo);
    PadInput p[2] = {};
    CHECK(g.solo);
    // The target never moves, shoots or crouches, however long you wait; the round never times out.
    const float x = g.players[1].x, z = g.players[1].z;
    run(g, p, 60 * 60);
    CHECK(g.players[1].x == x && g.players[1].z == z && g.players[1].crouch == 0 && g.players[1].cooldown == 0);
    CHECK(g.players[0].hp == G::kMaxHp && g.phase() == arcade::MatchPhase::Playing);
    // A stray input for the second player is ignored in solo.
    p[1].aCount = 5; p[1].stickX = 100; p[1].b = true;
    run(g, p, 30);
    CHECK(g.players[1].x == x && g.players[1].crouch == 0);
    // Kills accumulate without ever ending the practice, and the target respawns in the zone.
    p[1] = PadInput();
    float xs = 0;
    for (int k = 1; k <= 8; ++k) {
        waitReady(g, p);
        aimAt(p[0], g, 0, g.players[1].x, 1.6f, g.players[1].z);
        fire(p[0]);
        g.step(tick, p);
        CHECK(g.score[0] == k && g.phase() == arcade::MatchPhase::RoundEnd);
        run(g, p, int(G::kRoundPause / tick) + 2);
        CHECK(g.phase() == arcade::MatchPhase::Playing && g.players[1].hp == G::kMaxHp);
        CHECK(std::fabs(g.players[1].x) <= 3.0f + 1e-3f);
        xs = std::max(xs, std::fabs(g.players[1].x - x));
    }
    CHECK(xs > 0.3f);
}

static void inputsAndPrime() {
    G g = fresh();
    PadInput p[2] = {};
    p[0].aCount = 9;
    g.prime(p);
    waitReady(g, p);
    CHECK(!(g.step(tick, p) & G::ShotP0));
    // Pressing during the opening cooldown fires as soon as it ends.
    G h = fresh();
    PadInput q[2] = {};
    q[0].aCount = 0;
    ++q[0].aCount;
    h.step(tick, q);
    bool fired = false;
    for (int i = 0; i < 60 && !fired; ++i) fired = (h.step(tick, q) & G::ShotP0) != 0;
    CHECK(!fired);
    G k = fresh();
    PadInput r[2] = {};
    run(k, r, int(G::kRoundStartCooldown / tick) - 3);
    ++r[0].aCount;
    fired = false;
    for (int i = 0; i < 10 && !fired; ++i) fired = (k.step(tick, r) & G::ShotP0) != 0;
    CHECK(fired);
    // Frozen between rounds: input does nothing.
    G e = fresh();
    PadInput s[2] = {};
    waitReady(e, s);
    aimAt(s[0], e, 0, 0, 1.6f, e.players[1].z);
    ++s[0].aCount;
    e.step(tick, s);
    const float zBefore = e.players[0].z;
    s[0].stickY = 100;
    run(e, s, 10);
    CHECK(e.players[0].z == zBefore);
}

static void codec() {
    G g = fresh();
    PadInput p[2] = {};
    waitReady(g, p);
    p[0].stickX = 70; p[0].stickY = 30; p[0].aim = 33.3f; p[0].pitch = -8.4f; p[0].b = true;
    p[1].stickX = -40; p[1].stickY = 90; p[1].aim = -71.2f; p[1].pitch = 12.7f;
    aimAt(p[0], g, 0, 0, 1.2f, g.players[1].z);
    ++p[0].aCount;
    run(g, p, 17);
    arcade::Writer w;
    g.encode(w);
    CHECK(w.n < 120);
    G c;
    c.begin(arcade::Mode::Client, 1);
    arcade::Reader r(w.buf, w.n);
    CHECK(c.decode(r));
    arcade::Writer w2;
    c.encode(w2);
    CHECK(w.n == w2.n && memcmp(w.buf, w2.buf, w.n) == 0);
    CHECK(std::fabs(c.players[0].x - g.players[0].x) < 1e-3f && std::fabs(c.players[0].yaw - g.players[0].yaw) < 1e-2f);
    // Truncated and corrupt data is rejected and leaves the state untouched.
    const G before = c;
    for (size_t cut = 0; cut < w.n; ++cut) {
        arcade::Reader bad(w.buf, cut);
        CHECK(!c.decode(bad));
        CHECK(same(c, before));
    }
    uint8_t extra[250];
    memcpy(extra, w.buf, w.n);
    extra[w.n] = 1;
    arcade::Reader tail(extra, w.n + 1);
    CHECK(!c.decode(tail));
    for (size_t at = 0; at < w.n; ++at) {
        for (int bit = 0; bit < 8; ++bit) {
            uint8_t mutated[250];
            memcpy(mutated, w.buf, w.n);
            mutated[at] ^= uint8_t(1 << bit);
            arcade::Reader m(mutated, w.n);
            G probe = before;
            const bool ok = probe.decode(m);
            if (!ok) CHECK(same(probe, before));
            else { arcade::Writer w3; probe.encode(w3); arcade::Reader again(w3.buf, w3.n); G twice = before; CHECK(twice.decode(again)); }
        }
    }
    // A player outside their own zone is not valid state.
    G bad = g;
    bad.players[0].z = 3;
    arcade::Writer wb;
    bad.encode(wb);
    arcade::Reader rb(wb.buf, wb.n);
    CHECK(!c.decode(rb));
    bad = g;
    bad.players[1].hp = 200;
    arcade::Writer wh;
    bad.encode(wh);
    arcade::Reader rh(wh.buf, wh.n);
    CHECK(!c.decode(rh));
    // A client shows its own sensor straight away.
    c.localInput(1, p[1]);
    CHECK(std::fabs(c.lookYaw(1) - p[1].aim) < 1e-4f && c.lookYaw(0) == c.players[0].yaw);
}

static void gyroLook() {
    using arcade::GyroLook;
    // Device held upright in landscape: gravity along the device's X axis (up), screen right edge along +Y.
    auto turn = [](GyroLook& l, float ax, float ay, float az, float gx, float gy, float gz, int frames) {
        for (int i = 0; i < frames; ++i) l.update(ax, ay, az, gx, gy, gz, tick);
    };
    GyroLook l;
    l.configure(3.0f, 3.0f, 2.0f, 2.0f, 90, 30);
    l.reset();
    // Holding still: no drift, even with a small constant bias that the estimator absorbs.
    turn(l, 1, 0, 0, 0.8f, -0.6f, 0.5f, 240);
    CHECK(std::fabs(l.yaw()) < 1.0f && std::fabs(l.pitch()) < 1.0f && l.settled());
    // Turning about the vertical axis (here +X is up) turns the view by the gain; clockwise seen from above is right.
    l.recenter();
    turn(l, 1, 0, 0, -10, 0, 0, 60);  // 10 deg/s for 1 s
    CHECK(std::fabs(l.yaw() - 30.0f * GyroLook::kYawSign) < 3.0f);
    // The same turn gives the same yaw however the device is tilted: gravity is the reference.
    GyroLook t;
    t.configure(3.0f, 3.0f, 2.0f, 2.0f, 90, 30);
    t.reset();
    turn(t, 0, 0, 1, 0, 0, 0, 120);
    t.recenter();
    turn(t, 0, 0, 1, 0, 0, -10, 60);
    CHECK(std::fabs(t.yaw() - l.yaw()) < 3.0f);
    // The view is limited, and returns as soon as the device does: no wind-up.
    GyroLook c;
    c.configure(3.0f, 3.0f, 2.0f, 2.0f, 90, 30);
    c.reset();
    turn(c, 1, 0, 0, 0, 0, 0, 120);
    turn(c, 1, 0, 0, -40, 0, 0, 120);  // 80 deg of device turn, far beyond the 30 that reach the limit
    CHECK(c.yaw() == 90.0f * GyroLook::kYawSign || c.yaw() == -90.0f * GyroLook::kYawSign);
    const float limit = c.yaw();
    turn(c, 1, 0, 0, 40, 0, 0, 20);  // 13 deg back
    CHECK(std::fabs(c.yaw()) < std::fabs(limit) - 20.0f);
    // Pitch follows the angle at which the screen faces up (gyro about the screen's right edge, tied to gravity) and
    // is limited. Raising the top edge towards the player (the screen turns to face down) looks up.
    GyroLook p;
    p.configure(3.0f, 3.0f, 2.0f, 2.0f, 90, 30);
    p.reset();
    auto tilt = [&](GyroLook& l, float from, float to, int frames) {
        for (int i = 1; i <= frames; ++i) {
            const float s = (from + (to - from) * i / frames) / rad;
            l.update(std::cos(s), 0, std::sin(s), 0, (to - from) / (frames * tick), 0, tick);
        }
    };
    turn(p, 1, 0, 0, 0, 0, 0, 120);
    tilt(p, 0, 10, 60);  // screen turns to face up by 10 degrees
    CHECK(std::fabs(p.pitch() - 20.0f * GyroLook::kPitchSign) < 2.0f && std::fabs(p.yaw()) < 0.5f);
    CHECK(GyroLook::kPitchSign < 0 ? p.pitch() < 0 : p.pitch() > 0);
    tilt(p, 10, 0, 60);
    CHECK(std::fabs(p.pitch()) < 1.0f);
    tilt(p, 0, -10, 60);
    CHECK(GyroLook::kPitchSign < 0 ? p.pitch() > 10 : p.pitch() < -10);
    tilt(p, -10, 60, 120);
    CHECK(std::fabs(p.pitch()) == 30.0f);
    p.recenter();
    CHECK(p.yaw() == 0 && p.pitch() == 0);
    // A gyro that reports a steady turn the device is not making cannot walk the view away: gravity holds it.
    GyroLook d;
    d.configure(3.0f, 3.0f, 2.0f, 2.0f, 90, 30);
    d.reset();
    turn(d, 1, 0, 0, 0, 0, 0, 120);
    turn(d, 1, 0, 0, 0, 2.5f, 0, 1200);  // 20 s of 2.5 deg/s bias on the pitch axis
    CHECK(std::fabs(d.pitch()) < 3.0f);
    // Absurd time steps are ignored.
    GyroLook s;
    s.configure(3.0f, 3.0f, 2.0f, 2.0f, 90, 30);
    s.reset();
    s.update(1, 0, 0, 500, 500, 500, 5.0f);
    s.update(1, 0, 0, 500, 500, 500, 0.0f);
    CHECK(s.yaw() == 0 && s.pitch() == 0);
    // A gain that grows with the view angle is fine near the centre and quick towards the limit, and a turn
    // there and back always returns to the same view.
    GyroLook v;
    v.configure(1.0f, 5.0f, 1.0f, 3.0f, 90, 30);
    v.reset();
    turn(v, 1, 0, 0, 0, 0, 0, 120);
    turn(v, 1, 0, 0, -10, 0, 0, 30);  // 5 deg of device turn
    CHECK(std::fabs(v.yaw()) > 3.0f && std::fabs(v.yaw()) < 8.0f);
    v.recenter();
    int frames = 0;
    while (std::fabs(v.yaw()) < 90.0f && frames < 600) { v.update(1, 0, 0, -20, 0, 0, tick); ++frames; }
    const float travelled = 19.6f * frames * tick;  // deg/s after the dead zone times seconds
    CHECK(travelled > 30.0f && travelled < 45.0f);
    for (int i = 0; i < frames; ++i) v.update(1, 0, 0, 20, 0, 0, tick);
    CHECK(std::fabs(v.yaw()) < 2.0f);
}

static void stickMap() {
    arcade::StickAxis a;
    a.seed(128);
    CHECK(a.map(128) == 0 && a.map(135) == 0 && a.map(121) == 0);
    CHECK(a.map(128 + 96) == 100 && a.map(128 - 96) == -100);
    CHECK(a.map(128 + 60) > 0 && a.map(128 + 60) < 100 && a.map(128 - 60) == -a.map(128 + 60));
    // Travel further than assumed is learned, never above full scale.
    CHECK(a.map(255) == 100 && a.range <= 127.0f);
    CHECK(a.map(128 + 96) < 100);
    // A different rest value is honoured.
    arcade::StickAxis b;
    b.seed(120);
    CHECK(b.map(120) == 0 && b.map(215) > 90);
}

int main() {
    movement();
    viewLimits();
    staminaAndCrouch();
    shooting();
    coverAndPeeking();
    doubleKnockout();
    matchFlow();
    timeout();
    soloPractice();
    inputsAndPrime();
    codec();
    gyroLook();
    stickMap();
    puts(failures ? "FAILURES" : "ALL PASSED");
    return failures ? 1 : 0;
}
