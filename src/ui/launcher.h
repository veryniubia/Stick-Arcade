#pragma once

#include <stdint.h>

// The game picker: a row of cards, B scrolls to the next, A opens. The last card is SETTINGS.
namespace launcher {

enum class Result { None, Game, Random, Settings };

void begin();  // remembers the previous selection
Result update(float dt);
int selectedGame();  // valid after update() returned Result::Game
void draw();
void drawRandomCard(int x, int y, int w, int h, uint32_t ms);

}  // namespace launcher
