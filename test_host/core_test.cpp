// Host-side tests for the platform pieces that need no hardware: shake detector, settings format, arena,
// game creation and Throwhalla encoding.
// Build: g++ -std=c++17 -I src test_host/core_test.cpp src/core/game_instance.cpp
//        src/games/throwhalla/throwhalla.cpp src/games/throwhalla/game.cpp -o test_host/core_test.exe
#include <cmath>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <limits>

#include "core/bitmap.h"
#include "core/settings_data.h"
#include "core/shake.h"
#include "core/portrait_tilt.h"
#include "games/throwhalla/throwhalla.h"

// The tests never draw: the view halves of the games are not linked.
void ThrowhallaGame::draw(int, const arcade::DrawCtx&) {}
void ThrowhallaGame::sfx(uint32_t) {}

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
    // Captured format-1 layout, independent of the current encoder.
    const uint8_t oldBlob[] = {1,4,1,3,0x85,0xff,0,0xbe};
    settings::Data old;
    CHECK(settings::decode(oldBlob, sizeof(oldBlob), old));
    CHECK(old.volume==4 && old.bright==1 && old.aim==3 && old.tiltOffsetX10==-123);
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
    for (const arcade::GameInfo* info : {&kThrowhallaInfo}) {
        arcade::GameInstance gi = arcade::gameCreate(*info);
        CHECK(gi.game != nullptr);
        gi.game->begin(arcade::Mode::Solo, 5);
        PadInput in[2];
        for (int i = 0; i < 600; ++i) gi.game->step(1.0f / 60.0f, in);
        arcade::gameDestroy(gi);
        CHECK(gi.game == nullptr && gi.block == nullptr);
    }
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
        p.scale = 2.56f;
        p.speedScale = 0.9f;
        p.hitPlayers = 2;
    }
    g.powerups[0] = {PowerupType::Burger, 80, 30};
    g.powerups[1] = {PowerupType::Split, 120, 55};
    g.powerups[2] = {PowerupType::Ice, 160, 75};
    arcade::Writer w;
    a.encode(w);
    CHECK(w.n + 21 <= arcade::kMaxPacket);
    arcade::Reader r(w.buf, w.n);
    CHECK(b.decode(r));
    CHECK(b.core().players[0].hitSources == 3 && b.core().players[0].hitSerial == 2);
    CHECK(b.core().players[1].score == 2);
    CHECK(std::fabs(b.core().players[0].hitFlash - 0.85f) < 0.005f);
    CHECK(r.pos == w.n);
    for (const auto& p : b.core().proj) {
        CHECK(std::fabs(p.scale - 2.56f) < .011f && std::fabs(p.speedScale - .9f) < .011f);
        CHECK(p.hitPlayers == 2);
    }
    for (int i = 0; i < cfg::MAX_POWERUPS; ++i) {
        CHECK(b.core().powerups[i].type == g.powerups[i].type);
        CHECK(b.core().powerups[i].x == g.powerups[i].x && b.core().powerups[i].y == g.powerups[i].y);
    }
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

// Same screen rotation must have the same reference regardless of starting pose or fore/aft pitch.
static void testPortraitTilt() {
    constexpr float radians = .01745329252f;
    auto sample = [&](arcade::PortraitTilt& t, float roll, float pitch, float gravity = 1.f) {
        t.update(std::sin(roll*radians)*std::cos(pitch*radians)*gravity,
                 std::cos(roll*radians)*std::cos(pitch*radians)*gravity,
                 std::sin(pitch*radians)*gravity);
    };
    auto error = [&](float a, float b) { return std::fabs(std::remainder(a-b,360.f)); };
    for (float initial : {-160.f,-30.f,0.f,65.f,170.f}) {
        for (float pitch : {-65.f,-30.f,0.f,30.f,65.f}) {
            arcade::PortraitTilt t;
            sample(t,initial,pitch);
            CHECK(error(t.angleDeg(),initial)<.001f); // first pose is measured, never subtracted as zero
            for (int n=0;n<50;++n) sample(t,15,pitch,.9f);
            CHECK(error(t.angleDeg(),15)<.16f);
            for (int n=0;n<50;++n) sample(t,-22.5f,-pitch,1.1f);
            CHECK(error(t.angleDeg(),-22.5f)<.16f);
        }
    }
    arcade::PortraitTilt t;
    sample(t,179,0); sample(t,-179,0);
    CHECK(std::fabs(t.angleDeg())>179); // circular filtering must not sweep through zero
    for(int n=0;n<50;++n)sample(t,-179,0);
    CHECK(error(t.angleDeg(),-179)<.16f);
    float before=t.angleDeg();
    for(int n=0;n<30;++n)sample(t,45,89);
    CHECK(t.angleDeg()==before); // nearly horizontal: hold the last reliable heading
    sample(t,45,73); CHECK(t.angleDeg()==before); // hysteresis: not enough projection to resume
    sample(t,45,60); CHECK(error(t.angleDeg(),45)<.001f);
    before=t.angleDeg();
    t.update(0,0,0);t.update(0,0,1);
    t.update(std::numeric_limits<float>::quiet_NaN(),0,1);
    t.update(0,1,std::numeric_limits<float>::infinity());
    t.update(2,0,0);CHECK(t.angleDeg()==before); // shaking/invalid samples cannot redefine the angle
    t.reset();t.update(0,0,1);CHECK(t.angleDeg()==0);
    sample(t,25,35);CHECK(error(t.angleDeg(),25)<.001f);
}

int main() {
    testPortraitTilt();
    testThrowhallaEncoding();
    testBitmapDecode();
    testShake();
    testSettingsCodec();
    testArena();
    testGameInstances();
    std::printf(failures ? "%d FAILURES\n" : "ALL PASSED\n", failures);
    return failures ? 1 : 0;
}
