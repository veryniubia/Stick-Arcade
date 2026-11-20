#include "core/sound.h"

#include <M5Unified.h>

namespace {

constexpr int kQueueSize = 8;
sound::Note g_queue[kQueueSize];
int g_head = 0;
int g_count = 0;
uint32_t g_nextAt = 0;
uint32_t g_busyUntil = 0;
uint8_t g_prio = 0;
uint8_t g_volume = 140;

}  // namespace

namespace sound {

void begin() { M5.Speaker.setVolume(g_volume); }

void setVolume(uint8_t speakerVolume) {
    g_volume = speakerVolume;
    M5.Speaker.setVolume(speakerVolume);
    if (speakerVolume == 0) {
        g_count = 0;
        M5.Speaker.stop();
    }
}

void update() {
    if (g_count == 0 || millis() < g_nextAt) return;
    const Note n = g_queue[g_head++];
    --g_count;
    M5.Speaker.tone(n.freq, n.ms, 0, true);
    g_nextAt = millis() + n.ms;
}

void play(const Note* notes, int count, uint8_t priority) {
    if (g_volume == 0) return;
    const uint32_t now = millis();
    if (now < g_busyUntil && priority < g_prio) return;  // a more important sound is still playing
    g_head = 0;
    g_count = 0;
    uint32_t total = 0;
    for (int i = 0; i < count && i < kQueueSize; ++i) {
        g_queue[g_count++] = notes[i];
        total += notes[i].ms;
    }
    g_prio = priority;
    g_busyUntil = now + total;
    g_nextAt = 0;
}

void playUi(Ui sfx) {
    switch (sfx) {
        case Ui::Move: { static const Note n[] = {{1200, 20}}; play(n, 1, 1); break; }
        case Ui::Select: { static const Note n[] = {{880, 50}, {1320, 70}}; play(n, 2, 3); break; }
        case Ui::Denied: { static const Note n[] = {{220, 90}, {180, 120}}; play(n, 2, 3); break; }
        case Ui::Refresh: { static const Note n[] = {{900, 40}, {1200, 60}}; play(n, 2, 2); break; }
        case Ui::Win: { static const Note n[] = {{660, 100}, {880, 100}, {1320, 100}, {1760, 220}}; play(n, 4, 5); break; }
    }
}

}  // namespace sound
