#include "ui/title.h"

#include "core/display.h"
#include "core/platform.h"
#include "core/sound.h"
#include "ui/gfx.h"
#include "ui/title_art.h"

namespace title {
namespace {

using platform::rgb;
constexpr auto kGold = rgb(255, 185, 64);
constexpr auto kCream = rgb(255, 238, 162);
constexpr auto kAmber = rgb(217, 102, 41);
constexpr auto kRim = rgb(119, 53, 38);
constexpr uint32_t kDropMs = 240;
constexpr uint32_t kEntryMs = 440;
uint32_t idleStart;
uint32_t entryStart;
bool entering;

void coin(int x, int y, unsigned phase) {
    auto& c = display::canvas();
    constexpr int widths[] = {4, 3, 2, 1, 2, 3};
    const int r = widths[phase % 6];
    c.fillRect(x - r, y - 3, 2 * r + 1, 7, kRim);
    c.fillRect(x - r + 1, y - 5, 2 * r - 1, 10, kAmber);
    c.fillRect(x - r, y - 2, 2 * r, 5, kGold);
    c.fillRect(x - r + 1, y - 4, 2 * r - 1, 7, kGold);
    c.drawFastHLine(x - r + 1, y - 4, 2 * r - 1, kCream);
    c.drawFastVLine(x - r, y - 2, 4, kCream);
    if (r >= 3) {
        c.drawFastVLine(x + 1, y - 2, 5, kAmber);
        c.drawFastVLine(x, y - 2, 4, kCream);
    }
}

void idleCoin(uint32_t ms) {
    constexpr int cycle = 1500;
    const int t = ms % cycle;
    if (t >= 880) return;
    const bool right = (ms / cycle) % 2 != 0;
    int x = 46;
    int y;
    if (t < 600) {
        y = -6 + 96 * t * t / (600 * 600);
        x += 4 * t / 600;
    } else {
        const int bounce = t - 600;
        y = 90 - 4 * 13 * bounce * (280 - bounce) / (280 * 280);
        x += 4 + 6 * bounce / 280;
    }
    coin(right ? 239 - x : x, y, ms / 65);
}

void sideScreens(uint32_t ms) {
    auto& c = display::canvas();
    // A tiny samurai on the bridge and a two-frame crescent inside the left CRT.
    constexpr auto armor = rgb(128, 144, 148);
    c.fillRect(10, 57, 4, 4, armor);
    c.drawFastHLine(9, 58, 6, rgb(214, 160, 111));
    c.fillTriangle(11, 60, 7, 64, 14, 64, armor);
    c.drawLine(14, 62, 19, 57, rgb(183, 196, 162));
    const unsigned slash = ms % 2100;
    if (slash >= 320 && slash < 640) {
        const int x = 17 + (slash - 320) / 64;
        c.drawLine(x - 1, 54, x + 3, 56, armor);
        c.drawLine(x + 3, 56, x + 4, 59, kCream);
        c.drawLine(x + 4, 59, x, 63, armor);
    }

    // Two top-down sumo fighters close the gap and rebound on the right CRT.
    const int beat = ms % 1600;
    const int advance = beat < 320 ? beat / 64 : beat < 720 ? (720 - beat) / 80 : 0;
    for (int side = 0; side < 2; ++side) {
        const int dir = side == 0 ? 1 : -1;
        const int x = side == 0 ? 216 + advance : 234 - advance;
        c.fillCircle(x, 56, 4, side == 0 ? rgb(91, 137, 160) : rgb(191, 90, 77));
        c.fillRect(x + dir - 1, 54, 3, 5, rgb(229, 170, 125));
        c.drawPixel(x + 2 * dir, 55, rgb(48, 32, 53));
    }
}

}  // namespace

void begin(uint32_t now) {
    idleStart = now;
    entering = false;
}

bool update(uint32_t now, bool pressed) {
    if (pressed && !entering) {
        entryStart = now;
        entering = true;
        sound::playUi(sound::Ui::Select);
    }
    return entering && now - entryStart >= kEntryMs;
}

void draw(uint32_t now) {
    const uint32_t idle = now - idleStart;
    auto& c = display::canvas();
    ui::drawBitmap(assets::titleCabinet, 0, 0);
    sideScreens(idle);
    ui::shadowText("PRESS ANY KEY", 120, 76, 1, kCream);
    if (!entering) {
        const auto glint = (idle / 480) % 2 ? kGold : kAmber;
        c.fillTriangle(73, 74, 73, 78, 76, 76, glint);
        c.fillTriangle(167, 74, 167, 78, 164, 76, glint);
        idleCoin(idle);
        return;
    }

    const uint32_t t = now - entryStart;
    if (t < kDropMs) {
        const int y = 85 + static_cast<int>(36 * t * t / (kDropMs * kDropMs));
        c.setClipRect(111, 79, 18, 37);  // lower half disappears into the slot
        coin(120, y, t / 40);
        c.clearClipRect();
    } else {
        c.drawRect(107, 112, 26, 7, kGold);
        c.drawFastHLine(110, 114, 20, kCream);
        c.drawFastHLine(102, 115, 3, kGold);
        c.drawFastHLine(135, 115, 3, kGold);
    }
}

}  // namespace title
