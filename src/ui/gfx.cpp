#include "ui/gfx.h"

#include <M5Unified.h>

#include "core/display.h"
#include "core/platform.h"

namespace ui {

using platform::rgb;

uint16_t yellow() { return rgb(255, 220, 60); }
uint16_t red() { return rgb(255, 90, 80); }
uint16_t green() { return rgb(120, 255, 120); }
uint16_t keyBlue() { return rgb(60, 140, 255); }
uint16_t keyRed() { return rgb(225, 60, 50); }

void shadowText(const char* text, int x, int y, int size, uint16_t color) {
    auto& c = display::canvas();
    c.setTextDatum(middle_center);
    c.setTextSize(size);
    c.setTextColor(kBlack);
    c.drawString(text, x + size / 2 + 1, y + size / 2 + 1);
    c.setTextColor(color);
    c.drawString(text, x, y);
    c.setTextSize(1);
}

void hintBar(const char* bLabel, const char* aLabel, bool blinkB) {
    auto& c = display::canvas();
    c.fillRect(0, platform::kHintY, platform::kScreenW, platform::kScreenH - platform::kHintY, rgb(48, 50, 56));
    c.fillRect(0, platform::kHintY, platform::kScreenW, 1, rgb(240, 240, 240));
    c.setTextSize(1);
    c.setTextDatum(middle_center);
    c.setTextColor(kWhite);
    if (bLabel && (!blinkB || (millis() / 350) % 2 == 0)) {
        c.fillRoundRect(10, 121, 14, 12, 3, keyBlue());
        c.drawString("B", 17, 127);
        c.setTextDatum(middle_left);
        c.drawString(bLabel, 30, 127);
    }
    c.setTextDatum(middle_center);
    if (aLabel) {
        c.fillRoundRect(142, 121, 14, 12, 3, keyRed());
        c.drawString("A", 149, 127);
        c.setTextDatum(middle_left);
        c.drawString(aLabel, 162, 127);
    }
}

void dim(int x, int y, int w, int h) {
    auto& c = display::canvas();
    for (int row = 0; row < h; row += 2) c.fillRect(x, y + row, w, 1, rgb(10, 12, 20));
}

void drawBitmap(const arcade::Bitmap& bmp, int x, int y) {
    auto& c = display::canvas();
    // M5GFX expands packed palette rows directly into the existing RGB565 canvas.
    // Odd-width artwork in our format carries its last nibble across row boundaries.
    if (!bmp.rle && bmp.colors == 16 && bmp.w % 2 == 0 && bmp.size >= size_t(bmp.w / 2) * bmp.h) {
        c.pushImage(x, y, bmp.w, bmp.h, bmp.data, lgfx::palette_4bit,
                    reinterpret_cast<const lgfx::rgb565_t*>(bmp.palette));
        return;
    }
    arcade::forEachRun(bmp, [&](int rx, int ry, int len, uint8_t color) {
        c.fillRect(x + rx, y + ry, len, 1, bmp.palette[color]);
    });
}

}  // namespace ui
