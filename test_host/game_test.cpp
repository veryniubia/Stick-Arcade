// Host-side rule tests for the Throwhalla game core.
// Build: g++ -std=c++17 -I src test_host/game_test.cpp src/games/throwhalla/game.cpp -o test_host/game_test.exe
#include <cmath>
#include <cstdio>
#include <cstdlib>

#include "games/throwhalla/game.h"

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
        g.world.powerups = false;
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
    CHECK(s.g.players[0].score == 1);
    s.g.players[1].itemUsed = true;
    s.run(0.1f);
    CHECK(s.g.phase == Phase::RoundEnd);
    CHECK(s.g.players[0].score == 1 && s.g.players[1].score == 0);
}

static void testJumpHeightDependsOnHold() {
    Sim tap;
    tap.g.begin(GameMode::Solo, 1);
    const float low = tap.jumpApex(0, 0.0f);
    Sim hold;
    hold.g.begin(GameMode::Solo, 1);
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

static void placeHit(Sim& s, int slot, int owner, int victim, bool blast = false) {
    Projectile& p = s.g.proj[slot];
    p = Projectile();
    p.active = true;
    p.type = blast ? ItemType::Bomb : ItemType::Knife;
    p.owner = owner;
    p.life = 1.0f;
    p.x = s.g.players[victim].x;
    p.y = s.g.players[victim].feetY - 8;
    p.exploding = blast;
    p.blastT = cfg::BOMB_BLAST_SEC;
}

static void testBothHit() {
    Sim s;
    s.g.begin(GameMode::Versus, 1);
    placeHit(s, 0, 0, 1);
    placeHit(s, 1, 1, 0);
    s.tick();
    CHECK(s.g.players[0].hit && s.g.players[1].hit);
    CHECK(s.g.players[0].score == 1 && s.g.players[1].score == 1);
}

static void testTwoSources() {
    for (int first = 0; first < 2; ++first) {
        Sim s;
        s.g.begin(GameMode::Versus, 1);
        placeHit(s, 0, first, 0, true);
        s.run(0.2f);
        CHECK(s.g.players[1].score == 1);
        CHECK(s.g.players[0].hitSerial == 1);
        placeHit(s, 1, 1 - first, 0, true);
        s.tick();
        CHECK(s.g.players[1].score == 2);
        CHECK(s.g.players[0].hitSources == 3);
        CHECK(s.g.players[0].hitSerial == 2);
        CHECK(s.g.players[0].hitFlash > 0.99f);
        s.run(0.5f);
        CHECK(s.g.players[1].score == 2); // persistent blasts cannot deal repeated damage
        s.g.players[0].itemUsed = s.g.players[1].itemUsed = true;
        s.run(2.0f);
        CHECK(s.g.players[0].hitSources == 0);
    }
}

static void testImmediateWinAndDraw() {
    for (int draw = 0; draw < 2; ++draw) {
        Sim s;
        s.g.begin(GameMode::Versus, 1);
        s.g.players[0].score = 4;
        s.g.players[1].score = draw ? 4 : 0;
        placeHit(s, 0, 0, 1);
        if (draw) placeHit(s, 1, 1, 0);
        s.tick();
        CHECK(s.g.phase == Phase::RoundEnd);
        CHECK(s.g.players[0].score == 5);
        CHECK(s.g.isDraw() == bool(draw));
        placeHit(s, 2, 1, 0); // later damage must not count
        s.run(cfg::ROUND_END_SEC + 0.1f);
        CHECK(s.g.phase == Phase::MatchEnd);
        CHECK(s.g.winner == (draw ? -1 : 0));
        CHECK(s.g.players[1].score == (draw ? 5 : 0));
    }
}

static void testWeaponCollisions() {
    for (int ta = 1; ta <= 4; ++ta) for (int tb = 1; tb <= 4; ++tb) {
        Sim s;
        s.g.begin(GameMode::Versus, 1);
        Projectile& a = s.g.proj[0];
        Projectile& b = s.g.proj[1];
        a.active = b.active = true;
        a.type = static_cast<ItemType>(ta); b.type = static_cast<ItemType>(tb);
        a.owner = 0; b.owner = 1;
        a.x = 114; b.x = 126; a.y = b.y = 40;
        a.vx = 360; b.vx = -360;
        s.tick();
        CHECK(a.vx < 0 && b.vx > 0);
        CHECK(a.owner == 0 && b.owner == 1);
        CHECK(a.x < b.x);
        CHECK(s.ev & EV_BOUNCE);
        CHECK(ta == 3 ? a.fuseOn && !a.exploding : a.bounces == 1);
        CHECK(tb == 3 ? b.fuseOn && !b.exploding : b.bounces == 1);
        s.tick();
        CHECK(ta == 3 || a.bounces == 1);
        CHECK(tb == 3 || b.bounces == 1);
    }
    Sim s;
    s.g.begin(GameMode::Versus, 1);
    placeHit(s, 0, 0, 1, true);
    Projectile& b = s.g.proj[1];
    b.active = true; b.type = ItemType::Knife; b.x = 194; b.y = 65; b.vx = 100;
    s.tick();
    CHECK(b.vx == 100 && b.bounces == 0); // blasts do not deflect weapons
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

// Plays one round with a hit on each side, from 3:3 to 4:4.
static void reachFourEach(Sim& s) {
    s.g.players[0].score = 3;
    s.g.players[1].score = 3;
    placeHit(s, 0, 0, 1);
    placeHit(s, 1, 1, 0);
    s.g.players[0].itemUsed = s.g.players[1].itemUsed = true;
    s.tick();
    s.run(cfg::ROUND_END_SEC + 0.1f);
}

static void testFirstToFive() {
    Sim s;
    s.g.begin(GameMode::Versus, 3);
    reachFourEach(s);
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

static void testLateDamageIgnored() {
    Sim s;
    s.g.begin(GameMode::Versus, 3);
    reachFourEach(s);
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
    g.begin(GameMode::Solo, 1);
    CHECK(g.matchLoser() == -1);
}

// Solo: the dummy never throws, so the round ends right after the player's throw; rounds go on forever.
static void testSoloRound() {
    Sim s;
    s.g.begin(GameMode::Solo, 3);
    CHECK(s.g.players[1].itemUsed && s.g.players[1].item == ItemType::None);
    s.g.players[0].item = ItemType::Knife;
    s.shoot(0, 45.0f, 0.3f);
    CHECK(s.g.players[0].itemUsed);
    CHECK(s.g.phase == Phase::Playing);  // the knife is still flying
    for (int i = 0; i < 60 * 10 && s.g.phase != Phase::RoundEnd; ++i) s.tick();
    CHECK(s.g.phase == Phase::RoundEnd);
    s.run(cfg::ROUND_END_SEC + 0.1f);
    CHECK(s.g.phase == Phase::Playing);
    CHECK(!s.g.players[0].itemUsed && s.g.players[0].item != ItemType::None);
    CHECK(s.g.players[1].itemUsed && s.g.players[1].item == ItemType::None);  // still a dummy
}

static void testSoloNeverEnds() {
    Sim s;
    s.g.begin(GameMode::Solo, 5);
    s.g.players[0].score = 20;
    for (int round = 0; round < 8; ++round) {
        s.g.players[0].item = ItemType::Knife;
        s.shoot(0, 0.0f, 0.75f);  // straight at the dummy
        s.run(1.0f);
        s.run(cfg::ROUND_END_SEC + 0.1f);
    }
    CHECK(s.g.phase != Phase::MatchEnd && s.g.winner == -1);
    CHECK(s.g.players[0].score > 20);  // hits still score
    CHECK(s.g.matchLoser() == -1);
}

// Versus: a player who never throws forfeits the throw after TURN_TIMEOUT_SEC so the round cannot hang.
static void testTurnTimeout() {
    Sim s;
    s.g.begin(GameMode::Versus, 7);
    s.g.players[0].item = ItemType::Knife;
    s.g.players[1].item = ItemType::Knife;
    s.shoot(0, 0.0f, 0.2f);  // only player 0 throws
    s.run(cfg::TURN_TIMEOUT_SEC - 2.0f);
    CHECK(s.g.phase == Phase::Playing);
    CHECK(!s.g.players[1].itemUsed);
    CHECK(s.g.turnLeft() > 0.0f && s.g.turnLeft() < 3.0f);
    s.run(3.0f);
    CHECK(s.g.players[1].itemUsed);
    s.run(cfg::ROUND_END_SEC + 0.3f);
    CHECK(s.g.phase == Phase::Playing);  // next round started, the clock restarted
    CHECK(s.g.turnLeft() > cfg::TURN_TIMEOUT_SEC - 2.0f);
    CHECK(!s.g.players[1].itemUsed);
}

static void testTurnTimeoutNotInSolo() {
    Sim s;
    s.g.begin(GameMode::Solo, 7);
    s.run(cfg::TURN_TIMEOUT_SEC + 5.0f);
    CHECK(!s.g.players[0].itemUsed);
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
    s.g.begin(GameMode::Solo, 9);
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
    s.g.begin(GameMode::Solo, 9);
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
    s.g.begin(GameMode::Solo, 9);
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
    s.g.begin(GameMode::Solo, 9);
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
    s.g.begin(GameMode::Solo, 21);
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
    s.g.begin(GameMode::Solo, 13);
    s.g.players[0].item = ItemType::Axe;
    s.shoot(0, 10.0f, 0.2f);
    s.run(7.0f);
    for (const auto& p : s.g.proj) CHECK(!p.active);
}

static void testMovingPlatformsStayInRange() {
    Sim s(true, true);
    s.g.begin(GameMode::Solo, 21);
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

static void testAirbornePowerups() {
    bool counts[4] = {}, duplicate = false;
    for (uint32_t seed = 1; seed < 150; ++seed) {
        Game a, b;
        a.begin(GameMode::Versus, seed); b.begin(GameMode::Versus, seed);
        int count = 0;
        for (int i = 0; i < cfg::MAX_POWERUPS; ++i) {
            const auto& p = a.powerups[i];
            CHECK(p.type == b.powerups[i].type && p.x == b.powerups[i].x && p.y == b.powerups[i].y);
            if (p.type == PowerupType::None) continue;
            ++count;
            CHECK(p.x > 65 && p.x < 185 && p.y >= 22 && p.y <= 82);
            for (int j = 0; j < i; ++j) if (a.powerups[j].type == p.type) duplicate = true;
        }
        counts[count] = true;
    }
    CHECK(counts[0] && counts[1] && counts[2] && counts[3] && duplicate);

    for (int type = 1; type <= 4; ++type) {
        Sim s;
        s.g.begin(GameMode::Versus, 1);
        s.g.world.powerups = true;
        Projectile& p = s.g.proj[0];
        p.active = true; p.type = static_cast<ItemType>(type); p.owner = 0;
        p.x = 120; p.y = 45; p.vx = 100; p.life = 1;
        auto collect = [&](PowerupType t) {
            s.g.powerups[0] = {t, p.x, p.y};
            s.tick();
            CHECK(s.g.powerups[0].type == PowerupType::None);
        };
        collect(PowerupType::Burger);
        collect(PowerupType::Burger);
        CHECK(std::fabs(p.scale - 2.56f) < .001f);
        collect(PowerupType::Ice);
        CHECK(std::fabs(p.speedScale - .6f) < .001f);
        collect(PowerupType::Lightning);
        CHECK(std::fabs(p.speedScale - .9f) < .001f);
        const float age = p.life;
        collect(PowerupType::Split);
        Projectile& child = s.g.proj[1];
        CHECK(p.active && child.active && child.type == p.type && child.owner == p.owner);
        CHECK(child.scale == p.scale && child.speedScale == p.speedScale && child.life >= age);
        CHECK(p.vy != child.vy);
        // Independent children can each score, including overlapping persistent explosions.
        for (int i = 0; i < 2; ++i) {
            Projectile& hit = s.g.proj[i];
            hit.x = s.g.players[1].x; hit.y = s.g.players[1].feetY - 8;
            hit.vx = hit.vy = 0;
            hit.scale = 1; hit.bounces = 0;
            if (type == 3) { hit.exploding = true; hit.blastT = cfg::BOMB_BLAST_SEC; }
        }
        s.tick();
        CHECK(s.g.players[0].score == 2 && s.g.players[1].hitSerial == 2);
        s.run(.3f);
        CHECK(s.g.players[0].score == 2);
    }

    Sim s;
    s.g.begin(GameMode::Versus, 2); s.g.world.powerups = true;
    auto& p = s.g.proj[0];
    p.active = true; p.type = ItemType::Knife; p.x = 100; p.y = 30; p.vx = 360;
    s.g.powerups[0] = {PowerupType::Lightning, 105, 30};
    s.g.powerups[1] = {PowerupType::Lightning, 123, 30};
    s.g.powerups[2] = {PowerupType::Ice, 140, 30};
    s.g.update(.06f, s.in);
    CHECK(s.g.powerups[0].type == PowerupType::None && s.g.powerups[1].type == PowerupType::None);
    CHECK(s.g.powerups[2].type == PowerupType::None); // fast travel cannot skip an icon
    CHECK(std::fabs(p.vx - 486) < .01f);
}

int main() {
    testAirbornePowerups();
    testKnifeHit();
    testJumpHeightDependsOnHold();
    testFullJumpDodgesKnife();
    testShortHopDoesNotDodgeKnife();
    testBothHit();
    testTwoSources();
    testImmediateWinAndDraw();
    testWeaponCollisions();
    testRefreshRule();
    testRoundWaitsForBothThrows();
    testFirstToFive();
    testLateDamageIgnored();
    testMatchLoserOnlyWhenDecided();
    testSoloRound();
    testSoloNeverEnds();
    testTurnTimeout();
    testTurnTimeoutNotInSolo();
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
