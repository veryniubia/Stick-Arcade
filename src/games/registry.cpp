#include "games/registry.h"

#if !defined(ONLY_GAME_BUMPERDUEL) && !defined(ONLY_GAME_SAMURAIDUEL)
#include "games/throwhalla/throwhalla.h"
#include "games/throwhalla/view.h"
#endif


#if !defined(ONLY_GAME_THROWHALLA) && !defined(ONLY_GAME_SAMURAIDUEL)
#include "games/bumperduel/bumperduel.h"
#include "games/bumperduel/view.h"
#endif

#if !defined(ONLY_GAME_THROWHALLA) && !defined(ONLY_GAME_BUMPERDUEL)
#include "games/samuraiduel/samuraiduel.h"
#include "games/samuraiduel/view.h"
#endif

namespace registry {

namespace {

const GameEntry kGames[] = {
#if !defined(ONLY_GAME_BUMPERDUEL) && !defined(ONLY_GAME_SAMURAIDUEL)
    {&kThrowhallaInfo, "THROWHALLA", throwhalla_view::init, throwhalla_view::drawCard, throwhalla_view::drawLobby},
#endif
#if !defined(ONLY_GAME_THROWHALLA) && !defined(ONLY_GAME_SAMURAIDUEL)
    {&kBumperDuelInfo, "BIG BUMP", nullptr, bumperduel_view::drawCard, bumperduel_view::drawLobby},
#endif
#if !defined(ONLY_GAME_THROWHALLA) && !defined(ONLY_GAME_BUMPERDUEL)
    {&kSamuraiDuelInfo, "MoonDuel", nullptr, samuraiduel_view::drawCard, samuraiduel_view::drawLobby},
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
