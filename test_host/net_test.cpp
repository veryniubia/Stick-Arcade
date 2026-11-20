// Host-side tests for the online session (pairing, sync, loss, disconnects).
// Build: g++ -std=c++17 -I src test_host/net_test.cpp src/net_session.cpp src/game.cpp -o test_host/net_test.exe
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <vector>

// The tests poke at session internals to script the game; production code keeps them private.
#define private public
#include "net_session.h"
#undef private

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
    net::LocalInput in;
    uint32_t nextFrame = 0;
    int frameParity = 0;
    uint32_t events = 0;      // OR of everything returned by update()
    int fireEvents = 0;       // frames whose events contained EV_FIRE
    bool dead = false;        // stops calling update (device turned off)

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

    Endpoint& add(uint32_t nonce, uint32_t startMs = 0) {
        auto ep = std::make_unique<Endpoint>();
        ep->link = this;
        ep->id = static_cast<int>(eps.size());
        for (int i = 0; i < 6; ++i) ep->mac[i] = static_cast<uint8_t>(0x10 * (ep->id + 1) + i);
        ep->s.begin(ep.get(), nonce, now);
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
            e->in.jump = false;
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
    host.s.game_.world.obstacles = false;
    host.s.game_.world.movePlatforms = false;
    client.s.view_.world.obstacles = false;
    client.s.view_.world.movePlatforms = false;
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

static void expectViewMatches(Endpoint& h, Endpoint& c, int& mismatches) {
    const Game& g = h.s.game_;
    const Game& v = c.s.view_;
    bool ok = g.phase == v.phase && g.winner == v.winner;
    for (int i = 0; i < 2; ++i) {
        const Player& a = g.players[i];
        const Player& b = v.players[i];
        ok = ok && a.score == b.score && a.item == b.item && a.itemUsed == b.itemUsed && a.hit == b.hit &&
             a.charging == b.charging && a.face == b.face && std::fabs(a.h - b.h) < 0.1f &&
             std::fabs(a.feetY - b.feetY) < 0.2f && std::fabs(a.power - b.power) < 0.01f;
        ok = ok && std::fabs(g.platformY(i) - v.platformY(i)) < 0.01f;
    }
    for (int i = 0; i < cfg::OBSTACLE_COUNT; ++i) ok = ok && g.obstacleXs[i] == v.obstacleXs[i];
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
             std::fabs(a.x - b.x) < 0.1f && std::fabs(a.y - b.y) < 0.1f && std::fabs(a.vx - b.vx) < 0.2f &&
             std::fabs(a.vy - b.vy) < 0.2f;
    }
    if (!ok) ++mismatches;
}

static void scriptRandomInput(Link& l, Endpoint& e, uint32_t& rng) {
    auto r = [&] {
        rng ^= rng << 13;
        rng ^= rng >> 17;
        rng ^= rng << 5;
        return rng;
    };
    if (r() % 40 == 0) e.in.aimDeg = static_cast<float>(r() % 120) - 20.0f;
    if (r() % 50 == 0) e.in.charge = !e.in.charge;
    if (r() % 90 == 0) e.in.jump = true;
    e.in.jumpHeld = (r() % 3) != 0;
    (void)l;
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
            scriptRandomInput(l, h, rngA);
            scriptRandomInput(l, c, rngB);
        }
        const uint16_t seq = h.s.snapSeq_;
        l.tick();
        if (h.s.snapSeq_ != seq && l.q.empty()) {
            expectViewMatches(h, c, mismatches);
            ++compared;
            for (const auto& p : h.s.game_.proj) projSeen += p.active;
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
            scriptRandomInput(l, h, rngA);
            scriptRandomInput(l, c, rngB);
        }
        l.tick();
    }
    h.in = net::LocalInput();
    c.in = net::LocalInput();
    l.run(300);
    const Game& g = h.s.game_;
    const Game& v = c.s.view_;
    for (int i = 0; i < 2; ++i) {
        ++checks;
        bad += g.players[i].score != v.players[i].score;
    }
    ++checks;
    bad += g.phase != v.phase;
    CHECK(checks == 3 && bad == 0);
}

static void duelSetup(Link& l, Endpoint& h, Endpoint& c) {
    freezeWorld(h, c);
    CHECK(l.runUntil(20000, [&] { return bothPlaying(l); }));
    h.s.game_.players[0].item = ItemType::Knife;
    h.s.game_.players[0].itemUsed = false;
    h.s.game_.players[0].score = 4;
    h.s.game_.players[1].item = ItemType::Axe;
    h.s.game_.players[1].itemUsed = true;  // the round ends once both have thrown
}

static void throwKnife(Link& l, Endpoint& h) {
    h.in.aimDeg = 0.0f;
    h.in.charge = true;
    l.run(400);
    h.in.charge = false;
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
    CHECK(l.runUntil(5000, [&] { return h.s.game_.phase == Phase::MatchEnd; }));
    CHECK(h.s.game_.winner == 0 && h.s.game_.players[0].score == 5);
    CHECK(l.runUntil(2000, [&] { return c.s.view_.phase == Phase::MatchEnd; }));
    CHECK(c.s.view_.winner == 0);
    CHECK(c.s.view_.players[0].score == 5 && c.s.view_.players[1].score == 0);
    CHECK(h.fireEvents == 1);
    CHECK(c.fireEvents <= 1);
    if (loss == 0.0f) CHECK(c.fireEvents == 1);

    // One side asking is not enough.
    h.in.again = true;
    l.run(500);
    CHECK(h.s.game_.phase == Phase::MatchEnd);
    CHECK(h.s.myAgain() && !h.s.oppAgain());
    CHECK(l.runUntil(1000, [&] { return c.s.oppAgain(); }));

    c.in.again = true;
    CHECK(l.runUntil(3000, [&] { return h.s.matchId_ != firstMatch; }));
    CHECK(h.s.game_.players[0].score == 0 && h.s.game_.players[1].score == 0);
    CHECK(l.runUntil(6000, [&] { return bothPlaying(l); }));
    l.run(300);
    CHECK(c.s.view_.players[0].score == 0 && c.s.view_.phase == Phase::Playing);
    CHECK(c.s.matchId_ == h.s.matchId_);
    CHECK(!c.s.myAgain() && !h.s.myAgain());
}

static void testMatchEndWithoutRematchStays() {
    Link l;
    Endpoint& c = l.add(kClient);
    Endpoint& h = l.add(kHost);
    duelSetup(l, h, c);
    throwKnife(l, h);
    CHECK(l.runUntil(5000, [&] { return h.s.game_.phase == Phase::MatchEnd; }));
    l.run(3000);
    CHECK(h.s.status() == Status::Playing && c.s.status() == Status::Playing);
    CHECK(h.s.game_.phase == Phase::MatchEnd);
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
        c.in.jump = true;
        c.in.jumpHeld = true;
        bool jumped = false;
        for (int i = 0; i < 600 && !jumped; ++i) {
            l.tick();
            jumped = h.s.game_.players[1].h > 0.0f;
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
    h.s.game_.players[1].item = ItemType::Knife;
    h.s.game_.players[1].itemUsed = false;
    c.in.aimDeg = 0.0f;
    c.in.charge = true;
    l.run(400);
    c.in.charge = false;
    bool fired = false;
    for (int i = 0; i < 600 && !fired; ++i) {
        l.tick();
        for (const auto& p : h.s.game_.proj) fired = fired || (p.active && p.owner == 1);
    }
    CHECK(fired);
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
    const float clock = h.s.game_.clock();
    l.run(2000);
    CHECK(std::fabs(h.s.game_.clock() - clock) < 0.001f);  // the host pauses while the link is down
    CHECK(h.s.status() == Status::LinkLost);

    l.cut = false;
    CHECK(l.runUntil(1000, [&] { return bothPlaying(l); }));
    l.run(500);
    CHECK(h.s.game_.clock() > clock + 0.2f);
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
    uint8_t pkt[14] = {0xD7, 4, static_cast<uint8_t>(h.s.sid_ & 0xFF), static_cast<uint8_t>(h.s.sid_ >> 8),
                       0, 0, 0, 0, 0, 0, 0x10, 0x27, 1, 0};  // aim = 100.00, charge
    h.s.remote_.aim = 0.0f;
    h.s.onReceive(x.mac, pkt, sizeof(pkt), l.now);
    CHECK(h.s.remote_.aim == 0.0f);

    // Same MAC but wrong session id.
    pkt[2] ^= 0xFF;
    h.s.onReceive(c.mac, pkt, sizeof(pkt), l.now);
    CHECK(h.s.remote_.aim == 0.0f);

    // The client must not rewind to an older snapshot.
    const uint16_t seq = c.s.rxSeq_;
    const float clock = c.s.view_.clock();
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
    CHECK(c.s.view_.clock() == clock);
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

int main() {
    testPairingClean();
    testPairingSimultaneousAnyOrder();
    testPairingUnderLoss();
    testThreeDevices();
    testSnapshotMatchesHostWithoutLoss();
    testSnapshotConvergesUnderLoss();
    testMatchFlowAndRematch(0.0f, 0, 0);
    testMatchFlowAndRematch(0.2f, 15, 20);
    testMatchEndWithoutRematchStays();
    testClientJumpSurvivesLoss();
    testClientFireReachesHost();
    testLinkLostAndRecover();
    testTimeout();
    testPeerLeaves(false, 0.0f);
    testPeerLeaves(true, 0.0f);
    testPeerLeaves(false, 0.3f);
    testRtt();
    testIgnoresForeignAndStalePackets();
    testGarbageDoesNotCrash();
    std::printf(failures ? "%d FAILURES\n" : "ALL PASSED\n", failures);
    return failures ? 1 : 0;
}
