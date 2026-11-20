#pragma once

// The game picker: a row of cards, B scrolls to the next, A opens. The last card is SETTINGS.
namespace launcher {

enum class Result { None, Game, Settings };

void begin();  // remembers the previous selection
Result update(float dt);
int selectedGame();  // valid after update() returned Result::Game
void draw();

}  // namespace launcher
