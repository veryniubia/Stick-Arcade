#include "games/registry.h"

#include "games/throwhalla/throwhalla.h"
#include "games/throwhalla/view.h"

#ifndef ONLY_GAME_THROWHALLA
#include "games/paddle/paddle.h"
#include "games/paddle/view.h"
#endif

namespace registry {

namespace {

const GameEntry kGames[] = {
    {&kThrowhallaInfo, "THROWHALLA", throwhalla_view::init, throwhalla_view::drawCard, throwhalla_view::drawLobby},
#ifndef ONLY_GAME_THROWHALLA
    {&kPaddleInfo, "PADDLE", nullptr, paddle_view::drawCard, paddle_view::drawLobby},
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
