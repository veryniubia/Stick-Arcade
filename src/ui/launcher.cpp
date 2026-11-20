#include "ui/launcher.h"

#include <M5Unified.h>
#include <math.h>

#include "core/debug.h"
#include "core/display.h"
#include "core/input.h"
#include "core/platform.h"
#include "core/sound.h"
#include "games/registry.h"
#include "ui/gfx.h"

namespace launcher {

namespace {

using platform::rgb;

constexpr int kCardW = 120;
constexpr int kCardH = 78;
constexpr int kCardY = 14;
constexpr int kCardX = (platform::kScreenW - kCardW) / 2;
constexpr int kPitch = 132;            // distance between card centres: a 12 px gap
constexpr float kSlideTau = 0.055f;    // seconds; the slide settles in about 180 ms

int g_sel = 0;
float g_pos = 0.0f;  // card index currently at the centre, fractional while sliding

int cardCount() { return registry::count() + 1; }  // the games plus SETTINGS

int wrap(int i) {
    const int n = cardCount();
    return ((i % n) + n) % n;
}

// Gear for the SETTINGS card.
void drawSettingsCard(int x, int y, int w, int h, uint32_t ms) {
    auto& c = display::canvas();
    c.setClipRect(x, y, w, h);
    for (int i = 0; i < 6; ++i) {
        const int bandH = h / 6 + 1;
        c.fillRect(x, y + i * bandH, w, bandH, rgb(static_cast<uint8_t>(40 + i * 4), static_cast<uint8_t>(44 + i * 4), static_cast<uint8_t>(64 + i * 5)));
    }
    const int cx = x + w / 2;
    const int cy = y + h / 2;
    const float a0 = ms / 1800.0f;
    for (int k = 0; k < 8; ++k) {
        const float a = a0 + k * 0.7853982f;
        const int tx = cx + static_cast<int>(cosf(a) * 24.0f);
        const int ty = cy + static_cast<int>(sinf(a) * 24.0f);
        c.fillCircle(tx, ty, 5, rgb(150, 156, 176));
    }
    c.fillCircle(cx, cy, 21, rgb(170, 176, 196));
    c.drawCircle(cx, cy, 21, rgb(110, 116, 140));
    c.fillCircle(cx, cy, 8, rgb(52, 56, 80));
    c.fillCircle(cx - 8, cy - 9, 3, rgb(230, 234, 248));
    c.clearClipRect();
}

void drawCard(int index, int x, uint32_t ms) {
    if (index < registry::count()) registry::get(index).drawCard(x, kCardY, kCardW, kCardH, ms);
    else drawSettingsCard(x, kCardY, kCardW, kCardH, ms);
}

const char* cardName(int index) { return index < registry::count() ? registry::get(index).name : "SETTINGS"; }

}  // namespace

void begin() {
    if (g_sel >= cardCount()) g_sel = 0;
    g_pos = static_cast<float>(g_sel);
}

Result update(float dt) {
    const int n = cardCount();
    if (input::bPressed() || debug::bPress()) {
        g_sel = (g_sel + 1) % n;
        sound::playUi(sound::Ui::Move);
    }

    // Slide along the shortest way round, so the step from the last card back to the first is one card, not n.
    float delta = g_sel - g_pos;
    if (delta > n / 2.0f) delta -= n;
    if (delta < -n / 2.0f) delta += n;
    if (fabsf(delta) < 0.005f) {
        g_pos = static_cast<float>(g_sel);
    } else {
        g_pos += delta * (1.0f - expf(-dt / kSlideTau));
        if (g_pos < 0.0f) g_pos += n;
        if (g_pos >= n) g_pos -= n;
    }

    if (input::aPressed() || debug::aPress()) {
        sound::playUi(sound::Ui::Select);
        return g_sel < registry::count() ? Result::Game : Result::Settings;
    }
    return Result::None;
}

int selectedGame() { return g_sel; }

void draw() {
    auto& c = display::canvas();
    const uint32_t ms = millis();
    c.fillRect(0, 0, platform::kScreenW, platform::kHintY, rgb(20, 22, 36));
    for (int y = 0; y < platform::kHintY; y += 4) c.fillRect(0, y, platform::kScreenW, 1, rgb(24, 26, 42));

    ui::shadowText(platform::kPlatformName, platform::kScreenW / 2, 6, 1, ui::yellow());

    const int base = static_cast<int>(floorf(g_pos));
    const float frac = g_pos - base;
    for (int k = -1; k <= 2; ++k) {
        const int idx = wrap(base + k);
        const int x = kCardX + static_cast<int>((k - frac) * kPitch);
        if (x >= platform::kScreenW || x + kCardW <= 0) continue;
        const bool centre = fabsf(x - kCardX) < kPitch / 2;
        drawCard(idx, x, ms);
        if (centre) {
            c.drawRect(x - 2, kCardY - 2, kCardW + 4, kCardH + 4, ui::yellow());
            c.drawRect(x - 1, kCardY - 1, kCardW + 2, kCardH + 2, ui::yellow());
        } else {
            ui::dim(x, kCardY, kCardW, kCardH);
            c.drawRect(x - 1, kCardY - 1, kCardW + 2, kCardH + 2, rgb(70, 74, 96));
        }
    }

    ui::shadowText(cardName(g_sel), platform::kScreenW / 2, 101, 2, ui::kWhite);

    const int n = cardCount();
    for (int i = 0; i < n; ++i) {
        const int dx = platform::kScreenW / 2 + static_cast<int>((i - (n - 1) / 2.0f) * 10);
        if (i == g_sel) c.fillRect(dx - 3, 113, 6, 4, ui::yellow());
        else c.fillRect(dx - 2, 114, 4, 3, rgb(90, 94, 120));
    }

    ui::hintBar("NEXT", "OK");
}

}  // namespace launcher
