// Host-side tests for the online session (pairing, game filtering, sync, loss, disconnects), run against Throwhalla.
// Build: g++ -std=c++17 -I src test_host/net_test.cpp src/net/net_session.cpp src/core/game_instance.cpp
//        src/games/throwhalla/throwhalla.cpp src/games/throwhalla/game.cpp
//        -o test_host/net_test.exe
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <vector>

// The tests poke at session internals to script the game; production code keeps them private.
#define private public
#include "net/net_session.h"
#undef private

#include "games/bumperduel/bumperduel.h"
#include "games/throwhalla/throwhalla.h"
#include "games/samuraiduel/samuraiduel.h"
#include "games/middiff/middiff.h"

// The tests never draw: the view half of the games is not linked.
void ThrowhallaGame::draw(int, const arcade::DrawCtx&) {}
void ThrowhallaGame::sfx(uint32_t) {}

void BumperDuelGame::draw(int, const arcade::DrawCtx&) {}
void BumperDuelGame::sfx(uint32_t) {}
void SamuraiDuelGame::draw(int, const arcade::DrawCtx&) {}
void SamuraiDuelGame::sfx(uint32_t) {}
void MidDiffGame::draw(int, const arcade::DrawCtx&) {}
void MidDiffGame::sfx(uint32_t) {}

static int failures = 0;

#define CHECK(cond)                                                      \
    do {                                                                 \
        if (!(cond)) {                                                   \
            std::printf("FAIL line %d: %s\n", __LINE__, #cond);          \
            ++failures;                                                  \
        }                                                                \
    } while (0)

using net::Status;

struct Link;

struct Endpoint : net::Transport {
    Link* link = nullptr;
    int id = 0;
    uint8_t mac[6] = {};
    net::Session s;
    arcade::GameInstance gi;
    net::LocalInput in;
    uint32_t nextFrame = 0;
    int frameParity = 0;
    uint32_t events = 0;      // OR of everything returned by update()
    int fireEvents = 0;       // frames whose events contained EV_FIRE
    bool dead = false;        // stops calling update (device turned off)

    ~Endpoint() { arcade::gameDestroy(gi); }
    Game& g() { return static_cast<ThrowhallaGame*>(gi.game)->core(); }
    void pressB() { ++in.pad.bCount; }
    void send(const uint8_t* mac, const uint8_t* data, size_t len) override;
};

struct Packet {
    uint32_t at;
    int to;
    int from;
    std::vector<uint8_t> data;
};

struct Link {
    uint32_t now = 0;
    float loss = 0.0f;
    int latency = 0;
    int jitter = 0;
    bool cut = false;
    uint32_t rng = 12345;
    size_t maxLen = 0;
    std::vector<Packet> q;
    std::vector<std::unique_ptr<Endpoint>> eps;

    uint32_t rand() {
        rng ^= rng << 13;
        rng ^= rng >> 17;
        rng ^= rng << 5;
        return rng;
    }
    float frand() { return (rand() % 10000) / 10000.0f; }

    Endpoint& add(uint32_t nonce, uint32_t startMs = 0, const arcade::GameInfo* info = &kThrowhallaInfo) {
        auto ep = std::make_unique<Endpoint>();
        ep->link = this;
        ep->id = static_cast<int>(eps.size());
        for (int i = 0; i < 6; ++i) ep->mac[i] = static_cast<uint8_t>(0x10 * (ep->id + 1) + i);
        ep->gi = arcade::gameCreate(*info);
        ep->s.begin(ep.get(), nonce, now, *info, ep->gi.game);
        ep->nextFrame = now + startMs;
        eps.push_back(std::move(ep));
        return *eps.back();
    }

    void push(int from, const uint8_t* dst, const uint8_t* data, size_t len) {
        if (len > maxLen) maxLen = len;
        for (auto& e : eps) {
            if (e->id == from) continue;
            if (dst && std::memcmp(dst, e->mac, 6) != 0) continue;
            if (cut || frand() < loss) continue;
            Packet p;
            p.at = now + latency + (jitter ? rand() % (jitter + 1) : 0);
            p.to = e->id;
            p.from = from;
            p.data.assign(data, data + len);
            q.push_back(std::move(p));
        }
    }

    void deliver() {
        for (bool any = true; any;) {
            any = false;
            for (size_t i = 0; i < q.size();) {
                if (q[i].at <= now) {
                    Packet p = std::move(q[i]);
                    q.erase(q.begin() + i);
                    eps[p.to]->s.onReceive(eps[p.from]->mac, p.data.data(), p.data.size(), now);
                    any = true;
                } else {
                    ++i;
                }
            }
        }
    }

    void tick() {
        ++now;
        for (auto& e : eps) {
            if (e->dead || now < e->nextFrame) continue;
            e->nextFrame = now + 16 + (e->frameParity++ & 1);
            const uint32_t ev = e->s.update(now, e->in);
            e->events |= ev;
            if (ev & EV_FIRE) ++e->fireEvents;
            e->in.again = false;
        }
        deliver();
    }

    void run(uint32_t ms) {
        for (uint32_t i = 0; i < ms; ++i) tick();
    }

    template <class F>
    bool runUntil(uint32_t maxMs, F cond) {
        for (uint32_t i = 0; i < maxMs; ++i) {
            tick();
            if (cond()) return true;
        }
        return false;
    }
};

void Endpoint::send(const uint8_t* dst, const uint8_t* data, size_t len) { link->push(id, dst, data, len); }

constexpr uint32_t kHost = 900;
constexpr uint32_t kClient = 100;

static void freezeWorld(Endpoint& host, Endpoint& client) {
    host.g().world.obstacles = false;
    host.g().world.movePlatforms = false;
    client.g().world.obstacles = false;
    client.g().world.movePlatforms = false;
}

static bool bothPlaying(Link& l) {
    return l.eps[0]->s.status() == Status::Playing && l.eps[1]->s.status() == Status::Playing;
}

static void testPairingClean() {
    Link l;
    Endpoint& c = l.add(kClient);
    Endpoint& h = l.add(kHost, 5);
    CHECK(l.runUntil(2000, [&] { return c.s.status() != Status::Searching && h.s.status() != Status::Searching; }));
    CHECK(h.s.role() == net::Role::Host);
    CHECK(c.s.role() == net::Role::Client);
    CHECK(h.s.localIndex() == 0 && c.s.localIndex() == 1);
    CHECK(l.runUntil(8000, [&] { return bothPlaying(l); }));
    CHECK(l.maxLen <= 200);
}

static void testPairingSimultaneousAnyOrder() {
    // Whichever device has the larger nonce hosts, regardless of who starts first.
    Link l;
    Endpoint& h = l.add(kHost);
    Endpoint& c = l.add(kClient, 40);
    CHECK(l.runUntil(8000, [&] { return bothPlaying(l); }));
    CHECK(h.s.role() == net::Role::Host && c.s.role() == net::Role::Client);
}

static void testPairingUnderLoss() {
    for (uint32_t seed = 1; seed <= 20; ++seed) {
        Link l;
        l.rng = seed * 7919;
        l.loss = 0.4f;
        l.latency = 10;
        l.jitter = 15;
        l.add(kClient);
        l.add(kHost);
        CHECK(l.runUntil(20000, [&] { return bothPlaying(l); }));
    }
}

static void testThreeDevices() {
    for (uint32_t seed = 1; seed <= 10; ++seed) {
        Link l;
        l.rng = seed * 104729;
        l.loss = 0.1f;
        l.latency = 5;
        l.jitter = 5;
        Endpoint& a = l.add(300);
        Endpoint& b = l.add(700, 3);
        Endpoint& c = l.add(500, 9);
        l.run(10000);
        int connected = 0, hosts = 0, clients = 0;
        for (Endpoint* e : {&a, &b, &c}) {
            if (e->s.status() != Status::Searching) {
                ++connected;
                hosts += e->s.role() == net::Role::Host;
                clients += e->s.role() == net::Role::Client;
            }
        }
        CHECK(connected == 2);
        CHECK(hosts == 1 && clients == 1);
    }
}

// Devices running another game, or another version of the same game, ignore each other and stay unnoticed.
static void testOtherGameIgnored() {
    const arcade::GameInfo otherGame = {"other", 9, 1, sizeof(ThrowhallaGame), 0, kThrowhallaInfo.create, 2};
    const arcade::GameInfo otherVersion = {"throwhalla", 1, uint8_t(kThrowhallaInfo.protoVer + 1), sizeof(ThrowhallaGame), 0, kThrowhallaInfo.create, 2};
    const arcade::GameInfo otherPlayers = {"throwhalla", 1, kThrowhallaInfo.protoVer, sizeof(ThrowhallaGame), 0, kThrowhallaInfo.create, 4};
    for (const arcade::GameInfo* other : {&otherGame, &otherVersion, &otherPlayers}) {
        Link l;
        Endpoint& a = l.add(kClient);
        Endpoint& b = l.add(kHost, 0, other);
        l.run(5000);
        CHECK(a.s.status() == Status::Searching && b.s.status() == Status::Searching);
        CHECK(!a.s.peerNearby(l.now) && !b.s.peerNearby(l.now));
    }
    // The third device with the right game still finds its partner among the strangers.
    Link l;
    l.add(kClient);
    l.add(kHost, 0, &otherGame);
    Endpoint& c = l.add(700);
    l.run(8000);
    CHECK(l.eps[0]->s.status() != Status::Searching && c.s.status() != Status::Searching);
    CHECK(l.eps[1]->s.status() == Status::Searching);
}

// A device that is not joinable (solo play) is noticed but does not pair until it opts in.
static void testJoinableGate() {
    Link l;
    Endpoint& c = l.add(kClient);
    Endpoint& h = l.add(kHost);
    c.s.setJoinable(false);
    h.s.setJoinable(false);
    l.run(3000);
    CHECK(c.s.status() == Status::Searching && h.s.status() == Status::Searching);
    CHECK(c.s.peerNearby(l.now) && h.s.peerNearby(l.now));

    c.s.setJoinable(true);  // only one side wants to play: still nothing
    l.run(3000);
    CHECK(c.s.status() == Status::Searching && h.s.status() == Status::Searching);

    h.s.setJoinable(true);
    CHECK(l.runUntil(8000, [&] { return bothPlaying(l); }));
    CHECK(h.s.role() == net::Role::Host);
}

static void testPeerNearbyExpires() {
    Link l;
    Endpoint& a = l.add(kClient);
    Endpoint& b = l.add(kHost);
    a.s.setJoinable(false);
    b.s.setJoinable(false);
    l.run(1000);
    CHECK(a.s.peerNearby(l.now));
    l.cut = true;
    l.run(2000);
    CHECK(!a.s.peerNearby(l.now));
    Link alone;
    Endpoint& x = alone.add(kClient);
    alone.run(2000);
    CHECK(!x.s.peerNearby(alone.now));
}

static void expectViewMatches(Endpoint& h, Endpoint& c, int& mismatches) {
    const Game& g = h.g();
    const Game& v = c.g();
    bool ok = g.phase == v.phase && g.winner == v.winner;
    ok = ok && std::fabs(g.turnTimer - v.turnTimer) < 0.11f;
    for (int i = 0; i < 2; ++i) {
        const Player& a = g.players[i];
        const Player& b = v.players[i];
        ok = ok && a.score == b.score && a.item == b.item && a.itemUsed == b.itemUsed && a.hit == b.hit &&
             a.hitSources == b.hitSources && a.hitSerial == b.hitSerial &&
             std::fabs(a.hitFlash - b.hitFlash) < 0.01f &&
             a.charging == b.charging && a.face == b.face && std::fabs(a.h - b.h) < 0.1f &&
             std::fabs(a.feetY - b.feetY) < 0.2f && std::fabs(a.power - b.power) < 0.01f;
        ok = ok && std::fabs(g.platformY(i) - v.platformY(i)) < 0.01f;
    }
    for (int i = 0; i < cfg::OBSTACLE_COUNT; ++i) ok = ok && g.obstacleXs[i] == v.obstacleXs[i];
    for (int i = 0; i < cfg::MAX_POWERUPS; ++i) {
        ok = ok && g.powerups[i].type == v.powerups[i].type &&
             g.powerups[i].x == v.powerups[i].x && g.powerups[i].y == v.powerups[i].y;
    }
    int ia = 0, ib = 0;
    for (;;) {
        while (ia < cfg::MAX_PROJ && !g.proj[ia].active) ++ia;
        while (ib < cfg::MAX_PROJ && !v.proj[ib].active) ++ib;
        if (ia == cfg::MAX_PROJ || ib == cfg::MAX_PROJ) {
            ok = ok && ia == ib;
            break;
        }
        const Projectile& a = g.proj[ia++];
        const Projectile& b = v.proj[ib++];
        ok = ok && a.type == b.type && a.owner == b.owner && a.exploding == b.exploding && a.fuseOn == b.fuseOn &&
             a.hitPlayers == b.hitPlayers && std::fabs(a.scale - b.scale) < .011f &&
             std::fabs(a.speedScale - b.speedScale) < .011f &&
             std::fabs(a.x - b.x) < 0.1f && std::fabs(a.y - b.y) < 0.1f && std::fabs(a.vx - b.vx) < 0.2f &&
             std::fabs(a.vy - b.vy) < 0.2f;
    }
    if (!ok) ++mismatches;
}

static void scriptRandomInput(Endpoint& e, uint32_t& rng) {
    auto r = [&] {
        rng ^= rng << 13;
        rng ^= rng >> 17;
        rng ^= rng << 5;
        return rng;
    };
    if (r() % 40 == 0) e.in.pad.aim = static_cast<float>(r() % 120) - 20.0f;
    if (r() % 50 == 0) e.in.pad.a = !e.in.pad.a;
    if (r() % 90 == 0) e.pressB();
    e.in.pad.b = (r() % 3) != 0;
}

static void testSnapshotMatchesHostWithoutLoss() {
    Link l;
    Endpoint& c = l.add(kClient);
    Endpoint& h = l.add(kHost);
    CHECK(l.runUntil(8000, [&] { return bothPlaying(l); }));

    uint32_t rngA = 99, rngB = 1234;
    int mismatches = 0, compared = 0;
    int projSeen = 0;
    for (int i = 0; i < 60000; ++i) {
        if (i % 16 == 0) {
            scriptRandomInput(h, rngA);
            scriptRandomInput(c, rngB);
        }
        const uint16_t seq = h.s.snapSeq_;
        l.tick();
        if (h.s.snapSeq_ != seq && l.q.empty()) {
            expectViewMatches(h, c, mismatches);
            ++compared;
            for (const auto& p : h.g().proj) projSeen += p.active;
        }
    }
    CHECK(compared > 3000);
    CHECK(projSeen > 100);
    CHECK(mismatches == 0);
    CHECK(l.maxLen <= 200);
}

static void testSnapshotConvergesUnderLoss() {
    Link l;
    l.loss = 0.2f;
    l.latency = 15;
    l.jitter = 20;
    Endpoint& c = l.add(kClient);
    Endpoint& h = l.add(kHost);
    CHECK(l.runUntil(20000, [&] { return bothPlaying(l); }));

    uint32_t rngA = 5, rngB = 6;
    int checks = 0, bad = 0;
    for (int i = 0; i < 60000; ++i) {
        if (i % 16 == 0) {
            scriptRandomInput(h, rngA);
            scriptRandomInput(c, rngB);
        }
        l.tick();
    }
    h.in.pad = PadInput();
    c.in.pad = PadInput();
    l.run(300);
    const Game& g = h.g();
    const Game& v = c.g();
    for (int i = 0; i < 2; ++i) {
        ++checks;
        bad += g.players[i].score != v.players[i].score;
    }
    ++checks;
    bad += g.phase != v.phase;
    CHECK(checks == 3 && bad == 0);
}

static void duelSetup(Link& l, Endpoint& h, Endpoint& c) {
    CHECK(l.runUntil(20000, [&] { return bothPlaying(l); }));
    freezeWorld(h, c);  // configure the fixture after begin() initializes a new match
    h.g().world.powerups = false;
    h.g().players[0].item = ItemType::Knife;
    h.g().players[0].itemUsed = false;
    h.g().players[0].score = 4;
    h.g().players[1].item = ItemType::Axe;
    h.g().players[1].itemUsed = true;  // the round ends once both have thrown
}

static void throwKnife(Link& l, Endpoint& h) {
    h.in.pad.aim = 0.0f;
    h.in.pad.a = true;
    l.run(400);
    h.in.pad.a = false;
}

static void testMatchFlowAndRematch(float loss, int latency, int jitter) {
    Link l;
    l.loss = loss;
    l.latency = latency;
    l.jitter = jitter;
    l.rng = 777;
    Endpoint& c = l.add(kClient);
    Endpoint& h = l.add(kHost);
    duelSetup(l, h, c);
    const uint8_t firstMatch = h.s.matchId_;
    h.fireEvents = 0;
    c.fireEvents = 0;

    throwKnife(l, h);
    CHECK(l.runUntil(5000, [&] { return h.g().phase == Phase::MatchEnd; }));
    CHECK(h.g().winner == 0 && h.g().players[0].score == 5);
    CHECK(l.runUntil(2000, [&] { return c.g().phase == Phase::MatchEnd; }));
    CHECK(c.g().winner == 0);
    CHECK(c.g().players[0].score == 5 && c.g().players[1].score == 0);
    CHECK(h.fireEvents == 1);
    CHECK(c.fireEvents <= 1);
    if (loss == 0.0f) CHECK(c.fireEvents == 1);

    // One side asking is not enough.
    h.in.again = true;
    l.run(500);
    CHECK(h.g().phase == Phase::MatchEnd);
    CHECK(h.s.myAgain() && !h.s.oppAgain());
    CHECK(l.runUntil(1000, [&] { return c.s.oppAgain(); }));

    c.in.again = true;
    CHECK(l.runUntil(3000, [&] { return h.s.matchId_ != firstMatch; }));
    CHECK(h.g().players[0].score == 0 && h.g().players[1].score == 0);
    CHECK(l.runUntil(6000, [&] { return bothPlaying(l); }));
    l.run(300);
    CHECK(c.g().players[0].score == 0 && c.g().phase == Phase::Playing);
    CHECK(c.s.matchId_ == h.s.matchId_);
    CHECK(!c.s.myAgain() && !h.s.myAgain());
}

static void testMatchEndWithoutRematchStays() {
    Link l;
    Endpoint& c = l.add(kClient);
    Endpoint& h = l.add(kHost);
    duelSetup(l, h, c);
    throwKnife(l, h);
    CHECK(l.runUntil(5000, [&] { return h.g().phase == Phase::MatchEnd; }));
    l.run(3000);
    CHECK(h.s.status() == Status::Playing && c.s.status() == Status::Playing);
    CHECK(h.g().phase == Phase::MatchEnd);
}

static void testClientJumpSurvivesLoss() {
    for (uint32_t seed = 1; seed <= 20; ++seed) {
        Link l;
        l.rng = seed * 31337;
        l.loss = 0.5f;
        l.latency = 10;
        l.jitter = 10;
        Endpoint& c = l.add(kClient);
        Endpoint& h = l.add(kHost);
        freezeWorld(h, c);
        CHECK(l.runUntil(30000, [&] { return bothPlaying(l); }));
        c.pressB();
        c.in.pad.b = true;
        bool jumped = false;
        for (int i = 0; i < 600 && !jumped; ++i) {
            l.tick();
            jumped = h.g().players[1].h > 0.0f;
        }
        CHECK(jumped);
    }
}

static void testClientFireReachesHost() {
    Link l;
    l.loss = 0.2f;
    l.latency = 10;
    l.jitter = 10;
    Endpoint& c = l.add(kClient);
    Endpoint& h = l.add(kHost);
    freezeWorld(h, c);
    CHECK(l.runUntil(20000, [&] { return bothPlaying(l); }));
    h.g().players[1].item = ItemType::Knife;
    h.g().players[1].itemUsed = false;
    c.in.pad.aim = 0.0f;
    c.in.pad.a = true;
    l.run(400);
    c.in.pad.a = false;
    bool fired = false;
    for (int i = 0; i < 600 && !fired; ++i) {
        l.tick();
        for (const auto& p : h.g().proj) fired = fired || (p.active && p.owner == 1);
    }
    CHECK(fired);
}

// Presses made before the match starts (menus, READY, countdown) must not turn into a jump when play begins.
static void testEarlyPressesAreNotJumps() {
    Link l;
    Endpoint& c = l.add(kClient);
    Endpoint& h = l.add(kHost);
    freezeWorld(h, c);
    for (int i = 0; i < 5; ++i) {
        h.pressB();
        c.pressB();
    }
    CHECK(l.runUntil(8000, [&] { return bothPlaying(l); }));
    l.run(500);
    CHECK(h.g().players[0].h == 0.0f && h.g().players[1].h == 0.0f);
}

// Nobody throws: the turn timeout ends the round on the host and the client sees the same state.
static void testTurnTimeoutReachesClient() {
    Link l;
    Endpoint& c = l.add(kClient);
    Endpoint& h = l.add(kHost);
    freezeWorld(h, c);
    CHECK(l.runUntil(8000, [&] { return bothPlaying(l); }));
    CHECK(l.runUntil(30000, [&] { return h.g().phase == Phase::RoundEnd; }));
    CHECK(h.g().players[0].itemUsed && h.g().players[1].itemUsed);
    CHECK(l.runUntil(500, [&] { return c.g().phase == Phase::RoundEnd; }));
    CHECK(c.g().players[0].itemUsed && c.g().players[1].itemUsed);
    CHECK(h.g().players[0].score == 0 && h.g().players[1].score == 0);
}

static void testLinkLostAndRecover() {
    Link l;
    l.latency = 5;
    Endpoint& c = l.add(kClient);
    Endpoint& h = l.add(kHost);
    CHECK(l.runUntil(8000, [&] { return bothPlaying(l); }));
    l.run(500);

    l.cut = true;
    CHECK(l.runUntil(1500, [&] { return h.s.status() == Status::LinkLost && c.s.status() == Status::LinkLost; }));
    const float clock = h.g().clock();
    l.run(2000);
    CHECK(std::fabs(h.g().clock() - clock) < 0.001f);  // the host pauses while the link is down
    CHECK(h.s.status() == Status::LinkLost);

    l.cut = false;
    CHECK(l.runUntil(1000, [&] { return bothPlaying(l); }));
    l.run(500);
    CHECK(h.g().clock() > clock + 0.2f);
}

static void testTimeout() {
    Link l;
    Endpoint& c = l.add(kClient);
    Endpoint& h = l.add(kHost);
    CHECK(l.runUntil(8000, [&] { return bothPlaying(l); }));
    l.cut = true;
    l.run(4500);
    CHECK(h.s.status() == Status::LinkLost && c.s.status() == Status::LinkLost);
    l.run(1500);
    CHECK(h.s.status() == Status::TimedOut && c.s.status() == Status::TimedOut);
}

static void testPeerLeaves(bool hostLeaves, float loss) {
    Link l;
    l.loss = loss;
    l.latency = 8;
    l.jitter = 8;
    l.rng = 4242;
    Endpoint& c = l.add(kClient);
    Endpoint& h = l.add(kHost);
    CHECK(l.runUntil(20000, [&] { return bothPlaying(l); }));
    Endpoint& leaver = hostLeaves ? h : c;
    Endpoint& stayer = hostLeaves ? c : h;
    leaver.s.leave();
    CHECK(l.runUntil(500, [&] { return stayer.s.status() == Status::PeerLeft; }));
}

static void testRtt() {
    Link l;
    l.latency = 20;
    Endpoint& c = l.add(kClient);
    Endpoint& h = l.add(kHost);
    CHECK(l.runUntil(8000, [&] { return bothPlaying(l); }));
    l.run(2000);
    CHECK(h.s.rttMs() >= 38 && h.s.rttMs() <= 100);
    CHECK(c.s.rttMs() >= 38 && c.s.rttMs() <= 100);
}

static void testIgnoresForeignAndStalePackets() {
    Link l;
    Endpoint& c = l.add(kClient);
    Endpoint& h = l.add(kHost);
    Endpoint& x = l.add(555);
    CHECK(l.runUntil(8000, [&] { return h.s.status() == Status::Playing && c.s.status() == Status::Playing; }));

    // A forged input with the right session id but from another MAC must not move the host's opponent.
    uint8_t pkt[19] = {0xD7, 4, static_cast<uint8_t>(h.s.sid_ & 0xFF), static_cast<uint8_t>(h.s.sid_ >> 8),
                       0, 0, 0, 0, 0, 0, 0x10, 0x27, 1, 0, 0, 0, 0, 0, 0};  // aim = 100.00, A held
    h.s.remotePad_.aim = 0.0f;
    h.s.onReceive(x.mac, pkt, sizeof(pkt), l.now);
    CHECK(h.s.remotePad_.aim == 0.0f);

    // Same MAC but wrong session id.
    pkt[2] ^= 0xFF;
    h.s.onReceive(c.mac, pkt, sizeof(pkt), l.now);
    CHECK(h.s.remotePad_.aim == 0.0f);

    // The client must not rewind to an older snapshot.
    const uint16_t seq = c.s.rxSeq_;
    const float clock = c.g().clock();
    uint8_t stale[250];
    size_t n = 0;
    {
        // Re-send a plausible-but-old snapshot header by hand: same sid, seq far behind.
        stale[n++] = 0xD7;
        stale[n++] = 5;
        stale[n++] = static_cast<uint8_t>(c.s.sid_ & 0xFF);
        stale[n++] = static_cast<uint8_t>(c.s.sid_ >> 8);
        const uint16_t old = static_cast<uint16_t>(seq - 100);
        stale[n++] = old & 0xFF;
        stale[n++] = old >> 8;
    }
    c.s.onReceive(h.mac, stale, n, l.now);  // truncated: must be rejected without touching state
    CHECK(c.g().clock() == clock);
}

static void testGarbageDoesNotCrash() {
    Link l;
    Endpoint& c = l.add(kClient);
    Endpoint& h = l.add(kHost);
    uint32_t rng = 2024;
    auto r = [&] {
        rng ^= rng << 13;
        rng ^= rng >> 17;
        rng ^= rng << 5;
        return rng;
    };
    for (int phase = 0; phase < 3; ++phase) {
        for (int i = 0; i < 20000; ++i) {
            uint8_t buf[250];
            const size_t len = r() % 251;
            for (size_t k = 0; k < len; ++k) buf[k] = static_cast<uint8_t>(r());
            if (len > 2 && (r() & 1)) {
                buf[0] = 0xD7;
                buf[1] = static_cast<uint8_t>(1 + r() % 6);
            }
            c.s.onReceive(h.mac, buf, len, l.now);
            h.s.onReceive(c.mac, buf, len, l.now);
            if (i % 50 == 0) l.run(20);
        }
        l.run(8000);
    }
    CHECK(true);
}


static BumperDuelGame& bumperOf(Endpoint& e) { return *static_cast<BumperDuelGame*>(e.gi.game); }
static void testBumperOverNetwork(float loss, int latency, int jitter) {
    Link l; l.loss=loss; l.latency=latency; l.jitter=jitter; l.rng=91;
    auto& c=l.add(kClient,0,&kBumperDuelInfo); auto& h=l.add(kHost,0,&kBumperDuelInfo);
    CHECK(l.runUntil(30000,[&]{return bothPlaying(l);}));
    l.run(150);CHECK(bumperOf(c).seed==bumperOf(h).seed);
    c.in.pad.a=true;c.in.pad.aim=20; l.run(600);
    CHECK(bumperOf(h).cars[1].control==1 && fabsf(bumperOf(h).cars[1].angle-bumperOf(h).heading(1,20))<.01f);
    c.in.pad.a=false;c.in.pad.b=true;++c.in.pad.bCount;c.in.pad.aim=0;l.run(1200);
    CHECK(bumperOf(h).cars[1].cooldown>1 && bumperOf(h).cars[1].cooldown<2.1f);
    c.in.pad.b=false;
    for(int n=0;n<5;++n){bumperOf(h).fixture(n==0?'c':'s');l.run(300);CHECK(bumperOf(h).score[0]==n+1);}
    CHECK(l.runUntil(3000,[&]{return bumperOf(c).phase()==arcade::MatchPhase::MatchEnd;}));
    CHECK(bumperOf(c).winner==bumperOf(h).winner && bumperOf(c).score[0]==5);
    l.run(100);arcade::Writer a,b;bumperOf(h).encode(a);bumperOf(c).encode(b);
    CHECK(a.n==b.n && memcmp(a.buf,b.buf,a.n)==0);CHECK(l.maxLen==104);
    bool swapped=bumperOf(h).swapped;uint32_t seed=bumperOf(h).seed;
    h.in.again=true;l.run(500);CHECK(bumperOf(h).phase()==arcade::MatchPhase::MatchEnd);
    c.in.again=true;CHECK(l.runUntil(4000,[&]{return bumperOf(h).seed!=seed;}));
    CHECK(bumperOf(h).swapped!=swapped);
    CHECK(l.runUntil(10000,[&]{return bothPlaying(l);}));l.run(300);
    CHECK(bumperOf(c).swapped==bumperOf(h).swapped && bumperOf(c).score[0]==0);
    l.cut=true;l.run(1000);CHECK(h.s.status()==Status::LinkLost&&c.s.status()==Status::LinkLost);
    float time=bumperOf(h).elapsed;l.run(500);CHECK(bumperOf(h).elapsed==time);
    l.cut=false;CHECK(l.runUntil(2000,[&]{return bothPlaying(l);}));
    c.s.leave();CHECK(l.runUntil(500,[&]{return h.s.status()==Status::PeerLeft;}));
}

static void testBumperWireIsolation() {
    for(int variant=0;variant<2;++variant){
        arcade::GameInfo incompatible=kBumperDuelInfo;
        if(variant==0)incompatible.wireId=3;else incompatible.protoVer=4;  // previous fixed-reference sumo without items
        Link l;auto& a=l.add(kClient,0,&incompatible);auto& b=l.add(kHost,0,&kBumperDuelInfo);
        l.run(10000);CHECK(a.s.status()==Status::Searching&&b.s.status()==Status::Searching);
        CHECK(!a.s.peerNearby(l.now)&&!b.s.peerNearby(l.now));
    }
}

static SamuraiDuelGame& samuraiOf(Endpoint& e) { return *static_cast<SamuraiDuelGame*>(e.gi.game); }
static void testSamuraiOverNetwork(float loss, int latency, int jitter) {
    Link l;l.loss=loss;l.latency=latency;l.jitter=jitter;l.rng=98;
    auto& c=l.add(kClient,0,&kSamuraiDuelInfo);auto& h=l.add(kHost,0,&kSamuraiDuelInfo);
    CHECK(l.runUntil(30000,[&]{return bothPlaying(l);}));l.run(200);
    CHECK(samuraiOf(c).seed==samuraiOf(h).seed);
    const float spawn[2]={samuraiOf(h).fighters[0].x,samuraiOf(h).fighters[1].x};
    const int sword=samuraiOf(h).swordPlayer;
    CHECK(samuraiOf(c).fighters[0].x==spawn[0] && samuraiOf(c).fighters[1].x==spawn[1]);
    const int moveDirection=spawn[1]>spawn[0] ? -1:1;
    c.in.pad.b=true;++c.in.pad.bCount;c.in.pad.aim=32*moveDirection;++c.in.pad.aCount;
    CHECK(l.runUntil(1200,[&]{return (h.events&SamuraiDuelGame::Shot)!=0;}));
    CHECK((samuraiOf(h).fighters[1].x-spawn[1])*moveDirection>0);
    c.in.pad.b=false;c.in.pad.aim=0;
    l.run(1300);
    // Complete real rounds using guaranteed incoming bullets; roles and scores must replicate.
    for(int round=0;round<SamuraiDuelGame::kWinningScore;++round) {
        auto& g=samuraiOf(h);
        auto& b=g.bullets[0];b.active=true;b.owner=0;b.direction=1;
        b.reflected=g.isSword(0);b.x=g.fighters[1].x-7;b.y=g.fighters[1].y-8;
        l.run(200);CHECK(g.score[0]==round+1);
        CHECK(l.runUntil(1500,[&]{return samuraiOf(c).score[0]==round+1;}));
        if(round<SamuraiDuelGame::kWinningScore-1) {
            CHECK(l.runUntil(2500,[&]{return g.phase()==arcade::MatchPhase::Playing;}));l.run(200);
            CHECK(samuraiOf(c).swordPlayer==sword && g.swordPlayer==sword);
            CHECK(g.fighters[0].x==spawn[0] && g.fighters[1].x==spawn[1]);
            CHECK(samuraiOf(c).fighters[0].x==spawn[0] && samuraiOf(c).fighters[1].x==spawn[1]);
        }
    }
    CHECK(l.runUntil(1500,[&]{return samuraiOf(c).phase()==arcade::MatchPhase::MatchEnd;}));
    l.run(300);arcade::Writer a,b;samuraiOf(h).encode(a);samuraiOf(c).encode(b);
    CHECK(a.n==b.n && memcmp(a.buf,b.buf,a.n)==0 && l.maxLen<=250);
    uint32_t seed=samuraiOf(h).seed;
    h.in.again=true;l.run(300);CHECK(samuraiOf(h).seed==seed);
    c.in.again=true;CHECK(l.runUntil(4000,[&]{return samuraiOf(h).seed!=seed;}));
    CHECK(l.runUntil(10000,[&]{return bothPlaying(l);}));l.run(300);
    CHECK(samuraiOf(h).swordPlayer==1-sword && samuraiOf(c).swordPlayer==1-sword && samuraiOf(c).score[0]==0);
    for (int i=0;i<2;++i) CHECK(samuraiOf(c).fighters[i].x==samuraiOf(h).fighters[i].x);
    l.cut=true;l.run(1000);CHECK(h.s.status()==Status::LinkLost && c.s.status()==Status::LinkLost);
    float elapsed=samuraiOf(h).elapsed;l.run(500);CHECK(samuraiOf(h).elapsed==elapsed);
    l.cut=false;CHECK(l.runUntil(2500,[&]{return bothPlaying(l);}));
    c.s.leave();CHECK(l.runUntil(1000,[&]{return h.s.status()==Status::PeerLeft;}));
    for(int variant=0;variant<2;++variant) {
        Link isolated;arcade::GameInfo info=kSamuraiDuelInfo;
        if(variant)++info.protoVer;else info.wireId=kBumperDuelInfo.wireId;
        auto& x=isolated.add(kClient,0,&info);auto& y=isolated.add(kHost,0,&kSamuraiDuelInfo);
        isolated.run(6000);CHECK(x.s.status()==Status::Searching && y.s.status()==Status::Searching);
    }
}

static MidDiffGame& midOf(Endpoint& e) { return *static_cast<MidDiffGame*>(e.gi.game); }
// Points the pad of `shooter` at the opponent's head.
static void aimAtHead(PadInput& pad, const MidDiffGame& g, int shooter) {
    const float f = MidDiffGame::facing(shooter);
    const auto& me = g.players[shooter];
    const auto& foe = g.players[1 - shooter];
    const float dx = (foe.x - me.x) * f, dz = (foe.z - me.z) * f;
    pad.aim = std::atan2(dx, dz) * 57.29578f;
    pad.pitch = std::atan2(MidDiffGame::bodyHeight(foe.crouch) - 0.2f - MidDiffGame::eyeHeight(me.crouch), std::hypot(dx, dz)) * 57.29578f;
}
static void testMidDiffOverNetwork(float loss, int latency, int jitter) {
    Link l;l.loss=loss;l.latency=latency;l.jitter=jitter;l.rng=77;
    auto& c=l.add(kClient,0,&kMidDiffInfo);auto& h=l.add(kHost,0,&kMidDiffInfo);
    CHECK(l.runUntil(30000,[&]{return bothPlaying(l);}));l.run(300);
    CHECK(midOf(c).seed==midOf(h).seed);
    // The client's joystick and view reach the host: right for player 1 is -x, and the view is the sensor's.
    c.in.pad.stickX=100;c.in.pad.aim=-20;c.in.pad.pitch=4;
    CHECK(l.runUntil(2000,[&]{return midOf(h).players[1].x<-0.5f;}));
    CHECK(std::fabs(midOf(h).players[1].yaw+20)<0.05f && std::fabs(midOf(h).players[1].pitch-4)<0.05f);
    CHECK(l.runUntil(2000,[&]{return std::fabs(midOf(c).players[1].x-midOf(h).players[1].x)<0.01f && midOf(c).players[1].x<-0.5f;}));
    c.in.pad.stickX=0;
    // Complete real rounds: the client shoots the host's head each time; scores replicate and the rematch works.
    for(int round=0;round<MidDiffGame::kWinningScore;++round) {
        auto& g=midOf(h);
        CHECK(l.runUntil(5000,[&]{return g.phase()==arcade::MatchPhase::Playing && g.players[1].cooldown==0;}));
        l.run(300);
        aimAtHead(c.in.pad,g,1);++c.in.pad.aCount;
        CHECK(l.runUntil(3000,[&]{return g.score[1]==round+1;}));
        CHECK(l.runUntil(3000,[&]{return midOf(c).score[1]==round+1;}));
    }
    CHECK(l.runUntil(1500,[&]{return midOf(c).phase()==arcade::MatchPhase::MatchEnd;}));
    l.run(700);arcade::Writer a,b;midOf(h).encode(a);midOf(c).encode(b);
    CHECK(a.n==b.n && memcmp(a.buf,b.buf,a.n)==0 && l.maxLen<=250);
    const uint32_t seed=midOf(h).seed;
    h.in.again=true;l.run(300);CHECK(midOf(h).seed==seed);
    c.in.again=true;CHECK(l.runUntil(4000,[&]{return midOf(h).seed!=seed;}));
    CHECK(l.runUntil(10000,[&]{return bothPlaying(l);}));l.run(300);
    CHECK(midOf(h).score[1]==0 && midOf(c).score[1]==0 && midOf(c).seed==midOf(h).seed);
    l.cut=true;l.run(1000);CHECK(h.s.status()==Status::LinkLost && c.s.status()==Status::LinkLost);
    const float roundTime=midOf(h).roundTime;l.run(500);CHECK(midOf(h).roundTime==roundTime);
    l.cut=false;CHECK(l.runUntil(2500,[&]{return bothPlaying(l);}));
    c.s.leave();CHECK(l.runUntil(1000,[&]{return h.s.status()==Status::PeerLeft;}));
    for(int variant=0;variant<2;++variant) {
        Link isolated;arcade::GameInfo info=kMidDiffInfo;
        if(variant)++info.protoVer;else info.wireId=kSamuraiDuelInfo.wireId;
        auto& x=isolated.add(kClient,0,&info);auto& y=isolated.add(kHost,0,&kMidDiffInfo);
        isolated.run(6000);CHECK(x.s.status()==Status::Searching && y.s.status()==Status::Searching);
    }
}

int main() {
    testPairingClean();
    testPairingSimultaneousAnyOrder();
    testPairingUnderLoss();
    testThreeDevices();
    testOtherGameIgnored();
    testJoinableGate();
    testPeerNearbyExpires();
    testSnapshotMatchesHostWithoutLoss();
    testSnapshotConvergesUnderLoss();
    testMatchFlowAndRematch(0.0f, 0, 0);
    testMatchFlowAndRematch(0.2f, 15, 20);
    testMatchEndWithoutRematchStays();
    testClientJumpSurvivesLoss();
    testClientFireReachesHost();
    testEarlyPressesAreNotJumps();
    testTurnTimeoutReachesClient();
    testLinkLostAndRecover();
    testTimeout();
    testPeerLeaves(false, 0.0f);
    testPeerLeaves(true, 0.0f);
    testPeerLeaves(false, 0.3f);
    testRtt();
    testIgnoresForeignAndStalePackets();
    testBumperWireIsolation();
    testBumperOverNetwork(0,0,0);
    testBumperOverNetwork(0.2f,15,20);
    testSamuraiOverNetwork(0,0,0);
    testSamuraiOverNetwork(.2f,15,20);
    testMidDiffOverNetwork(0,0,0);
    testMidDiffOverNetwork(.2f,15,20);
    testGarbageDoesNotCrash();
    std::printf(failures ? "%d FAILURES\n" : "ALL PASSED\n", failures);
    return failures ? 1 : 0;
}
