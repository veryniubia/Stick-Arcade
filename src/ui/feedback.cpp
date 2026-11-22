#include "ui/gfx.h"
#include "core/platform.h"
#include "core/display.h"

namespace ui {

void hitNotice(bool localWasHit, int x, int y, int size) {
    shadowText(localWasHit ? "OUCH!" : "HIT!", x, y, size,
               localWasHit ? platform::rgb(255, 90, 80) : platform::rgb(255, 220, 60));
}

void matchResult(int winner, int local, int x, int y, int size) {
    if (winner < 0) {
        shadowText("DRAW", x, y, size, platform::rgb(255, 220, 60));
        return;
    }
    const bool won = winner == local;
    shadowText(won ? "YOU WIN" : "YOU LOSE", x, y, size,
               won ? platform::rgb(120, 255, 120) : platform::rgb(255, 90, 80));
}

using platform::rgb;
void drawBurger(int centerX, int centerY) {
    auto& c = display::canvas();
    const int x = centerX - 5, y = centerY - 5;
    c.fillRect(x+3,y+1,5,4,rgb(243,169,62));
    c.fillRect(x+1,y+2,9,3,rgb(243,169,62));
    c.fillRect(x,y+5,11,1,rgb(111,208,77));
    c.fillRect(x+1,y+6,9,2,rgb(113,57,41));
    c.fillRect(x+1,y+8,9,2,rgb(243,169,62));
    c.drawPixel(x+4,y+2,ui::kWhite); c.drawPixel(x+7,y+3,ui::kWhite);
}

}  // namespace ui
