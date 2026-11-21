#pragma once

#include <M5Unified.h>

#include <stdint.h>

// Screen output: two canvases in internal RAM so the DMA transfer of one frame overlaps with drawing the next,
// plus a frame limiter. If the two canvases do not fit, one canvas in PSRAM is pushed with the CPU.
namespace display {

bool begin();
M5Canvas& canvas();  // the buffer to draw the next frame into
void present();      // waits for the frame time, starts the transfer, switches to the other buffer
void setTargetFps(int fps);
void setPortrait(bool portrait);
int width();
int height();

const void* lastFrame();  // pixels of the frame that was presented last (RGB565, big-endian)
bool dmaActive();
uint32_t pushUs();  // time spent starting the transfer of the last frame
uint32_t waitUs();  // time slept by the frame limiter before it

}  // namespace display
