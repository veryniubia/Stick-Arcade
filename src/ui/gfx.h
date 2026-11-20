#pragma once

#include <stdint.h>

#include "core/bitmap.h"

// Drawing helpers shared by the launcher, the settings page and the play flow. They draw into display::canvas().
namespace ui {

constexpr uint16_t kBlack = 0x0000;
constexpr uint16_t kWhite = 0xFFFF;
uint16_t yellow();
uint16_t red();
uint16_t green();
uint16_t keyBlue();   // colour of the B key chip
uint16_t keyRed();    // colour of the A key chip

// Text centred on (x, y) with a one pixel drop shadow.
void shadowText(const char* text, int x, int y, int size, uint16_t color);

// Bottom strip with the two key hints, e.g. hintBar("NEXT", "OK") draws "B NEXT" on the left and "A OK" on the
// right. A null label hides that side. `blinkB` makes the B hint flash (used to nudge the user).
void hintBar(const char* bLabel, const char* aLabel, bool blinkB = false);

// Darkens a rectangle with horizontal scan lines (used for the neighbour cards of the launcher).
void dim(int x, int y, int w, int h);

// Draws a palette bitmap (see core/bitmap.h) with its top-left corner at (x, y).
void drawBitmap(const arcade::Bitmap& bmp, int x, int y);

}  // namespace ui
