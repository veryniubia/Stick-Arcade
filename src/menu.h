#pragma once

namespace menu {

enum class Choice { None, Training, Versus };

void begin();
Choice update();  // handles input; returns a game start request when chosen
void draw();

}  // namespace menu
