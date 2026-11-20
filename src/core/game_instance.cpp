#include "core/game_iface.h"

#include <stdlib.h>

#if defined(ESP_PLATFORM)
#include <esp_heap_caps.h>
#endif

namespace arcade {

GameInstance gameCreate(const GameInfo& info) {
    constexpr size_t kAlign = 16;
    const size_t objBytes = (info.instanceSize + kAlign - 1) & ~(kAlign - 1);
    const size_t total = objBytes + info.arenaBytes;
#if defined(ESP_PLATFORM)
    void* mem = heap_caps_malloc(total, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (!mem) mem = heap_caps_malloc(total, MALLOC_CAP_8BIT);
#else
    void* mem = malloc(total);
#endif
    GameInstance inst;
    if (!mem) return inst;
    Arena arena(static_cast<uint8_t*>(mem) + objBytes, info.arenaBytes);
    inst.block = mem;
    inst.game = info.create(mem, arena);
    return inst;
}

void gameDestroy(GameInstance& inst) {
    if (inst.game) inst.game->~IGame();
#if defined(ESP_PLATFORM)
    heap_caps_free(inst.block);
#else
    free(inst.block);
#endif
    inst = GameInstance();
}

}  // namespace arcade
