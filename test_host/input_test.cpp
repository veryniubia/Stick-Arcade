// Host-side tests for the hardware independent input helpers: gyro look, stick mapping and the stick press history.
#include "core/gyro_look.h"
#include "core/heading_look.h"
#include "core/stick_history.h"
#include "core/stick_map.h"
#include <cmath>
#include <cstdio>

static int failures = 0;
#define CHECK(c) do { if (!(c)) { printf("FAIL %d: %s\n", __LINE__, #c); ++failures; } } while (0)
constexpr float tick = 1.f / 60;
constexpr float rad = 57.2957795f;

static void gyroLook() {
    using arcade::GyroLook;
    // Device held upright in landscape: gravity along the device's X axis (up), screen right edge along +Y.
    auto turn = [](GyroLook& l, float ax, float ay, float az, float gx, float gy, float gz, int frames) {
        for (int i = 0; i < frames; ++i) l.update(ax, ay, az, gx, gy, gz, tick);
    };
    GyroLook l;
    arcade::HeadingLook heading;
    heading.sample(179.f, 0, false);
    heading.center(10);
    CHECK(heading.active());
    for (int i=0; i<6000; ++i) heading.sample(179.f, 20+i*10, true);
    CHECK(heading.yaw()==0); // even a falsely detected gyro turn cannot accumulate
    for (int i=0; i<100; ++i) heading.sample(-171.f, 60100+i*10, true);
    CHECK(std::fabs(heading.yaw()-70.f/3)<.01f);
    for (int i=0; i<500; ++i) heading.sample(-171.f-(i+1)*.02f, 61100+i*10, true);
    for (int i=0; i<100; ++i) heading.sample(179.f, 66100+i*10, false);
    CHECK(std::fabs(heading.yaw())<.01f);
    heading.center(68000);
    CHECK(!heading.active()); // stale sensor at calibration uses the gyro fallback
    // Same physical heading after a quick turn and a slow return, even with
    // a nonzero sensor offset. Exercise both directions at phone sample rate.
    for (int sign = -1; sign <= 1; sign += 2) {
        GyroLook slow;
        auto samples = [&](float speed, int count) {
            for (int i = 0; i < count; ++i)
                slow.update(1, 0, 0, 6 + sign * speed, 0, 0, .01f);
        };
        slow.beginCalibration();
        samples(0, 125);
        samples(-10, 100);
        CHECK(std::fabs(slow.yaw() - sign * 70.f / 3) < .01f);
        samples(2, 500);
        CHECK(std::fabs(slow.yaw()) < .01f);
        samples(0, 1000);
        CHECK(std::fabs(slow.yaw()) < .01f);
    }
    l.reset();
    // Holding still: no drift, even with a small constant bias that the estimator absorbs.
    turn(l, 1, 0, 0, 0.8f, -0.6f, 0.5f, 240);
    CHECK(std::fabs(l.yaw()) < 1.0f && std::fabs(l.pitch()) < 1.0f && l.settled());
    // Turning about the vertical axis (here +X is up) turns the view by the gain; clockwise seen from above is right.
    l.recenter();
    turn(l, 1, 0, 0, -9.2f, -0.6f, 0.5f, 60);  // 10 deg/s plus the same sensor offset
    CHECK(std::fabs(l.yaw() - (70.0f / 3) * GyroLook::kYawSign) < 0.1f);
    // The same turn gives the same yaw however the device is tilted: gravity is the reference.
    GyroLook t;
    t.reset();
    turn(t, 0, 0, 1, 0, 0, 0, 120);
    t.recenter();
    turn(t, 0, 0, 1, 0, 0, -10, 60);
    CHECK(std::fabs(t.yaw() - l.yaw()) < 3.0f);
    // Overshooting either stop must preserve the calibrated centre, even at different return speeds.
    GyroLook c;
    c.reset();
    turn(c, 1, 0, 0, 0, 0, 0, 120);
    for (int side = -1; side <= 1; side += 2) {
        turn(c, 1, 0, 0, -15 * side, 0, 0, 60);
        CHECK(std::fabs(c.yaw() - 35 * side) < .01f);
        turn(c, 1, 0, 0, -15 * side, 0, 0, 60);
        CHECK(std::fabs(c.yaw() - 70 * side) < .01f);
        turn(c, 1, 0, 0, -15 * side, 0, 0, 60);
        CHECK(c.yaw() == 70 * side);
        turn(c, 1, 0, 0, 5 * side, 0, 0, 180);
        CHECK(std::fabs(c.yaw() - 70 * side) < .01f);
        turn(c, 1, 0, 0, 30 * side, 0, 0, 60);
        CHECK(std::fabs(c.yaw()) < .01f);
    }
    // Pitch follows the angle at which the screen faces up (gyro about the screen's right edge, tied to gravity) and
    // is limited. Raising the top edge towards the player (the screen turns to face down) looks up.
    GyroLook p;
    p.configurePitch(2.0f, 2.0f);
    p.reset();
    auto tilt = [&](GyroLook& g, float from, float to, int frames) {
        for (int i = 1; i <= frames; ++i) {
            const float s = (from + (to - from) * i / frames) / rad;
            g.update(std::cos(s), 0, std::sin(s), 0, (to - from) / (frames * tick), 0, tick);
        }
    };
    turn(p, 1, 0, 0, 0, 0, 0, 120);
    tilt(p, 0, 10, 60);  // screen turns to face up by 10 degrees
    CHECK(std::fabs(p.pitch() - 20.0f * GyroLook::kPitchSign) < 2.0f && std::fabs(p.yaw()) < 0.5f);
    CHECK(GyroLook::kPitchSign < 0 ? p.pitch() < 0 : p.pitch() > 0);
    tilt(p, 10, 0, 60);
    CHECK(std::fabs(p.pitch()) < 1.0f);
    tilt(p, 0, -10, 60);
    CHECK(GyroLook::kPitchSign < 0 ? p.pitch() > 10 : p.pitch() < -10);
    tilt(p, -10, 60, 120);
    CHECK(std::fabs(p.pitch()) == 30.0f);
    p.recenter();
    CHECK(p.yaw() == 0 && p.pitch() == 0);
    // A gyro that reports a steady turn the device is not making cannot walk the view away: gravity holds it.
    GyroLook d;
    d.configurePitch(2.0f, 2.0f);
    d.reset();
    turn(d, 1, 0, 0, 0, 0, 0, 120);
    turn(d, 1, 0, 0, 0, 2.5f, 0, 1200);  // 20 s of 2.5 deg/s bias on the pitch axis
    CHECK(std::fabs(d.pitch()) < 3.0f);
    // Stationary offset above the legacy automatic bias threshold; a jolt leaves progress intact.
    GyroLook calibrated;
    calibrated.beginCalibration();
    turn(calibrated, 1, 0, 0, 6, 0, 0, 30);
    CHECK(calibrated.calibrationProgress() > .45f);
    turn(calibrated, 1, 0, 0, 65, 0, 0, 1);
    CHECK(calibrated.calibrationProgress() > .5f);
    turn(calibrated, 1, 0, 0, 6, 0, 0, 65);
    CHECK(calibrated.calibrationProgress() == 1 && calibrated.settled());
    turn(calibrated, 1, 0, 0, 6, 0, 0, 1800);
    CHECK(std::fabs(calibrated.yaw()) < .05f);
    // A post-calibration bias change used to accumulate into a leftward turn.
    turn(calibrated, 1, 0, 0, 7, 0, 0, 1800);
    CHECK(calibrated.yaw() == 0);
    turn(calibrated, 1, 0, 0, -8, 0, 0, 60);  // turn right 15 degrees
    const float held = calibrated.yaw();
    CHECK(std::fabs(held - 35) < .01f);
    turn(calibrated, 1, 0, 0, 6, 0, 0, 1800);  // another bias change off-centre
    CHECK(calibrated.yaw() == held);
    turn(calibrated, 1, 0, 0, 11, 0, 0, 180);  // turn back at 5 deg/s
    CHECK(std::fabs(calibrated.yaw()) < .01f);
    // Natural tremor repeatedly crosses the old per-frame rejection thresholds.
    GyroLook handheld;
    handheld.beginCalibration();
    for (int i = 0; i < 65; ++i) {
        const float wobble = std::sin(i * .9f);
        handheld.update(1, .05f * wobble, .03f * wobble, 6 + 8 * wobble, 9 * wobble, 0, tick);
    }
    CHECK(handheld.calibrationProgress() == 1);
    turn(handheld, 1, 0, 0, 6, -12, 0, 120);
    const float level = handheld.pitch();
    turn(handheld, 1, 0, 0, 6, -12, 0, 3600);
    CHECK(std::fabs(level) < 1 && std::fabs(handheld.pitch() - level) < .01f);
    // Absurd time steps are ignored.
    GyroLook s;
    s.reset();
    s.update(1, 0, 0, 500, 500, 500, 5.0f);
    s.update(1, 0, 0, 500, 500, 500, 0.0f);
    CHECK(s.yaw() == 0 && s.pitch() == 0);
}

static void stickMap() {
    arcade::StickAxis a;
    a.seed(128);
    CHECK(a.map(128) == 0 && a.map(135) == 0 && a.map(121) == 0);
    CHECK(a.map(128 + 96) == 100 && a.map(128 - 96) == -100);
    CHECK(a.map(128 + 60) > 0 && a.map(128 + 60) < 100 && a.map(128 - 60) == -a.map(128 + 60));
    // Travel further than assumed is learned, never above full scale.
    CHECK(a.map(255) == 100 && a.range <= 127.0f);
    CHECK(a.map(128 + 96) < 100);
    // A different rest value is honoured.
    arcade::StickAxis b;
    b.seed(120);
    CHECK(b.map(120) == 0 && b.map(215) > 90);
}

static void stickHistory() {
    arcade::StickHistory h;
    int8_t x = 55, y = 55;
    // Nothing recorded: centred.
    h.latch(1000, x, y);
    CHECK(x == 0 && y == 0);
    // The stick is pushed forward 150 ms before the press and keeps being pushed; the press itself nudges it
    // sideways (those samples are not recorded by the driver, but even if they were the look-back skips them).
    for (uint32_t t = 0; t < 1000; t += 16) h.push(t, 0, t < 850 ? 0 : 100);
    h.latch(1000, x, y);
    CHECK(x == 0 && y == 100);
    // A flick that started less than the look-back before the press: the position from before it is used.
    arcade::StickHistory g;
    for (uint32_t t = 0; t < 1000; t += 16) g.push(t, t < 940 ? 0 : 80, t < 940 ? 0 : 20);
    g.latch(1000, x, y);
    CHECK(x == 0 && y == 0);
    // Samples that are too old do not count.
    arcade::StickHistory o;
    for (uint32_t t = 0; t < 200; t += 16) o.push(t, 70, 70);
    o.latch(5000, x, y);
    CHECK(x == 0 && y == 0);
    // The oldest recent sample is used when the history is shorter than the look-back.
    arcade::StickHistory s;
    s.push(980, -60, 30);
    s.push(996, -62, 31);
    s.latch(1000, x, y);
    CHECK(x == -60 && y == 30);
    // clear() forgets.
    s.clear();
    s.latch(1000, x, y);
    CHECK(x == 0 && y == 0);
    // Wrapping the ring buffer keeps the newest samples.
    arcade::StickHistory w;
    for (uint32_t t = 0; t < 3000; t += 16) w.push(t, 0, static_cast<int8_t>((t / 16) % 100));
    w.latch(3000, x, y);
    CHECK(y == static_cast<int8_t>(((3000 - 112) / 16) % 100) || y == static_cast<int8_t>(((3000 - 96) / 16) % 100) ||
          y == static_cast<int8_t>(((3000 - 128) / 16) % 100));
}

int main() {
    gyroLook();
    stickMap();
    stickHistory();
    puts(failures ? "FAILURES" : "INPUT ALL PASSED");
    return failures ? 1 : 0;
}
