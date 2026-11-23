#include "games/registry.h"

// ONLY_GAME_<NAME> builds compile just that game. Keep games that need the joystick after all the others: the
// launcher shows them in their own group.
#if defined(ONLY_GAME_THROWHALLA)
#define GAME_THROWHALLA 1
#elif defined(ONLY_GAME_BUMPERDUEL)
#define GAME_BUMPERDUEL 1
#elif defined(ONLY_GAME_SAMURAIDUEL)
#define GAME_SAMURAIDUEL 1
#elif defined(ONLY_GAME_MIDDIFF)
#define GAME_MIDDIFF 1
#else
#define GAME_THROWHALLA 1
#define GAME_BUMPERDUEL 1
#define GAME_SAMURAIDUEL 1
#define GAME_MIDDIFF 1
#endif

#ifdef GAME_THROWHALLA
#include "games/throwhalla/throwhalla.h"
#include "games/throwhalla/view.h"
#endif

#ifdef GAME_BUMPERDUEL
#include "games/bumperduel/bumperduel.h"
#include "games/bumperduel/view.h"
#endif

#ifdef GAME_SAMURAIDUEL
#include "games/samuraiduel/samuraiduel.h"
#include "games/samuraiduel/view.h"
#endif

#ifdef GAME_MIDDIFF
#include "games/middiff/middiff.h"
#include "games/middiff/view.h"
#endif

namespace registry {

namespace {

const GameEntry kGames[] = {
#ifdef GAME_THROWHALLA
    {&kThrowhallaInfo, "THROWHALLA", throwhalla_view::init, throwhalla_view::drawCard, throwhalla_view::drawLobby},
#endif
#ifdef GAME_BUMPERDUEL
    {&kBumperDuelInfo, "BIG BUMP", nullptr, bumperduel_view::drawCard, bumperduel_view::drawLobby},
#endif
#ifdef GAME_SAMURAIDUEL
    {&kSamuraiDuelInfo, "MoonDuel", nullptr, samuraiduel_view::drawCard, samuraiduel_view::drawLobby},
#endif
#ifdef GAME_MIDDIFF
    {&kMidDiffInfo, "MidDiff", nullptr, middiff_view::drawCard, middiff_view::drawLobby},
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
