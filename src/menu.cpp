#include "menu.h"

#include <M5Unified.h>
#include <math.h>

#include "input.h"
#include "render.h"
#include "sound.h"

namespace menu {

namespace {

enum Entry { kTraining, kVersus, kSound, kEntryCount };

int g_sel = 0;

}  // namespace

void begin() {
    g_sel = 0;
}

Choice update() {
    if (input::bPressed()) {
        g_sel = (g_sel + 1) % kEntryCount;
        sound::play(sound::Sfx::Move);
    }
    if (!input::aPressed()) return Choice::None;

    switch (g_sel) {
        case kTraining:
            sound::play(sound::Sfx::Select);
            return Choice::Training;
        case kVersus:
            sound::play(sound::Sfx::Select);
            return Choice::Versus;
        case kSound:
            sound::setEnabled(!sound::enabled());
            sound::play(sound::Sfx::Select);
            break;
    }
    return Choice::None;
}

void draw() {
    auto& c = render::canvas;
    render::drawBackground();

    render::drawPlatform(24, 92);
    render::drawPlatform(216, 92);
    render::drawFigure(24, 92, 1, 0, false, false, 20.0f);
    render::drawFigure(216, 92, -1, 1, false, false, -20.0f);

    c.setTextDatum(top_center);
    c.setTextSize(2);
    c.setTextColor(render::rgb(0, 0, 0));
    c.drawString("STICK DUEL", cfg::SCREEN_W / 2 + 2, 6);
    c.setTextColor(render::rgb(255, 220, 60));
    c.drawString("STICK DUEL", cfg::SCREEN_W / 2, 4);

    const char* labels[kEntryCount] = {"TRAINING", "VERSUS", sound::enabled() ? "SOUND ON" : "SOUND OFF"};

    c.setTextSize(2);
    for (int i = 0; i < kEntryCount; ++i) {
        const int y = 28 + i * 20;
        const bool sel = i == g_sel;
        if (sel) {
            c.fillRoundRect(62, y - 2, 118, 19, 4, render::rgb(40, 44, 70));
            c.fillTriangle(50, y + 1, 50, y + 13, 58, y + 7, render::rgb(255, 220, 60));
        }
        c.setTextDatum(top_left);
        c.setTextColor(render::rgb(255, 255, 255));
        c.drawString(labels[i], 68, y);
    }

    const float t = millis() / 400.0f;
    const ItemType icons[] = {ItemType::Knife, ItemType::Axe, ItemType::Bomb, ItemType::Boomerang};
    for (int i = 0; i < 4; ++i) {
        render::drawItem(icons[i], 78 + i * 28, 97 + static_cast<int>(2 * sinf(t + i)), t * (i == 0 ? 0.0f : 1.5f),
                         1.1f);
    }

    render::drawHudStrip();
    c.setTextSize(1);
    c.setTextDatum(middle_center);
    c.fillRoundRect(10, 121, 14, 12, 3, render::rgb(60, 140, 255));
    c.fillRoundRect(142, 121, 14, 12, 3, render::rgb(225, 60, 50));
    c.setTextColor(render::rgb(255, 255, 255));
    c.drawString("B", 17, 127);
    c.drawString("A", 149, 127);
    c.setTextDatum(middle_left);
    c.drawString("NEXT", 30, 127);
    c.drawString("OK", 162, 127);
}

}  // namespace menu
