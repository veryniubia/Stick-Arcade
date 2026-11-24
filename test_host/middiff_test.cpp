// Host-side tests for the MidDiff rules and its state codec.
#include "games/middiff/middiff.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <vector>

void MidDiffGame::draw(int, const arcade::DrawCtx&) {}
void MidDiffGame::sfx(uint32_t) {}
static int failures = 0;
#define CHECK(c) do { if (!(c)) { printf("FAIL %d: %s\n", __LINE__, #c); ++failures; } } while (0)
using G = MidDiffGame;
constexpr float tick = 1.f / 60;
constexpr float rad = 57.2957795f;

static void run(G& g, PadInput p[2], int frames) { for (int i = 0; i < frames; ++i) g.step(tick, p); }
static void ready(G& g) {  // no spawn protection, guns ready
    for (auto& pl : g.players) { pl.invuln = 0; pl.cooldown = 0; }
}
static G fresh(arcade::Mode m = arcade::Mode::Host, uint32_t seed = 7) {
    G g;
    g.begin(m, seed);
    PadInput p[2] = {};
    g.prime(p);
    ready(g);
    return g;
}
static void place(G& g, int i, float x, float depth) {
    g.players[i].x = x;
    g.players[i].z = (g.face(i) > 0 ? -1.0f : 1.0f) * depth;
}
static float depthOf(const G& g, int i) { return std::fabs(g.players[i].z); }
static void openGate(G& g) { g.gate.state = G::GateState::Open; g.gate.pos = 1; g.gate.timer = 100; }
static void closeGate(G& g) { g.gate.state = G::GateState::Closed; g.gate.pos = 0; g.gate.timer = 100; }
// Points shooter i's view at a world point.
static void aimAt(PadInput& p, const G& g, int i, float tx, float ty, float tz) {
    const float f = g.face(i);
    const auto& me = g.players[i];
    const float dx = (tx - me.x) * f, dz = (tz - me.z) * f;
    p.aim = std::atan2(dx, dz) * rad;
    p.pitch = std::atan2(ty - (G::kEye + G::jumpHeight(me)), std::hypot(dx, dz)) * rad;
}
static void aimHead(PadInput& p, const G& g, int i) {
    const auto& foe = g.players[1 - i];
    aimAt(p, g, i, foe.x, G::jumpHeight(foe) + G::kStandHeight - 0.2f, foe.z);
}
static void fire(PadInput& p) { ++p.aCount; }
static void jump(PadInput& p, int jx, int jy) { p.jumpX = static_cast<int8_t>(jx); p.jumpY = static_cast<int8_t>(jy); ++p.jumpCount; }
// Hands off the controls without touching the press counters (a counter that goes back would look like a press).
static void quiet(PadInput& p) { p.aim = p.pitch = 0; p.stickX = p.stickY = 0; }
// Both players face each other along x = 0, through the centre arch, ready to shoot.
static G duel() {
    G g = fresh();
    place(g, 0, 0, 8.5f);
    place(g, 1, 0, 8.5f);
    return g;
}
static bool sees(const G& g, int i, float y) {
    PadInput p;
    G copy = g;
    const auto& foe = g.players[1 - i];
    aimAt(p, g, i, foe.x, y, foe.z);
    copy.players[i].yaw = p.aim;
    copy.players[i].pitch = p.pitch;
    return copy.trace(i).hit;
}

static void world() {
    // Every box is well formed; the covers are inside the arena and clear of the mid wall.
    for (int i = 0; i < G::kBoxCount; ++i) {
        const auto& b = G::boxes[i];
        CHECK(b.x0 < b.x1 && b.y0 <= b.y1 && b.z0 < b.z1);
        CHECK(b.x0 >= -G::kArenaHalfWidth && b.x1 <= G::kArenaHalfWidth && b.z0 >= -G::kWallZ && b.z1 <= G::kWallZ);
    }
    for (int i = G::kWalkFirst; i < G::kWalkEnd; ++i) {
        const auto& b = G::boxes[i];
        CHECK(std::fabs(b.z0) >= G::kNearZ - 1e-4f && std::fabs(b.z1) <= G::kFarZ);
        CHECK(!(b.z0 < 0 && b.z1 > 0));
    }
    // The covers of the two halves are laid out differently but offer the same amount of cover.
    float width[2][2] = {};  // [team][high, low]
    for (int i = G::kWalkFirst; i < G::kWalkEnd; ++i) {
        const auto& b = G::boxes[i];
        if (b.kind != G::Kind::HighCover && b.kind != G::Kind::LowCover) continue;
        width[b.z0 < 0 ? 0 : 1][b.kind == G::Kind::HighCover ? 0 : 1] += b.x1 - b.x0;
    }
    CHECK(std::fabs(width[0][0] - 6.0f) < 0.7f && std::fabs(width[1][0] - 5.4f) < 0.7f);
    CHECK(std::fabs(width[0][1] - 5.0f) < 1.3f && std::fabs(width[1][1] - 6.2f) < 1.3f);
    CHECK(G::boxes[11].x0 != -G::boxes[15].x1 || G::boxes[12].x1 != -G::boxes[16].x0);
    // Respawn and start positions are free.
    CHECK(!G::blockedAt(0, -G::kStartDepth, G::kCollideRadius) && !G::blockedAt(0, G::kStartDepth, G::kCollideRadius));
}

static void movement() {
    G g = duel();
    PadInput p[2] = {};
    // Forward for both players is towards the middle and stops at the line nobody may cross.
    p[0].stickY = 100; p[1].stickY = 100;
    run(g, p, 300);
    CHECK(std::fabs(depthOf(g, 0) - G::kNearZ) < 1e-3f && std::fabs(depthOf(g, 1) - G::kNearZ) < 1e-3f);
    CHECK(g.players[1].z - g.players[0].z >= 2 * G::kNearZ - 1e-3f);
    // Walking speed.
    g = duel();
    p[0].stickY = 100; p[1].stickY = 0;
    run(g, p, 30);
    CHECK(std::fabs((8.5f - depthOf(g, 0)) - G::kWalkSpeed * 0.5f) < 0.03f);
    // Backwards stops at the back of the zone, or at the low cover standing in the way.
    g = duel();
    p[0].stickY = p[1].stickY = -100;
    run(g, p, 400);
    CHECK(std::fabs(depthOf(g, 1) - G::kFarZ) < 1e-3f);                                  // gap between the CT low walls
    CHECK(std::fabs(depthOf(g, 0) - (11.1f - G::kCollideRadius)) < 0.08f);               // T low wall in the way
    // Right is the player's right: +x for team 0, -x for team 1.
    g = duel();
    p[0].stickY = p[1].stickY = 0;
    p[0].stickX = p[1].stickX = 100;
    run(g, p, 30);
    CHECK(g.players[0].x > 1.0f && g.players[1].x < -1.0f);
    run(g, p, 300);
    CHECK(std::fabs(g.players[0].x - G::kHalfWidth) < 1e-3f && std::fabs(g.players[1].x + G::kHalfWidth) < 1e-3f);
    // Covers are solid: a player behind a high wall cannot walk through it, and slides along it.
    g = duel();
    place(g, 0, -3.0f, 9.0f);
    p[0] = PadInput(); p[0].stickY = 100;
    run(g, p, 120);
    CHECK(std::fabs(depthOf(g, 0) - (6.8f + G::kCollideRadius)) < 0.08f && std::fabs(g.players[0].x + 3.0f) < 1e-3f);
    p[0].stickX = 100;
    run(g, p, 120);
    CHECK(g.players[0].x > -1.8f + G::kCollideRadius - 1e-3f && depthOf(g, 0) < 6.9f);  // went round the end into the gap
    // ... and through the gap the whole way to the line.
    g = duel();
    place(g, 0, 0.0f, 9.0f);
    p[0] = PadInput(); p[0].stickY = 100;
    run(g, p, 120);
    CHECK(std::fabs(depthOf(g, 0) - G::kNearZ) < 1e-3f);
    // The view direction never changes where the stick takes the player.
    G a = duel(), b = duel();
    PadInput pa[2] = {}, pb[2] = {};
    pa[0].stickX = pb[0].stickX = 60; pa[0].stickY = pb[0].stickY = 80;
    pb[0].aim = 85; pb[0].pitch = -20;
    run(a, pa, 40); run(b, pb, 40);
    CHECK(a.players[0].x == b.players[0].x && a.players[0].z == b.players[0].z);
    // Diagonals are not faster than straight lines.
    G c = duel(), d = duel();
    PadInput pc[2] = {}, pd[2] = {};
    pc[0].stickX = 100;
    pd[0].stickX = 100; pd[0].stickY = 100;
    run(c, pc, 20); run(d, pd, 20);
    CHECK(std::hypot(d.players[0].x, d.players[0].z + 8.5f) <= std::hypot(c.players[0].x, c.players[0].z + 8.5f) + 1e-3f);
    // Garbage input is harmless.
    PadInput bad[2] = {};
    bad[0].aim = std::numeric_limits<float>::quiet_NaN();
    bad[0].pitch = std::numeric_limits<float>::infinity();
    bad[0].stickX = -128; bad[0].stickY = 127;
    bad[0].jumpX = -128; bad[0].jumpY = 127; ++bad[0].jumpCount;
    run(c, bad, 10);
    CHECK(std::isfinite(c.players[0].x) && c.players[0].yaw == 0 && c.players[0].pitch == 0);
    // View limits.
    G v = duel();
    PadInput pv[2] = {};
    pv[0].aim = 500; pv[0].pitch = -500;
    v.step(tick, pv);
    CHECK(v.players[0].yaw == G::kYawLimit && v.players[0].pitch == -G::kPitchLimit);
}

static void jumping() {
    G g = duel();
    PadInput p[2] = {};
    place(g, 0, 0, 9.0f);
    place(g, 1, 0, 9.0f);
    jump(p[0], 0, 100);
    g.step(tick, p);
    CHECK(g.players[0].jumpT > 0 && g.players[0].stamina < 0.72f && g.players[0].stamina > 0.6f);
    float peak = 0;
    for (int i = 0; i < 40; ++i) { g.step(tick, p); peak = std::max(peak, G::jumpHeight(g.players[0])); }
    CHECK(g.players[0].jumpT == 0);
    CHECK(std::fabs(peak - G::kJumpHeight) < 0.03f);
    CHECK(std::fabs((9.0f - depthOf(g, 0)) - G::kJumpDistance) < 0.1f);  // towards the middle
    CHECK(g.players[1].z == 9.0f && g.players[1].x == 0);
    // Direction is relative to the player: right is +x for team 0 and -x for team 1, back is away from the middle.
    G s = duel();
    PadInput ps[2] = {};
    jump(ps[0], 100, 0); jump(ps[1], 100, 0);
    run(s, ps, 40);
    CHECK(std::fabs(s.players[0].x - G::kJumpDistance) < 0.1f && std::fabs(s.players[1].x + G::kJumpDistance) < 0.1f);
    G back = duel();
    place(back, 0, -4.5f, 8.5f);
    PadInput pb[2] = {};
    jump(pb[0], 0, -100);
    run(back, pb, 40);
    CHECK(std::fabs((depthOf(back, 0) - 8.5f) - G::kJumpDistance) < 0.1f);
    // Diagonal hops cover the same distance.
    G diag = duel();
    place(diag, 0, -4.0f, 9.5f);
    PadInput pd[2] = {};
    jump(pd[0], 90, 90);
    run(diag, pd, 40);
    CHECK(std::fabs(std::hypot(diag.players[0].x + 4.0f, depthOf(diag, 0) - 9.5f) - G::kJumpDistance) < 0.1f);
    // No stick: a hop on the spot, which still costs stamina.
    G spot = duel();
    PadInput pp[2] = {};
    jump(pp[0], 5, -8);
    spot.step(tick, pp);
    CHECK(spot.players[0].jumpT > 0 && spot.players[0].stamina < 0.72f);
    run(spot, pp, 40);
    CHECK(spot.players[0].x == 0 && std::fabs(depthOf(spot, 0) - 8.5f) < 1e-4f);
    // In the air the direction is fixed and the stick does nothing; another press is ignored.
    G air = duel();
    PadInput pa[2] = {};
    jump(pa[0], 0, 100);
    air.step(tick, pa);
    pa[0].stickX = 100; pa[0].stickY = -100;
    jump(pa[0], 100, 0);
    const float stamina = air.players[0].stamina;
    run(air, pa, 10);
    CHECK(air.players[0].x == 0 && air.players[0].stamina <= stamina + 0.06f);
    // A hop cannot cross a cover: it stops at the wall.
    G wall = duel();
    place(wall, 0, -3.0f, 7.6f);
    PadInput pw[2] = {};
    jump(pw[0], 0, 100);
    run(wall, pw, 40);
    CHECK(std::fabs(depthOf(wall, 0) - (6.8f + G::kCollideRadius)) < 0.08f);
    // Hops cost a third of the stamina; it refills a third per second, so they cannot be chained for ever.
    G chain = duel();
    PadInput pc[2] = {};
    int hops = 0;
    for (int k = 0; k < 20; ++k) {
        const bool before = chain.players[0].jumpT > 0;
        (void)before;
        jump(pc[0], 100, 0);
        chain.step(tick, pc);
        if (chain.players[0].jumpT > 0) ++hops;
        run(chain, pc, 34);
        pc[0].jumpX = (k % 2) ? 100 : -100;
    }
    CHECK(hops >= 5 && hops < 20);
    // Without stamina the press does nothing; after a second there is enough for one more.
    G dry = duel();
    PadInput py[2] = {};
    dry.players[0].stamina = 0.2f;
    jump(py[0], 0, 100);
    dry.step(tick, py);
    CHECK(dry.players[0].jumpT == 0);
    run(dry, py, 60);
    jump(py[0], 0, 100);
    dry.step(tick, py);
    CHECK(dry.players[0].jumpT > 0);
    // Hitting a jumper: the head rides 0.7 m higher, so a shot at the standing head height hits the body.
    G hit = duel();
    PadInput ph[2] = {};
    hit.players[1].jumpT = G::kJumpTime / 2;
    aimAt(ph[0], hit, 0, 0, 1.6f, hit.players[1].z);
    G probe = hit;
    probe.players[0].yaw = ph[0].aim; probe.players[0].pitch = ph[0].pitch;
    const auto t = probe.trace(0);
    CHECK(t.hit && !t.head);
    // The shooter's own eye rises when jumping.
    G eye = duel();
    eye.players[0].jumpT = G::kJumpTime / 2;
    eye.players[0].pitch = 0;
    const auto te = eye.trace(0);
    CHECK(std::fabs(te.y - (G::kEye + G::kJumpHeight)) < 1e-3f);
}

static void shooting() {
    // A head shot kills and scores.
    G g = duel();
    PadInput p[2] = {};
    aimHead(p[0], g, 0);
    fire(p[0]);
    uint32_t ev = g.step(tick, p);
    CHECK(ev & G::ShotP0);
    CHECK((ev & G::HurtP1) && (ev & G::Headshot) && (ev & G::DieP1) && !(ev & G::DieP0));
    CHECK(g.players[1].hp == 0 && g.score[0] == 1 && g.score[1] == 0);
    CHECK(g.phase() == arcade::MatchPhase::Playing);  // no rounds: the match goes on
    // A body shot costs half the health and a second one kills.
    g = duel();
    p[0] = PadInput();
    aimAt(p[0], g, 0, 0, 1.0f, g.players[1].z);
    fire(p[0]);
    ev = g.step(tick, p);
    CHECK((ev & G::HurtP1) && !(ev & G::Headshot) && !(ev & G::DieP1) && g.players[1].hp == G::kMaxHp - G::kBodyDamage);
    CHECK(g.players[0].marker > 0 && !g.players[0].markerHead && g.players[1].hurt > 0);
    // The shot has a cooldown: an early press is lost, one just before the gun is ready is buffered.
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
    bool fired = false, killed = false;
    for (int i = 0; i < 10 && !fired; ++i) {
        const uint32_t e = g.step(tick, idle);
        fired = (e & G::ShotP0) != 0;
        killed = (e & G::DieP1) != 0;
    }
    CHECK(fired && killed);
    // Team 1 shoots the other way.
    g = duel();
    p[0] = p[1] = PadInput();
    aimHead(p[1], g, 1);
    fire(p[1]);
    ev = g.step(tick, p);
    CHECK((ev & G::ShotP1) && (ev & G::DieP0) && g.score[1] == 1);
    // Both shoot in the same step: both die and both score, and the match goes on.
    g = duel();
    p[0] = p[1] = PadInput();
    aimHead(p[0], g, 0);
    aimHead(p[1], g, 1);
    fire(p[0]); fire(p[1]);
    ev = g.step(tick, p);
    CHECK((ev & G::DieP0) && (ev & G::DieP1) && g.score[0] == 1 && g.score[1] == 1);
    CHECK(g.phase() == arcade::MatchPhase::Playing);
    // A press during the opening cooldown fires as soon as it ends.
    G k = fresh();
    PadInput r[2] = {};
    k.players[0].cooldown = G::kSpawnCooldown;
    run(k, r, 14);
    fire(r[0]);
    fired = false;
    for (int i = 0; i < 20 && !fired; ++i) fired = (k.step(tick, r) & G::ShotP0) != 0;
    CHECK(fired);
}

static void coverAndSight() {
    // High cover: the head is over it, the chest is not.
    G g = duel();
    openGate(g);
    place(g, 0, -3.0f, 8.5f);
    place(g, 1, -3.0f, 8.5f);
    CHECK(sees(g, 0, 1.6f) && sees(g, 1, 1.6f));
    CHECK(!sees(g, 0, 1.0f) && !sees(g, 1, 1.0f));
    PadInput p;
    G probe = g;
    aimAt(p, g, 0, -3.0f, 1.0f, 8.5f);
    probe.players[0].yaw = p.aim; probe.players[0].pitch = p.pitch;
    CHECK(probe.trace(0).covered);
    // Low cover: chest and head over it, legs and hips behind it.
    G low = duel();
    place(low, 0, 0.5f, 8.5f);
    place(low, 1, 0.5f, 12.5f);
    CHECK(sees(low, 0, 1.3f) && sees(low, 0, 1.6f));
    CHECK(!sees(low, 0, 1.0f) && !sees(low, 0, 0.5f));
    // The far team's low cover is about 24 m away from the other team's.
    CHECK(std::fabs((low.players[1].z - low.players[0].z) - 21.0f) < 1e-3f);
    place(low, 0, 0.5f, 10.7f);
    place(low, 1, 0.5f, 14.3f);
    CHECK(low.players[1].z - low.players[0].z >= 24.9f);
    // The mid wall: a pier stops everything, the gate stops everything while closed and lets shots through when lifted.
    G wall = duel();
    place(wall, 0, -1.5f, 8.5f);
    place(wall, 1, -1.5f, 8.5f);
    openGate(wall);
    CHECK(!sees(wall, 0, 1.6f) && !sees(wall, 0, 2.3f));
    place(wall, 0, -3.0f, 8.5f);
    place(wall, 1, -3.0f, 8.5f);
    closeGate(wall);
    CHECK(!sees(wall, 0, 1.6f));
    openGate(wall);
    CHECK(sees(wall, 0, 1.6f));
    wall.gate.state = G::GateState::Opening; wall.gate.pos = 0.3f;  // lifted a little: still shut at head height
    CHECK(!sees(wall, 0, 1.6f));
    // The centre arch is always open.
    place(wall, 0, 0.0f, 8.5f);
    place(wall, 1, 0.0f, 8.5f);
    closeGate(wall);
    CHECK(sees(wall, 0, 1.6f));
    // Barrels: the stack of two blocks the head, the single barrels leave it free.
    place(wall, 0, 2.7f, 8.5f);
    place(wall, 1, 2.7f, 8.5f);
    CHECK(!sees(wall, 0, 1.6f));
    place(wall, 0, 3.5f, 8.5f);
    place(wall, 1, 3.5f, 8.5f);
    CHECK(sees(wall, 0, 1.6f) && !sees(wall, 0, 0.8f));
    // Nobody is hurt through a cover and nothing registers a hit on a miss.
    G miss = duel();
    place(miss, 0, -1.5f, 8.5f);
    place(miss, 1, -1.5f, 8.5f);
    PadInput pm[2] = {};
    aimHead(pm[0], miss, 0);
    fire(pm[0]);
    const uint32_t ev = miss.step(tick, pm);
    CHECK((ev & G::ShotP0) && !(ev & G::HurtP1) && miss.players[1].hp == G::kMaxHp && miss.players[0].marker == 0);
}

static void gate() {
    G g = fresh();
    PadInput p[2] = {};
    struct Wait { G::GateState state; float seconds; };
    std::vector<Wait> waits;
    G::GateState last = g.gate.state;
    float stateStart = 0, t = 0;
    int warnings = 0, moves = 0;
    float warnBefore = -1;
    for (int i = 0; i < 60 * 240; ++i) {
        const uint32_t ev = g.step(tick, p);
        t += tick;
        if (ev & G::GateMove) { ++warnings; warnBefore = t; }
        if (g.gate.state != last) {
            waits.push_back({last, t - stateStart});
            if (last == G::GateState::Closed || last == G::GateState::Open) {
                ++moves;
                CHECK(std::fabs((t - warnBefore) - G::kGateWarn) < 0.05f);  // rattles just before it moves
            }
            last = g.gate.state;
            stateStart = t;
        }
        CHECK(g.gate.pos >= 0 && g.gate.pos <= 1);
    }
    float closedMin = 99, closedMax = 0, openMin = 99, openMax = 0;
    int closedN = 0, openN = 0;
    for (const auto& w : waits) {
        if (w.state == G::GateState::Closed) { closedMin = std::min(closedMin, w.seconds); closedMax = std::max(closedMax, w.seconds); ++closedN; }
        if (w.state == G::GateState::Open) { openMin = std::min(openMin, w.seconds); openMax = std::max(openMax, w.seconds); ++openN; }
        if (w.state == G::GateState::Opening || w.state == G::GateState::Closing) CHECK(std::fabs(w.seconds - G::kGateMoveTime) < 0.05f);
    }
    CHECK(closedN >= 10 && openN >= 10);
    CHECK(closedMin >= G::kGateClosedMin - 0.05f && closedMax <= G::kGateClosedMax + 0.05f);
    CHECK(openMin >= G::kGateOpenMin - 0.05f && openMax <= G::kGateOpenMax + 0.05f);
    CHECK(closedMax - closedMin > 1.0f && openMax - openMin > 1.0f);  // irregular, not a fixed rhythm
    CHECK(warnings >= moves - 1 && warnings <= moves + 1);
    // The same seed gives the same gate; another seed another.
    G a = fresh(arcade::Mode::Host, 11), b = fresh(arcade::Mode::Host, 11), c = fresh(arcade::Mode::Host, 12);
    run(a, p, 1000); run(b, p, 1000); run(c, p, 1000);
    CHECK(a.gate.pos == b.gate.pos && a.gate.timer == b.gate.timer);
    CHECK(a.gate.timer != c.gate.timer || a.gate.pos != c.gate.pos);
    // The gate keeps working after the match is over (it is scenery).
    G e = duel();
    e.phase_ = arcade::MatchPhase::MatchEnd;
    const float before = e.gate.timer;
    run(e, p, 30);
    CHECK(e.gate.timer < before);
}

static void lifeAndDeath() {
    G g = duel();
    PadInput p[2] = {};
    aimHead(p[0], g, 0);
    fire(p[0]);
    g.step(tick, p);
    CHECK(g.players[1].dead > G::kRespawnTime - 0.02f && g.players[1].hp == 0);
    quiet(p[0]);
    // The dead cannot move, shoot or be shot, and stay where they fell.
    const float x = g.players[1].x, z = g.players[1].z;
    p[1].stickX = 100; p[1].stickY = 100; fire(p[1]); jump(p[1], 100, 100);
    uint32_t ev = 0;
    for (int i = 0; i < 60; ++i) ev |= g.step(tick, p);
    CHECK(!(ev & G::ShotP1) && g.players[1].x == x && g.players[1].z == z && g.players[1].dead > 0);
    G probe = g;
    aimAt(p[0], g, 0, x, 1.6f, z);
    probe.players[0].yaw = p[0].aim; probe.players[0].pitch = p[0].pitch;
    CHECK(!probe.trace(0).hit);
    // After 1.5 s they are back, at full health, protected, somewhere in their own half.
    for (int i = 0; i < 40 && g.players[1].dead > 0; ++i) g.step(tick, p);
    CHECK(g.players[1].dead == 0 && g.players[1].hp == G::kMaxHp);
    CHECK(g.players[1].invuln > G::kSpawnInvuln - 0.05f && g.players[1].invuln <= G::kSpawnInvuln);
    CHECK(g.players[1].z > 0 && depthOf(g, 1) >= G::kNearZ && depthOf(g, 1) <= G::kFarZ && std::fabs(g.players[1].x) <= G::kHalfWidth);
    CHECK(!G::blockedAt(g.players[1].x, g.players[1].z, G::kCollideRadius));
    CHECK(g.players[1].stamina == 1.0f && g.players[1].jumpT == 0);
    // Protection: shots are absorbed (and show as such), no health is lost and nobody scores.
    place(g, 1, 0, 8.5f);
    g.players[0].cooldown = 0;
    quiet(p[1]);
    aimHead(p[0], g, 0);
    fire(p[0]);
    ev = g.step(tick, p);
    CHECK((ev & G::ShotP0) && !(ev & G::HurtP1) && g.players[1].hp == G::kMaxHp && g.players[0].markerShield);
    CHECK(g.score[0] == 1);
    // It ends after 1.5 s...
    G time = duel();
    PadInput pt[2] = {};
    time.players[1].invuln = G::kSpawnInvuln;
    run(time, pt, 85);
    CHECK(time.players[1].invuln > 0);
    run(time, pt, 8);
    CHECK(time.players[1].invuln == 0);
    // ... or when the protected player fires.
    G fireEnd = duel();
    PadInput pf[2] = {};
    fireEnd.players[1].invuln = G::kSpawnInvuln;
    fire(pf[1]);
    const uint32_t evf = fireEnd.step(tick, pf);
    CHECK((evf & G::ShotP1) && fireEnd.players[1].invuln == 0);
    // Respawns are spread over the half and never in a cover.
    G spread = duel();
    PadInput ps[2] = {};
    std::vector<std::pair<float, float>> spots;
    for (int k = 0; k < 24; ++k) {
        ready(spread);
        spread.score[0] = 0;  // keep the match going
        place(spread, 0, 0, 8.5f);
        place(spread, 1, 0, 8.5f);
        spread.players[1].dead = 0;
        spread.players[1].hp = G::kMaxHp;
        aimHead(ps[0], spread, 0);
        fire(ps[0]);
        spread.step(tick, ps);
        quiet(ps[0]);
        for (int i = 0; i < 100 && spread.players[1].dead > 0; ++i) spread.step(tick, ps);
        CHECK(spread.players[1].dead == 0);
        spots.push_back(std::make_pair(spread.players[1].x, spread.players[1].z));
        CHECK(!G::blockedAt(spots.back().first, spots.back().second, G::kCollideRadius));
        CHECK(spots.back().second >= G::kNearZ && spots.back().second <= G::kFarZ);
    }
    float minX = 9, maxX = -9, minZ = 99, maxZ = 0;
    for (const auto& s : spots) {
        minX = std::min(minX, s.first); maxX = std::max(maxX, s.first);
        minZ = std::min(minZ, s.second); maxZ = std::max(maxZ, s.second);
    }
    CHECK(maxX - minX > 4.0f && maxZ - minZ > 3.0f);
}

static void matchFlow() {
    // Scoring: first to 5, then the match is over and stays over.
    G g = duel();
    PadInput p[2] = {};
    for (int k = 1; k <= G::kWinningScore; ++k) {
        ready(g);
        place(g, 0, 0, 8.5f);
        place(g, 1, 0, 8.5f);
        g.players[1].dead = 0;
        g.players[1].hp = G::kMaxHp;
        aimHead(p[0], g, 0);
        fire(p[0]);
        const uint32_t ev = g.step(tick, p);
        CHECK(g.score[0] == k && (ev & G::DieP1));
        if (k < G::kWinningScore) CHECK(g.phase() == arcade::MatchPhase::Playing && g.winner == -1);
    }
    CHECK(g.phase() == arcade::MatchPhase::MatchEnd && g.winner == 0 && g.idle(0));
    const uint8_t s0 = g.score[0];
    quiet(p[0]);
    run(g, p, 200);
    CHECK(g.phase() == arcade::MatchPhase::MatchEnd && g.score[0] == s0 && g.players[1].dead > 0);
    // A tie at 5 does not end the game; the next point does.
    G t = duel();
    t.score[0] = t.score[1] = 4;
    PadInput pt[2] = {};
    aimHead(pt[0], t, 0);
    aimHead(pt[1], t, 1);
    fire(pt[0]); fire(pt[1]);
    t.step(tick, pt);
    CHECK(t.score[0] == 5 && t.score[1] == 5 && t.phase() == arcade::MatchPhase::Playing && t.winner == -1);
    for (int i = 0; i < 100 && (t.players[0].dead > 0 || t.players[1].dead > 0); ++i) t.step(tick, pt);
    ready(t);
    place(t, 0, 0, 8.5f);
    place(t, 1, 0, 8.5f);
    aimHead(pt[0], t, 0);
    fire(pt[0]);
    t.step(tick, pt);
    CHECK(t.score[0] == 6 && t.phase() == arcade::MatchPhase::MatchEnd && t.winner == 0);
    // A rematch swaps the sides: player 0 becomes team 1 and moves to the +z half, and the scores start again.
    CHECK(g.team(0) == 0 && g.team(1) == 1);
    g.begin(arcade::Mode::Host, 99);
    CHECK(g.team(0) == 1 && g.team(1) == 0 && g.face(0) == -1.0f && g.score[0] == 0 && g.score[1] == 0 && g.winner == -1);
    CHECK(g.players[0].z > 0 && g.players[1].z < 0 && g.phase() == arcade::MatchPhase::Playing);
    // The stick follows the team: right is still the player's right.
    ready(g);
    place(g, 0, 0, 9.0f);
    PadInput ps[2] = {};
    ps[0].stickX = 100;
    run(g, ps, 20);
    CHECK(g.players[0].x < -1.0f);
    // The next rematch swaps back.
    g.phase_ = arcade::MatchPhase::MatchEnd;
    g.begin(arcade::Mode::Host, 100);
    CHECK(g.team(0) == 0 && g.players[0].z < 0);
    // A new match that was not preceded by one that ended does not swap.
    g.begin(arcade::Mode::Host, 101);
    CHECK(g.team(0) == 0);
}

static void soloPractice() {
    bool seen[2] = {};
    for (uint32_t seed = 0; seed < 16; ++seed) seen[fresh(arcade::Mode::Solo, seed).team(0)] = true;
    CHECK(seen[0] && seen[1]);  // the team is random
    G g = fresh(arcade::Mode::Solo, 42);
    PadInput p[2] = {};
    CHECK(g.solo);
    place(g, 0, 0, 8.5f);
    place(g, 1, 0, 8.5f);
    // The target never moves, shoots or jumps; the player is never hurt; nothing times out.
    run(g, p, 60 * 60);
    CHECK(g.players[1].x == 0 && std::fabs(depthOf(g, 1) - 8.5f) < 1e-4f && g.players[1].jumpT == 0);
    CHECK(g.players[0].hp == G::kMaxHp && g.phase() == arcade::MatchPhase::Playing && g.score[1] == 0);
    // A stray input for the second player is ignored in solo.
    p[1].aCount = 5; p[1].stickX = 100; p[1].jumpCount = 3;
    run(g, p, 30);
    CHECK(g.players[1].x == 0 && g.players[1].jumpT == 0 && g.score[1] == 0);
    // Kills pile up without ending the practice; the target comes back in plain sight of the player.
    p[1] = PadInput();
    g.prime(p);
    int distinct = 0;
    float lastX = -99, lastZ = -99;
    for (int k = 1; k <= 14; ++k) {
        ready(g);
        g.players[1].hp = G::kMaxHp;
        aimHead(p[0], g, 0);
        fire(p[0]);
        g.step(tick, p);
        CHECK(g.score[0] == k && g.phase() == arcade::MatchPhase::Playing);
        quiet(p[0]);
        for (int i = 0; i < 100 && g.players[1].dead > 0; ++i) g.step(tick, p);
        const auto& tg = g.players[1];
        CHECK(tg.dead == 0 && tg.hp == G::kMaxHp && tg.invuln > 1.0f);
        CHECK(g.segmentClear(g.players[0].x, G::kEye, g.players[0].z, tg.x, G::kStandHeight - 0.2f, tg.z));
        CHECK(depthOf(g, 1) >= G::kNearZ && depthOf(g, 1) <= G::kFarZ && !G::blockedAt(tg.x, tg.z, G::kCollideRadius));
        if (std::fabs(tg.x - lastX) > 0.3f || std::fabs(tg.z - lastZ) > 0.3f) ++distinct;
        lastX = tg.x; lastZ = tg.z;
    }
    CHECK(distinct >= 10);
    CHECK(g.score[0] == 14 && g.winner == -1);
    CHECK(g.players[0].z * g.players[1].z < 0);  // the target is on the other side
}

static void inputsAndPrime() {
    G g = fresh();
    PadInput p[2] = {};
    p[0].aCount = 9; p[0].jumpCount = 4;
    g.prime(p);
    place(g, 0, 0, 9.0f);
    run(g, p, 60);
    CHECK(g.players[0].jumpT == 0 && g.players[0].stamina == 1.0f && g.players[0].cooldown == 0 && g.players[0].flash == 0);
    // A press made while dead does not fire or hop when the player comes back.
    G d = duel();
    PadInput pd[2] = {};
    aimHead(pd[0], d, 0);
    fire(pd[0]);
    d.step(tick, pd);
    quiet(pd[0]);
    fire(pd[1]); jump(pd[1], 0, 100);
    run(d, pd, 130);
    CHECK(d.players[1].dead == 0 && d.players[1].jumpT == 0 && d.players[1].cooldown == 0 && d.players[1].flash == 0);
}

static bool same(const G& a, const G& b) {
    arcade::Writer x, y;
    a.encode(x);
    b.encode(y);
    return x.n == y.n && memcmp(x.buf, y.buf, x.n) == 0;
}

static void codec() {
    G g = duel();
    PadInput p[2] = {};
    place(g, 0, -1.2f, 9.4f);
    place(g, 1, 1.1f, 12.2f);
    p[0].stickX = 70; p[0].stickY = 30; p[0].aim = 33.3f; p[0].pitch = -8.4f;
    p[1].stickX = -40; p[1].stickY = 90; p[1].aim = -71.2f; p[1].pitch = 12.7f;
    aimAt(p[0], g, 0, 1.1f, 1.0f, g.players[1].z);
    fire(p[0]);
    jump(p[1], 100, 100);
    run(g, p, 7);
    g.gate.state = G::GateState::Opening; g.gate.pos = 0.43f; g.gate.timer = 0.5f;
    g.score[0] = 2; g.score[1] = 1;
    arcade::Writer w;
    g.encode(w);
    CHECK(w.n < 140);
    G c;
    c.begin(arcade::Mode::Client, 1);
    arcade::Reader r(w.buf, w.n);
    CHECK(c.decode(r));
    arcade::Writer w2;
    c.encode(w2);
    CHECK(w.n == w2.n && memcmp(w.buf, w2.buf, w.n) == 0);
    CHECK(std::fabs(c.players[0].x - g.players[0].x) < 1e-3f && std::fabs(c.players[0].yaw - g.players[0].yaw) < 1e-2f);
    CHECK(c.score[0] == 2 && c.score[1] == 1 && c.gate.state == G::GateState::Opening && std::fabs(c.gate.pos - 0.43f) < 0.01f);
    CHECK(c.players[1].jumpT > 0 && std::fabs(G::jumpHeight(c.players[1]) - G::jumpHeight(g.players[1])) < 0.02f);
    // A dead player with its timers, in a swapped match, round-trips too.
    G d = duel();
    d.begin(arcade::Mode::Host, 5);
    d.phase_ = arcade::MatchPhase::MatchEnd;
    d.begin(arcade::Mode::Host, 6);
    ready(d);
    place(d, 0, 0, 8.5f); place(d, 1, 0, 8.5f);
    PadInput pk[2] = {};
    aimHead(pk[0], d, 0);
    fire(pk[0]);
    d.step(tick, pk);
    run(d, pk, 20);
    arcade::Writer wd;
    d.encode(wd);
    G cd;
    cd.begin(arcade::Mode::Client, 1);
    arcade::Reader rd(wd.buf, wd.n);
    CHECK(cd.decode(rd) && same(cd, d) && cd.swap_ == 1 && cd.players[1].dead > 0 && cd.players[1].hp == 0);
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
            if (!probe.decode(m)) CHECK(same(probe, before));
            else {
                arcade::Writer w3;
                probe.encode(w3);
                arcade::Reader again(w3.buf, w3.n);
                G twice = before;
                CHECK(twice.decode(again));
            }
        }
    }
    // States the rules can never produce are not valid.
    auto invalid = [&](void (*poke)(G&)) {
        G bad = g;
        poke(bad);
        arcade::Writer wb;
        bad.encode(wb);
        arcade::Reader rb(wb.buf, wb.n);
        G probe = before;
        CHECK(!probe.decode(rb));
        CHECK(same(probe, before));
    };
    invalid([](G& s) { s.players[0].z = 3; });                                   // outside its own half
    invalid([](G& s) { s.players[1].yaw = 71; });
    invalid([](G& s) { s.players[1].hp = 200; });
    invalid([](G& s) { s.players[0].dead = 1.0f; });                              // dead with health left
    invalid([](G& s) { s.players[0].invuln = 3.0f; });
    invalid([](G& s) { s.players[0].jumpT = 1.0f; });
    invalid([](G& s) { s.swap_ = 2; });
    invalid([](G& s) { s.phase_ = static_cast<arcade::MatchPhase>(1); });
    invalid([](G& s) { s.phase_ = arcade::MatchPhase::MatchEnd; });               // over without a winner
    invalid([](G& s) { s.score[0] = 5; s.winner = -1; });
    invalid([](G& s) { s.gate.state = static_cast<G::GateState>(4); });
    // The local client applies the same stop as the host, then follows fresh input immediately.
    c.localInput(1, p[1]);
    CHECK(c.lookYaw(1) == -G::kYawLimit);
    p[1].aim = 35.0f;
    c.localInput(1, p[1]);
    CHECK(std::fabs(c.lookYaw(1) - p[1].aim) < 1e-4f && c.lookYaw(0) == c.players[0].yaw);
}

static void burgers() {
    PadInput p[2]={}; G g=duel();
    const float firstSpawn=g.burger.timer;
    CHECK(firstSpawn>=8 && firstSpawn<=15);
    run(g,p,479); CHECK(g.burger.state==G::BurgerState::Waiting);
    while(g.burger.state==G::BurgerState::Waiting) g.step(tick,p);
    CHECK(g.elapsed>=8 && g.elapsed<=15.02f && g.burger.timer==0);
    run(g,p,1200); CHECK(g.burger.state==G::BurgerState::Pickup);
    g.burger.state=G::BurgerState::Pickup;g.burger.timer=0;
    g.burger.x=0; g.burger.y=1.9f; g.burger.z=0;
    aimAt(p[0],g,0,0,1.9f,0); fire(p[0]); g.step(tick,p);
    CHECK(g.holdingBurger(0) && g.players[1].hp==100);
    fire(p[0]); g.step(tick,p); CHECK(g.holdingBurger(0));
    run(g,p,70); fire(p[0]); g.step(tick,p);
    CHECK(g.burger.state==G::BurgerState::Flying && g.burger.timer==G::kBurgerFuse);
    run(g,p,119); CHECK(g.burger.state==G::BurgerState::Flying);
    run(g,p,2); CHECK(g.burger.state==G::BurgerState::Blast);
    run(g,p,28); CHECK(g.burger.state==G::BurgerState::Waiting);
    // Closed gate blocks a pickup shot.
    g=duel(); p[0]=PadInput(); g.burger.state=G::BurgerState::Pickup;g.burger.timer=0;
    g.burger.x=-3;g.burger.y=2;g.burger.z=.6f;place(g,0,-3,8);
    aimAt(p[0],g,0,-3,2,.6f);fire(p[0]);g.step(tick,p);
    CHECK(!g.holdingBurger(0));
    // Floor and thin gate bounce, with no early detonation.
    g=duel();g.burger.state=G::BurgerState::Flying;g.burger.timer=2;
    g.burger.x=0;g.burger.y=.16f;g.burger.z=-4;g.burger.vy=-10;
    g.step(tick,p);CHECK(g.burger.vy>0 && g.burger.y>=G::kGrenadeRadius);
    g.burger.x=-3;g.burger.y=2;g.burger.z=-.4f;g.burger.vz=18;g.burger.vy=0;
    g.step(tick,p);CHECK(g.burger.vz<0 && g.burger.z<0);
    // Lethal blast, protection, radius, cover, suicide credit.
    for(int mode=0;mode<5;++mode) {
        g=duel();g.burger.state=G::BurgerState::Flying;g.burger.timer=.001f;
        g.burger.x=0;g.burger.y=.3f;g.burger.z=8;
        int victim=1;
        if(mode==1) g.players[1].invuln=1;
        if(mode==2) g.burger.z=5;
        if(mode==3) {place(g,1,-3,7.3f);g.burger.x=-3;g.burger.z=5.8f;}
        if(mode==4) {g.burger.z=-8;victim=0;}
        g.step(tick,p);
        CHECK((g.players[victim].hp==0)==(mode==0 || mode==4));
        if(mode==0 || mode==4) CHECK(g.score[1-victim]==1);
    }
    // Respawning clears unclaimed, held and in-flight burgers and restarts the random wait.
    for (int state=1;state<=3;++state) {
        g=duel(); g.burger.state=static_cast<G::BurgerState>(state);
        g.burger.timer=state==3?1.f:0.f;g.burger.y=2;
        g.players[1].dead=.001f;g.players[1].hp=0;
        g.step(tick,p);
        CHECK(g.burger.state==G::BurgerState::Waiting && g.burger.timer>7.9f && g.burger.timer<=15);
    }
    // Closing gates eject an overlapping grenade rather than trapping it inside geometry.
    g=duel();g.burger.state=G::BurgerState::Flying;g.burger.timer=1;
    g.burger.x=-3;g.burger.y=2;g.burger.z=-.01f;
    g.step(tick,p);CHECK(g.burger.z<-.3f && g.burger.vz<0);
    // Death cancels a held item, but a thrown grenade survives its owner's death.
    for(int thrown=0;thrown<2;++thrown) {
        g=duel();p[0]=p[1]=PadInput();g.burger.owner=0;
        g.burger.state=thrown?G::BurgerState::Flying:G::BurgerState::Held;
        g.burger.timer=1;g.burger.x=0;g.burger.y=2;g.burger.z=-3;
        aimHead(p[1],g,1);fire(p[1]);g.step(tick,p);
        CHECK(g.players[0].dead>0);
        CHECK(g.burger.state==(thrown?G::BurgerState::Flying:G::BurgerState::Waiting));
        g.begin(arcade::Mode::Solo,3);CHECK(g.burger.state==G::BurgerState::Waiting && g.burger.timer>=8 && g.burger.timer<=15);
    }
    // An explosion can win a match.
    g=duel();p[0]=p[1]=PadInput();g.score[0]=4;
    g.burger.state=G::BurgerState::Flying;g.burger.timer=.001f;
    g.burger.x=0;g.burger.y=.3f;g.burger.z=8;
    g.step(tick,p);CHECK(g.phase()==arcade::MatchPhase::MatchEnd && g.winner==0);
    // Every visible lifecycle state survives replication.
    for(int state=0;state<=4;++state) {
        g=duel();g.burger.state=static_cast<G::BurgerState>(state);g.burger.timer=.2f;
        g.burger.x=.4f;g.burger.y=2;g.burger.z=3;g.burger.owner=1;
        arcade::Writer w;g.encode(w);G c; c.begin(arcade::Mode::Client,1);
        arcade::Reader r(w.buf,w.n);CHECK(c.decode(r) && same(g,c));
    }
}

int main() {
    burgers();
    world();
    movement();
    jumping();
    shooting();
    coverAndSight();
    gate();
    lifeAndDeath();
    matchFlow();
    soloPractice();
    inputsAndPrime();
    codec();
    puts(failures ? "FAILURES" : "ALL PASSED");
    return failures ? 1 : 0;
}
