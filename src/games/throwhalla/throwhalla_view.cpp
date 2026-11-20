#include <math.h>

#include "core/display.h"
#include "core/sound.h"
#include "games/throwhalla/render.h"
#include "games/throwhalla/throwhalla.h"
#include "games/throwhalla/view.h"

// The device-only half of Throwhalla: drawing, sound and the launcher artwork.

namespace {

using platform::rgb;
using sound::Note;

}  // namespace

void ThrowhallaGame::draw(int local, const arcade::DrawCtx&) { render::drawGame(g_, local); }

void ThrowhallaGame::sfx(uint32_t ev) {
    static const Note kWin[] = {{660, 100}, {880, 100}, {1320, 100}, {1760, 220}};
    static const Note kExplode[] = {{150, 80}, {100, 80}, {70, 160}};
    static const Note kHit[] = {{300, 60}, {200, 60}, {120, 120}};
    static const Note kRoundEnd[] = {{660, 80}, {520, 80}, {400, 120}};
    static const Note kFire[] = {{700, 35}, {500, 45}};
    static const Note kJump[] = {{500, 30}, {800, 40}};
    static const Note kBounce[] = {{1000, 25}};
    static const Note kRefresh[] = {{900, 40}, {1200, 60}};

    if (ev & EV_WIN) sound::play(kWin, 4, 5);
    else if (ev & EV_EXPLODE) sound::play(kExplode, 3, 4);
    else if (ev & EV_HIT) sound::play(kHit, 3, 4);
    else if (ev & EV_ROUND_END) sound::play(kRoundEnd, 3, 3);
    else if (ev & EV_FIRE) sound::play(kFire, 2, 2);
    else if (ev & EV_JUMP) sound::play(kJump, 2, 1);
    else if (ev & EV_BOUNCE) sound::play(kBounce, 1, 1);
    else if (ev & EV_REFRESH) sound::play(kRefresh, 2, 1);
}

namespace throwhalla_view {

bool init() { return render::begin(); }

// Waiting screen background: the two vikings on their platforms in front of the lava pit.
void drawLobby(uint32_t ms) {
    (void)ms;
    render::drawBackground();
    render::drawPlatform(24, 92);
    render::drawPlatform(216, 92);
    render::drawFigure(24, 92, 1, 0, false, false, 20.0f);
    render::drawFigure(216, 92, -1, 1, false, false, -20.0f);
}

// Launcher card: two vikings trading an axe over a bubbling lava strip.
void drawCard(int x, int y, int w, int h, uint32_t ms) {
    auto& c = display::canvas();
    c.setClipRect(x, y, w, h);

    for (int i = 0; i < 6; ++i) {
        const int bandH = h / 6 + 1;
        c.fillRect(x, y + i * bandH, w, bandH, rgb(static_cast<uint8_t>(46 + i * 7), static_cast<uint8_t>(54 + i * 6), 74));
    }
    const int lavaTop = y + h - 14;
    c.fillRect(x, lavaTop, w, 14, rgb(190, 42, 30));
    const int scroll = static_cast<int>(ms / 120);
    for (int px = 0; px < w; px += 4) {
        const int tri = abs(((px / 4 + scroll) % 6) - 3);
        c.fillRect(x + px, lavaTop - 2 + tri, 4, 2, rgb(255, 216, 60));
        c.fillRect(x + px, lavaTop + tri, 4, 3, rgb(244, 128, 30));
    }

    const int baseY = y + 56;
    const float swing = sinf(ms / 450.0f);
    render::drawPlatform(x + 20, baseY);
    render::drawPlatform(x + w - 20, baseY);
    render::drawFigure(x + 20, baseY, 1, 0, false, false, 35.0f + 25.0f * swing);
    render::drawFigure(x + w - 20, baseY, -1, 1, false, false, 35.0f - 25.0f * swing);

    // The axe flies left to right and back, tumbling along an arc.
    const float u = fmodf(ms / 1100.0f, 2.0f);
    const bool back = u > 1.0f;
    const float f = back ? 2.0f - u : u;
    const int ax = x + 32 + static_cast<int>((w - 64) * f);
    const int ay = baseY - 14 - static_cast<int>(22.0f * sinf(3.14159f * f));
    render::drawItem(ItemType::Axe, ax, ay, ms / 70.0f * (back ? -1.0f : 1.0f));

    c.clearClipRect();
}

}  // namespace throwhalla_view
