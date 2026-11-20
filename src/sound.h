#pragma once

#include <stdint.h>

namespace sound {

enum class Sfx : uint8_t { Move, Select, Denied, Fire, Jump, Hit, Bounce, Explode, RoundEnd, Refresh, Win };

void begin();
void update();  // call every loop; plays queued notes
void play(Sfx sfx);
void playEvents(uint32_t gameEvents);

void setEnabled(bool on);
bool enabled();

}  // namespace sound
