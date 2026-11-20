#pragma once

#include <stdint.h>

// Single-channel note queue. The platform owns the user-interface sounds; every game has its own sound table
// and plays it through play(). A note sequence only interrupts a running one of equal or lower priority.
namespace sound {

struct Note {
    uint16_t freq;  // Hz
    uint16_t ms;
};

enum class Ui : uint8_t { Move, Select, Denied, Refresh, Win };

void begin();
void update();  // call every loop; plays queued notes

void play(const Note* notes, int count, uint8_t priority);
void playUi(Ui sfx);

void setVolume(uint8_t speakerVolume);  // 0 mutes

}  // namespace sound
