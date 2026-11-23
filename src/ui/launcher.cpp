#include "ui/launcher.h"

#include <M5Unified.h>
#include <math.h>

#include "core/debug.h"
#include "core/display.h"
#include "core/input.h"
#include "core/joystick.h"
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
constexpr int kGroupGap = 26;          // extra space where the joystick games start or end
constexpr float kSlideTau = 0.055f;    // seconds; the slide settles in about 180 ms
constexpr uint32_t kToastMs = 1600;
constexpr uint32_t kCheckMs = 700;     // how long a freshly powered joystick gets to answer before it counts as missing

int g_sel = 0;
float g_pos = 0.0f;  // card index currently at the centre, fractional while sliding
uint32_t g_toastUntil = 0;
uint32_t g_stickPoweredMs = 0;

int cardCount() { return registry::count() + 1; }  // the games plus SETTINGS

int wrap(int i) {
    const int n = cardCount();
    return ((i % n) + n) % n;
}

// Games that need the joystick form their own group, set apart from the rest.
bool needsStick(int index) { return index < registry::count() && registry::get(index).info->needsJoystick; }

// Distance from the card at `index` to the next one: wider where the joystick group begins or ends.
int pitchAfter(int index) { return kPitch + (needsStick(wrap(index)) != needsStick(wrap(index + 1)) ? kGroupGap : 0); }

const uint16_t kStickColor = rgb(255, 150, 40);

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

// Orange strip across the bottom of a joystick game's card. On the selected card it also says whether the unit is
// plugged in.
void drawStickRibbon(int x, bool selected, uint32_t now) {
    auto& c = display::canvas();
    const int y = kCardY + kCardH - 12;
    uint16_t color = kStickColor;
    const char* text = "JOYSTICK REQUIRED";
    if (selected) {
        if (joystick::ok()) {
            color = rgb(70, 190, 90);
            text = "JOYSTICK READY";
        } else if (now - g_stickPoweredMs < kCheckMs) {
            text = "CHECKING JOYSTICK";
        } else {
            color = rgb(225, 60, 50);
            text = "NO JOYSTICK FOUND";
        }
    }
    c.fillRect(x, y, kCardW, 12, color);
    c.drawFastHLine(x, y, kCardW, ui::kBlack);
    c.setTextSize(1);
    c.setTextDatum(middle_center);
    c.setTextColor(ui::kBlack);
    c.drawString(text, x + kCardW / 2, y + 7);
}

// Dashed orange line with a small joystick badge in the gap between two groups.
void drawGroupDivider(int centreX) {
    auto& c = display::canvas();
    for (int y = kCardY - 8; y < kCardY + kCardH + 8; y += 6) c.fillRect(centreX - 1, y, 2, 3, kStickColor);
    const int cy = kCardY + kCardH / 2;
    c.fillRoundRect(centreX - 9, cy - 11, 18, 22, 4, rgb(36, 30, 26));
    c.drawRoundRect(centreX - 9, cy - 11, 18, 22, 4, kStickColor);
    c.fillRect(centreX - 1, cy - 5, 3, 8, rgb(190, 192, 205));      // stick shaft
    c.fillCircle(centreX, cy - 6, 4, kStickColor);                   // stick ball
    c.fillRoundRect(centreX - 6, cy + 3, 13, 5, 2, rgb(120, 124, 140));  // base
}

const char* cardName(int index) { return index < registry::count() ? registry::get(index).name : "SETTINGS"; }

}  // namespace

void begin() {
    if (g_sel >= cardCount()) g_sel = 0;
    g_pos = static_cast<float>(g_sel);
    g_toastUntil = 0;
}

Result update(float dt) {
    const int n = cardCount();
    const uint32_t now = millis();
    if (input::bPressed() || debug::bPress()) {
        g_sel = (g_sel + 1) % n;
        sound::playUi(sound::Ui::Move);
    }

    // The joystick only gets power while its games are selected.
    const bool wantStick = needsStick(g_sel);
    if (wantStick != joystick::powered()) {
        joystick::setPower(wantStick);
        g_stickPoweredMs = now;
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
        if (wantStick && !joystick::ok()) {
            g_toastUntil = now + kToastMs;
            sound::playUi(sound::Ui::Denied);
            return Result::None;
        }
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

    // Cards are placed one after the other so the gap before and after the joystick group can be wider.
    const int base = static_cast<int>(floorf(g_pos));
    const float frac = g_pos - base;
    int xs[4];
    xs[1] = kCardX - static_cast<int>(frac * pitchAfter(base));
    xs[0] = xs[1] - pitchAfter(base - 1);
    xs[2] = xs[1] + pitchAfter(base);
    xs[3] = xs[2] + pitchAfter(base + 1);
    for (int k = -1; k <= 2; ++k) {
        const int idx = wrap(base + k);
        const int x = xs[k + 1];
        if (x >= platform::kScreenW || x + kCardW <= 0) continue;
        const bool centre = abs(x - kCardX) < kPitch / 2;
        drawCard(idx, x, ms);
        if (needsStick(idx)) drawStickRibbon(x, idx == g_sel && centre, ms);
        if (centre) {
            const uint16_t frame = needsStick(idx) ? kStickColor : ui::yellow();
            c.drawRect(x - 2, kCardY - 2, kCardW + 4, kCardH + 4, frame);
            c.drawRect(x - 1, kCardY - 1, kCardW + 2, kCardH + 2, frame);
        } else {
            ui::dim(x, kCardY, kCardW, kCardH);
            c.drawRect(x - 1, kCardY - 1, kCardW + 2, kCardH + 2, needsStick(idx) ? rgb(150, 90, 30) : rgb(70, 74, 96));
        }
    }
    for (int k = -1; k <= 1; ++k) {
        if (needsStick(wrap(base + k)) == needsStick(wrap(base + k + 1))) continue;
        const int left = xs[k + 1] + kCardW, right = xs[k + 2];
        if (right < 0 || left > platform::kScreenW) continue;
        drawGroupDivider((left + right) / 2);
    }

    ui::shadowText(cardName(g_sel), platform::kScreenW / 2, 101, 2, needsStick(g_sel) ? kStickColor : ui::kWhite);

    const int n = cardCount();
    for (int i = 0; i < n; ++i) {
        const int dx = platform::kScreenW / 2 + static_cast<int>((i - (n - 1) / 2.0f) * 10);
        if (i == g_sel) c.fillRect(dx - 3, 113, 6, 4, needsStick(i) ? kStickColor : ui::yellow());
        else c.fillRect(dx - 2, 114, 4, 3, needsStick(i) ? rgb(150, 90, 30) : rgb(90, 94, 120));
    }

    if (ms < g_toastUntil) {
        c.fillRoundRect(30, 38, 180, 44, 6, rgb(24, 20, 26));
        c.drawRoundRect(30, 38, 180, 44, 6, ui::red());
        ui::shadowText("NO JOYSTICK", platform::kScreenW / 2, 54, 2, ui::red());
        ui::shadowText("PLUG IT INTO THE GROVE PORT", platform::kScreenW / 2, 72, 1, ui::kWhite);
    }

    ui::hintBar("NEXT", "OK");
}

}  // namespace launcher
