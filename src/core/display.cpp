#include "core/display.h"

#include <esp_timer.h>

#include "core/platform.h"

namespace display {

namespace {

M5Canvas g_cv[2] = {M5Canvas(&M5.Display), M5Canvas(&M5.Display)};
int g_back = 0;
int g_front = 0;
bool g_dma = false;
int64_t g_nextUs = 0;
int64_t g_periodUs = 1000000 / 60;
uint32_t g_pushUs = 0;
uint32_t g_waitUs = 0;

void pace() {
    const int64_t start = esp_timer_get_time();
    if (g_nextUs == 0) g_nextUs = start;
    const int64_t wait = g_nextUs - start;
    if (wait > 1500) delay(static_cast<uint32_t>((wait - 500) / 1000));
    while (esp_timer_get_time() < g_nextUs) {}
    const int64_t now = esp_timer_get_time();
    g_waitUs = static_cast<uint32_t>(now - start);
    g_nextUs += g_periodUs;
    if (g_nextUs < now) g_nextUs = now;  // fell behind: do not try to catch up
}

}  // namespace

bool begin() {
    g_dma = true;
    for (auto& c : g_cv) {
        c.setColorDepth(16);
        c.setPsram(false);
        if (!c.createSprite(platform::kScreenW, platform::kScreenH)) g_dma = false;
    }
    if (!g_dma) {
        for (auto& c : g_cv) c.deleteSprite();
        g_cv[0].setPsram(true);
        if (!g_cv[0].createSprite(platform::kScreenW, platform::kScreenH)) return false;
    } else {
        M5.Display.startWrite();  // stays open: the transfers are started with DMA and finished by waitDMA()
    }
    g_back = g_front = 0;
    return true;
}

M5Canvas& canvas() { return g_cv[g_back]; }

void setTargetFps(int fps) { g_periodUs = 1000000 / (fps > 0 ? fps : 60); }

void present() {
    pace();
    const int64_t t0 = esp_timer_get_time();
    if (g_dma) {
        M5.Display.waitDMA();  // the previous frame must be out before its buffer is drawn into again
        g_cv[g_back].pushSprite(0, 0);
        g_front = g_back;
        g_back ^= 1;
    } else {
        g_cv[0].pushSprite(0, 0);
    }
    g_pushUs = static_cast<uint32_t>(esp_timer_get_time() - t0);
}

const void* lastFrame() { return g_cv[g_front].getBuffer(); }
bool dmaActive() { return g_dma; }
uint32_t pushUs() { return g_pushUs; }
uint32_t waitUs() { return g_waitUs; }

}  // namespace display
