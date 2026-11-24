#include "games/middiff/middiff.h"
#include "games/middiff/view.h"
#include "core/display.h"
#include "core/platform.h"
#include "core/sound.h"
#include "ui/gfx.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

// A small perspective renderer. The arena is a few dozen flat polygons and boxes projected with a real pinhole
// camera (yaw and pitch), painted back to front, plus one upright figure drawn from rectangles. Everything is
// clipped to the screen so large walls close to the camera stay cheap.
namespace {
using platform::rgb;
using G = MidDiffGame;

constexpr uint16_t ink = rgb(14, 12, 16), white = rgb(246, 244, 232), gold = rgb(255, 205, 90), redHot = rgb(241, 102, 108);
// Sandstone town.
constexpr uint16_t sandLight = rgb(236, 214, 166), sandFront = rgb(216, 186, 132), sandSide = rgb(192, 160, 108);
constexpr uint16_t sandDark = rgb(158, 126, 84), mudFront = rgb(190, 150, 104), mudSide = rgb(164, 126, 86);
constexpr uint16_t mudTop = rgb(214, 178, 130), mudDark = rgb(112, 84, 58);
constexpr uint16_t bagFront = rgb(176, 166, 112), bagSide = rgb(146, 138, 92), bagTop = rgb(196, 186, 130), bagDark = rgb(104, 96, 64);
constexpr uint16_t crateFront = rgb(168, 124, 80), crateSide = rgb(130, 94, 60), crateTop = rgb(198, 156, 106), crateLine = rgb(84, 58, 38);
constexpr uint16_t floorColor = rgb(150, 136, 112), gridColor = rgb(128, 114, 92), shadow = rgb(96, 86, 70);
constexpr uint16_t doorDark = rgb(54, 40, 34), doorFrame = rgb(150, 108, 68), plank = rgb(98, 70, 46);
constexpr uint16_t iron = rgb(62, 66, 76), ironLight = rgb(112, 118, 130), hazardYellow = rgb(244, 190, 40);
constexpr uint16_t rust = rgb(158, 74, 50), rustDark = rgb(112, 52, 38), rustLight = rgb(190, 100, 70);

// Teams: T is red and brown, CT is blue.
constexpr uint16_t teamColor[2] = {rgb(228, 104, 64), rgb(78, 150, 245)};
constexpr uint16_t teamDark[2] = {rgb(132, 58, 40), rgb(40, 70, 138)};
constexpr uint16_t teamHead[2] = {rgb(44, 36, 36), rgb(58, 98, 176)};

float g_focal = 190.f;  // projection scale for the current frame
constexpr float kCx = 120.f, kCy = 67.5f, kNear = 0.3f, kDeg = 0.017453292f, kPi = 3.14159265f;

int g_viewLocal = 0;

struct Cam {
    float x, y, z;      // eye position in the world
    float f;            // +1 looking towards +z, -1 towards -z
    float cyaw, syaw, cp, sp;
};
struct V3 { float x, y, z; };
struct P3 { float x, y, z; };
struct P2 { float x, y; };

Cam makeCam(float x, float eye, float z, float f, float yawDeg, float pitchDeg) {
    Cam c;
    c.x = x; c.y = eye; c.z = z; c.f = f;
    c.cyaw = std::cos(yawDeg * kDeg); c.syaw = std::sin(yawDeg * kDeg);
    c.cp = std::cos(pitchDeg * kDeg); c.sp = std::sin(pitchDeg * kDeg);
    return c;
}

// World to camera space: +z ahead, +x right, +y up.
V3 toCam(const Cam& c, float wx, float wy, float wz) {
    const float lx = (wx - c.x) * c.f, lz = (wz - c.z) * c.f, ly = wy - c.y;
    const float xc = lx * c.cyaw - lz * c.syaw;
    const float z1 = lx * c.syaw + lz * c.cyaw;
    V3 v = {xc, ly * c.cp - z1 * c.sp, ly * c.sp + z1 * c.cp};
    return v;
}
V3 toCam(const Cam& c, const P3& p) { return toCam(c, p.x, p.y, p.z); }

float horizonY(const Cam& c) { return kCy + g_focal * c.sp / c.cp; }
float lerpf(float a, float b, float t) { return a + (b - a) * t; }
int roundi(float v) { return static_cast<int>(lroundf(v)); }

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

// The library's fillTriangle spends most of its time per scanline, and a tall wall is a few hundred scanlines. The
// polygons here are convex and already clipped to the screen, so they are filled straight into the canvas buffer
// (a plain 240x135 RGB565 sprite, stored with the bytes swapped), one span per row.
uint16_t* g_pixels = nullptr;  // set at the start of each frame; null falls back to the library

// 16-bit stores into this RAM are several times slower than 32-bit ones, so spans are written two pixels at a time.
void fill16(uint16_t* d, int n, uint16_t s) {
    if (n <= 0) return;
    if (reinterpret_cast<uintptr_t>(d) & 2) { *d++ = s; --n; }
    uint32_t* w = reinterpret_cast<uint32_t*>(d);
    const uint32_t ss = static_cast<uint32_t>(s) | (static_cast<uint32_t>(s) << 16);
    for (int k = n >> 1; k > 0; --k) *w++ = ss;
    if (n & 1) *reinterpret_cast<uint16_t*>(w) = s;
}

void fillSpan(int y, int x0, int x1, uint16_t color) {
#ifndef MIDDIFF_HOST_CANVAS
    if (g_pixels) {
        fill16(g_pixels + y * 240 + x0, x1 - x0 + 1, static_cast<uint16_t>((color << 8) | (color >> 8)));
        return;
    }
#endif
    display::canvas().fillRect(x0, y, x1 - x0 + 1, 1, color);
}

// One side of a convex polygon walked from the top vertex down: x advances by a constant slope per row.
struct Chain {
    const P2* p;
    int n, step, cur;
    float x, dx, yEnd;
    // Finds the edge that covers row y, skipping edges that end above it. False when the chain has run out.
    bool edgeFor(float y) {
        for (int guard = 0; guard < n; ++guard) {
            const P2& a = p[cur];
            const int nx = (cur + step + n) % n;
            const P2& b = p[nx];
            if (b.y >= y && b.y > a.y) {
                dx = (b.x - a.x) / (b.y - a.y);
                x = a.x + dx * (y - a.y);
                yEnd = b.y;
                return true;
            }
            if (b.y < a.y) return false;
            cur = nx;
        }
        return false;
    }
};

void fillPolyConvex(const P2* p, int n, uint16_t color) {
    int top = 0, bot = 0;
    for (int i = 1; i < n; ++i) {
        if (p[i].y < p[top].y) top = i;
        if (p[i].y > p[bot].y) bot = i;
    }
    const int y0 = std::max(0, static_cast<int>(std::ceil(p[top].y))), y1 = std::min(134, static_cast<int>(std::floor(p[bot].y)));
    if (y0 > y1) return;
    Chain a = {p, n, 1, top, 0, 0, 0}, b = {p, n, -1, top, 0, 0, 0};
    if (!a.edgeFor(static_cast<float>(y0)) || !b.edgeFor(static_cast<float>(y0))) return;
    for (int y = y0; y <= y1; ++y) {
        if (y > a.yEnd) { a.cur = (a.cur + 1) % n; if (!a.edgeFor(static_cast<float>(y))) break; }
        if (y > b.yEnd) { b.cur = (b.cur + n - 1) % n; if (!b.edgeFor(static_cast<float>(y))) break; }
        const float xl = a.x < b.x ? a.x : b.x, xr = a.x < b.x ? b.x : a.x;
        int xs = static_cast<int>(xl);
        if (static_cast<float>(xs) < xl) ++xs;
        int xe = static_cast<int>(xr);
        if (xe > 239) xe = 239;
        if (xe >= xs) fillSpan(y, xs, xe, color);
        a.x += a.dx;
        b.x += b.dx;
    }
}

// Convex polygon in camera space, clipped to the near plane and to the screen, then filled scanline by scanline.
void poly(const V3* in, int n, uint16_t color) {
    V3 buf[2 * 10];
    int m = 0;
    for (int i = 0; i < n; ++i) {
        const V3& a = in[i];
        const V3& b = in[(i + 1) % n];
        const bool ina = a.z >= kNear, inb = b.z >= kNear;
        if (ina) buf[m++] = a;
        if (ina != inb) {
            const float t = (kNear - a.z) / (b.z - a.z);
            V3 v = {a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, kNear};
            buf[m++] = v;
        }
    }
    if (m < 3) return;
    P2 p[32], q[32];
    float lo = 1e9f, hi = -1e9f, left = 1e9f, right = -1e9f;
    for (int i = 0; i < m; ++i) {
        const float k = g_focal / buf[i].z;  // one division per vertex
        p[i].x = kCx + k * buf[i].x;
        p[i].y = kCy - k * buf[i].y;
        lo = std::min(lo, p[i].y); hi = std::max(hi, p[i].y);
        left = std::min(left, p[i].x); right = std::max(right, p[i].x);
    }
    if (hi < 0 || lo > 134.99f || right < 0 || left > 239.99f) return;  // entirely off screen
    if (lo < 0.f || hi > 134.99f || left < 0.f || right > 239.99f) {
        m = clipHalf(p, m, q, false, 0.f, true);
        if (m < 3) return;
        m = clipHalf(q, m, p, false, 239.99f, false);
        if (m < 3) return;
        m = clipHalf(p, m, q, true, 0.f, true);
        if (m < 3) return;
        m = clipHalf(q, m, p, true, 134.99f, false);
        if (m < 3) return;
    }
    fillPolyConvex(p, m, color);
}

void quad(const Cam& cam, uint16_t color, P3 a, P3 b, P3 c, P3 d) {
    const V3 v[4] = {toCam(cam, a), toCam(cam, b), toCam(cam, c), toCam(cam, d)};
    poly(v, 4, color);
}

// Lines clipped to the screen, written straight into the canvas buffer like the polygons.
void fastLine(int x0, int y0, int x1, int y1, uint16_t color) {
    x0 = std::max(0, std::min(239, x0)); x1 = std::max(0, std::min(239, x1));
    y0 = std::max(0, std::min(134, y0)); y1 = std::max(0, std::min(134, y1));
    if (y0 == y1) {
        fillSpan(y0, std::min(x0, x1), std::max(x0, x1), color);
        return;
    }
    if (!g_pixels) {
        display::canvas().drawLine(x0, y0, x1, y1, color);
        return;
    }
    const int dx = std::abs(x1 - x0), dy = std::abs(y1 - y0), sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1;
    int err = dx - dy;
    const uint16_t s = static_cast<uint16_t>((color << 8) | (color >> 8));
    for (;;) {
        g_pixels[y0 * 240 + x0] = s;
        if (x0 == x1 && y0 == y1) break;
        const int e2 = 2 * err;
        if (e2 > -dy) { err -= dy; x0 += sx; }
        if (e2 < dx) { err += dx; y0 += sy; }
    }
}

// Line in camera space, clipped to the near plane and the screen (Liang-Barsky).
void lineC(V3 a, V3 b, uint16_t color) {
    if (a.z < kNear && b.z < kNear) return;
    if (a.z < kNear) { const float t = (kNear - a.z) / (b.z - a.z); a.x += (b.x - a.x) * t; a.y += (b.y - a.y) * t; a.z = kNear; }
    if (b.z < kNear) { const float t = (kNear - b.z) / (a.z - b.z); b.x += (a.x - b.x) * t; b.y += (a.y - b.y) * t; b.z = kNear; }
    const float x0 = kCx + g_focal * a.x / a.z, y0 = kCy - g_focal * a.y / a.z;
    const float x1 = kCx + g_focal * b.x / b.z, y1 = kCy - g_focal * b.y / b.z;
    const float dx = x1 - x0, dy = y1 - y0;
    float t0 = 0, t1 = 1;
    const float pp[4] = {-dx, dx, -dy, dy}, qq[4] = {x0, 239.99f - x0, y0, 134.99f - y0};
    for (int i = 0; i < 4; ++i) {
        if (pp[i] == 0) { if (qq[i] < 0) return; continue; }
        const float r = qq[i] / pp[i];
        if (pp[i] < 0) { if (r > t1) return; t0 = std::max(t0, r); }
        else { if (r < t0) return; t1 = std::min(t1, r); }
    }
    fastLine(roundi(x0 + dx * t0), roundi(y0 + dy * t0), roundi(x0 + dx * t1), roundi(y0 + dy * t1), color);
}
void lineW(const Cam& cam, P3 a, P3 b, uint16_t color) { lineC(toCam(cam, a), toCam(cam, b), color); }

// Screen position of a world point; false when it is behind the camera.
bool project(const Cam& cam, float wx, float wy, float wz, float& sx, float& sy, float& depth) {
    const V3 v = toCam(cam, wx, wy, wz);
    if (v.z < kNear) return false;
    sx = kCx + g_focal * v.x / v.z;
    sy = kCy - g_focal * v.y / v.z;
    depth = v.z;
    return true;
}

// ---- sky ----

float wrapPi(float a) {
    while (a > kPi) a -= 2 * kPi;
    while (a < -kPi) a += 2 * kPi;
    return a;
}

// Screen x and y for something far away at a fixed direction (azimuth measured from +z towards +x, elevation up).
bool skyPoint(const Cam& cam, float az, float elev, float& sx, float& sy) {
    const float viewAz = std::atan2(cam.syaw * cam.f, cam.cyaw * cam.f);
    const float d = wrapPi(az - viewAz);
    if (std::fabs(d) > 1.2f) return false;
    sx = kCx + g_focal * std::tan(d);
    sy = horizonY(cam) - g_focal * std::tan(elev) / std::cos(d);
    return true;
}

void sky(const Cam& cam, uint32_t ms) {
    auto& c = display::canvas();
    const int yh = roundi(horizonY(cam));
    c.fillRect(0, 0, 240, 135, rgb(214, 226, 226));  // haze at the horizon
    const int bands[4] = {13, 30, 52, 80};
    const uint16_t colors[4] = {rgb(196, 218, 232), rgb(158, 196, 228), rgb(122, 168, 216), rgb(92, 140, 200)};
    for (int i = 3; i >= 0; --i) c.fillRect(0, 0, 240, std::max(0, yh - bands[i]), colors[3 - i]);
    // The sun and a few clouds drifting past; far towers stand behind the walls.
    float sx, sy;
    if (skyPoint(cam, 0.65f, 0.5f, sx, sy)) {
        c.fillCircle(roundi(sx), roundi(sy), 15, rgb(232, 232, 214));
        c.fillCircle(roundi(sx), roundi(sy), 9, rgb(255, 250, 220));
    }
    const float t = ms * 0.00004f;
    const float cloudAz[4] = {-0.5f, 0.2f, 1.8f, 3.6f}, cloudEl[4] = {0.55f, 0.38f, 0.5f, 0.45f};
    for (int i = 0; i < 4; ++i) {
        if (!skyPoint(cam, cloudAz[i] + t * (1 + i * 0.3f), cloudEl[i], sx, sy)) continue;
        const int w = 26 + i * 6;
        c.fillRect(roundi(sx) - w / 2, roundi(sy), w, 3, rgb(236, 240, 242));
        c.fillRect(roundi(sx) - w / 3, roundi(sy) - 2, w * 2 / 3, 3, rgb(244, 246, 248));
    }
    const float towerAz[7] = {-2.4f, -1.5f, -0.7f, 0.1f, 0.9f, 1.7f, 2.6f}, towerH[7] = {0.14f, 0.2f, 0.12f, 0.17f, 0.22f, 0.13f, 0.18f};
    for (int i = 0; i < 7; ++i) {
        if (!skyPoint(cam, towerAz[i], 0.0f, sx, sy)) continue;
        float tx, ty;
        skyPoint(cam, towerAz[i], towerH[i], tx, ty);
        const int w = 8 + (i % 3) * 3;
        c.fillRect(roundi(sx) - w / 2, roundi(ty), w, roundi(sy - ty) + 1, rgb(176, 160, 138));
        c.fillRect(roundi(sx) - w / 2 - 1, roundi(ty) - 2, w + 2, 3, rgb(190, 172, 146));
    }
    // Two birds.
    for (int i = 0; i < 2; ++i) {
        if (!skyPoint(cam, 0.2f + i * 1.7f + std::sin(t * 9 + i) * 0.15f, 0.34f + 0.03f * std::sin(ms * 0.004f + i), sx, sy)) continue;
        const int flap = (ms / 180 + i) % 2 ? 2 : -1;
        c.drawLine(roundi(sx) - 3, roundi(sy) + flap, roundi(sx), roundi(sy), rgb(70, 64, 60));
        c.drawLine(roundi(sx), roundi(sy), roundi(sx) + 3, roundi(sy) + flap, rgb(70, 64, 60));
    }
}

// ---- solid boxes ----

struct Style { uint16_t zFace, xFace, top, edge; int bands; bool brace; };

Style styleOf(G::Kind k) {
    switch (k) {
        case G::Kind::HighCover: return {mudFront, mudSide, mudTop, mudDark, 3, false};
        case G::Kind::LowCover: return {bagFront, bagSide, bagTop, bagDark, 4, false};
        case G::Kind::Sandbag: return {bagFront, bagSide, bagTop, bagDark, 2, false};
        case G::Kind::Crate: return {crateFront, crateSide, crateTop, crateLine, 3, true};
        case G::Kind::Lintel: return {sandFront, sandSide, sandLight, sandDark, 2, false};
        default: return {sandFront, sandSide, sandLight, sandDark, 6, false};
    }
}

void drawBox(const Cam& cam, const G::Box& b, float y0) {
    const Style st = styleOf(b.kind);
    const float x0 = b.x0, x1 = b.x1, z0 = b.z0, z1 = b.z1, y1 = b.y1;
    for (int side = 0; side < 2; ++side) {  // faces across z, then faces across x
        for (int end = 0; end < 2; ++end) {
            if (side == 0) {
                const float z = end ? z1 : z0;
                if (end ? cam.z <= z1 : cam.z >= z0) continue;
                quad(cam, st.zFace, {x0, y0, z}, {x1, y0, z}, {x1, y1, z}, {x0, y1, z});
                if (b.kind == G::Kind::Lintel && y0 > 2.0f) {  // chamfered corners turn the doorway into an arch
                    const float w = std::min(0.55f, (x1 - x0) * 0.25f);
                    quad(cam, st.zFace, {x0, y0, z}, {x0 + w, y0, z}, {x0, y0 - w, z}, {x0, y0 - w, z});
                    quad(cam, st.zFace, {x1 - w, y0, z}, {x1, y0, z}, {x1, y0 - w, z}, {x1, y0 - w, z});
                }
                for (int k = 1; k < st.bands; ++k) {
                    const float y = lerpf(y0, y1, static_cast<float>(k) / st.bands);
                    lineW(cam, {x0, y, z}, {x1, y, z}, st.edge);
                }
                if (st.brace) {
                    lineW(cam, {x0, y0, z}, {x1, y1, z}, st.edge);
                    lineW(cam, {x0, y1, z}, {x1, y0, z}, st.edge);
                }
                lineW(cam, {x0, y1, z}, {x1, y1, z}, st.edge);
            } else {
                const float x = end ? x1 : x0;
                if (end ? cam.x <= x1 : cam.x >= x0) continue;
                quad(cam, st.xFace, {x, y0, z0}, {x, y0, z1}, {x, y1, z1}, {x, y1, z0});
                if (b.kind == G::Kind::Stone || b.kind == G::Kind::Lintel) {
                    for (int k = 1; k < st.bands; ++k) {
                        const float y = lerpf(y0, y1, static_cast<float>(k) / st.bands);
                        lineW(cam, {x, y, z0}, {x, y, z1}, st.edge);
                    }
                }
            }
        }
    }
    if (cam.y > y1) {
        quad(cam, st.top, {x0, y1, z0}, {x1, y1, z0}, {x1, y1, z1}, {x0, y1, z1});
        lineW(cam, {x0, y1, z0}, {x1, y1, z0}, st.edge);
        lineW(cam, {x0, y1, z1}, {x1, y1, z1}, st.edge);
    } else if (cam.y < y0) {
        quad(cam, sandDark, {x0, y0, z0}, {x1, y0, z0}, {x1, y0, z1}, {x0, y0, z1});  // underside of a lintel
    }
}

// An oil drum: eight sides, hoops and a lid.
void drawBarrel(const Cam& cam, const G::Box& b) {
    const float cx = (b.x0 + b.x1) * 0.5f, cz = (b.z0 + b.z1) * 0.5f, r = (b.x1 - b.x0) * 0.5f;
    const float y0 = b.y0, y1 = b.y1;
    P3 lo[8], hi[8];
    for (int i = 0; i < 8; ++i) {
        const float a = i * kPi / 4 + kPi / 8;
        lo[i] = {cx + std::cos(a) * r, y0, cz + std::sin(a) * r};
        hi[i] = {cx + std::cos(a) * r, y1, cz + std::sin(a) * r};
    }
    for (int i = 0; i < 8; ++i) {
        const int j = (i + 1) % 8;
        const float mx = (lo[i].x + lo[j].x) * 0.5f - cx, mz = (lo[i].z + lo[j].z) * 0.5f - cz;
        if (mx * (cam.x - cx) + mz * (cam.z - cz) <= r * r * 0.9f) continue;  // facing away
        const float shade = 0.5f + 0.5f * (mx / r);
        quad(cam, shade > 0.55f ? rust : rustDark, lo[i], lo[j], hi[j], hi[i]);
        for (float h : {0.18f, 0.5f, 0.82f}) {
            const float y = y0 + (y1 - y0) * h;
            lineW(cam, {lo[i].x, y, lo[i].z}, {lo[j].x, y, lo[j].z}, ironLight);
        }
        if (shade > 0.65f) lineW(cam, {lo[i].x, y0, lo[i].z}, {hi[i].x, y1, hi[i].z}, rustLight);
    }
    if (cam.y > y1) {
        V3 v[8];
        for (int i = 0; i < 8; ++i) v[i] = toCam(cam, hi[i]);
        poly(v, 8, rustLight);
        for (int i = 0; i < 8; ++i) lineW(cam, hi[i], hi[(i + 1) % 8], rustDark);
    }
    if (y1 - y0 > 1.5f) {  // two drums stacked
        for (int i = 0; i < 8; ++i) lineW(cam, {lo[i].x, (y0 + y1) * 0.5f, lo[i].z}, {lo[(i + 1) % 8].x, (y0 + y1) * 0.5f, lo[(i + 1) % 8].z}, ink);
    }
}

// The iron lift gate. It rattles for a moment before it moves and a lamp over it warns the players.
void drawGate(const Cam& cam, const G& g, uint32_t ms) {
    const G::Box& b = G::boxes[0];
    const float y0 = g.boxBottom(0), y1 = b.y1;
    const bool waiting = g.gate.state == G::GateState::Closed || g.gate.state == G::GateState::Open;
    const bool warn = waiting && g.gate.timer < G::kGateWarn;
    const bool moving = !waiting;
    const float shake = warn ? std::sin(ms * 0.06f) * 0.035f : (moving ? std::sin(ms * 0.05f) * 0.012f : 0.0f);
    const float x0 = b.x0 + shake, x1 = b.x1 + shake;
    if (y1 - y0 > 0.02f) {
        const float z = cam.z < 0 ? b.z0 : b.z1;
        if (cam.z < b.z0 || cam.z > b.z1) {
            quad(cam, iron, {x0, y0, z}, {x1, y0, z}, {x1, y1, z}, {x0, y1, z});
            for (float x = x0 + 0.2f; x < x1 - 0.05f; x += 0.2f) lineW(cam, {x, y0, z}, {x, y1, z}, ironLight);
            for (float y : {y1 - 0.35f, y0 + 0.35f, (y0 + y1) * 0.5f}) {
                if (y > y0 && y < y1) {
                    lineW(cam, {x0, y, z}, {x1, y, z}, ink);
                    lineW(cam, {x0, y + 0.04f, z}, {x1, y + 0.04f, z}, ironLight);
                }
            }
            // Hazard stripes along the lower edge.
            for (float x = x0; x < x1 - 0.01f; x += 0.4f) {
                const float xe = std::min(x1, x + 0.2f);
                quad(cam, hazardYellow, {x, y0, z}, {xe, y0, z}, {xe, y0 + 0.22f, z}, {x, y0 + 0.22f, z});
            }
        }
    }
    // Warning lamp above the gate, on whichever face the player sees.
    const float lz = cam.z < 0 ? b.z0 - 0.32f : b.z1 + 0.32f;
    float sx, sy, depth;
    if (project(cam, (b.x0 + b.x1) * 0.5f, 3.45f, lz, sx, sy, depth)) {
        const bool on = warn ? (ms / 90) % 2 == 0 : moving;
        const int r = std::max(2, roundi(0.16f * g_focal / depth));
        auto& c = display::canvas();
        c.fillCircle(roundi(sx), roundi(sy), r + 1, ink);
        c.fillCircle(roundi(sx), roundi(sy), r, on ? rgb(255, 170, 40) : rgb(120, 44, 34));
        if (on) c.fillCircle(roundi(sx), roundi(sy), std::max(1, r / 2), white);
    }
}

// ---- the arena shell ----

void floorAndWalls(const Cam& cam, uint32_t ms) {
    const float hw = G::kArenaHalfWidth, wz = G::kWallZ, wh = G::kWallHeight;
    quad(cam, floorColor, {-hw, 0, -wz}, {hw, 0, -wz}, {hw, 0, wz}, {-hw, 0, wz});
    for (float x = -hw + 1.5f; x < hw - 0.01f; x += 1.5f) lineW(cam, {x, 0, -wz}, {x, 0, wz}, gridColor);
    for (float z = -wz + 2.0f; z < wz - 0.01f; z += 2.0f) lineW(cam, {-hw, 0, z}, {hw, 0, z}, gridColor);
    // The line nobody may cross, in front of each team's high cover.
    for (float side : {-1.0f, 1.0f}) {
        const float z0 = side * (G::kNearZ - 0.32f), z1 = side * G::kNearZ;
        quad(cam, ink, {-hw, 0, z0}, {hw, 0, z0}, {hw, 0, z1}, {-hw, 0, z1});
        for (float x = -hw; x < hw - 0.01f; x += 2.0f)
            quad(cam, hazardYellow, {x, 0, z0}, {x + 1.0f, 0, z0}, {x + 1.0f, 0, z1}, {x, 0, z1});
    }
    // Plaza pattern in the closed middle.
    for (float r : {2.0f, 4.0f}) {
        lineW(cam, {-r, 0, -r}, {r, 0, -r}, sandDark);
        lineW(cam, {r, 0, -r}, {r, 0, r}, sandDark);
        lineW(cam, {r, 0, r}, {-r, 0, r}, sandDark);
        lineW(cam, {-r, 0, r}, {-r, 0, -r}, sandDark);
    }

    // Walls with a shaded foot and a coping.
    struct Wall { P3 a, b; uint16_t color; };
    const Wall walls[4] = {
        {{-hw, 0, -wz}, {hw, 0, -wz}, sandFront}, {{-hw, 0, wz}, {hw, 0, wz}, sandFront},
        {{-hw, 0, -wz}, {-hw, 0, wz}, sandSide}, {{hw, 0, -wz}, {hw, 0, wz}, sandSide},
    };
    for (const Wall& w : walls) {
        auto at = [&](const P3& p, float y) { P3 q = {p.x, y, p.z}; return q; };
        quad(cam, w.color, w.a, w.b, at(w.b, wh), at(w.a, wh));
        quad(cam, sandDark, w.a, w.b, at(w.b, 0.3f), at(w.a, 0.3f));
        quad(cam, sandLight, at(w.a, wh - 0.3f), at(w.b, wh - 0.3f), at(w.b, wh), at(w.a, wh));
        for (float y : {1.4f, 2.7f}) lineW(cam, at(w.a, y), at(w.b, y), sandDark);
    }
    // Side walls: arched windows, striped awnings, hanging cloth.
    for (float s : {-1.0f, 1.0f}) {
        const float x = s * hw;
        for (float z : {-10.5f, -4.5f, 4.5f, 10.5f}) {
            quad(cam, doorDark, {x, 1.9f, z - 0.55f}, {x, 1.9f, z + 0.55f}, {x, 3.2f, z + 0.55f}, {x, 3.2f, z - 0.55f});
            lineW(cam, {x, 3.2f, z - 0.55f}, {x, 3.2f, z + 0.55f}, sandLight);
            lineW(cam, {x, 1.9f, z - 0.55f}, {x, 1.9f, z + 0.55f}, sandLight);
        }
        for (float z : {-10.5f, 4.5f}) {  // awnings shade some of the windows
            const float xo = x - s * 0.7f;
            for (int k = 0; k < 2; ++k) {
                const float za = z - 0.9f + k * 0.9f, zb = za + 0.9f;
                quad(cam, k % 2 ? white : (z < 0 ? teamColor[0] : teamColor[1]), {x, 3.45f, za}, {x, 3.45f, zb}, {xo, 3.0f, zb}, {xo, 3.0f, za});
            }
        }
        for (float z : {-9.8f, 9.8f}) {  // cloth swaying a little
            const float sway = std::sin(ms * 0.0017f + z) * 0.06f;
            quad(cam, rgb(176, 64, 54), {x - s * 0.05f, 3.3f, z - 0.35f}, {x - s * 0.05f, 3.3f, z + 0.35f},
                 {x - s * (0.08f + sway), 2.0f, z + 0.35f}, {x - s * (0.08f + sway), 2.0f, z - 0.35f});
        }
    }
    // End walls: big double doors with a frame, windows, crenellations.
    for (float z : {-wz, wz}) {
        quad(cam, doorFrame, {-1.7f, 0, z}, {1.7f, 0, z}, {1.7f, 3.0f, z}, {-1.7f, 3.0f, z});
        quad(cam, doorDark, {-1.45f, 0, z}, {1.45f, 0, z}, {1.45f, 2.75f, z}, {-1.45f, 2.75f, z});
        lineW(cam, {0, 0, z}, {0, 2.75f, z}, plank);
        for (float y : {0.9f, 1.8f}) lineW(cam, {-1.45f, y, z}, {1.45f, y, z}, plank);
        for (float x : {-4.0f, 4.0f}) {
            quad(cam, doorDark, {x - 0.5f, 1.9f, z}, {x + 0.5f, 1.9f, z}, {x + 0.5f, 3.2f, z}, {x - 0.5f, 3.2f, z});
            lineW(cam, {x - 0.5f, 3.2f, z}, {x + 0.5f, 3.2f, z}, sandLight);
        }
        for (float x = -hw + 0.3f; x < hw - 0.5f; x += 2.4f)
            quad(cam, sandLight, {x, wh, z}, {x + 1.2f, wh, z}, {x + 1.2f, wh + 0.4f, z}, {x, wh + 0.4f, z});
    }
}

// ---- the figure ----

struct Part { int x, y, w, h; uint16_t color; bool circle; };

struct Figure {
    Part parts[28];
    int n = 0;
    void rect(int x, int y, int w, int h, uint16_t color) {
        if (n < 28) { Part p = {x, y, w < 1 ? 1 : w, h < 1 ? 1 : h, color, false}; parts[n++] = p; }
    }
    void circle(int x, int y, int r, uint16_t color) {
        if (n < 28) { Part p = {x, y, r < 1 ? 1 : r, 0, color, true}; parts[n++] = p; }
    }
    void flush(bool outline) const {
        auto& c = display::canvas();
        if (outline) {
            for (int i = 0; i < n; ++i) {
                const Part& p = parts[i];
                if (p.circle) c.fillCircle(p.x, p.y, p.w + 1, ink);
                else c.fillRect(p.x - 1, p.y - 1, p.w + 2, p.h + 2, ink);
            }
        }
        for (int i = 0; i < n; ++i) {
            const Part& p = parts[i];
            if (p.circle) c.fillCircle(p.x, p.y, p.w, p.color);
            else c.fillRect(p.x, p.y, p.w, p.h, p.color);
        }
    }
};

struct Motion { float x = 0, z = 0, moving = 0; bool seen = false; };
Motion g_motion[2];

// A soldier seen from the front: legs, vest, arms, rifle, head with a helmet (CT) or a mask (T). The head and
// the rifle turn the way the player is aiming, so you can tell when you are being aimed at.
void drawFigure(const Cam& cam, const G& g, int idx, uint32_t ms) {
    const G::Player& p = g.players[idx];
    const int team = g.team(idx);
    const V3 foot = toCam(cam, p.x, 0, p.z);
    if (foot.z < 0.8f) return;
    const float s = g_focal / foot.z;
    const float sx0 = kCx + g_focal * foot.x / foot.z;
    const float lift = G::jumpHeight(p);
    auto yAt = [&](float y) {
        const V3 v = toCam(cam, p.x, y + lift, p.z);
        return kCy - g_focal * v.y / v.z;
    };
    auto& c = display::canvas();

    Motion& m = g_motion[idx];
    if (!m.seen || std::fabs(p.x - m.x) + std::fabs(p.z - m.z) > 1.5f) { m.moving = 0; m.seen = true; }
    const float moved = std::fabs(p.x - m.x) + std::fabs(p.z - m.z);
    m.moving = lerpf(m.moving, moved > 0.012f ? 1.0f : 0.0f, 0.25f);
    m.x = p.x; m.z = p.z;

    // Shadow on the floor (it stays down while the figure hops).
    const int footY = roundi(yAt(-lift));
    const int shadowW = std::max(3, roundi(0.5f * s));
    if (p.dead <= 0.f || p.dead > G::kRespawnTime - 0.3f) c.fillRect(roundi(sx0) - shadowW, footY - 1, shadowW * 2, std::max(1, roundi(0.12f * s)), shadow);

    const bool blink = p.hurt > 0 && (ms / 60) % 2 == 0;
    if (p.invuln > 0 && (ms / 70) % 3 == 0) return;  // spawn protection flickers
    const uint16_t skin = blink ? white : rgb(236, 190, 150);
    const uint16_t vest = blink ? white : teamColor[team];
    const uint16_t vestDark = blink ? white : teamDark[team];
    const uint16_t head = blink ? white : teamHead[team];
    const uint16_t pants = blink ? white : (team ? rgb(40, 48, 66) : rgb(86, 70, 52));
    const uint16_t boots = blink ? white : rgb(26, 24, 26);
    const bool detail = s >= 10.0f;

    auto X = [&](float xm) { return roundi(sx0 + xm * s); };
    auto Y = [&](float ym) { return roundi(yAt(ym)); };
    Figure f;

    const float death = p.dead > 0 ? std::min(1.0f, (G::kRespawnTime - p.dead) / 0.35f) : 0.0f;
    if (death > 0) {  // falling over backwards: shrinks to a body lying across the floor
        const float k = death;
        const float half = lerpf(0.3f, 0.95f, k);
        const float top = lerpf(1.45f, 0.32f, k);
        f.rect(X(-half), Y(top), X(half) - X(-half), Y(lerpf(0.85f, 0.0f, k)) - Y(top), vest);
        f.rect(X(-half), Y(lerpf(0.85f, 0.18f, k)), X(-half + 0.55f) - X(-half), Y(0.0f) - Y(lerpf(0.85f, 0.18f, k)), pants);
        f.circle(X(lerpf(0.0f, half + 0.25f, k)), Y(lerpf(1.6f, 0.2f, k)), std::max(1, roundi(0.2f * s)), skin);
        f.flush(detail);
        return;
    }

    // Aim: how far the rifle is turned from pointing at the viewer, and how high.
    const float beta = std::max(-1.2f, std::min(1.2f, -p.yaw * kDeg));
    const float elev = p.pitch * kDeg;
    const float sinB = std::sin(beta);
    const float phase = ms * 0.014f;
    const float stride = m.moving * std::sin(phase);

    if (!detail) {  // far away: a handful of pixels with the right colours
        f.rect(X(-0.2f), Y(0.85f), X(0.2f) - X(-0.2f), Y(0.0f) - Y(0.85f), pants);
        f.rect(X(-0.28f), Y(1.45f), X(0.28f) - X(-0.28f), Y(0.85f) - Y(1.45f), vest);
        f.circle(X(sinB * 0.1f), Y(1.62f), std::max(1, roundi(0.2f * s)), head);
        const int rx = X(0.1f), ry = Y(1.2f);
        f.rect(std::min(rx, X(0.1f + 0.8f * sinB)), ry - 1, std::max(1, std::abs(X(0.1f + 0.8f * sinB) - rx)), 2, ink);
        f.flush(false);
    } else {
        const float hip = 0.85f - 0.2f * (lift > 0 ? 1.0f : 0.0f);
        const float stepL = std::max(0.0f, stride) * 0.14f, stepR = std::max(0.0f, -stride) * 0.14f;
        f.rect(X(-0.24f), Y(hip), X(-0.04f) - X(-0.24f), Y(stepL) - Y(hip), pants);
        f.rect(X(0.04f), Y(hip), X(0.24f) - X(0.04f), Y(stepR) - Y(hip), pants);
        f.rect(X(-0.27f), Y(stepL + 0.1f), X(-0.03f) - X(-0.27f), Y(stepL) - Y(stepL + 0.1f), boots);
        f.rect(X(0.03f), Y(stepR + 0.1f), X(0.27f) - X(0.03f), Y(stepR) - Y(stepR + 0.1f), boots);
        // Vest over a shirt, belt, shoulder straps.
        f.rect(X(-0.3f), Y(1.45f), X(0.3f) - X(-0.3f), Y(hip - 0.02f) - Y(1.45f), vest);
        f.rect(X(-0.3f), Y(hip + 0.1f), X(0.3f) - X(-0.3f), Y(hip) - Y(hip + 0.1f), vestDark);
        f.rect(X(-0.07f), Y(1.42f), std::max(1, X(0.07f) - X(-0.07f)), Y(hip + 0.12f) - Y(1.42f), vestDark);
        // Arms: the left hangs, the right holds the rifle.
        f.rect(X(-0.44f), Y(1.38f), X(-0.3f) - X(-0.44f), Y(1.0f) - Y(1.38f), vestDark);
        f.rect(X(0.3f), Y(1.38f), X(0.44f) - X(0.3f), Y(1.1f) - Y(1.38f), vestDark);
        // Neck and head, turned towards where the player aims.
        const float hx = sinB * 0.1f;
        f.rect(X(-0.07f + hx * 0.5f), Y(1.52f), std::max(1, X(0.07f) - X(-0.07f)), Y(1.44f) - Y(1.52f), skin);
        f.circle(X(hx), Y(1.62f), std::max(2, roundi(0.19f * s)), skin);
        if (team == 1) {  // helmet with a visor strip
            f.rect(X(hx - 0.2f), Y(1.84f), X(hx + 0.2f) - X(hx - 0.2f), Y(1.66f) - Y(1.84f), head);
            f.rect(X(hx - 0.21f), Y(1.7f), X(hx + 0.21f) - X(hx - 0.21f), Y(1.66f) - Y(1.7f), vestDark);
        } else {          // balaclava and cap, eyes showing
            f.rect(X(hx - 0.2f), Y(1.83f), X(hx + 0.2f) - X(hx - 0.2f), Y(1.7f) - Y(1.83f), head);
            f.rect(X(hx - 0.2f), Y(1.57f), X(hx + 0.2f) - X(hx - 0.2f), Y(1.45f) - Y(1.57f), head);
        }
        f.flush(true);
        // Eyes on top of everything else: they look where the rifle points.
        if (!blink) {
            const int ey = Y(1.64f), ex = X(hx + sinB * 0.04f);
            const int gap = std::max(1, roundi(0.07f * s));
            c.drawPixel(ex - gap, ey, ink);
            c.drawPixel(ex + gap, ey, ink);
        }
    }

    // The rifle across the chest, foreshortened towards the viewer and tilted up or down with the aim.
    const float baseX = 0.1f, baseY = 1.18f;
    const float tipX = baseX + 0.95f * sinB, tipY = baseY + 0.5f * std::sin(elev) + 0.04f;
    const int bx = X(baseX), by = Y(baseY), tx = X(tipX), ty = Y(tipY);
    const int thick = std::max(1, roundi(0.07f * s));
    for (int t = 0; t < thick; ++t) {
        c.drawLine(bx, by + t, tx, ty + t, ink);
    }
    c.drawLine(bx, by + thick, (bx + tx) / 2, (by + ty) / 2 + thick, rgb(109, 129, 66));
    if (detail) {
        c.drawLine(bx, by - 1, tx, ty - 1, rgb(96, 100, 112));
        c.fillRect(X(baseX - 0.12f), Y(baseY + 0.14f), std::max(1, X(baseX + 0.1f) - X(baseX - 0.12f)), std::max(1, roundi(0.06f * s)), ink);  // scope
    }
    if (p.flash > 0.04f) {
        const int r = std::max(2, roundi(0.2f * s));
        c.fillCircle(tx, ty, r, gold);
        c.fillCircle(tx, ty, std::max(1, r / 2), white);
    }
    // Spawn protection: a ring round the feet.
    if (p.invuln > 0) {
        const int rw = std::max(4, roundi(0.75f * s));
        c.drawFastHLine(roundi(sx0) - rw, footY, rw * 2, rgb(150, 230, 255));
        c.drawFastHLine(roundi(sx0) - rw + 1, footY + 1, rw * 2 - 2, rgb(90, 170, 220));
    }
}

// ---- first person ----

// Hand-pixelled at 2x scale. Each row is a palette-indexed silhouette; spaces show the world.
void drawGun(const G::Player& me, uint32_t ms, bool moving) {
    static const char* const art[] = {
        "",
        "",
        "                  ....",
        "  .              .aaaa.      .",
        "...a..          ..abbba.     ....",
        ".aabba..        ..aaaaa..   .....",
        " ...abbba..      .aaaaaabb..  ...",
        " .....aabbb...   .aaaaaaaabbb....",
        "   ......aabbb......aaaaaaaaabbb......",
        "      ......aabbb.........aaaaaabb........",
        "        ......aaabb..........aaaaaaaa......",
        "           ......aaaa...........aaaa...ghg..",
        "             ..d...aaaaa..............ghggg..",
        "              .cddd...a....... ......ahgggg..",
        "              ..cdddddd.......... ...aggggg..",
        "               .ccccddddd.............aggg...",
        "               ....ccccddddd.d.........aa...    .",
        "                 ....b..cccdddddd...............d...",
        "                   ...bbbcccccddddddd.....dddddddddd....",
        "                     .bbbb..cccecddddd.ddddddddddedddddd....",
        "                     ..bbb.....ceeeccccccccdddeeeeeeeeeec...",
        "                     .....b.....ceeeeeeeeeeeeee.eeeeeeeec...",
        "                      ...........ceeeeeeeeeee...ff..eeeec...",
        "                      .............eeeeeeee...ff...cccccc...",
        "                       ...... ........cccccec......cccccc...",
        "                        ...... ...........cccc....cccccc....",
        "                         ...    .............ccccccccccc....",
        "                          .         .   .......ccccccccc....",
        "                                           .......cccccc....",
        "                                              ......c.......",
        "                                                 ...........",
    };
    // '.' is the outline, a..h are the flat steel, stock and lens colours.
    static constexpr uint16_t colors[] = {
        rgb(23, 28, 32), rgb(66, 78, 85), rgb(151, 164, 159),
        rgb(122, 163, 66), rgb(199, 219, 117), rgb(82, 119, 58),
        rgb(79, 90, 69), rgb(57, 135, 142), rgb(181, 235, 211)
    };
    const float kick = std::min(1.f, me.flash / 0.12f);
    const int x = 134 + roundi(kick * 5);
    const int y = 76 + roundi(kick * 6) + (me.jumpT > 0 ? 5 : 0)
                     + (moving ? roundi(std::sin(ms * .012f)) : 0);
    for (unsigned row = 0; row < sizeof(art) / sizeof(art[0]); ++row) {
        const int top = y + 2 * int(row);
        if (top >= 135) break;
        const char* pixels = art[row];
        for (int start = 0; pixels[start];) {
            const char color = pixels[start];
            int end = start + 1;
            while (pixels[end] == color) ++end;
            const int left = x + 2 * start;
            if (left >= 240) break;
            if (color != ' ') {
                const int right = std::min(239, x + 2 * end - 1);
                const uint16_t value = colors[color == '.' ? 0 : 1 + color - 'a'];
                fillSpan(top, left, right, value);
                if (top + 1 < 135) fillSpan(top + 1, left, right, value);
            }
            start = end;
        }
    }
    auto& c = display::canvas();
    const bool cycling = me.cooldown > .25f && me.cooldown < .9f;
    const int bx = x + (cycling ? 88 : 84), by = y + (cycling ? 32 : 43);
    c.drawLine(x + 76, y + 37, bx, by, colors[0]);
    c.fillCircle(bx, by, 3, colors[0]);
    c.drawPixel(bx - 1, by - 1, colors[2]);
    if (me.flash > .07f) {
        const int mx = x + 1, my = y + 10;
        c.drawLine(mx - 9, my - 5, mx + 2, my + 1, gold);
        c.fillCircle(mx, my, 5, gold);
        c.fillCircle(mx, my, 2, white);
    }
}

void drawCrosshair(const G::Player& me, bool scoped) {
    auto& c = display::canvas();
    const int cx = 120, cy = 67;
    if (scoped) {
        // Mask the world outside the lens. Keep HUD and hit feedback above the mask.
        static uint8_t halfWidth[135];
        static bool ready = false;
        if (!ready) {
            for (int y = 0; y < 135; ++y) {
                const int dy = y - cy;
                halfWidth[y] = int(std::sqrt(float(67 * 67 - dy * dy)));
            }
            ready = true;
        }
        for (int y = 0; y < 135; ++y) {
            const int left = cx - halfWidth[y], right = cx + halfWidth[y] + 1;
            fillSpan(y, 0, left - 1, ink);
            fillSpan(y, right, 239, ink);
        }
        c.drawFastHLine(cx - 67, cy, 135, ink);
        c.drawFastVLine(cx, 0, 135, ink);
    } else for (int o = 0; o < 2; ++o) {
        const uint16_t col = o == 0 ? ink : white;
        c.drawFastHLine(cx - 8 + o, cy + o, 5, col); c.drawFastHLine(cx + 4 + o, cy + o, 5, col);
        c.drawFastVLine(cx + o, cy - 8 + o, 5, col); c.drawFastVLine(cx + o, cy + 4 + o, 5, col);
    }
    c.drawPixel(cx, cy, redHot);
    if (me.marker > 0) {
        const uint16_t col = me.markerShield ? rgb(150, 230, 255) : (me.markerHead ? redHot : white);
        for (int k = 5; k <= 10; ++k) {
            c.drawPixel(cx - k, cy - k, col); c.drawPixel(cx + k, cy - k, col);
            c.drawPixel(cx - k, cy + k, col); c.drawPixel(cx + k, cy + k, col);
        }
    }
    if (me.cooldown > 0) {
        const int w = static_cast<int>(18 * (1 - std::min(1.0f, me.cooldown / G::kShotCooldown)));
        c.fillRect(cx - 9, cy + 12, 18, 2, ink);
        c.fillRect(cx - 9, cy + 12, w, 2, gold);
    }
}

// Small, stepped pixel silhouette shared by the pickup, hand and tumbling projectile.
// Palette: outline, toasted bun, bun light, sesame, lettuce, tomato, cheese, beef.
void burgerIcon(int x, int y, int r, bool armed, float angle = 0) {
    static const char* rows[] = {
        "........ooooooo.........",
        "......oobbbbbbbboo......",
        "....oobbblllllbbboo.....",
        "...obblsslllsllbbbbo....",
        "..obllllllsllllllbbbo...",
        "..obllsllllllssllbbbo...",
        ".oblllllllllllllllbbbo..",
        ".obbbbbbbbbbbbbbbbbbbo..",
        ".ooooooooooooooooooooo..",
        "oogggggggggggggggggggoo.",
        ".ogggoggggoggggoggggo...",
        "..ottttttttttttttttto...",
        ".occccccccccccccccccco..",
        "..ommmmccmmmmccmmmmo....",
        ".ommmmmmcmmmmmcmmmmmo...",
        "..oooooooooooooooooo....",
        "..oblllllllllllllbbo....",
        "...obbbbbbbbbbbbbbo.....",
        "....oooooooooooooo......"
    };
    const uint16_t colors[] = {rgb(59,38,27),rgb(183,108,44),rgb(231,170,78),rgb(255,230,161),
        rgb(104,150,52),rgb(183,65,38),rgb(245,195,58),rgb(86,48,32)};
    auto& c=display::canvas();
    r=std::max(3,std::min(32,r));
    const float scale=r/12.f, co=std::cos(angle), si=std::sin(angle);
    const int bound=roundi(r*1.4f);
    for (int dy=-bound;dy<=bound;++dy) for (int dx=-bound;dx<=bound;++dx) {
        const int u=static_cast<int>(std::floor((dx*co+dy*si)/scale+12));
        const int v=static_cast<int>(std::floor((-dx*si+dy*co)/scale+9));
        if (v<0 || v>=19 || u<0 || u>=24) continue;
        const char pixel=rows[v][u];
        int col=-1;
        switch(pixel) {case 'o':col=0;break;case 'b':col=1;break;case 'l':col=2;break;case 's':col=3;break;
            case 'g':col=4;break;case 't':col=5;break;case 'c':col=6;break;case 'm':col=7;break;}
        if(col>=0)c.drawPixel(x+dx,y+dy,colors[col]);
    }
    if (armed && angle==0) {
        // A tiny metal lever and pin, tucked beside the bun rather than a giant ring.
        c.fillRect(x+r/2,y-r*3/4-2,5,2,ink);
        c.drawRect(x+r/2+3,y-r*3/4-5,4,4,rgb(171,176,163));
    }
}

void burgerHand(int x, int y, int team, bool open) {
    auto& c=display::canvas();
    c.fillRect(x-5,y+7,18,27,ink);
    c.fillRect(x-3,y+9,14,25,teamDark[team]);
    c.fillRect(x-6,y+5,18,6,rgb(59,62,47)); // glove cuff
    c.fillRect(x-8,y-2,19,10,rgb(129,91,58));
    c.fillRect(x-7,y-3,16,8,rgb(193,143,91));
    c.fillRect(x-6,y-3,12,2,rgb(223,174,115));
    if (open) {
        for(int i=0;i<4;++i)c.fillRect(x-9+i*5,y-9+(i%2),3,9,rgb(203,153,99));
    } else {
        c.fillRect(x-11,y-6,5,9,rgb(102,70,45));
        c.fillRect(x-10,y-6,4,7,rgb(219,164,107));
    }
}

void drawBurger(const Cam& cam, const G& g, uint32_t ms) {
    const auto& b=g.burger;
    if (!g.segmentClear(cam.x,cam.y,cam.z,b.x,b.y,b.z)) return;
    float x,y,d;
    if (!project(cam,b.x,b.y,b.z,x,y,d)) return;
    auto& c=display::canvas();
    if (b.state==G::BurgerState::Blast) {
        const float age=.45f-b.timer, progress=std::max(0.f,age/.45f);
        const float size=std::min(70.f,g_focal*G::kBlastRadius/d);
        // Expanding, irregular flash: short bright centre, then dusty smoke and food fragments.
        if(age<.20f) {
            const float r=size*(.3f+progress);
            for(int i=0;i<10;++i) {
                const float a=i*kPi/5;
                const float reach=r*(i%2?.9f:1.25f);
                c.fillTriangle(roundi(x+std::cos(a)*reach),roundi(y+std::sin(a)*reach),
                    roundi(x+std::cos(a+.4f)*r*.35f),roundi(y+std::sin(a+.4f)*r*.35f),
                    roundi(x+std::cos(a-.4f)*r*.35f),roundi(y+std::sin(a-.4f)*r*.35f),rgb(223,117,42));
            }
            c.fillCircle(roundi(x),roundi(y),std::max(2,roundi(r*.36f)),gold);
            c.fillCircle(roundi(x)-1,roundi(y)-1,std::max(1,roundi(r*.18f)),rgb(255,239,186));
        }
        for(int i=0;i<12;++i) {
            const float a=i*2.39996f;
            const float spread=size*(.25f+progress*.8f)*(i%3==0?.65f:1.f);
            const int px=roundi(x+std::cos(a)*spread),py=roundi(y+std::sin(a)*spread-progress*size*.25f);
            const int r=std::max(2,roundi(size*(.06f+progress*.09f)));
            const uint16_t smoke=i%2?rgb(159,133,99):rgb(188,161,116);
            c.fillRect(px-r,py-r/2,r*2+1,r+1,smoke);
            c.fillRect(px-r/2,py-r,r+1,r*2+1,smoke);
            const int fx=roundi(x+std::cos(a)*spread*1.3f),fy=roundi(y+std::sin(a)*spread*1.3f+progress*progress*size*.4f);
            const uint16_t crumb=i%3==0?rgb(108,143,48):i%3==1?gold:rgb(187,70,35);
            c.fillRect(fx,fy,3,i%2?2:3,crumb);
        }
    } else {
        const bool flying=b.state==G::BurgerState::Flying;
        const int r=std::max(3,roundi(g_focal*(flying?G::kGrenadeRadius:G::kBurgerRadius)/d));
        const float angle=flying?(G::kBurgerFuse-b.timer)*12.f:0;
        burgerIcon(roundi(x),roundi(y),r,false,angle);
        if (flying && (ms/90)%2) c.fillRect(roundi(x)+r/2,roundi(y)-r,2,2,rgb(255,99,45));
        if (!flying) { // Two quiet glints distinguish the collectible from scenery.
            const int drift=(ms/250)%2;
            c.drawFastHLine(roundi(x)-r-4,roundi(y)-2+drift,3,gold);
            c.drawFastVLine(roundi(x)-r-3,roundi(y)-3+drift,3,gold);
        }
    }
}

struct Item { float dist; int index; };  // index < kBoxCount: a box; kBoxCount: the opponent

float boxDistance(const Cam& cam, const G::Box& b) {
    const float dx = std::max(std::max(b.x0 - cam.x, 0.0f), cam.x - b.x1);
    const float dz = std::max(std::max(b.z0 - cam.z, 0.0f), cam.z - b.z1);
    return dx * dx + dz * dz;
}

}  // namespace

void MidDiffGame::draw(int local, const arcade::DrawCtx& ctx) {
    g_viewLocal = local;
    auto& c = display::canvas();
#ifndef MIDDIFF_HOST_CANVAS
    g_pixels = (c.width() == 240 && c.height() == 135 && c.getColorDepth() == 16 && c.bufferLength() == 240 * 135 * 2)
                   ? static_cast<uint16_t*>(c.getBuffer()) : nullptr;
#endif
    const Player& me = players[local];
    const float throwAge = burger.state==BurgerState::Flying && burger.owner==local ? kBurgerFuse-burger.timer : 10.f;
    const bool throwing = throwAge < .30f;
    const bool scoped = ctx.scoped && me.dead <= 0 && !holdingBurger(local) && !throwing;
    g_focal = scoped ? 380.f : 190.f;
    const int foeIndex = 1 - local;
    const int myTeam = team(local);

    // A dead player's eye drops to the floor and the view tilts up.
    const float fall = me.dead > 0 ? std::min(1.0f, (kRespawnTime - me.dead) / 0.4f) : 0.0f;
    const float eye = lerpf(kEye + jumpHeight(me), 0.3f, fall);
    const float kick = 2.5f * std::min(1.0f, me.flash / 0.12f);
    const Cam cam = makeCam(me.x, eye, me.z, face(local), lookYaw(local), lookPitch(local) + kick + 14.0f * fall);

    sky(cam, ctx.nowMs);
    floorAndWalls(cam, ctx.nowMs);

    // Everything solid and the opponent, back to front.
    Item items[kBoxCount + 2];
    int count = 0;
    for (int i = 0; i < kBoxCount; ++i) items[count++] = {boxDistance(cam, boxes[i]), i};
    {
        const Player& foe = players[foeIndex];
        const float dx = foe.x - cam.x, dz = foe.z - cam.z;
        items[count++] = {dx * dx + dz * dz, kBoxCount};
    }
    if (burger.state==BurgerState::Pickup || burger.state==BurgerState::Flying || burger.state==BurgerState::Blast) {
        const float dx=burger.x-cam.x,dz=burger.z-cam.z;
        items[count++]={dx*dx+dz*dz,kBoxCount+1};
    }
    std::sort(items, items + count, [](const Item& a, const Item& b) { return a.dist > b.dist; });
    for (int k = 0; k < count; ++k) {
        const int i = items[k].index;
        if (i == kBoxCount+1) { drawBurger(cam,*this,ctx.nowMs); continue; }
        if (i == kBoxCount) { drawFigure(cam, *this, foeIndex, ctx.nowMs); continue; }
        const Box& b = boxes[i];
        if (b.kind == Kind::Gate) drawGate(cam, *this, ctx.nowMs);
        else if (b.kind == Kind::Barrel) drawBarrel(cam, b);
        else drawBox(cam, b, boxBottom(i));
    }

    // Trails of the shots that were just fired.
    for (int i = 0; i < 2; ++i) {
        const Player& p = players[i];
        if (p.flash <= 0) continue;
        const uint16_t col = p.flash > 0.06f ? white : gold;
        const V3 end = toCam(cam, p.shotX, p.shotY, p.shotZ);
        if (i == local) {
            lineC({0.2f, -0.2f, 0.5f}, end, col);
        } else {
            const V3 from = toCam(cam, p.x, kEye - 0.45f + jumpHeight(p), p.z);
            lineC(from, end, col);
        }
    }

    if (me.dead <= 0) {
        bool moving = false;
        static float lastX[2], lastZ[2];
        moving = std::fabs(me.x - lastX[local]) + std::fabs(me.z - lastZ[local]) > 0.012f;
        lastX[local] = me.x; lastZ[local] = me.z;
        if (holdingBurger(local)) {
            const int bob=roundi(std::sin(ctx.nowMs*(moving?.014f:.004f))*(moving?2.f:1.f));
            burgerHand(202,117+bob,myTeam,false);
            burgerIcon(199,108+bob,14,true);
            ui::shadowText("A: THROW",196,87,1,gold);
        } else if (throwing) {
            const float t=throwAge/.30f;
            const int x=roundi(201-29*std::sin(t*kPi));
            const int y=roundi(115-12*std::sin(t*kPi)+t*t*30);
            burgerHand(x,y,myTeam,true);
        } else if (!scoped) drawGun(me, ctx.nowMs, moving);
        drawCrosshair(me, scoped);
    }

    // Damage flash around the screen edge.
    if (me.hurt > 0 && me.dead <= 0) {
        const int t = 2 + static_cast<int>(me.hurt * 20);
        const uint16_t col = rgb(210, 32, 32);
        c.fillRect(0, 0, 240, t, col); c.fillRect(0, 135 - t, 240, t, col);
        c.fillRect(0, 0, t, 135, col); c.fillRect(240 - t, 0, t, 135, col);
    }
    // Spawn protection: a pale frame, brighter when it is about to end.
    if (me.invuln > 0 && me.dead <= 0) {
        const bool lowTime = me.invuln < 0.4f;
        const uint16_t col = lowTime ? ((ctx.nowMs / 80) % 2 ? rgb(150, 230, 255) : rgb(60, 110, 150)) : rgb(110, 190, 235);
        c.drawRect(0, 0, 240, 135, col);
        c.drawRect(1, 1, 238, 133, col);
        if (me.invuln > kSpawnInvuln - 0.2f) {  // a white flash as you appear
            for (int y = 0; y < 135; y += 2) c.drawFastHLine(0, y, 240, white);
        }
    }

    // Health and stamina (one chunk per hop).
    const uint16_t hpCol = me.hp > 50 ? rgb(110, 230, 110) : rgb(240, 80, 70);
    c.fillRect(5, 123, 70, 8, ink);
    c.fillRect(6, 124, me.hp * 68 / kMaxHp, 6, hpCol);
    ui::shadowText("HP", 14, 118, 1, white);
    ui::shadowText("HOP", 100, 118, 1, white);
    for (int k = 0; k < 3; ++k) {
        const int x = 80 + k * 24;
        c.fillRect(x, 123, 22, 8, ink);
        const float fill = std::max(0.0f, std::min(1.0f, me.stamina * 3.0f - k));
        c.fillRect(x + 1, 124, static_cast<int>(20 * fill), 6, fill >= 1.0f ? rgb(90, 190, 255) : rgb(60, 120, 170));
    }

    // Scores by team, and the local player's mark.
    const int playerOfTeam[2] = {swap_ ? 1 : 0, swap_ ? 0 : 1};
    if (ctx.solo) {
        char label[20];
        snprintf(label, sizeof(label), "%s KILLS %u", myTeam ? "CT" : "T", unsigned(score[0]));
        ui::shadowText(label, 120, 7, 1, teamColor[myTeam]);
    } else {
        for (int t = 0; t < 2; ++t) {
            const int center = t ? 172 : 68;
            char label[16];
            snprintf(label, sizeof(label), "%s %u", t ? "CT" : "T", unsigned(score[playerOfTeam[t]]));
            ui::shadowText(label, center, 7, 1, teamColor[t]);
            if (t == myTeam) c.drawFastHLine(center - 13, 12, 26, teamColor[t]);
        }
        char first[16];
        snprintf(first, sizeof(first), "FIRST %d", kWinningScore);
        ui::shadowText(first, 120, 7, 1, white);
    }

    // Death: red drifts up the screen and the respawn countdown fills.
    if (me.dead > 0 && phase_ == arcade::MatchPhase::Playing) {
        const int drift = static_cast<int>(ctx.nowMs / 25) % 4;
        const uint16_t veil = rgb(170, 20, 24), deep = rgb(110, 8, 14);
        for (int y = 0; y < 135; y += 2) c.drawFastHLine(0, y + (drift & 1), 240, (y / 2 + drift) % 3 ? veil : deep);
        c.fillRect(0, 0, 240, 3, deep); c.fillRect(0, 132, 240, 3, deep);
        ui::shadowText("YOU DIED", 120, 50, 2, white);
        const int w = static_cast<int>(100 * (1 - me.dead / kRespawnTime));
        c.fillRect(70, 72, 100, 5, ink);
        c.fillRect(70, 72, w, 5, white);
    }

    if (phase_ == arcade::MatchPhase::MatchEnd) {
        ui::matchResult(winner, local, 120, 40, 3);
    }
}

void MidDiffGame::sfx(uint32_t ev) {
    using sound::Note;
    static const uint8_t shot[] = {
#include "awp_pcm.inc"
    };
    static const Note ping[] = {{1500, 18}, {2100, 24}},
                      head[] = {{1800, 16}, {2600, 20}, {3400, 40}}, hurt[] = {{180, 30}, {90, 70}},
                      die[] = {{300, 40}, {200, 50}, {120, 90}}, gate[] = {{240, 18}, {180, 22}, {250, 18}, {170, 26}},
                      finish[] = {{660, 70}, {880, 70}, {1320, 130}}, lose[] = {{440, 70}, {330, 80}, {220, 130}};
    const int me = g_viewLocal;
    const uint32_t myShot = me ? ShotP1 : ShotP0, theirShot = me ? ShotP0 : ShotP1;
    const uint32_t iWasHit = me ? HurtP1 : HurtP0, theyWereHit = me ? HurtP0 : HurtP1;
    const uint32_t iDied = me ? DieP1 : DieP0, theyDied = me ? DieP0 : DieP1;
    if (ev & myShot) sound::playSample(shot, sizeof(shot), 12000, false);
    if (ev & theirShot) sound::playSample(shot, sizeof(shot), 10600, true);
    const uint16_t delay = ev & (myShot | theirShot) ? 75 : 0;
    if ((ev & (DieP0 | DieP1)) && phase_ == arcade::MatchPhase::MatchEnd) sound::play(winner == me ? finish : lose, 3, 5, delay);
    else if (ev & iDied) sound::play(die, 3, 4, delay);
    else if (ev & iWasHit) sound::play(hurt, 2, 4, delay);
    else if (ev & theyDied) sound::play(head, 3, 4, delay);
    else if (ev & theyWereHit) sound::play(ping, 2, 3, delay);
    else if (ev & BurgerBoom) sound::play(hurt, 2, 4);
    else if (ev & BurgerGet) sound::play(head, 3, 3);
    else if (ev & BurgerThrow) sound::play(gate, 4, 2);
    else if (ev & BurgerTick) sound::play(ping, 1, 1);
    else if (ev & GateMove) sound::play(gate, 4, 1);
}

namespace middiff_view {

// Mini-scene for the launcher card and the waiting screen: the mid wall with its arch, gate and barrels, and a
// soldier peeking over cover while a crosshair drifts around and a shot is fired now and then.
void drawCard(int x, int y, int w, int h, uint32_t ms) {
    auto& c = display::canvas();
    c.setClipRect(x, y, w, h);
    const int horizon = y + h * 45 / 100;
    c.fillRect(x, y, w, horizon - y, rgb(112, 158, 210));
    c.fillRect(x, y, w, h * 14 / 100, rgb(92, 138, 200));
    c.fillCircle(x + w * 80 / 100, y + h * 14 / 100, 6, rgb(255, 250, 220));
    // The wall: piers, the arch in the middle, the iron gate on the left, barrels on the right.
    const int wallTop = y + h * 12 / 100;
    c.fillRect(x, wallTop, w, horizon - wallTop, sandFront);
    c.fillRect(x, wallTop, w, 3, sandLight);
    c.fillRect(x, horizon - 4, w, 4, sandDark);
    const int doorTop = wallTop + (horizon - wallTop) * 38 / 100;
    const int centre = x + w / 2, aw = w * 16 / 100;
    c.fillRect(centre - aw / 2 - 2, doorTop - 2, aw + 4, horizon - doorTop + 2, sandDark);
    c.fillRect(centre - aw / 2, doorTop, aw, horizon - doorTop, doorDark);
    const int gx = x + w * 12 / 100, gw = w * 26 / 100;
    c.fillRect(gx, doorTop, gw, horizon - doorTop, iron);
    for (int bx = gx + 3; bx < gx + gw; bx += 4) c.drawFastVLine(bx, doorTop, horizon - doorTop, ironLight);
    const bool lamp = (ms / 300) % 2 == 0;
    c.fillCircle(gx + gw / 2, doorTop - 5, 2, lamp ? rgb(255, 170, 40) : rgb(120, 44, 34));
    const int rx = x + w * 62 / 100;
    c.fillRect(rx, doorTop, gw, horizon - doorTop, doorDark);
    c.fillRect(rx + 2, doorTop + 2, 8, horizon - doorTop - 2, rust);
    c.fillRect(rx + 2, doorTop - 5, 8, 7, rust);
    c.fillRect(rx + 12, horizon - 10, 7, 10, rustDark);
    c.fillRect(rx + 20, horizon - 10, 5, 10, rust);
    // Floor with lines running to the vanishing point.
    c.fillRect(x, horizon, w, y + h - horizon, floorColor);
    for (int i = -4; i <= 4; ++i) c.drawLine(centre + i * 6, horizon, centre + i * w / 4, y + h, gridColor);
    for (int k = 1; k <= 4; ++k) c.drawFastHLine(x, horizon + k * k * (y + h - horizon) / 18, w, gridColor);
    // A soldier comes up behind the high cover and ducks again.
    const int coverW = w * 40 / 100, coverX = x + (w - coverW) / 2, coverTop = y + h * 66 / 100, coverBottom = y + h * 84 / 100;
    const uint32_t t = ms % 3600;
    const int rise = t < 600 ? int(t) * 12 / 600 : t < 2400 ? 12 : t < 3000 ? 12 - int(t - 2400) * 12 / 600 : 0;
    const int bodyTop = coverTop - rise;
    if (rise > 0) {
        c.fillRect(centre - 6, bodyTop + 6, 12, rise, teamColor[1]);
        c.fillCircle(centre, bodyTop + 3, 4, rgb(236, 190, 150));
        c.fillRect(centre - 4, bodyTop - 2, 9, 3, teamHead[1]);
    }
    c.fillRect(coverX, coverTop, coverW, coverBottom - coverTop, mudFront);
    c.fillRect(coverX, coverTop, coverW, 3, mudTop);
    c.drawRect(coverX, coverTop, coverW, coverBottom - coverTop, mudDark);
    // Crosshair on the head, a flash and a trail when it fires.
    const int aimX = centre + int(std::sin(ms / 400.0f) * 5), aimY = bodyTop + 3 + int(std::sin(ms / 530.0f) * 3);
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
    ui::shadowText("STICK MOVE  CLICK+DIR HOP", 120, 96, 1, white);
    ui::shadowText("A FIRE   B SCOPE   HOLD B ZERO", 120, 108, 1, white);
}

}  // namespace middiff_view
