#include <math.h>
#include <stdio.h>

#include "core/display.h"
#include "core/platform.h"
#include "core/sound.h"
#include "games/paddle/paddle.h"
#include "games/paddle/view.h"
#include "ui/gfx.h"

// The device-only half of PADDLE: drawing, sound and the launcher artwork.

namespace {

using platform::rgb;
using sound::Note;

constexpr uint16_t kBg = rgb(18, 22, 40);
constexpr uint16_t kLine = rgb(70, 78, 110);
constexpr uint16_t kBlue = rgb(60, 140, 255);
constexpr uint16_t kRed = rgb(225, 60, 50);

void drawCourt(int ox, int oy, float scale) {
    auto& c = display::canvas();
    const int w = static_cast<int>(PaddleGame::kFieldW * scale);
    const int h = static_cast<int>(PaddleGame::kFieldH * scale);
    c.fillRect(ox, oy, w, h, kBg);
    for (int y = 0; y < h; y += 8) c.fillRect(ox + w / 2 - 1, oy + y, 2, 4, kLine);
    c.fillRect(ox, oy, w, 2, kLine);
    c.fillRect(ox, oy + h - 2, w, 2, kLine);
}

void drawPaddle(int cx, int cy, int hw, int hh, uint16_t color) {
    auto& c = display::canvas();
    c.fillRect(cx - hw, cy - hh, hw * 2, hh * 2, color);
    c.fillRect(cx - hw, cy - hh, hw * 2, 2, rgb(255, 255, 255));
}

void drawBall(int cx, int cy, int r) {
    auto& c = display::canvas();
    c.fillRect(cx - r + 1, cy - r + 1, r * 2, r * 2, rgb(30, 30, 50));
    c.fillRect(cx - r, cy - r, r * 2, r * 2, rgb(255, 232, 120));
}

}  // namespace

void PaddleGame::draw(int local, const arcade::DrawCtx& ctx) {
    auto& c = display::canvas();
    (void)ctx;
    drawCourt(0, 0, 1.0f);
    for (int i = 0; i < 2; ++i) {
        if (solo && i == 1) {  // the right side is a wall
            c.fillRect(kFieldW - 4, 2, 4, kFieldH - 4, rgb(120, 126, 150));
            continue;
        }
        drawPaddle(static_cast<int>(kPaddleX[i]), static_cast<int>(paddleY[i]), static_cast<int>(kPaddleW / 2),
                   static_cast<int>(kPaddleH / 2), i == 0 ? kBlue : kRed);
    }
    if (phase_ != arcade::MatchPhase::MatchEnd) drawBall(static_cast<int>(bx), static_cast<int>(by), 3);

    char buf[8];
    c.setTextDatum(top_center);
    c.setTextSize(3);
    if (solo) {
        snprintf(buf, sizeof(buf), "%d", rally);
        c.setTextColor(kBlue);
        c.drawString(buf, kFieldW / 2, 14);
        c.setTextSize(1);
        c.setTextColor(rgb(190, 192, 200));
        snprintf(buf, sizeof(buf), "BEST %d", score[0]);
        c.drawString(buf, kFieldW / 2, 42);
    } else {
        snprintf(buf, sizeof(buf), "%d", score[local]);
        c.setTextColor(local == 0 ? kBlue : kRed);
        c.drawString(buf, kFieldW / 2 - 28, 14);
        snprintf(buf, sizeof(buf), "%d", score[1 - local]);
        c.setTextColor(local == 0 ? kRed : kBlue);
        c.drawString(buf, kFieldW / 2 + 28, 14);
        // Your own paddle gets a small marker at its back.
        const int mx = local == 0 ? 2 : kFieldW - 4;
        c.fillRect(mx, static_cast<int>(paddleY[local]) - 2, 2, 4, rgb(255, 255, 255));
    }
    if (phase_ == arcade::MatchPhase::MatchEnd) {
        const bool won = winner == local;
        ui::shadowText(won ? "YOU WIN" : "YOU LOSE", kFieldW / 2, 80, 3, won ? ui::green() : ui::red());
    }
    c.setTextSize(1);
}

void PaddleGame::sfx(uint32_t ev) {
    static const Note kWin[] = {{660, 100}, {880, 100}, {1320, 100}, {1760, 220}};
    static const Note kScore[] = {{400, 70}, {300, 110}};
    static const Note kPaddle[] = {{900, 25}};
    static const Note kWall[] = {{600, 20}};
    static const Note kServe[] = {{1000, 30}, {1300, 50}};

    if (ev & EV_WIN) sound::play(kWin, 4, 5);
    else if (ev & EV_SCORE) sound::play(kScore, 2, 3);
    else if (ev & EV_PADDLE) sound::play(kPaddle, 1, 2);
    else if (ev & EV_WALL) sound::play(kWall, 1, 1);
    else if (ev & EV_SERVE) sound::play(kServe, 2, 1);
}

namespace paddle_view {

void drawLobby(uint32_t ms) {
    drawCourt(0, 0, 1.0f);
    const int y = 67 + static_cast<int>(30.0f * sinf(ms / 700.0f));
    drawPaddle(10, y, 2, 14, kBlue);
    drawPaddle(230, 134 - y, 2, 14, kRed);
    const float t = fmodf(ms / 1500.0f, 2.0f);
    const float f = t > 1.0f ? 2.0f - t : t;
    drawBall(30 + static_cast<int>(180.0f * f), 20 + static_cast<int>(95.0f * fabsf(sinf(ms / 520.0f))), 3);
}

// Launcher card: a miniature court with moving paddles and a bouncing ball.
void drawCard(int x, int y, int w, int h, uint32_t ms) {
    auto& c = display::canvas();
    c.setClipRect(x, y, w, h);
    c.fillRect(x, y, w, h, kBg);
    for (int py = 0; py < h; py += 8) c.fillRect(x + w / 2 - 1, y + py, 2, 4, kLine);
    c.fillRect(x, y, w, 2, kLine);
    c.fillRect(x, y + h - 2, w, 2, kLine);

    const float t = fmodf(ms / 1300.0f, 2.0f);
    const float f = t > 1.0f ? 2.0f - t : t;
    const int bxp = x + 14 + static_cast<int>((w - 28) * f);
    const int byp = y + 8 + static_cast<int>((h - 16) * fabsf(sinf(ms / 480.0f)));
    drawPaddle(x + 8, y + h / 2 + static_cast<int>((h / 2 - 16) * sinf(ms / 480.0f + 1.0f) * 0.7f), 3, 13, kBlue);
    drawPaddle(x + w - 8, y + h / 2 + static_cast<int>((h / 2 - 16) * sinf(ms / 480.0f) * 0.7f), 3, 13, kRed);
    drawBall(bxp, byp, 3);
    c.clearClipRect();
}

}  // namespace paddle_view
