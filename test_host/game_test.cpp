// Host-side rule tests for the game core.
// Build: g++ -std=c++17 -I src test_host/game_test.cpp src/game.cpp -o test_host/game_test.exe
#include <cmath>
#include <cstdio>
#include <cstdlib>

#include "game.h"

static int failures = 0;

#define CHECK(cond)                                                      \
    do {                                                                 \
        if (!(cond)) {                                                   \
            std::printf("FAIL line %d: %s\n", __LINE__, #cond);          \
            ++failures;                                                  \
        }                                                                \
    } while (0)

constexpr float DT = 1.0f / 60.0f;

// By default the world is frozen and has no obstacles so shots are deterministic.
struct Sim {
    Game g;
    PlayerInput in[2];
    uint32_t ev = 0;

    explicit Sim(bool obstacles = false, bool moving = false) {
        g.world.obstacles = obstacles;
        g.world.movePlatforms = moving;
    }
    void tick() {
        ev |= g.update(DT, in);
        in[0].jump = false;
        in[1].jump = false;
    }
    void run(float sec) {
        for (int i = 0; i < static_cast<int>(sec / DT); ++i) tick();
    }
    // Hold fire for `sec` seconds, then release (fires on the release tick).
    void shoot(int idx, float aim, float sec) {
        in[idx].aimDeg = aim;
        in[idx].charge = true;
        run(sec);
        in[idx].charge = false;
        tick();
    }
    // Press jump and keep it held for `holdSec`, then release.
    float jumpApex(int idx, float holdSec) {
        in[idx].jump = true;
        in[idx].jumpHeld = true;
        float apex = 0;
        const int holdTicks = static_cast<int>(holdSec / DT);
        for (int i = 0; i < 120; ++i) {
            if (i >= holdTicks) in[idx].jumpHeld = false;
            tick();
            if (g.players[idx].h > apex) apex = g.players[idx].h;
        }
        return apex;
    }
};

static void testKnifeHit() {
    Sim s;
    s.g.begin(GameMode::Versus, 1);
    s.g.players[0].item = ItemType::Knife;
    s.shoot(0, 0.0f, 0.75f);
    CHECK(s.g.players[0].itemUsed);
    s.run(1.0f);
    CHECK(s.g.players[1].hit);
    CHECK(!s.g.players[0].hit);
    // The round only ends once the opponent has thrown as well.
    CHECK(s.g.phase == Phase::Playing);
    CHECK(s.g.players[0].score == 0);
    s.g.players[1].itemUsed = true;
    s.run(0.1f);
    CHECK(s.g.phase == Phase::RoundEnd);
    CHECK(s.g.players[0].score == 1 && s.g.players[1].score == 0);
}

static void testJumpHeightDependsOnHold() {
    Sim tap;
    tap.g.begin(GameMode::Training, 1);
    const float low = tap.jumpApex(0, 0.0f);
    Sim hold;
    hold.g.begin(GameMode::Training, 1);
    const float high = hold.jumpApex(0, 1.0f);
    std::printf("jump apex tap=%.1f hold=%.1f\n", low, high);
    CHECK(low > 5.0f && low < 11.0f);
    CHECK(high > 35.0f);
}

static void testFullJumpDodgesKnife() {
    Sim s;
    s.g.begin(GameMode::Versus, 1);
    s.g.players[0].item = ItemType::Knife;
    s.shoot(0, 0.0f, 0.75f);
    s.run(0.1f);
    s.in[1].jump = true;
    s.in[1].jumpHeld = true;
    s.run(0.6f);
    CHECK(!s.g.players[1].hit);
}

static void testShortHopDoesNotDodgeKnife() {
    Sim s;
    s.g.begin(GameMode::Versus, 1);
    s.g.players[0].item = ItemType::Knife;
    s.shoot(0, 0.0f, 0.75f);
    s.run(0.1f);
    s.in[1].jump = true;
    s.in[1].jumpHeld = false;
    s.run(0.6f);
    CHECK(s.g.players[1].hit);
}

static void testBothHit() {
    Sim s;
    s.g.begin(GameMode::Versus, 1);
    s.g.players[0].item = ItemType::Knife;
    s.g.players[1].item = ItemType::Knife;
    s.in[0].charge = true;
    s.in[1].charge = true;
    s.run(0.75f);
    s.in[0].charge = false;
    s.in[1].charge = false;
    s.run(1.0f);
    CHECK(s.g.players[0].hit && s.g.players[1].hit);
    CHECK(s.g.players[0].score == 1 && s.g.players[1].score == 1);
}

static void testRefreshRule() {
    Sim s;
    s.g.begin(GameMode::Versus, 7);
    s.g.players[0].item = ItemType::Knife;
    s.g.players[1].item = ItemType::Knife;
    s.shoot(0, 60.0f, 0.4f);
    CHECK(s.g.players[0].itemUsed && !s.g.players[1].itemUsed);
    s.run(0.5f);
    s.in[0].charge = true;
    s.run(0.3f);
    CHECK(!s.g.players[0].charging);  // cannot throw a second item in the same round
    s.in[0].charge = false;
    s.shoot(1, 60.0f, 0.4f);
    CHECK(s.g.players[0].itemUsed && s.g.players[1].itemUsed);
    CHECK(!(s.ev & EV_REFRESH));  // items stay as they are until the round is over
    s.run(7.0f);                  // everything flying disappears, then the next round starts
    CHECK(s.ev & EV_REFRESH);
    CHECK(s.g.phase == Phase::Playing);
    CHECK(!s.g.players[0].itemUsed && !s.g.players[1].itemUsed);
    CHECK(s.g.players[0].item != ItemType::None && s.g.players[1].item != ItemType::None);
}

static void testRoundWaitsForBothThrows() {
    // The hit player still throws later in the same round and may hit back: one point each, no more.
    Sim s;
    s.g.begin(GameMode::Versus, 1);
    s.g.players[0].item = ItemType::Knife;
    s.g.players[1].item = ItemType::Knife;
    s.shoot(0, 0.0f, 0.75f);
    s.run(0.8f);
    CHECK(s.g.players[1].hit && !s.g.players[0].hit);
    CHECK(s.g.phase == Phase::Playing);
    s.shoot(1, 0.0f, 0.75f);
    s.run(1.0f);
    CHECK(s.g.players[0].hit);
    s.run(cfg::ROUND_END_SEC + 0.1f);
    CHECK(s.g.players[0].score == 1 && s.g.players[1].score == 1);
    CHECK(s.g.phase == Phase::Playing);
    CHECK(!s.g.players[0].hit && !s.g.players[1].hit);
}

// Plays one round where both knives hit, from 3:3 to 4:4, so the next round is sudden death.
static void reachSuddenDeath(Sim& s) {
    s.g.players[0].score = 3;
    s.g.players[1].score = 3;
    s.g.players[0].item = ItemType::Knife;
    s.g.players[1].item = ItemType::Knife;
    s.in[0].charge = true;
    s.in[1].charge = true;
    s.run(0.75f);
    s.in[0].charge = false;
    s.in[1].charge = false;
    s.run(1.0f);
    s.run(cfg::ROUND_END_SEC + 0.1f);
}

static void testSuddenDeath() {
    Sim s;
    s.g.begin(GameMode::Versus, 3);
    reachSuddenDeath(s);
    CHECK(s.g.players[0].score == 4 && s.g.players[1].score == 4);
    CHECK(s.g.phase == Phase::Playing);

    // The first damage decides at once, without waiting for the opponent's throw.
    s.g.players[0].item = ItemType::Knife;
    s.shoot(0, 0.0f, 0.75f);
    s.run(0.8f);
    CHECK(s.g.players[1].hit);
    CHECK(s.g.phase == Phase::RoundEnd);
    CHECK(s.g.players[0].score == 5 && s.g.players[1].score == 4);
    CHECK(s.g.matchLoser() == 1);
    s.run(cfg::ROUND_END_SEC + 0.1f);
    CHECK(s.g.phase == Phase::MatchEnd && s.g.winner == 0);
    CHECK(s.g.matchLoser() == 1);
}

static void testSuddenDeathLateDamageIgnored() {
    Sim s;
    s.g.begin(GameMode::Versus, 3);
    reachSuddenDeath(s);
    s.g.players[0].item = ItemType::Knife;
    s.shoot(0, 0.0f, 0.75f);
    s.run(0.8f);
    CHECK(s.g.phase == Phase::RoundEnd);
    // A throw that would have hit back after the decision changes nothing.
    s.g.players[1].item = ItemType::Knife;
    s.shoot(1, 0.0f, 0.75f);
    s.run(1.0f);
    CHECK(s.g.players[0].score == 5 && s.g.players[1].score == 4);
    CHECK(!s.g.players[0].hit);
}

static void testMatchLoserOnlyWhenDecided() {
    Game g;
    g.begin(GameMode::Versus, 1);
    CHECK(g.matchLoser() == -1);
    g.begin(GameMode::Training, 1);
    CHECK(g.matchLoser() == -1);
}

static void testTrainingRefresh() {
    Sim s;
    s.g.begin(GameMode::Training, 3);
    const ItemType first = s.g.players[0].item;
    s.shoot(0, 45.0f, 0.3f);
    CHECK(!s.g.players[0].itemUsed);
    CHECK(s.g.players[0].item != first);
}

static void testMatchEnd() {
    Sim s;
    s.g.begin(GameMode::Versus, 5);
    for (int round = 0; round < 5; ++round) {
        s.g.players[0].item = ItemType::Knife;
        s.g.players[0].itemUsed = false;
        s.g.players[1].itemUsed = true;  // the opponent has already thrown
        s.shoot(0, 0.0f, 0.75f);
        s.run(1.0f);
        s.run(cfg::ROUND_END_SEC + 0.1f);
    }
    CHECK(s.g.phase == Phase::MatchEnd);
    CHECK(s.g.winner == 0);
    CHECK(s.g.players[0].score == 5);
    CHECK(s.ev & EV_WIN);
}

static void testKnifeBouncesOffObstacle() {
    Sim s(true);
    s.g.begin(GameMode::Versus, 1);
    for (int i = 0; i < cfg::OBSTACLE_COUNT; ++i) s.g.obstacleXs[i] = 230.0f;  // out of the way
    s.g.obstacleXs[0] = 110.0f;
    s.g.players[0].item = ItemType::Knife;
    // hand height is feet - HAND_DY; the obstacle row sits at OBSTACLE_CENTER_Y, so aim at it
    const float handY = cfg::PLATFORM_CENTER_Y - cfg::HAND_DY;
    const float dy = handY - cfg::OBSTACLE_CENTER_Y;
    const float dx = 110.0f - (cfg::PLAYER_X[0] + cfg::HAND_DX);
    s.shoot(0, std::atan2(dy, dx) * 57.2957795f, 0.75f);
    s.run(1.0f);
    CHECK(s.ev & EV_BOUNCE);
    CHECK(!s.g.players[1].hit);
}

static void testBombFallsIntoLava() {
    Sim s;
    s.g.begin(GameMode::Versus, 9);
    s.g.players[0].item = ItemType::Bomb;
    s.shoot(0, -20.0f, 0.1f);  // thrown downward, never reaches a wall
    s.run(3.0f);
    CHECK(s.ev & EV_EXPLODE);
    for (const auto& p : s.g.proj) CHECK(!p.active);
}

static void testBombFuseAfterBounce() {
    Sim s;
    s.g.begin(GameMode::Versus, 9);
    s.g.players[0].item = ItemType::Bomb;
    s.shoot(0, 45.0f, 0.75f);
    s.run(8.0f);
    CHECK(s.ev & EV_BOUNCE);
    CHECK(s.ev & EV_EXPLODE);
    for (const auto& p : s.g.proj) CHECK(!p.active);
}

static void testBombDoesNotExplodeOnContact() {
    Sim s;
    s.g.begin(GameMode::Versus, 9);
    Projectile& b = s.g.proj[0];
    b.active = true;
    b.type = ItemType::Bomb;
    b.owner = 0;
    b.x = cfg::PLAYER_X[1] - 12.0f;
    b.y = s.g.players[1].feetY - 8.0f;
    b.vx = 120.0f;
    b.life = 1.0f;
    s.run(0.3f);
    CHECK(!(s.ev & EV_EXPLODE));
    CHECK(!s.g.players[1].hit);
    CHECK(b.vx < 0.0f);  // it bounced off the player
    CHECK(b.fuseOn);
}

static void testBombBlastIsSlow() {
    Sim s;
    s.g.begin(GameMode::Versus, 9);
    Projectile& b = s.g.proj[0];
    b.active = true;
    b.type = ItemType::Bomb;
    b.owner = 0;
    b.x = 120.0f;
    b.y = 30.0f;
    b.fuseOn = true;
    b.fuse = 0.02f;
    b.life = 1.0f;
    int blastTicks = 0;
    float r0 = -1.0f, r1 = -1.0f;
    for (int i = 0; i < 120; ++i) {
        s.tick();
        if (b.active && b.exploding) {
            ++blastTicks;
            if (blastTicks == 3) r0 = Game::blastRadius(b);
            if (blastTicks == 20) r1 = Game::blastRadius(b);
        }
    }
    std::printf("blast ticks=%d r(3)=%.1f r(20)=%.1f\n", blastTicks, r0, r1);
    CHECK(blastTicks >= 38);  // about 0.7 s
    CHECK(r0 > 0.0f && r0 < r1);
    CHECK(r0 < 0.5f * cfg::BOMB_BLAST_R);
}

static void testBombMinPowerReachesTwoThirds() {
    Sim s;
    s.g.begin(GameMode::Training, 9);
    s.g.players[0].item = ItemType::Bomb;
    s.shoot(0, 45.0f, 0.02f);
    float x = -1;
    for (int i = 0; i < 600 && x < 0; ++i) {
        s.tick();
        for (const auto& p : s.g.proj) {
            if (p.active && p.exploding) x = p.x;
        }
    }
    std::printf("bomb min power explodes at x=%.1f (2/3 of the screen is 160)\n", x);
    CHECK(x > 140.0f && x < 180.0f);
}

static void testBombMaxPowerBouncesBackToCenter() {
    Sim s;
    s.g.begin(GameMode::Training, 9);
    s.g.players[0].item = ItemType::Bomb;
    s.shoot(0, 30.0f, 0.75f);
    bool hitRightWall = false;
    float x = -1;
    for (int i = 0; i < 600 && x < 0; ++i) {
        s.tick();
        for (const auto& p : s.g.proj) {
            if (!p.active) continue;
            if (p.x > cfg::SCREEN_W - 6.0f) hitRightWall = true;
            if (p.exploding) x = p.x;
        }
    }
    std::printf("bomb max power: touched right wall=%d, explodes at x=%.1f\n", hitRightWall, x);
    CHECK(hitRightWall);
    CHECK(x > 80.0f && x < 160.0f);
}

static void testAimBackward() {
    Sim s;
    s.g.begin(GameMode::Training, 9);
    s.g.players[0].item = ItemType::Knife;
    s.in[0].aimDeg = 180.0f;
    s.run(0.1f);
    CHECK(s.g.players[0].face == -1);
    CHECK(Game::handX(s.g.players[0]) < s.g.players[0].x);  // the hand moves to the back
    s.shoot(0, 180.0f, 0.2f);
    bool movedBack = false;
    for (const auto& p : s.g.proj) movedBack = movedBack || (p.active && p.vx < 0);
    CHECK(movedBack);
}

static void testBackwardKnifeComesBackAndHitsThrower() {
    Sim s;
    s.g.begin(GameMode::Versus, 9);
    s.g.players[1].itemUsed = true;  // the round ends once both have thrown
    s.g.players[0].item = ItemType::Knife;
    s.shoot(0, 180.0f, 0.2f);
    s.run(1.5f);
    CHECK(s.g.players[1].score == 1);  // it bounced off the back wall and hit its own thrower
}

static void testFaceHysteresis() {
    Sim s;
    s.g.begin(GameMode::Training, 9);
    s.in[0].aimDeg = 100.0f;
    s.tick();
    CHECK(s.g.players[0].face == -1);
    s.in[0].aimDeg = 90.0f;
    s.tick();
    CHECK(s.g.players[0].face == -1);  // inside the hysteresis band it does not flicker
    s.in[0].aimDeg = 80.0f;
    s.tick();
    CHECK(s.g.players[0].face == 1);
}

static void testObstacleRange() {
    Sim s(true, true);
    s.g.begin(GameMode::Training, 21);
    float lo = 1e9f, hi = -1e9f;
    for (int i = 0; i < 60 * 12; ++i) {
        s.tick();
        for (int k = 0; k < cfg::OBSTACLE_COUNT; ++k) {
            lo = std::fmin(lo, s.g.obstacleY(k));
            hi = std::fmax(hi, s.g.obstacleY(k));
        }
    }
    CHECK(lo >= cfg::OBSTACLE_CENTER_Y - cfg::OBSTACLE_AMP - 0.01f);
    CHECK(hi <= cfg::OBSTACLE_CENTER_Y + cfg::OBSTACLE_AMP + 0.01f);
    CHECK(lo < 12.0f);                                   // reaches close to the ceiling
    CHECK(hi + cfg::OBSTACLE_R < cfg::LAVA_Y - 2.0f);    // and stays above the lava
}

static void testBombSelfBlast() {
    Sim s;
    s.g.begin(GameMode::Versus, 9);
    s.g.players[0].itemUsed = true;
    s.g.players[1].itemUsed = true;
    Projectile& b = s.g.proj[0];
    b.active = true;
    b.type = ItemType::Bomb;
    b.owner = 0;
    b.x = cfg::PLAYER_X[0] + 8.0f;
    b.y = s.g.players[0].feetY - 5.0f;
    b.fuseOn = true;
    b.fuse = 0.05f;
    b.life = 1.0f;
    s.run(2.0f);
    CHECK(s.ev & EV_EXPLODE);
    CHECK(s.g.players[1].score == 1);
    CHECK(s.g.players[0].score == 0);
}

static void testBoomerangReturns() {
    Sim s;
    s.g.begin(GameMode::Versus, 11);
    s.g.players[1].itemUsed = true;
    s.g.players[0].item = ItemType::Boomerang;
    s.shoot(0, 20.0f, 0.05f);  // minimum power: it must not reach the opponent
    s.run(8.0f);
    CHECK(s.g.players[0].score == 0);
    CHECK(s.g.players[1].score == 1);  // the returning boomerang hit its own thrower
}

static void testAxeDisappears() {
    Sim s(true, true);
    s.g.begin(GameMode::Training, 13);
    s.g.players[0].item = ItemType::Axe;
    s.shoot(0, 10.0f, 0.2f);
    s.run(7.0f);
    for (const auto& p : s.g.proj) CHECK(!p.active);
}

static void testMovingPlatformsStayInRange() {
    Sim s(true, true);
    s.g.begin(GameMode::Training, 21);
    float lo = 1e9f, hi = -1e9f;
    for (int i = 0; i < 600; ++i) {
        s.tick();
        for (int p = 0; p < 2; ++p) {
            lo = std::fmin(lo, s.g.platformY(p));
            hi = std::fmax(hi, s.g.platformY(p));
        }
    }
    CHECK(lo >= cfg::PLATFORM_CENTER_Y - cfg::MOTION_AMP - 0.01f);
    CHECK(hi <= cfg::PLATFORM_CENTER_Y + cfg::MOTION_AMP + 0.01f);
    CHECK(hi - lo > cfg::MOTION_AMP);  // it actually moves
}

int main() {
    testKnifeHit();
    testJumpHeightDependsOnHold();
    testFullJumpDodgesKnife();
    testShortHopDoesNotDodgeKnife();
    testBothHit();
    testRefreshRule();
    testRoundWaitsForBothThrows();
    testSuddenDeath();
    testSuddenDeathLateDamageIgnored();
    testMatchLoserOnlyWhenDecided();
    testTrainingRefresh();
    testMatchEnd();
    testKnifeBouncesOffObstacle();
    testBombFallsIntoLava();
    testBombFuseAfterBounce();
    testBombMinPowerReachesTwoThirds();
    testBombMaxPowerBouncesBackToCenter();
    testAimBackward();
    testBackwardKnifeComesBackAndHitsThrower();
    testFaceHysteresis();
    testObstacleRange();
    testBombDoesNotExplodeOnContact();
    testBombBlastIsSlow();
    testBombSelfBlast();
    testBoomerangReturns();
    testAxeDisappears();
    testMovingPlatformsStayInRange();
    std::printf(failures ? "%d FAILURES\n" : "ALL PASSED\n", failures);
    return failures ? 1 : 0;
}
