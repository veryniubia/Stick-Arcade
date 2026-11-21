#include "games/registry.h"

#ifndef ONLY_GAME_BUMPERDUEL
#include "games/throwhalla/throwhalla.h"
#include "games/throwhalla/view.h"
#endif


#ifndef ONLY_GAME_THROWHALLA
#include "games/bumperduel/bumperduel.h"
#include "games/bumperduel/view.h"
#endif

namespace registry {

namespace {

const GameEntry kGames[] = {
#ifndef ONLY_GAME_BUMPERDUEL
    {&kThrowhallaInfo, "THROWHALLA", throwhalla_view::init, throwhalla_view::drawCard, throwhalla_view::drawLobby},
#endif
#ifndef ONLY_GAME_THROWHALLA
    {&kBumperDuelInfo, "BIG BUMP", nullptr, bumperduel_view::drawCard, bumperduel_view::drawLobby},
#endif
};

}  // namespace

void begin() {
    for (const GameEntry& g : kGames) {
        if (g.init) g.init();
    }
}

int count() { return static_cast<int>(sizeof(kGames) / sizeof(kGames[0])); }

const GameEntry& get(int index) { return kGames[index]; }

}  // namespace registry
