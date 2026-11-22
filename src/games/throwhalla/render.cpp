#include "games/throwhalla/render.h"

#include <math.h>

#include "core/display.h"
#include "ui/gfx.h"

namespace render {

namespace {

// Drawing target of the figure/line helpers; the knock-down animation redirects it to a small sprite,
// everything else draws into the back buffer of the display.
lgfx::LovyanGFX* g_override = nullptr;
lgfx::LovyanGFX& dst() {
    if (g_override) return *g_override;
    return display::canvas();
}
#define canvas (dst())

constexpr uint16_t kShadowColor = rgb(30, 34, 34);
constexpr uint16_t kWall = rgb(240, 240, 240);
constexpr uint16_t kWhite = rgb(255, 255, 255);
constexpr uint16_t kYellow = rgb(255, 220, 60);
constexpr uint16_t kSteel = rgb(200, 206, 218);
constexpr uint16_t kSteelDark = rgb(110, 116, 132);
constexpr uint16_t kWood = rgb(150, 98, 48);
constexpr uint16_t kWoodDark = rgb(100, 62, 30);

constexpr uint16_t kBodyColor[2] = {rgb(60, 140, 255), rgb(225, 60, 50)};
constexpr uint16_t kBodyShade[2] = {rgb(30, 90, 190), rgb(150, 30, 30)};
constexpr uint16_t kSkin = rgb(255, 205, 160);
constexpr uint16_t kBeard = rgb(226, 112, 40);
constexpr uint16_t kHorn = rgb(244, 238, 216);

bool g_shadowPass = false;

M5Canvas g_figure(&M5.Display);
constexpr int kFigureSprite = 34;
constexpr int kFigureBaseX = 17;
constexpr int kFigureBaseY = 27;
constexpr uint16_t kKeyColor = 0xF81F;
constexpr float kFallSec = 0.55f;
float g_fallT = 0.0f;

uint16_t C(uint16_t c) { return g_shadowPass ? kShadowColor : c; }

struct Pt {
    int x, y;
};

Pt xf(float px, float py, float c, float s, int cx, int cy) {
    return {cx + static_cast<int>(roundf(px * c - py * s)), cy + static_cast<int>(roundf(px * s + py * c))};
}

void thickLine(Pt a, Pt b, uint16_t color, int w) {
    int n = abs(b.x - a.x);
    if (abs(b.y - a.y) > n) n = abs(b.y - a.y);
    if (n == 0) n = 1;
    for (int i = 0; i <= n; ++i) {
        const int x = a.x + (b.x - a.x) * i / n;
        const int y = a.y + (b.y - a.y) * i / n;
        canvas.fillRect(x - w / 2, y - w / 2, w, w, C(color));
    }
}

uint32_t g_rng = 12345;
float rnd() {
    g_rng ^= g_rng << 13;
    g_rng ^= g_rng >> 17;
    g_rng ^= g_rng << 5;
    return (g_rng & 0xFFFFu) / 65535.0f;
}

// ---- particles (trails, sparks, smoke) ----

struct Particle {
    float x = 0, y = 0, vx = 0, vy = 0;
    float life = 0, maxLife = 1;
    uint16_t color = 0;
    uint8_t size = 1;
};

constexpr int kMaxParticles = 72;
Particle g_parts[kMaxParticles];

void spawn(float x, float y, float vx, float vy, float life, uint16_t color, uint8_t size) {
    Particle* slot = &g_parts[0];
    for (auto& p : g_parts) {
        if (p.life <= 0.0f) { slot = &p; break; }
        if (p.life < slot->life) slot = &p;
    }
    Particle& p = *slot;
    p.x = x;
    p.y = y;
    p.vx = vx;
    p.vy = vy;
    p.life = life;
    p.maxLife = life;
    p.color = color;
    p.size = size;
}

void updateParticles(float dt) {
    for (auto& p : g_parts) {
        if (p.life <= 0.0f) continue;
        p.life -= dt;
        p.x += p.vx * dt;
        p.y += p.vy * dt;
    }
}

void drawParticles() {
    for (const auto& p : g_parts) {
        if (p.life <= 0.0f) continue;
        int s = static_cast<int>(roundf(p.size * p.life / p.maxLife));
        if (s < 1) s = 1;
        canvas.fillRect(static_cast<int>(p.x) - s / 2, static_cast<int>(p.y) - s / 2, s, s, p.color);
    }
}

void burstSparks(float x, float y, int count, float speed, float life, const uint16_t* colors, int nColors) {
    for (int i = 0; i < count; ++i) {
        const float a = rnd() * 6.2831853f;
        const float v = speed * (0.4f + 0.6f * rnd());
        spawn(x, y, cosf(a) * v, sinf(a) * v, life * (0.6f + 0.4f * rnd()), colors[i % nColors], 2);
    }
}

// ---- item shapes (drawn in a local frame rotated by `angle`) ----

void drawKnife(int cx, int cy, float c, float s, float k) {
    auto P = [&](float x, float y) { return xf(x * k, y * k, c, s, cx, cy); };
    const Pt tip = P(7, 0), b1 = P(-1, -2.2f), b2 = P(-1, 2.2f);
    canvas.fillTriangle(tip.x, tip.y, b1.x, b1.y, b2.x, b2.y, C(kSteel));
    thickLine(P(-1, 0), P(5, 0), rgb(250, 252, 255), 1);
    thickLine(P(-2, -3.5f), P(-2, 3.5f), rgb(230, 190, 70), 2);
    thickLine(P(-7, 0), P(-3, 0), kWoodDark, 2);
    const Pt pm = P(-7.5f, 0);
    canvas.fillRect(pm.x - 1, pm.y - 1, 2, 2, C(rgb(230, 190, 70)));
}

void drawAxe(int cx, int cy, float c, float s, float k) {
    auto P = [&](float x, float y) { return xf(x * k, y * k, c, s, cx, cy); };
    thickLine(P(-7, 0), P(6, 0), kWood, 2);
    const Pt pm = P(-7, 0);
    canvas.fillRect(pm.x - 1, pm.y - 1, 2, 2, C(kSteelDark));
    for (int sgn = -1; sgn <= 1; sgn += 2) {
        const Pt a = P(1.5f, sgn * 1.0f), b = P(-1.0f, sgn * 6.0f), d = P(7.0f, sgn * 6.0f), e = P(4.5f, sgn * 1.0f);
        canvas.fillTriangle(a.x, a.y, b.x, b.y, e.x, e.y, C(kSteel));
        canvas.fillTriangle(b.x, b.y, d.x, d.y, e.x, e.y, C(kSteel));
        canvas.drawLine(b.x, b.y, d.x, d.y, C(kWhite));
        canvas.drawLine(a.x, a.y, b.x, b.y, C(kSteelDark));
        canvas.drawLine(e.x, e.y, d.x, d.y, C(kSteelDark));
    }
    thickLine(P(2, 0), P(4, 0), kSteelDark, 3);
}

void drawBoomerang(int cx, int cy, float c, float s, float k) {
    auto P = [&](float x, float y) { return xf(x * k, y * k, c, s, cx, cy); };
    constexpr int kSeg = 7;
    Pt pts[kSeg + 1];
    for (int i = 0; i <= kSeg; ++i) {
        const float t = static_cast<float>(i) / kSeg;
        const float u = 1.0f - t;
        pts[i] = P(u * u * -7.5f + 2 * u * t * 0.0f + t * t * 7.5f, u * u * -5.5f + 2 * u * t * 6.0f + t * t * -5.5f);
    }
    for (int i = 0; i < kSeg; ++i) thickLine(pts[i], pts[i + 1], rgb(110, 60, 20), 5);
    for (int i = 0; i < kSeg; ++i) thickLine(pts[i], pts[i + 1], rgb(244, 176, 44), 3);
    for (int i = 0; i < kSeg; ++i) {
        const Pt a = {pts[i].x, pts[i].y - 1}, b = {pts[i + 1].x, pts[i + 1].y - 1};
        thickLine(a, b, rgb(255, 232, 140), 1);
    }
}

void drawBomb(int cx, int cy, float k, bool lit) {
    const int r = static_cast<int>(roundf(5 * k));
    canvas.fillCircle(cx, cy + 1, r, C(lit ? rgb(150, 40, 36) : rgb(30, 30, 40)));
    canvas.fillCircle(cx, cy, r, C(lit ? rgb(238, 82, 60) : rgb(54, 54, 70)));
    canvas.fillRect(cx - r / 2 - 1, cy - r / 2, 2, 2, C(lit ? rgb(255, 200, 190) : rgb(150, 150, 180)));
    canvas.fillRect(cx - 1, cy - r - 1, 3, 2, C(rgb(120, 120, 134)));
    const int fx = cx + 2;
    const int fy = cy - r - 1;
    canvas.drawLine(fx, fy, fx + 2, fy - 2, C(rgb(210, 180, 120)));
    canvas.drawLine(fx + 2, fy - 2, fx + 1, fy - 4, C(rgb(210, 180, 120)));
    const bool flick = (millis() / 80) & 1;
    canvas.fillRect(fx, fy - 6, 3, 3, C(flick ? kYellow : rgb(255, 130, 40)));
    canvas.fillRect(fx + 1, fy - 5, 1, 1, C(kWhite));
}

void drawItemBody(ItemType type, int cx, int cy, float angle, float scale, bool lit) {
    const float c = cosf(angle);
    const float s = sinf(angle);
    switch (type) {
        case ItemType::Knife: drawKnife(cx, cy, c, s, scale); break;
        case ItemType::Axe: drawAxe(cx, cy, c, s, scale); break;
        case ItemType::Bomb: drawBomb(cx, cy, scale, lit); break;
        case ItemType::Boomerang: drawBoomerang(cx, cy, c, s, scale); break;
        default: break;
    }
}

void drawPowerup(const Powerup& p) {
    if (p.type == PowerupType::None) return;
    const int x = static_cast<int>(p.x), y = static_cast<int>(p.y);
    const uint16_t edge = p.type == PowerupType::Split ? rgb(230, 120, 255) :
                          p.type == PowerupType::Lightning ? kYellow : rgb(120, 235, 255);
    switch (p.type) {
        case PowerupType::Burger:
            ui::drawBurger(x, y);
            break;
        case PowerupType::Split:
            canvas.drawLine(x - 4, y, x + 2, y - 4, edge);
            canvas.drawLine(x - 4, y, x + 2, y + 4, edge);
            canvas.fillCircle(x - 4, y, 2, kWhite);
            canvas.fillCircle(x + 3, y - 4, 2, edge);
            canvas.fillCircle(x + 3, y + 4, 2, edge);
            break;
        case PowerupType::Lightning:
            canvas.fillTriangle(x, y - 6, x - 4, y + 1, x + 2, y, edge);
            canvas.fillTriangle(x - 2, y, x + 4, y - 1, x, y + 6, edge);
            break;
        case PowerupType::Ice:
            canvas.drawLine(x - 5, y, x + 5, y, edge);
            canvas.drawLine(x, y - 5, x, y + 5, edge);
            canvas.drawLine(x - 4, y - 4, x + 4, y + 4, edge);
            canvas.drawLine(x - 4, y + 4, x + 4, y - 4, edge);
            canvas.fillCircle(x, y, 2, kWhite);
            break;
        default: break;
    }
}

// ---- viking ----

void drawFigureBody(int x, int by, int dir, int pal, bool hit, bool blink, float armDeg) {
    uint16_t body = kBodyColor[pal];
    uint16_t shade = kBodyShade[pal];
    if (blink) {
        body = kWhite;
        shade = rgb(200, 200, 200);
    }
    canvas.fillRect(x - 3, by - 2, 2, 2, C(kWoodDark));
    canvas.fillRect(x + 1, by - 2, 2, 2, C(kWoodDark));
    canvas.fillRect(x - 4, by - 7, 8, 5, C(body));
    canvas.fillRect(x - 4, by - 4, 8, 1, C(shade));
    canvas.fillRect(x - 3, by - 10, 6, 3, C(kBeard));
    canvas.fillRect(x - 3, by - 12, 6, 2, C(kSkin));
    canvas.fillRect(x - 3, by - 15, 6, 3, C(kSteel));
    canvas.fillRect(x - 3, by - 13, 6, 1, C(kSteelDark));
    canvas.fillRect(x - 5, by - 15, 2, 2, C(kHorn));
    canvas.fillRect(x - 6, by - 17, 1, 2, C(kHorn));
    canvas.fillRect(x + 3, by - 15, 2, 2, C(kHorn));
    canvas.fillRect(x + 5, by - 17, 1, 2, C(kHorn));

    if (!g_shadowPass) {
        const int e1 = x;
        const int e2 = x + dir * 2;
        if (hit) {
            for (int e : {e1, e2}) {
                canvas.drawLine(e, by - 12, e + 1, by - 11, rgb(200, 30, 30));
                canvas.drawLine(e + 1, by - 12, e, by - 11, rgb(200, 30, 30));
            }
        } else {
            canvas.fillRect(e1, by - 12, 1, 2, rgb(30, 24, 30));
            canvas.fillRect(e2, by - 12, 1, 2, rgb(30, 24, 30));
        }
    }

    const float a = armDeg * 3.14159265f / 180.0f;
    const int sx = x + dir * 3;
    const int sy = by - 6;
    const int hx = sx + static_cast<int>(roundf(dir * cosf(a) * 6));
    const int hy = sy - static_cast<int>(roundf(sinf(a) * 6));
    thickLine({sx, sy}, {hx, hy}, kSkin, 2);
}

// ---- scene pieces ----

void drawLava() {
    const int ly = static_cast<int>(cfg::LAVA_Y);
    const float t = millis() / 1000.0f;
    canvas.fillRect(0, ly, cfg::SCREEN_W, cfg::HUD_Y - ly, rgb(190, 42, 30));
    canvas.fillRect(0, cfg::HUD_Y - 3, cfg::SCREEN_W, 3, rgb(140, 28, 24));
    const int scroll = static_cast<int>(t * 3.0f);
    for (int x = 0; x < cfg::SCREEN_W; x += 4) {
        const int tri = abs(((x / 4 + scroll) % 6) - 3);
        const int top = ly - 3 + tri;
        canvas.fillRect(x, top, 4, 2, rgb(255, 216, 60));
        canvas.fillRect(x, top + 2, 4, ly + 2 - top, rgb(244, 128, 30));
    }
    for (int i = 0; i < 10; ++i) {
        const float px = fmodf(i * 47.0f + t * 5.0f, static_cast<float>(cfg::SCREEN_W));
        const float py = ly - fmodf(t * 10.0f + i * 11.0f, 30.0f);
        canvas.fillRect(static_cast<int>(px), static_cast<int>(py), (i & 1) + 1, (i & 1) + 1,
                        (i & 2) ? rgb(255, 216, 60) : rgb(244, 128, 30));
    }
}

void drawBlast(const Projectile& p) {
    const int x = static_cast<int>(p.x);
    const int y = static_cast<int>(p.y);
    const float r = Game::blastRadius(p);
    const float progress = 1.0f - p.blastT / cfg::BOMB_BLAST_SEC;
    canvas.fillCircle(x, y, static_cast<int>(r), rgb(214, 56, 30));
    canvas.fillCircle(x, y, static_cast<int>(r * 0.8f), rgb(255, 136, 40));
    canvas.fillCircle(x, y, static_cast<int>(r * 0.55f), rgb(255, 210, 80));
    canvas.fillCircle(x, y, static_cast<int>(r * 0.3f * (1.0f - progress)), rgb(255, 250, 220));
    for (int k = 0; k < 8; ++k) {
        const float a = k * 0.7854f + progress * 2.0f;
        canvas.fillRect(x + static_cast<int>(cosf(a) * r * 0.95f) - 1, y + static_cast<int>(sinf(a) * r * 0.95f) - 1, 2, 2,
                        (k & 1) ? kYellow : rgb(255, 150, 50));
    }
}

void drawAimGuide(const Game& g, int local) {
    const Player& p = g.players[local];
    if (p.item == ItemType::None || p.itemUsed) return;
    const float a = p.aimDeg * 3.14159265f / 180.0f;
    const float hx = Game::handX(p);
    const float hy = Game::handY(p);
    for (int i = 1; i <= 6; ++i) {
        const float d = 6.0f + i * 5.0f;
        const int px = static_cast<int>(hx + p.dir * cosf(a) * d);
        const int py = static_cast<int>(hy - sinf(a) * d);
        canvas.fillRect(px - 1, py - 1, 2, 2, i == 6 ? kWhite : kYellow);
    }
}

void drawPowerBar(const Player& p) {
    constexpr int x = 26, y = 123, w = 104, h = 7;
    canvas.fillRect(x - 2, y - 2, w + 4, h + 4, rgb(24, 20, 26));
    canvas.drawRect(x - 2, y - 2, w + 4, h + 4, rgb(220, 220, 230));
    if (!p.charging) return;
    const int fill = static_cast<int>(w * p.power);
    for (int px = 0; px < fill; px += 4) {
        const float f = static_cast<float>(px) / w;
        const uint16_t col = f < 0.5f ? rgb(static_cast<uint8_t>(60 + f * 2 * 195), 220, 60)
                                      : rgb(255, static_cast<uint8_t>(220 - (f - 0.5f) * 2 * 170), 40);
        canvas.fillRect(x + px, y, min(4, fill - px), h, col);
    }
    canvas.fillRect(x + fill - 1, y - 3, 2, h + 6, kWhite);
}

void drawHud(const Game& g, int local) {
    drawHudStrip();
    const Player& me = g.players[local];

    canvas.fillRect(3, 119, 16, 15, rgb(28, 30, 36));
    canvas.drawRect(3, 119, 16, 15, me.itemUsed ? rgb(90, 90, 100) : kYellow);
    if (me.item != ItemType::None && !me.itemUsed) {
        drawItemBody(me.item, 11, 127, -0.7f, 0.85f, false);
    } else if (me.itemUsed) {
        canvas.fillRect(7, 126, 8, 2, rgb(110, 110, 120));
    }
    drawPowerBar(me);

    canvas.setTextDatum(middle_center);
    canvas.setTextSize(2);
    char buf[8];
    if (g.mode == GameMode::Versus) {
        snprintf(buf, sizeof(buf), "%d", g.players[local].score);
        canvas.setTextColor(kBodyColor[local]);
        canvas.drawString(buf, 160, 127);
        snprintf(buf, sizeof(buf), "%d", g.players[1 - local].score);
        canvas.setTextColor(kBodyColor[1 - local]);
        canvas.drawString(buf, 205, 127);
    } else {
        // Solo: hits on the dummy
        snprintf(buf, sizeof(buf), "%d", g.players[0].score);
        canvas.setTextColor(kBodyColor[0]);
        canvas.drawString(buf, 160, 127);
        canvas.setTextSize(1);
        canvas.setTextColor(rgb(190, 192, 200));
        canvas.drawString("HITS", 205, 127);
    }
    canvas.setTextSize(1);
}

// Versus only: in the last seconds of the turn timeout the player who has not thrown yet gets a shrinking bar.
void drawTurnBar(const Game& g, int local) {
    if (g.mode != GameMode::Versus || g.phase != Phase::Playing || g.players[local].itemUsed) return;
    const float left = g.turnLeft();
    if (left > cfg::TURN_WARN_SEC) return;
    const int w = static_cast<int>(cfg::SCREEN_W * left / cfg::TURN_WARN_SEC);
    const bool urgent = left < 2.0f && (static_cast<int>(left * 6.0f) & 1);
    canvas.fillRect(0, 1, cfg::SCREEN_W, 4, rgb(24, 20, 26));
    canvas.fillRect(0, 2, w, 2, urgent ? kWhite : rgb(255, 90, 60));
}

void drawBanner(const Game& g, int local) {
    const bool meHit = g.players[local].hitFlash > 1.0f - ui::kHitNoticeSeconds;
    const bool oppHit = g.players[1 - local].hitFlash > 1.0f - ui::kHitNoticeSeconds;
    if (g.phase != Phase::MatchEnd && (meHit || oppHit)) {
        if (meHit) ui::hitNotice(true, cfg::SCREEN_W / 2, 40, 3);
        if (oppHit) ui::hitNotice(false, cfg::SCREEN_W / 2, meHit ? 64 : 40, 3);
    } else if (g.phase == Phase::MatchEnd) {
        ui::matchResult(g.isDraw() ? -1 : g.winner, local, cfg::SCREEN_W / 2, 40, 3);
    } else if (g.phase == Phase::RoundEnd && g.mode == GameMode::Solo && !g.players[1].hit) {
        ui::shadowText("MISS", cfg::SCREEN_W / 2, 40, 3, rgb(190, 192, 200));
    }
}

uint8_t g_prevHit[2] = {};
bool g_prevBlast[cfg::MAX_PROJ] = {};
uint32_t g_lastMs = 0;
uint32_t g_frame = 0;

void spawnEffects(const Game& g, float dt) {
    ++g_frame;
    for (int i = 0; i < 2; ++i) {
        if (g.players[i].hit && g.players[i].hitSerial != g_prevHit[i]) {
            const uint16_t cols[] = {kWhite, kYellow, rgb(255, 90, 60)};
            burstSparks(g.players[i].x, g.players[i].feetY - 8, 12, 90.0f, 0.45f, cols, 3);
        }
        g_prevHit[i] = g.players[i].hitSerial;
    }
    for (int i = 0; i < cfg::MAX_PROJ; ++i) {
        const Projectile& p = g.proj[i];
        const bool blasting = p.active && p.exploding;
        if (blasting && !g_prevBlast[i]) {
            const uint16_t cols[] = {kYellow, rgb(255, 140, 40), rgb(255, 250, 220)};
            burstSparks(p.x, p.y, 18, 110.0f, 0.6f, cols, 3);
            for (int k = 0; k < 6; ++k) {
                spawn(p.x + (rnd() - 0.5f) * 16, p.y + (rnd() - 0.5f) * 10, (rnd() - 0.5f) * 20, -10 - rnd() * 14, 0.9f,
                      rgb(120, 118, 124), 4);
            }
        }
        g_prevBlast[i] = blasting;

        if (!p.active || p.exploding || (g_frame & 1)) continue;
        if (p.speedScale != 1.0f) {
            spawn(p.x, p.y, 0, 0, 0.25f,
                  p.speedScale > 1.0f ? kYellow : rgb(120, 235, 255), 2);
        }
        switch (p.type) {
            case ItemType::Axe: spawn(p.x, p.y, 0, 0, 0.32f, rgb(150, 154, 162), 3); break;
            case ItemType::Boomerang: spawn(p.x, p.y, 0, 0, 0.3f, rgb(232, 212, 150), 3); break;
            case ItemType::Knife: spawn(p.x - p.vx * 0.01f, p.y - p.vy * 0.01f, 0, 0, 0.14f, rgb(235, 240, 250), 2); break;
            case ItemType::Bomb:
                if (p.fuseOn) spawn(p.x + 3, p.y - 7, (rnd() - 0.5f) * 20, -14.0f, 0.3f, rgb(255, 170, 50), 2);
                break;
            default: break;
        }
    }
    updateParticles(dt);
}

}  // namespace

bool begin() {
    g_figure.setColorDepth(16);
    return g_figure.createSprite(kFigureSprite, kFigureSprite) != nullptr && (g_figure.setPivot(kFigureBaseX, kFigureBaseY), true);
}

void drawBackground() {
    const int ly = static_cast<int>(cfg::LAVA_Y);
    constexpr int kBands = 9;
    const int bandH = ly / kBands + 1;
    for (int i = 0; i < kBands; ++i) {
        const float t = static_cast<float>(i) / (kBands - 1);
        canvas.fillRect(0, i * bandH, cfg::SCREEN_W, bandH,
                        rgb(static_cast<uint8_t>(52 + 34 * t), static_cast<uint8_t>(58 + 30 * t),
                            static_cast<uint8_t>(62 + 6 * t)));
    }
    drawLava();
    canvas.fillRect(0, 0, 2, cfg::HUD_Y, kWall);
    canvas.fillRect(cfg::SCREEN_W - 2, 0, 2, cfg::HUD_Y, kWall);
    canvas.fillRect(0, 0, cfg::SCREEN_W, 1, kWall);
}

void drawHudStrip() {
    canvas.fillRect(0, cfg::HUD_Y, cfg::SCREEN_W, cfg::SCREEN_H - cfg::HUD_Y, rgb(48, 50, 56));
    canvas.fillRect(0, cfg::HUD_Y, cfg::SCREEN_W, 1, kWall);
}

void drawPlatform(int x, int y) {
    const int hw = static_cast<int>(cfg::PLATFORM_HALF_W);
    const int h = static_cast<int>(cfg::PLATFORM_THICK);
    canvas.fillRect(x - hw + 3, y + 3, hw * 2, h, kShadowColor);
    canvas.fillRect(x - hw, y, hw * 2, h, rgb(138, 86, 46));
    canvas.fillRect(x - hw + 1, y + 1, hw * 2 - 2, h - 2, rgb(226, 178, 112));
    canvas.fillRect(x - hw + 1, y + 1, hw * 2 - 2, 1, rgb(248, 214, 154));
    canvas.fillRect(x - hw + 1, y + h - 2, hw * 2 - 2, 1, rgb(190, 140, 84));
}

void drawObstacle(int x, int y, int r) {
    canvas.fillCircle(x + 2, y + 2, r, kShadowColor);
    canvas.fillCircle(x, y, r, rgb(188, 192, 204));
    canvas.drawCircle(x, y, r, rgb(100, 104, 118));
    canvas.fillRect(x - r / 2, y - r / 2, 2, 2, kWhite);
}

void drawItem(ItemType type, int cx, int cy, float angle, float scale, bool fuseLit) {
    g_shadowPass = true;
    drawItemBody(type, cx + 2, cy + 2, angle, scale, fuseLit);
    g_shadowPass = false;
    drawItemBody(type, cx, cy, angle, scale, fuseLit);
}

void drawFigure(int x, int baseY, int dir, int paletteIdx, bool hit, bool blink, float armDeg) {
    g_shadowPass = true;
    drawFigureBody(x + 2, baseY + 2, dir, paletteIdx, hit, blink, armDeg);
    g_shadowPass = false;
    drawFigureBody(x, baseY, dir, paletteIdx, hit, blink, armDeg);
}

// The loser of the match tips over backwards (away from the opponent) and stays lying on the platform.
void drawFallenFigure(int x, int baseY, int dir, int faceDir, int paletteIdx, float progress) {
    const float ease = 1.0f - (1.0f - progress) * (1.0f - progress);
    g_figure.fillScreen(kKeyColor);
    g_override = &g_figure;
    drawFigureBody(kFigureBaseX, kFigureBaseY, faceDir, paletteIdx, true, false, -20.0f);
    g_override = nullptr;
    g_figure.pushRotateZoom(&canvas, static_cast<float>(x), baseY - 4.0f * ease, -90.0f * ease * dir, 1.0f, 1.0f,
                            kKeyColor);
}

void drawGame(const Game& g, int local) {
    const uint32_t now = millis();
    float dt = (now - g_lastMs) / 1000.0f;
    g_lastMs = now;
    if (dt > 0.05f) dt = 0.05f;
    spawnEffects(g, dt);

    const int loser = g.matchLoser();
    if (loser < 0 && !g.isDraw()) g_fallT = 0.0f;
    else g_fallT += dt;

    drawBackground();

    if (g.world.powerups) for (const auto& p : g.powerups) drawPowerup(p);

    if (g.world.obstacles) {
        for (int i = 0; i < cfg::OBSTACLE_COUNT; ++i) {
            drawObstacle(static_cast<int>(g.obstacleXs[i]), static_cast<int>(g.obstacleY(i)),
                         static_cast<int>(cfg::OBSTACLE_R));
        }
    }

    for (int i = 0; i < 2; ++i) {
        const Player& p = g.players[i];
        drawPlatform(static_cast<int>(p.x), static_cast<int>(g.platformY(i)));
        const bool blink = p.hit && p.hitFlash > 0.0f && (static_cast<int>(p.hitFlash * 14.0f) & 1);
        // The arm angle is drawn relative to the way the viking is facing (it turns around past 90 degrees).
        const float arm = (i != local) ? -20.0f : (p.face == p.dir ? p.aimDeg : 180.0f - p.aimDeg);
        if (i == loser || g.isDraw()) {
            const float progress = g_fallT >= kFallSec ? 1.0f : g_fallT / kFallSec;
            drawFallenFigure(static_cast<int>(p.x), static_cast<int>(p.feetY), p.dir, p.face, i, progress);
        } else {
            drawFigure(static_cast<int>(p.x), static_cast<int>(p.feetY), p.face, i, p.hit, blink, arm);
        }
    }

    drawParticles();
    drawAimGuide(g, local);
    for (const auto& p : g.proj) {
        if (!p.active) continue;
        if (p.exploding) {
            drawBlast(p);
        } else {
            const bool lit = p.fuseOn && (static_cast<int>(p.fuse * (p.fuse < 0.7f ? 12.0f : 6.0f)) & 1);
            drawItem(p.type, static_cast<int>(p.x), static_cast<int>(p.y), p.rot, p.scale, lit);
        }
    }
    drawHud(g, local);
    drawBanner(g, local);
    drawTurnBar(g, local);
}

}  // namespace render
