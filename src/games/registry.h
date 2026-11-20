#pragma once

#include "core/game_iface.h"

// The games compiled into this firmware, in launcher order. Add a game here and it shows up as a card.
namespace registry {

struct GameEntry {
    const arcade::GameInfo* info;
    const char* name;  // launcher title and waiting screen title: English and digits only, at most 10 characters
    bool (*init)();    // one-time allocation of artwork resources (may be null)
    void (*drawCard)(int x, int y, int w, int h, uint32_t ms);  // 120x78 launcher card, animated
    void (*drawLobby)(uint32_t ms);                             // full-screen background of the waiting screen
};

void begin();  // runs the init functions
int count();
const GameEntry& get(int index);

}  // namespace registry
