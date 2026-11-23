#include "games/middiff/middiff.h"
#include "games/middiff/view.h"
#include "core/display.h"
#include "core/platform.h"
#include "core/sound.h"
#include "ui/gfx.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

// A small perspective renderer: the arena is a handful of flat polygons, boxes and one upright figure, projected
// with a real pinhole camera (yaw and pitch), so a few dozen fills per frame are enough for a believable 3D view.
namespace {
using platform::rgb;
using G = MidDiffGame;

constexpr uint16_t ink = rgb(14, 12, 16), white = rgb(246, 244, 232), cyan = rgb(70, 182, 255), red = rgb(241, 102, 108);
constexpr uint16_t sandLight = rgb(232, 208, 158), sandWallZ = rgb(219, 190, 134), sandWallX = rgb(196, 166, 112);
constexpr uint16_t sandDark = rgb(168, 138, 94), floorColor = rgb(150, 136, 112), gridColor = rgb(128, 114, 92);
constexpr uint16_t doorDark = rgb(54, 40, 34), doorFrame = rgb(150, 108, 68), plank = rgb(98, 70, 46);
constexpr uint16_t crateFront = rgb(166, 124, 80), crateSide = rgb(128, 94, 60), crateTop = rgb(196, 156, 106), crateLine = rgb(84, 58, 38);
constexpr uint16_t gold = rgb(255, 205, 90);

constexpr float kFocal = 190.f, kCx = 120.f, kCy = 67.5f, kNear = 0.3f, kDeg = 0.017453292f;
constexpr float kWallHeight = 4.0f;

int g_viewLocal = 0;

struct Cam {
    float x, y, z;      // eye position in the world
    float f;            // +1 looking towards +z (player 0), -1 towards -z
    float cyaw, syaw, cp, sp;
};
struct V3 { float x, y, z; };
struct P3 { float x, y, z; };  // world point

Cam makeCam(float x, float eye, float z, float f, float yawDeg, float pitchDeg) {
    return {x, eye, z, f, std::cos(yawDeg * kDeg), std::sin(yawDeg * kDeg), std::cos(pitchDeg * kDeg), std::sin(pitchDeg * kDeg)};
}

// World to camera space: +z ahead, +x right, +y up.
V3 toCam(const Cam& c, float wx, float wy, float wz) {
    const float lx = (wx - c.x) * c.f, lz = (wz - c.z) * c.f, ly = wy - c.y;
    const float xc = lx * c.cyaw - lz * c.syaw;
    const float z1 = lx * c.syaw + lz * c.cyaw;
    return {xc, ly * c.cp - z1 * c.sp, ly * c.sp + z1 * c.cp};
}
V3 toCam(const Cam& c, const P3& p) { return toCam(c, p.x, p.y, p.z); }

struct P2 { float x, y; };

// One Sutherland-Hodgman pass of a convex polygon against an axis-aligned half plane in screen space.
int clipHalf(const P2* in, int n, P2* out, bool vertical, float bound, bool keepGreater) {
    int m = 0;
    for (int i = 0; i < n; ++i) {
        const P2 a = in[i], b = in[(i + 1) % n];
        const float va = vertical ? a.y : a.x, vb = vertical ? b.y : b.x;
        const bool ina = keepGreater ? va >= bound : va <= bound, inb = keepGreater ? vb >= bound : vb <= bound;
        if (ina) out[m++] = a;
        if (ina != inb) {
            const float t = (bound - va) / (vb - va);
            P2 p = {a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t};
            if (vertical) p.y = bound; else p.x = bound;
            out[m++] = p;
        }
    }
    return m;
}

// Convex polygon in camera space, clipped to the near plane and to the screen, filled as a fan. Clipping to the
// screen keeps huge, mostly off-screen shapes (walls close to the camera) cheap to fill.
void poly(const V3* in, int n, uint16_t color) {
    V3 buf[2 * 8];
    int m = 0;
    for (int i = 0; i < n; ++i) {
        const V3& a = in[i];
        const V3& b = in[(i + 1) % n];
        const bool ina = a.z >= kNear, inb = b.z >= kNear;
        if (ina) buf[m++] = a;
        if (ina != inb) {
            const float t = (kNear - a.z) / (b.z - a.z);
            buf[m++] = {a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, kNear};
        }
    }
    if (m < 3) return;
    P2 p[24], q[24];
    for (int i = 0; i < m; ++i) p[i] = {kCx + kFocal * buf[i].x / buf[i].z, kCy - kFocal * buf[i].y / buf[i].z};
    m = clipHalf(p, m, q, false, 0.f, true);
    if (m < 3) return;
    m = clipHalf(q, m, p, false, 239.99f, false);
    if (m < 3) return;
    m = clipHalf(p, m, q, true, 0.f, true);
    if (m < 3) return;
    m = clipHalf(q, m, p, true, 134.99f, false);
    if (m < 3) return;
    auto& c = display::canvas();
    const int x0 = int(lroundf(p[0].x)), y0 = int(lroundf(p[0].y));
    for (int i = 1; i + 1 < m; ++i)
        c.fillTriangle(x0, y0, int(lroundf(p[i].x)), int(lroundf(p[i].y)), int(lroundf(p[i + 1].x)), int(lroundf(p[i + 1].y)), color);
}

void quad(const Cam& cam, uint16_t color, P3 a, P3 b, P3 c, P3 d) {
    const V3 v[4] = {toCam(cam, a), toCam(cam, b), toCam(cam, c), toCam(cam, d)};
    poly(v, 4, color);
}

// Line in camera space, clipped to the near plane and the screen (Liang-Barsky).
void lineC(V3 a, V3 b, uint16_t color) {
    if (a.z < kNear && b.z < kNear) return;
    if (a.z < kNear) { const float t = (kNear - a.z) / (b.z - a.z); a = {a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, kNear}; }
    if (b.z < kNear) { const float t = (kNear - b.z) / (a.z - b.z); b = {b.x + (a.x - b.x) * t, b.y + (a.y - b.y) * t, kNear}; }
    float x0 = kCx + kFocal * a.x / a.z, y0 = kCy - kFocal * a.y / a.z;
    float x1 = kCx + kFocal * b.x / b.z, y1 = kCy - kFocal * b.y / b.z;
    const float dx = x1 - x0, dy = y1 - y0;
    float t0 = 0, t1 = 1;
    const float pp[4] = {-dx, dx, -dy, dy}, qq[4] = {x0, 239.99f - x0, y0, 134.99f - y0};
    for (int i = 0; i < 4; ++i) {
        if (pp[i] == 0) { if (qq[i] < 0) return; continue; }
        const float r = qq[i] / pp[i];
        if (pp[i] < 0) { if (r > t1) return; t0 = std::max(t0, r); }
        else { if (r < t0) return; t1 = std::min(t1, r); }
    }
    display::canvas().drawLine(int(lroundf(x0 + dx * t0)), int(lroundf(y0 + dy * t0)),
                               int(lroundf(x0 + dx * t1)), int(lroundf(y0 + dy * t1)), color);
}
void lineW(const Cam& cam, P3 a, P3 b, uint16_t color) { lineC(toCam(cam, a), toCam(cam, b), color); }

float lerp(float a, float b, float t) { return a + (b - a) * t; }

// ---- the arena ----

void sky() {
    auto& c = display::canvas();
    c.fillRect(0, 0, 240, 135, rgb(150, 190, 224));
    c.fillRect(0, 0, 240, 22, rgb(98, 144, 204));
    c.fillRect(0, 22, 240, 24, rgb(124, 168, 214));
}

void arena(const Cam& cam) {
    const float hw = G::kArenaHalfWidth, wz = G::kWallZ;
    quad(cam, floorColor, {-hw, 0, -wz}, {hw, 0, -wz}, {hw, 0, wz}, {-hw, 0, wz});

    // Walls, each with a shaded foot, a coping and stone courses.
    struct Wall { P3 a, b; uint16_t color; };
    const Wall walls[4] = {
        {{-hw, 0, -wz}, {hw, 0, -wz}, sandWallZ}, {{-hw, 0, wz}, {hw, 0, wz}, sandWallZ},
        {{-hw, 0, -wz}, {-hw, 0, wz}, sandWallX}, {{hw, 0, -wz}, {hw, 0, wz}, sandWallX},
    };
    for (const Wall& w : walls) {
        const P3 a = w.a, b = w.b;
        auto at = [&](const P3& p, float y) { return P3{p.x, y, p.z}; };
        quad(cam, w.color, a, b, at(b, kWallHeight), at(a, kWallHeight));
        quad(cam, sandDark, a, b, at(b, 0.25f), at(a, 0.25f));
        quad(cam, sandLight, at(a, kWallHeight - 0.25f), at(b, kWallHeight - 0.25f), at(b, kWallHeight), at(a, kWallHeight));
        for (float y : {1.3f, 2.6f}) lineW(cam, at(a, y), at(b, y), sandDark);
    }
    // Double doors in both end walls, arched windows along the sides.
    for (float z : {-wz, wz}) {
        quad(cam, doorFrame, {-1.5f, 0, z}, {1.5f, 0, z}, {1.5f, 2.85f, z}, {-1.5f, 2.85f, z});
        quad(cam, doorDark, {-1.3f, 0, z}, {1.3f, 0, z}, {1.3f, 2.6f, z}, {-1.3f, 2.6f, z});
        lineW(cam, {0, 0, z}, {0, 2.6f, z}, plank);
        for (float y : {0.9f, 1.8f}) lineW(cam, {-1.3f, y, z}, {1.3f, y, z}, plank);
    }
    for (float x : {-hw, hw}) {
        for (float z : {-6.0f, 0.0f, 6.0f})
            quad(cam, doorDark, {x, 2.0f, z - 0.7f}, {x, 2.0f, z + 0.7f}, {x, 3.1f, z + 0.7f}, {x, 3.1f, z - 0.7f});
    }

    // Floor tiles and the line nobody may cross (hazard stripes just in front of each player's zone).
    for (float x = -hw; x <= hw + 0.01f; x += 1.5f) lineW(cam, {x, 0, -wz}, {x, 0, wz}, gridColor);
    for (float z = -wz; z <= wz + 0.01f; z += 2.0f) lineW(cam, {-hw, 0, z}, {hw, 0, z}, gridColor);
    for (float side : {-1.0f, 1.0f}) {
        const float z0 = side * (G::kNearZ - 0.28f), z1 = side * G::kNearZ;
        quad(cam, ink, {-hw, 0, z0}, {hw, 0, z0}, {hw, 0, z1}, {-hw, 0, z1});
        for (float x = -hw; x < hw - 0.01f; x += 1.0f)
            quad(cam, rgb(244, 160, 40), {x, 0, z0}, {x + 0.5f, 0, z0}, {x + 0.5f, 0, z1}, {x, 0, z1});
    }
}

void cover(const Cam& cam, int side) {
    const float z0 = side < 0 ? -G::kCoverFarZ : G::kCoverNearZ, z1 = side < 0 ? -G::kCoverNearZ : G::kCoverFarZ;
    const float x0 = -G::kCoverHalfWidth, x1 = G::kCoverHalfWidth, h = G::kCoverHeight;
    const float cx = cam.x, cz = cam.z;
    // Faces whose outward normal points at the eye.
    if (cz < z0) {
        quad(cam, crateFront, {x0, 0, z0}, {x1, 0, z0}, {x1, h, z0}, {x0, h, z0});
        for (float y : {0.38f, 0.76f}) lineW(cam, {x0, y, z0}, {x1, y, z0}, crateLine);
    }
    if (cz > z1) {
        quad(cam, crateFront, {x0, 0, z1}, {x1, 0, z1}, {x1, h, z1}, {x0, h, z1});
        for (float y : {0.38f, 0.76f}) lineW(cam, {x0, y, z1}, {x1, y, z1}, crateLine);
    }
    if (cx < x0) quad(cam, crateSide, {x0, 0, z0}, {x0, 0, z1}, {x0, h, z1}, {x0, h, z0});
    if (cx > x1) quad(cam, crateSide, {x1, 0, z0}, {x1, 0, z1}, {x1, h, z1}, {x1, h, z0});
    if (cam.y > h) {
        quad(cam, crateTop, {x0, h, z0}, {x1, h, z0}, {x1, h, z1}, {x0, h, z1});
        lineW(cam, {x0, h, z0}, {x1, h, z0}, crateLine);
        lineW(cam, {x0, h, z1}, {x1, h, z1}, crateLine);
    }
}

// The opponent: an upright figure facing the camera, built from rectangles scaled by distance. Heights go through
// the real projection so the figure stays planted on the floor when the view pitches.
void figure(const Cam& cam, const G::Player& p, int index, uint32_t ms) {
    const V3 foot = toCam(cam, p.x, 0, p.z);
    if (foot.z < 0.6f) return;
    auto& c = display::canvas();
    const float s = kFocal / foot.z;
    const float sx0 = kCx + kFocal * foot.x / foot.z;
    auto yAt = [&](float y) {
        const V3 v = toCam(cam, p.x, y, p.z);
        return kCy - kFocal * v.y / v.z;
    };
    auto rect = [&](float xm0, float xm1, float ym0, float ym1, uint16_t color) {
        const int x0 = int(lroundf(sx0 + xm0 * s)), x1 = std::max(x0 + 1, int(lroundf(sx0 + xm1 * s)));
        const int y0 = int(lroundf(yAt(ym1))), y1 = std::max(y0 + 1, int(lroundf(yAt(ym0))));
        c.fillRect(x0, y0, x1 - x0, y1 - y0, color);
    };
    const bool blink = p.hurt > 0 && (ms / 60) % 2 == 0;
    const uint16_t team = index ? red : cyan, shirt = blink ? white : team;
    const uint16_t skin = blink ? white : rgb(232, 190, 150), pants = rgb(52, 56, 70);
    const float h = G::bodyHeight(p.crouch);
    const float shoulder = h - 0.4f, hip = shoulder - lerp(0.55f, 0.34f, p.crouch);
    rect(-0.22f, -0.02f, 0, hip, pants);
    rect(0.02f, 0.22f, 0, hip, pants);
    rect(-0.24f, -0.02f, 0, 0.07f, ink);
    rect(0.02f, 0.24f, 0, 0.07f, ink);
    rect(-0.28f, 0.28f, hip, shoulder, shirt);
    rect(-0.40f, -0.28f, hip + 0.05f, shoulder, blink ? white : rgb(40, 44, 60));
    rect(0.28f, 0.40f, hip + 0.05f, shoulder, blink ? white : rgb(40, 44, 60));
    // Rifle held across the chest, pointing at the viewer.
    rect(-0.06f, 0.34f, shoulder - 0.27f, shoulder - 0.16f, rgb(30, 32, 40));
    rect(0.0f, 0.16f, shoulder - 0.13f, shoulder - 0.1f, rgb(70, 76, 90));
    const int hx = int(lroundf(sx0)), hy = int(lroundf(yAt(h - 0.2f)));
    c.fillCircle(hx, hy, std::max(1, int(lroundf(s * 0.2f))), skin);
    rect(-0.2f, 0.2f, h - 0.12f, h + 0.02f, blink ? white : team);
    if (p.flash > 0) {
        const int mx = int(lroundf(sx0 + 0.3f * s)), my = int(lroundf(yAt(shoulder - 0.22f)));
        const int r = std::max(2, int(lroundf(s * 0.22f)));
        c.fillCircle(mx, my, r, gold);
        c.fillCircle(mx, my, std::max(1, r / 2), white);
    }
}

}  // namespace

void MidDiffGame::draw(int local, const arcade::DrawCtx& ctx) {
    g_viewLocal = local;
    auto& c = display::canvas();
    const Player& me = players[local];
    const Player& foe = players[1 - local];
    const float kick = 2.5f * std::min(1.f, me.flash / 0.12f);
    const Cam cam = makeCam(me.x, eyeHeight(me.crouch), me.z, facing(local), lookYaw(local), lookPitch(local) + kick);

    sky();
    arena(cam);

    // Back to front: the opponent, then the covers by distance.
    struct Item { float dist; int kind; };  // kind 0/1: cover on side -1/+1, 2: the opponent
    Item items[3] = {
        {(me.x) * (me.x) + (me.z + 5.2f) * (me.z + 5.2f), 0},
        {(me.x) * (me.x) + (me.z - 5.2f) * (me.z - 5.2f), 1},
        {(me.x - foe.x) * (me.x - foe.x) + (me.z - foe.z) * (me.z - foe.z), 2},
    };
    std::sort(items, items + 3, [](const Item& a, const Item& b) { return a.dist > b.dist; });
    for (const Item& it : items) {
        if (it.kind == 2) figure(cam, foe, 1 - local, ctx.nowMs);
        else cover(cam, it.kind == 0 ? -1 : 1);
    }

    // Trails of the shots that were just fired.
    for (int i = 0; i < 2; ++i) {
        const Player& p = players[i];
        if (p.flash <= 0) continue;
        const uint16_t col = p.flash > 0.06f ? white : gold;
        const V3 end = toCam(cam, p.shotX, p.shotY, p.shotZ);
        if (i == local) {
            lineC({0.2f, -0.16f, 0.5f}, end, col);
        } else {
            const float f = facing(i);
            const V3 from = toCam(cam, p.x + 0.3f * f, bodyHeight(p.crouch) - 0.62f, p.z);
            lineC(from, end, col);
        }
    }

    // Crosshair and hit marker.
    const int cx = 120, cy = 67;
    for (int o = 0; o < 2; ++o) {
        const uint16_t col = o == 0 ? ink : white;
        c.drawFastHLine(cx - 8 + o, cy + o, 5, col); c.drawFastHLine(cx + 4 + o, cy + o, 5, col);
        c.drawFastVLine(cx + o, cy - 8 + o, 5, col); c.drawFastVLine(cx + o, cy + 4 + o, 5, col);
    }
    c.drawPixel(cx, cy, red);
    if (me.marker > 0) {
        const uint16_t col = me.markerHead ? red : white;
        const int a = 5, b = 10;
        for (int k = a; k <= b; ++k) {
            c.drawPixel(cx - k, cy - k, col); c.drawPixel(cx + k, cy - k, col);
            c.drawPixel(cx - k, cy + k, col); c.drawPixel(cx + k, cy + k, col);
        }
    }
    if (me.cooldown > 0) {
        const int w = int(18 * (1 - me.cooldown / kShotCooldown));
        c.fillRect(cx - 9, cy + 12, 18, 2, ink);
        c.fillRect(cx - 9, cy + 12, w, 2, gold);
    }

    // Damage flash around the screen edge.
    if (me.hurt > 0) {
        const int t = 2 + int(me.hurt * 20);
        const uint16_t col = rgb(210, 32, 32);
        c.fillRect(0, 0, 240, t, col); c.fillRect(0, 135 - t, 240, t, col);
        c.fillRect(0, 0, t, 135, col); c.fillRect(240 - t, 0, t, 135, col);
    }

    // Health and stamina.
    const int hp = me.hp;
    const uint16_t hpCol = hp > 50 ? rgb(110, 230, 110) : hp > 25 ? rgb(255, 206, 70) : rgb(240, 80, 70);
    c.fillRect(5, 123, 74, 8, ink);
    c.fillRect(6, 124, hp * 72 / kMaxHp, 6, hpCol);
    ui::shadowText("HP", 14, 118, 1, white);
    c.fillRect(161, 123, 74, 8, ink);
    const bool low = me.winded || me.stamina < 0.2f;
    c.fillRect(162, 124, int(72 * me.stamina), 6, low ? ((ctx.nowMs / 120) % 2 ? red : rgb(150, 40, 40)) : rgb(90, 190, 255));
    ui::shadowText(me.winded ? "WINDED" : "STAMINA", me.winded ? 184 : 188, 118, 1, me.winded ? red : white);

    // Scores.
    if (ctx.solo) {
        char label[20];
        snprintf(label, sizeof(label), "KILLS %u", unsigned(score[0]));
        ui::shadowText(label, 120, 7, 1, cyan);
    } else {
        for (int i = 0; i < 2; ++i) {
            const int center = i ? 170 : 70;
            const uint16_t col = i ? red : cyan;
            char label[16];
            snprintf(label, sizeof(label), "P%d %u", i + 1, unsigned(score[i]));
            ui::shadowText(label, center, 7, 1, col);
            if (i == local) c.drawFastHLine(center - 13, 12, 26, col);
        }
        char first[16];
        snprintf(first, sizeof(first), "FIRST %d", kWinningScore);
        ui::shadowText(first, 120, 7, 1, white);
    }

    if (phase_ == arcade::MatchPhase::MatchEnd) {
        ui::matchResult(winner, local, 120, 40, 3);
    } else if (phase_ == arcade::MatchPhase::RoundEnd) {
        if (roundWinner < 0) ui::shadowText("DRAW", 120, 40, 3, white);
        else if (phaseTimer > kRoundPause - ui::kHitNoticeSeconds) ui::hitNotice(roundWinner != local, 120, 40, 3);
    }
}

void MidDiffGame::sfx(uint32_t ev) {
    using sound::Note;
    static const Note mine[] = {{1100, 10}, {140, 45}}, theirs[] = {{700, 10}, {120, 35}}, ping[] = {{1500, 18}, {2100, 24}},
                      hurt[] = {{180, 30}, {90, 70}}, round[] = {{660, 40}, {990, 70}},
                      finish[] = {{660, 70}, {880, 70}, {1320, 130}}, lose[] = {{440, 70}, {330, 80}, {220, 130}};
    const int me = g_viewLocal;
    const uint32_t myShot = me ? ShotP1 : ShotP0, theirShot = me ? ShotP0 : ShotP1;
    const uint32_t iWasHit = me ? HurtP1 : HurtP0, theyWereHit = me ? HurtP0 : HurtP1;
    if (ev & Finish) sound::play(winner == me ? finish : lose, 3, 5);
    else if (ev & Round) sound::play(round, 2, 4);
    else if (ev & iWasHit) sound::play(hurt, 2, 4);
    else if (ev & theyWereHit) sound::play(ping, 2, 3);
    else if (ev & myShot) sound::play(mine, 2, 2);
    else if (ev & theirShot) sound::play(theirs, 2, 1);
}

namespace middiff_view {

// Stylised mini-scene for the launcher card and the waiting screen: the opponent peeks over the cover while a
// crosshair drifts around and a shot is fired now and then.
void drawCard(int x, int y, int w, int h, uint32_t ms) {
    auto& c = display::canvas();
    c.setClipRect(x, y, w, h);
    const int horizon = y + h * 45 / 100;
    c.fillRect(x, y, w, horizon - y, rgb(112, 158, 210));
    c.fillRect(x, y, w, h * 14 / 100, rgb(92, 138, 200));
    // End wall with the double doors.
    const int wallTop = y + h * 12 / 100;
    c.fillRect(x, wallTop, w, horizon - wallTop, sandWallZ);
    c.fillRect(x, wallTop, w, 3, sandLight);
    c.fillRect(x, horizon - 4, w, 4, sandDark);
    const int doorW = w * 22 / 100, doorX = x + (w - doorW) / 2, doorTop = wallTop + (horizon - wallTop) * 28 / 100;
    c.fillRect(doorX - 2, doorTop - 2, doorW + 4, horizon - doorTop + 2, doorFrame);
    c.fillRect(doorX, doorTop, doorW, horizon - doorTop, doorDark);
    c.drawFastVLine(doorX + doorW / 2, doorTop, horizon - doorTop, plank);
    // Floor with lines running to the vanishing point.
    c.fillRect(x, horizon, w, y + h - horizon, floorColor);
    for (int i = -4; i <= 4; ++i) c.drawLine(x + w / 2 + i * 6, horizon, x + w / 2 + i * w / 4, y + h, gridColor);
    for (int k = 1; k <= 4; ++k) c.drawFastHLine(x, horizon + k * k * (y + h - horizon) / 18, w, gridColor);
    // The opponent comes up behind the cover and ducks again.
    const int coverW = w * 40 / 100, coverX = x + (w - coverW) / 2, coverTop = y + h * 62 / 100, coverBottom = y + h * 82 / 100;
    const uint32_t t = ms % 3600;
    const int rise = t < 600 ? int(t) * 14 / 600 : t < 2400 ? 14 : t < 3000 ? 14 - int(t - 2400) * 14 / 600 : 0;
    const int cx = x + w / 2, bodyTop = coverTop - rise;
    if (rise > 0) {
        c.fillRect(cx - 7, bodyTop + 6, 14, rise, cyan);
        c.fillCircle(cx, bodyTop + 3, 5, rgb(232, 190, 150));
        c.fillRect(cx - 5, bodyTop - 2, 10, 3, cyan);
    }
    c.fillRect(coverX, coverTop, coverW, coverBottom - coverTop, crateFront);
    c.fillRect(coverX, coverTop, coverW, 3, crateTop);
    c.drawFastHLine(coverX, coverTop + (coverBottom - coverTop) / 2, coverW, crateLine);
    c.drawRect(coverX, coverTop, coverW, coverBottom - coverTop, crateLine);
    // Crosshair on the head, a flash and a trail when it fires.
    const int aimX = cx + int(std::sin(ms / 400.0f) * 5), aimY = bodyTop + 3 + int(std::sin(ms / 530.0f) * 3);
    for (int o = 0; o < 2; ++o) {
        const uint16_t col = o ? white : ink;
        c.drawFastHLine(aimX - 9 + o, aimY + o, 5, col); c.drawFastHLine(aimX + 5 + o, aimY + o, 5, col);
        c.drawFastVLine(aimX + o, aimY - 9 + o, 5, col); c.drawFastVLine(aimX + o, aimY + 5 + o, 5, col);
    }
    if (t >= 1700 && t < 1800) {
        c.drawLine(x + w - 14, y + h - 8, aimX, aimY, white);
        c.fillCircle(x + w - 14, y + h - 8, 3, gold);
    }
    c.clearClipRect();
}

void drawLobby(uint32_t ms) {
    drawCard(0, 0, 240, 135, ms);
    ui::shadowText("STICK MOVE  TURN AIM  A FIRE  B DUCK", 120, 108, 1, white);
}

}  // namespace middiff_view
