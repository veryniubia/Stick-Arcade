#pragma once

#include <M5Unified.h>

#include "game.h"

namespace render {

constexpr uint16_t rgb(uint8_t r, uint8_t g, uint8_t b) {
    return static_cast<uint16_t>(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

extern M5Canvas canvas;

bool begin();
void present();

void drawBackground();
void drawHudStrip();
void drawPlatform(int x, int y);
void drawObstacle(int x, int y, int r);
void drawItem(ItemType type, int cx, int cy, float angle, float scale = 1.0f, bool fuseLit = false);
void drawFigure(int x, int baseY, int dir, int paletteIdx, bool hit, bool blink, float armDeg);

// `local` is the index of the player holding this device; only that player's private info is drawn.
// `exitHold` is 0..1 progress of the hold-to-exit gesture; `showHint` prints the shortcut at the top.
void drawGame(const Game& g, int local, float exitHold, bool showHint);

}  // namespace render
