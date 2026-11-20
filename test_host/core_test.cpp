// Host-side tests for the platform pieces that need no hardware: shake detector, settings format, arena,
// game creation and the PADDLE rules/encoding.
// Build: g++ -std=c++17 -I src test_host/core_test.cpp src/core/game_instance.cpp src/games/paddle/paddle.cpp
//        src/games/throwhalla/throwhalla.cpp src/games/throwhalla/game.cpp -o test_host/core_test.exe
#include <cmath>
#include <cstdio>
#include <cstring>
#include <initializer_list>

#include "core/bitmap.h"
#include "core/settings_data.h"
#include "core/shake.h"
#include "games/paddle/paddle.h"
#include "games/throwhalla/throwhalla.h"

// The tests never draw: the view halves of the games are not linked.
void ThrowhallaGame::draw(int, const arcade::DrawCtx&) {}
void ThrowhallaGame::sfx(uint32_t) {}
void PaddleGame::draw(int, const arcade::DrawCtx&) {}
void PaddleGame::sfx(uint32_t) {}

static int failures = 0;

#define CHECK(cond)                                                      \
    do {                                                                 \
        if (!(cond)) {                                                   \
            std::printf("FAIL line %d: %s\n", __LINE__, #cond);          \
            ++failures;                                                  \
        }                                                                \
    } while (0)

// ---- shake ----

static void testShake() {
    using arcade::ShakeDetector;
    {
        ShakeDetector d;
        CHECK(!d.update(0, 0.02f));
        CHECK(!d.update(100, 5.0f));   // one spike is not enough
        CHECK(!d.update(150, 0.2f));
        CHECK(d.update(400, 5.5f));    // the second within the window
        CHECK(!d.update(450, 0.2f));
        CHECK(!d.update(2000, 5.0f));  // starts over
    }
    {
        ShakeDetector d;
        CHECK(!d.update(0, 4.0f));
        CHECK(!d.update(50, 0.1f));
        CHECK(!d.update(1500, 4.0f));  // too late: this is the first spike of a new attempt
        CHECK(!d.update(1550, 0.1f));
        CHECK(d.update(1900, 4.0f));
    }
    {
        // One long strong acceleration (a drop, a bump) never re-arms, so it counts once at most.
        ShakeDetector d;
        bool any = false;
        for (uint32_t t = 0; t < 600; t += 5) any = any || d.update(t, 6.0f);
        CHECK(!any);
    }
    {
        // Rough play: the highest value measured in normal play is 1.13 g.
        ShakeDetector d;
        bool any = false;
        uint32_t t = 0;
        for (int i = 0; i < 4000; ++i, t += 5) any = any || d.update(t, 0.2f + 0.93f * ((i / 7) % 2));
        CHECK(!any);
    }
    {
        // Just under the threshold twice: nothing.
        ShakeDetector d;
        CHECK(!d.update(0, 2.4f));
        CHECK(!d.update(50, 0.1f));
        CHECK(!d.update(300, 2.4f));
    }
    {
        // Back to back triggers need a fresh double shake each time.
        ShakeDetector d;
        d.update(0, 5.0f);
        d.update(30, 0.1f);
        CHECK(d.update(300, 5.0f));
        d.update(330, 0.1f);
        CHECK(!d.update(400, 5.0f));
        d.update(430, 0.1f);
        CHECK(d.update(700, 5.0f));
    }
}

// ---- settings ----

static void testSettingsCodec() {
    settings::Data a;
    a.volume = 4;
    a.bright = 1;
    a.aim = 3;
    a.tiltOffsetX10 = -123;
    uint8_t blob[settings::kBlobSize];
    settings::encode(a, blob);
    settings::Data b;
    CHECK(settings::decode(blob, sizeof(blob), b));
    CHECK(b == a);

    settings::Data c;
    c.volume = 0;
    CHECK(!settings::decode(blob, sizeof(blob) - 1, c));  // wrong size
    CHECK(c.volume == 0);
    uint8_t bad[settings::kBlobSize];
    std::memcpy(bad, blob, sizeof(bad));
    bad[2] ^= 1;
    CHECK(!settings::decode(bad, sizeof(bad), c));  // checksum
    std::memcpy(bad, blob, sizeof(bad));
    bad[0] = settings::kFormat + 1;
    bad[7] = settings::crc8(bad, 7);
    CHECK(!settings::decode(bad, sizeof(bad), c));  // other format: defaults are used by the caller
    std::memcpy(bad, blob, sizeof(bad));
    bad[1] = 9;
    bad[7] = settings::crc8(bad, 7);
    CHECK(!settings::decode(bad, sizeof(bad), c));  // value out of range even with a valid checksum
    CHECK(settings::Data().volume == 3 && settings::kAimValue[settings::Data().aim] == 3.0f);
    CHECK(settings::kVolumeValue[settings::kVolumeLevels - 1] <= 191);  // 75% of the speaker range at most
}

// ---- arena and instances ----

static void testArena() {
    alignas(16) uint8_t mem[64];
    arcade::Arena a(mem, sizeof(mem));
    void* p1 = a.alloc(10);
    void* p2 = a.alloc(10);
    CHECK(p1 && p2 && p2 != p1);
    CHECK(reinterpret_cast<uintptr_t>(p2) % 8 == 0);
    CHECK(a.alloc(1000) == nullptr);
    CHECK(a.used() <= a.size());
}

static void testGameInstances() {
    for (const arcade::GameInfo* info : {&kThrowhallaInfo, &kPaddleInfo}) {
        arcade::GameInstance gi = arcade::gameCreate(*info);
        CHECK(gi.game != nullptr);
        gi.game->begin(arcade::Mode::Solo, 5);
        PadInput in[2];
        for (int i = 0; i < 600; ++i) gi.game->step(1.0f / 60.0f, in);
        arcade::gameDestroy(gi);
        CHECK(gi.game == nullptr && gi.block == nullptr);
    }
    CHECK(kThrowhallaInfo.wireId != kPaddleInfo.wireId);
}

// ---- PADDLE ----

constexpr float DT = 1.0f / 60.0f;

static void runFor(PaddleGame& g, float sec, const PadInput in[2]) {
    for (int i = 0; i < static_cast<int>(sec / DT); ++i) g.step(DT, in);
}

static void testPaddleServeAndMove() {
    PaddleGame g;
    g.begin(arcade::Mode::Host, 3);
    CHECK(g.phase() == arcade::MatchPhase::RoundEnd && g.idle(0));
    PadInput in[2];
    in[0].aim = PaddleGame::kAimRange;   // tilted up: paddle goes to the top
    in[1].aim = -PaddleGame::kAimRange;  // tilted down: paddle goes to the bottom
    runFor(g, 0.5f, in);
    CHECK(g.paddleY[0] < 25.0f && g.paddleY[1] > 110.0f);
    CHECK(g.paddleY[0] >= PaddleGame::kPaddleH / 2 - 0.01f && g.paddleY[1] <= PaddleGame::kFieldH - PaddleGame::kPaddleH / 2 + 0.01f);
    runFor(g, 1.0f, in);
    CHECK(g.phase() == arcade::MatchPhase::Playing && g.bvx != 0.0f);
}

static void testPaddleScoreAndWin() {
    PaddleGame g;
    g.begin(arcade::Mode::Host, 3);
    PadInput in[2];
    // Nobody moves the paddles to the ball's side: the ball leaves the field and somebody scores.
    int guard = 0;
    while (g.phase() != arcade::MatchPhase::MatchEnd && guard++ < 60 * 400) {
        in[0].aim = -PaddleGame::kAimRange;  // paddles parked at the bottom
        in[1].aim = -PaddleGame::kAimRange;
        g.step(DT, in);
        if (g.by > 60.0f) g.by = 20.0f;  // keep the ball clear of the parked paddles
    }
    CHECK(g.phase() == arcade::MatchPhase::MatchEnd);
    CHECK(g.winner == 0 || g.winner == 1);
    CHECK(g.score[g.winner] == PaddleGame::kWinScore);
    const uint8_t s0 = g.score[0], s1 = g.score[1];
    runFor(g, 2.0f, in);
    CHECK(g.score[0] == s0 && g.score[1] == s1);  // the match stays finished
}

static void testPaddleBounce() {
    PaddleGame g;
    g.begin(arcade::Mode::Host, 1);
    PadInput in[2];
    runFor(g, 1.3f, in);  // serve
    g.bx = PaddleGame::kPaddleX[0] + 20.0f;
    g.by = g.paddleY[0];
    g.bvx = -150.0f;
    g.bvy = 0.0f;
    uint32_t ev = 0;
    for (int i = 0; i < 30; ++i) ev |= g.step(DT, in);
    CHECK(ev & PaddleGame::EV_PADDLE);
    CHECK(g.bvx > 150.0f);  // returned, and a little faster
    CHECK(g.score[0] == 0 && g.score[1] == 0);
}

static void testPaddleSolo() {
    PaddleGame g;
    g.begin(arcade::Mode::Solo, 4);
    PadInput in[2];
    runFor(g, 1.3f, in);
    // Right side is a wall: the ball comes back by itself.
    g.bx = 200.0f;
    g.by = 60.0f;
    g.bvx = 200.0f;
    g.bvy = 0.0f;
    for (int i = 0; i < 20; ++i) g.step(DT, in);
    CHECK(g.bvx < 0.0f);
    // A rally counts returns of the left paddle; missing resets it but keeps the best.
    g.rally = 6;
    g.bx = 5.0f;
    g.by = 120.0f;
    g.bvx = -100.0f;
    g.bvy = 0.0f;
    g.paddleY[0] = 20.0f;
    in[0].aim = PaddleGame::kAimRange;
    for (int i = 0; i < 40; ++i) g.step(DT, in);
    CHECK(g.rally == 0 && g.score[0] == 6);
    CHECK(g.phase() != arcade::MatchPhase::MatchEnd);
}

static void testPaddleEncoding() {
    PaddleGame a;
    a.begin(arcade::Mode::Host, 9);
    PadInput in[2];
    in[0].aim = 20.0f;
    in[1].aim = -35.0f;
    runFor(a, 3.0f, in);
    arcade::Writer w;
    a.encode(w);
    CHECK(w.n < 40);

    PaddleGame b;
    b.begin(arcade::Mode::Client, 9);
    arcade::Reader r(w.buf, w.n);
    CHECK(b.decode(r));
    CHECK(b.phase() == a.phase() && b.score[0] == a.score[0] && b.score[1] == a.score[1]);
    CHECK(std::fabs(b.bx - a.bx) < 0.1f && std::fabs(b.by - a.by) < 0.1f);
    CHECK(std::fabs(b.paddleY[0] - a.paddleY[0]) < 0.1f && std::fabs(b.paddleY[1] - a.paddleY[1]) < 0.1f);

    // A truncated or invalid snapshot must not change anything.
    PaddleGame c;
    c.begin(arcade::Mode::Client, 9);
    const float bx = c.bx;
    arcade::Reader shortR(w.buf, w.n - 3);
    CHECK(!c.decode(shortR) && c.bx == bx);
    uint8_t bad[40];
    std::memcpy(bad, w.buf, w.n);
    bad[0] = 9;  // no such phase
    arcade::Reader badR(bad, w.n);
    CHECK(!c.decode(badR) && c.bx == bx);
}

static void testThrowhallaEncoding() {
    ThrowhallaGame a, b;
    a.begin(arcade::Mode::Host, 1);
    b.begin(arcade::Mode::Client, 1);
    auto& g = a.core();
    g.players[0].hit = true;
    g.players[0].hitSources = 3;
    g.players[0].hitSerial = 2;
    g.players[0].hitFlash = 0.85f;
    g.players[1].score = 2;
    for (auto& p : g.proj) {
        p.active = true;
        p.type = ItemType::Knife;
    }
    arcade::Writer w;
    a.encode(w);
    CHECK(w.n < 175);
    arcade::Reader r(w.buf, w.n);
    CHECK(b.decode(r));
    CHECK(b.core().players[0].hitSources == 3 && b.core().players[0].hitSerial == 2);
    CHECK(b.core().players[1].score == 2);
    CHECK(std::fabs(b.core().players[0].hitFlash - 0.85f) < 0.005f);
    for (size_t n = 0; n < w.n; ++n) {
        arcade::Reader cut(w.buf, n);
        CHECK(!b.decode(cut));
        CHECK(b.core().players[0].hitSources == 3 && b.core().players[1].score == 2);
    }
    g.phase = Phase::MatchEnd;
    g.winner = -1;
    g.players[0].score = g.players[1].score = 5;
    arcade::Writer tie;
    a.encode(tie);
    arcade::Reader tr(tie.buf, tie.n);
    CHECK(b.decode(tr) && b.core().isDraw() && b.core().winner == -1);
}

// ---- bitmaps ----

static void testBitmapDecode() {
    // 5x2 image, colour indices: row0 = 1 1 2 2 2, row1 = 0 3 3 3 1 (padded with a 0 nibble to fill the last byte)
    const uint16_t pal[4] = {0x0000, 0xF800, 0x07E0, 0x001F};
    const uint8_t raw[] = {0x11, 0x22, 0x20, 0x33, 0x31, 0x00};
    const uint8_t rle[] = {1, 0x11, 1, 0x22, 1, 0x20, 1, 0x33, 1, 0x31, 1, 0x00};
    const uint8_t rle2[] = {1, 0x11, 1, 0x22, 1, 0x20, 2, 0x33, 1, 0x00};  // 0x33 twice: row1 becomes 0 3 3 3 3 (and a tail)
    struct Run { int x, y, len, c; };

    for (int mode = 0; mode < 2; ++mode) {
        const arcade::Bitmap b = mode == 0 ? arcade::Bitmap{5, 2, pal, 4, raw, sizeof(raw), false}
                                           : arcade::Bitmap{5, 2, pal, 4, rle, sizeof(rle), true};
        Run runs[16];
        int n = 0;
        arcade::forEachRun(b, [&](int x, int y, int len, uint8_t c) { runs[n++] = {x, y, len, c}; });
        CHECK(n == 5);
        CHECK(runs[0].x == 0 && runs[0].y == 0 && runs[0].len == 2 && runs[0].c == 1);
        CHECK(runs[1].x == 2 && runs[1].len == 3 && runs[1].c == 2);
        CHECK(runs[2].y == 1 && runs[2].x == 0 && runs[2].len == 1 && runs[2].c == 0);
        CHECK(runs[3].x == 1 && runs[3].len == 3 && runs[3].c == 3);
        CHECK(runs[4].x == 4 && runs[4].len == 1 && runs[4].c == 1);
    }
    // A repeated RLE pair expands to the same pixels as writing the byte twice.
    const arcade::Bitmap b2 = {4, 2, pal, 4, rle2, sizeof(rle2), true};
    int total = 0;
    arcade::forEachRun(b2, [&](int, int, int len, uint8_t) { total += len; });
    CHECK(total == 8);
    // Truncated data and out-of-palette indices never crash: the missing pixels are colour 0.
    const uint8_t cut[] = {2, 0x77};
    const arcade::Bitmap b3 = {6, 1, pal, 4, cut, sizeof(cut), true};
    int pixels = 0;
    bool ok = true;
    arcade::forEachRun(b3, [&](int, int, int len, uint8_t c) { pixels += len; ok = ok && c == 0; });
    CHECK(pixels == 6 && ok);
}

int main() {
    testThrowhallaEncoding();
    testBitmapDecode();
    testShake();
    testSettingsCodec();
    testArena();
    testGameInstances();
    testPaddleServeAndMove();
    testPaddleScoreAndWin();
    testPaddleBounce();
    testPaddleSolo();
    testPaddleEncoding();
    std::printf(failures ? "%d FAILURES\n" : "ALL PASSED\n", failures);
    return failures ? 1 : 0;
}
