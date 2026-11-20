#pragma once

#include <M5Unified.h>

#include "core/platform.h"
#include "games/throwhalla/game.h"

// Pixel art of Throwhalla, all drawn by code into display::canvas().
namespace render {

using platform::rgb;

bool begin();  // allocates the small sprite used for the knock-down animation

void drawBackground();
void drawHudStrip();
void drawPlatform(int x, int y);
void drawObstacle(int x, int y, int r);
void drawItem(ItemType type, int cx, int cy, float angle, float scale = 1.0f, bool fuseLit = false);
void drawFigure(int x, int baseY, int dir, int paletteIdx, bool hit, bool blink, float armDeg);

// `local` is the index of the player holding this device; only that player's private info is drawn.
void drawGame(const Game& g, int local);

}  // namespace render
