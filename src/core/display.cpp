#include "core/display.h"

#include <esp_timer.h>
#include <esp_heap_caps.h>

#include "core/platform.h"

namespace display {

namespace {

M5Canvas g_cv[2] = {M5Canvas(&M5.Display), M5Canvas(&M5.Display)};
bool g_portrait = false;
int g_back = 0;
int g_front = 0;
bool g_dma = false;
void* g_buffers[2] = {};
bool g_initialized = false;
constexpr size_t kBufferBytes = platform::kScreenW * platform::kScreenH * 2 + 2;
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
    if (g_initialized) return g_buffers[0] != nullptr;
    // Reserve both frames before Wi-Fi starts. Rotation must never free/reallocate these
    // large internal blocks: repeated lobby/game switches otherwise fragment DMA RAM.
    g_buffers[0] = heap_caps_malloc(kBufferBytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA);
    g_buffers[1] = heap_caps_malloc(kBufferBytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA);
    g_dma = g_buffers[0] && g_buffers[1];
    if (!g_dma) {
        for (auto& buffer : g_buffers) { heap_caps_free(buffer); buffer = nullptr; }
        g_buffers[0] = heap_caps_malloc(kBufferBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!g_buffers[0]) return false;
    }
    for (int i = 0; i < (g_dma ? 2 : 1); ++i)
        g_cv[i].setBuffer(g_buffers[i], width(), height(), 16);
    if (g_dma) M5.Display.startWrite();
    g_initialized = true;
    g_back = g_front = 0;
    return true;
}

M5Canvas& canvas() { return g_cv[g_back]; }
int width() { return g_portrait ? 135 : 240; }
int height() { return g_portrait ? 240 : 135; }
void setPortrait(bool portrait) {
    if (portrait == g_portrait) return;
    M5.Display.waitDMA();
    if (g_dma) M5.Display.endWrite();
    g_portrait = portrait;
    M5.Display.setRotation(portrait ? 0 : 1);
    // setBuffer borrows external storage; only its dimensions/clip are rebound.
    for (int i = 0; i < (g_dma ? 2 : 1); ++i)
        g_cv[i].setBuffer(g_buffers[i], width(), height(), 16);
    g_back = g_front = 0;
    g_nextUs = 0;
    if (g_dma) M5.Display.startWrite();

}

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
