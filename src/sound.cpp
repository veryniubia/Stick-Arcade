#include "sound.h"

#include <M5Unified.h>

#include "game.h"

namespace {

struct Note {
    uint16_t freq;
    uint16_t ms;
};

constexpr int kQueueSize = 8;
Note g_queue[kQueueSize];
int g_head = 0;
int g_count = 0;
uint32_t g_nextAt = 0;
bool g_enabled = true;

void push(const Note* notes, int n) {
    g_head = 0;
    g_count = 0;
    for (int i = 0; i < n && i < kQueueSize; ++i) g_queue[g_count++] = notes[i];
    g_nextAt = 0;
}

}  // namespace

namespace sound {

void begin() {
    M5.Speaker.setVolume(140);
}

void setEnabled(bool on) {
    g_enabled = on;
    if (!on) {
        g_count = 0;
        M5.Speaker.stop();
    }
}

bool enabled() { return g_enabled; }

void update() {
    if (g_count == 0 || millis() < g_nextAt) return;
    const Note n = g_queue[g_head++];
    --g_count;
    M5.Speaker.tone(n.freq, n.ms, 0, true);
    g_nextAt = millis() + n.ms;
}

void play(Sfx sfx) {
    if (!g_enabled) return;
    switch (sfx) {
        case Sfx::Move: { const Note n[] = {{1200, 20}}; push(n, 1); break; }
        case Sfx::Select: { const Note n[] = {{880, 50}, {1320, 70}}; push(n, 2); break; }
        case Sfx::Denied: { const Note n[] = {{220, 90}, {180, 120}}; push(n, 2); break; }
        case Sfx::Fire: { const Note n[] = {{700, 35}, {500, 45}}; push(n, 2); break; }
        case Sfx::Jump: { const Note n[] = {{500, 30}, {800, 40}}; push(n, 2); break; }
        case Sfx::Hit: { const Note n[] = {{300, 60}, {200, 60}, {120, 120}}; push(n, 3); break; }
        case Sfx::Bounce: { const Note n[] = {{1000, 25}}; push(n, 1); break; }
        case Sfx::Explode: { const Note n[] = {{150, 80}, {100, 80}, {70, 160}}; push(n, 3); break; }
        case Sfx::RoundEnd: { const Note n[] = {{660, 80}, {520, 80}, {400, 120}}; push(n, 3); break; }
        case Sfx::Refresh: { const Note n[] = {{900, 40}, {1200, 60}}; push(n, 2); break; }
        case Sfx::Win: { const Note n[] = {{660, 100}, {880, 100}, {1320, 100}, {1760, 220}}; push(n, 4); break; }
    }
}

void playEvents(uint32_t ev) {
    if (ev & EV_WIN) play(Sfx::Win);
    else if (ev & EV_EXPLODE) play(Sfx::Explode);
    else if (ev & EV_HIT) play(Sfx::Hit);
    else if (ev & EV_ROUND_END) play(Sfx::RoundEnd);
    else if (ev & EV_FIRE) play(Sfx::Fire);
    else if (ev & EV_JUMP) play(Sfx::Jump);
    else if (ev & EV_BOUNCE) play(Sfx::Bounce);
    else if (ev & EV_REFRESH) play(Sfx::Refresh);
}

}  // namespace sound
