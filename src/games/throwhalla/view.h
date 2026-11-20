#pragma once

#include <stdint.h>

// Launcher and waiting-screen artwork of Throwhalla (device only).
namespace throwhalla_view {

bool init();
void drawCard(int x, int y, int w, int h, uint32_t ms);
void drawLobby(uint32_t ms);

}  // namespace throwhalla_view
