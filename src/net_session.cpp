#include "net_session.h"

#include <math.h>
#include <string.h>

namespace net {

namespace {

constexpr uint8_t kMagic = 0xD7;
constexpr uint8_t kVersion = 1;

enum : uint8_t { PT_HELLO = 1, PT_OFFER, PT_ACCEPT, PT_INPUT, PT_SNAP, PT_BYE };

constexpr uint32_t kHelloMs = 120;
constexpr uint32_t kOfferMs = 100;
constexpr uint32_t kPairTimeoutMs = 1500;
constexpr uint32_t kLostMs = 600;
constexpr uint32_t kTimeoutMs = 5000;
constexpr uint32_t kSendMs = 15;
constexpr float kReadyMs = 1000.0f;
constexpr float kCountdownSec = 3.0f;
constexpr float kTick = 1.0f / 60.0f;
constexpr int kByeCopies = 4;

// Snapshot flag bits.
constexpr uint8_t kFlagHostAgain = 1;
constexpr uint8_t kFlagClientAgain = 2;
constexpr uint8_t kFlagEcho = 4;

// Input flag bits.
constexpr uint8_t kInCharge = 1;
constexpr uint8_t kInJumpHeld = 2;
constexpr uint8_t kInAgain = 4;
constexpr uint8_t kInEcho = 8;

// Player flag bits.
constexpr uint8_t kPlItemUsed = 1;
constexpr uint8_t kPlCharging = 2;
constexpr uint8_t kPlHit = 4;
constexpr uint8_t kPlPowerUp = 8;
constexpr uint8_t kPlFaceRight = 16;

// Projectile flag bits.
constexpr uint8_t kPrExploding = 1;
constexpr uint8_t kPrFuseOn = 2;

struct Writer {
    uint8_t buf[250];
    size_t n = 0;

    void u8(uint8_t v) { if (n < sizeof(buf)) buf[n++] = v; }
    void u16(uint16_t v) { u8(v & 0xFF); u8(v >> 8); }
    void i16(int16_t v) { u16(static_cast<uint16_t>(v)); }
    void u32(uint32_t v) { u16(v & 0xFFFF); u16(v >> 16); }
    void f32(float v) { uint32_t b; memcpy(&b, &v, 4); u32(b); }
};

struct Reader {
    const uint8_t* p;
    size_t len;
    size_t pos = 0;
    bool ok = true;

    Reader(const uint8_t* d, size_t n) : p(d), len(n) {}
    uint8_t u8() {
        if (pos >= len) { ok = false; return 0; }
        return p[pos++];
    }
    uint16_t u16() { const uint16_t lo = u8(); return lo | (static_cast<uint16_t>(u8()) << 8); }
    int16_t i16() { return static_cast<int16_t>(u16()); }
    uint32_t u32() { const uint32_t lo = u16(); return lo | (static_cast<uint32_t>(u16()) << 16); }
    float f32() { const uint32_t b = u32(); float v; memcpy(&v, &b, 4); return v; }
};

int16_t q16(float v, float scale) {
    float s = v * scale;
    if (s > 32767.0f) s = 32767.0f;
    if (s < -32768.0f) s = -32768.0f;
    return static_cast<int16_t>(lroundf(s));
}

uint8_t q8(float v, float scale) {
    float s = v * scale;
    if (s > 255.0f) s = 255.0f;
    if (s < 0.0f) s = 0.0f;
    return static_cast<uint8_t>(lroundf(s));
}

uint32_t mix(uint32_t a, uint32_t b) {
    uint32_t h = a * 2654435761u ^ (b + 0x9E3779B9u + (a << 6) + (a >> 2));
    h ^= h >> 15;
    h *= 2246822519u;
    h ^= h >> 13;
    return h ? h : 1u;
}

}  // namespace

void Session::begin(Transport* tx, uint32_t nonce, uint32_t nowMs) {
    *this = Session();
    tx_ = tx;
    nonce_ = nonce ? nonce : 1u;
    lastUpdate_ = nowMs;
    stateSince_ = nowMs;
    lastSend_ = nowMs - kHelloMs;
}

Status Session::status() const {
    switch (state_) {
        case State::Searching:
        case State::Offering:
        case State::Accepting:
            return Status::Searching;
        case State::Closed:
            return closeReason_;
        case State::Connected:
            break;
    }
    if (linkLost_) return Status::LinkLost;
    if (stage_ == Stage::Ready) return Status::Ready;
    if (stage_ == Stage::Countdown) return Status::Countdown;
    return Status::Playing;
}

bool Session::myAgain() const { return myAgain_; }
bool Session::oppAgain() const { return oppAgain_; }

void Session::toSearching(uint32_t now) {
    state_ = State::Searching;
    role_ = Role::None;
    stateSince_ = now;
}

void Session::connect(Role role, uint32_t now) {
    state_ = State::Connected;
    role_ = role;
    lastRxMs_ = now;
    stateSince_ = now;
    linkLost_ = false;
    rtt_ = -1;
    haveEcho_ = false;
    rxSeqValid_ = false;
    stage_ = Stage::Ready;
    if (role == Role::Host) {
        heard_ = false;
        readyMs_ = 0.0f;
        newMatch(now);
    }
}

void Session::newMatch(uint32_t now) {
    ++matchId_;
    seed_ = mix(nonce_ ^ (candNonce_ << 1), now + matchId_);
    game_.begin(GameMode::Versus, seed_);
    myAgain_ = false;
    oppAgain_ = false;
    remote_.again = false;
    hostJump_ = false;
    accum_ = 0.0f;
    cd_ = 0.0f;
}

bool Session::fromPeer(const uint8_t* mac, uint16_t sid) const {
    return sid == sid_ && memcmp(mac, peer_, 6) == 0;
}

void Session::leave() {
    if (state_ != State::Connected) return;
    Writer w;
    w.u8(kMagic);
    w.u8(PT_BYE);
    w.u16(sid_);
    for (int i = 0; i < kByeCopies; ++i) tx_->send(peer_, w.buf, w.n);
    state_ = State::Closed;
    closeReason_ = Status::PeerLeft;
}

void Session::sendHello() {
    Writer w;
    w.u8(kMagic);
    w.u8(PT_HELLO);
    w.u8(kVersion);
    w.u32(nonce_);
    tx_->send(nullptr, w.buf, w.n);
}

void Session::sendOffer() {
    Writer w;
    w.u8(kMagic);
    w.u8(PT_OFFER);
    w.u32(nonce_);
    w.u32(candNonce_);
    w.u16(sid_);
    tx_->send(peer_, w.buf, w.n);
}

void Session::sendAccept() {
    Writer w;
    w.u8(kMagic);
    w.u8(PT_ACCEPT);
    w.u32(candNonce_);  // the host's nonce
    w.u32(nonce_);
    w.u16(sid_);
    tx_->send(peer_, w.buf, w.n);
}

void Session::sendInput(const LocalInput& in, uint32_t now) {
    Writer w;
    w.u8(kMagic);
    w.u8(PT_INPUT);
    w.u16(sid_);
    w.u16(txSeq_++);
    w.u16(static_cast<uint16_t>(now));
    w.u16(peerTs_);
    w.i16(q16(in.aimDeg, 100.0f));
    uint8_t flags = 0;
    if (in.charge) flags |= kInCharge;
    if (in.jumpHeld) flags |= kInJumpHeld;
    if (myAgain_) flags |= kInAgain;
    if (haveEcho_) flags |= kInEcho;
    w.u8(flags);
    w.u8(jumpCount_);
    tx_->send(peer_, w.buf, w.n);
}

void Session::sendSnapshot(uint32_t now) {
    Writer w;
    w.u8(kMagic);
    w.u8(PT_SNAP);
    w.u16(sid_);
    w.u16(snapSeq_++);
    w.u16(static_cast<uint16_t>(now));
    w.u16(peerTs_);
    w.u8(static_cast<uint8_t>(stage_));
    w.u8(q8(cd_, 10.0f));
    w.u8(matchId_);
    w.u32(seed_);
    w.u8(static_cast<uint8_t>(game_.phase));
    w.u8(static_cast<uint8_t>(static_cast<int8_t>(game_.winner)));
    w.u8(q8(game_.phaseTimer, 100.0f));
    w.f32(game_.clock());

    uint8_t flags = 0;
    if (myAgain_) flags |= kFlagHostAgain;
    if (remote_.again) flags |= kFlagClientAgain;
    if (haveEcho_) flags |= kFlagEcho;
    w.u8(flags);

    const uint8_t evCur = static_cast<uint8_t>(evAcc_ & 0xFF);
    w.u8(evCur);
    w.u8(evLast_);
    evLast_ = evCur;
    evAcc_ = 0;

    for (const Player& p : game_.players) {
        w.i16(q16(p.h, 16.0f));
        w.i16(q16(p.vh, 16.0f));
        w.i16(q16(p.aimDeg, 100.0f));
        w.u8(q8(p.power, 255.0f));
        w.u8(q8(p.hitFlash, 255.0f));
        w.u8(q8(p.cooldown, 255.0f));
        w.u8(p.score);
        w.u8(static_cast<uint8_t>(p.item));
        uint8_t pf = 0;
        if (p.itemUsed) pf |= kPlItemUsed;
        if (p.charging) pf |= kPlCharging;
        if (p.hit) pf |= kPlHit;
        if (p.powerDir > 0) pf |= kPlPowerUp;
        if (p.face > 0) pf |= kPlFaceRight;
        w.u8(pf);
    }

    uint8_t count = 0;
    for (const Projectile& p : game_.proj) count += p.active ? 1 : 0;
    w.u8(count);
    for (const Projectile& p : game_.proj) {
        if (!p.active) continue;
        uint8_t pf = 0;
        if (p.exploding) pf |= kPrExploding;
        if (p.fuseOn) pf |= kPrFuseOn;
        w.u8(pf);
        w.u8(static_cast<uint8_t>(static_cast<uint8_t>(p.type) | (p.owner << 4)));
        w.i16(q16(p.x, 16.0f));
        w.i16(q16(p.y, 16.0f));
        w.i16(q16(p.vx, 8.0f));
        w.i16(q16(p.vy, 8.0f));
        w.i16(q16(p.rot, 256.0f));
        w.u8(q8(p.fuse, 100.0f));
        w.u8(q8(p.blastT, 300.0f));
    }
    tx_->send(peer_, w.buf, w.n);
}

void Session::noteEcho(uint16_t echo, uint32_t now) {
    const uint16_t age = static_cast<uint16_t>(static_cast<uint16_t>(now) - echo);
    if (age >= 2000) return;
    rtt_ = rtt_ < 0 ? age : (rtt_ * 3 + age) / 4;
}

void Session::parseInput(const uint8_t* d, size_t len, uint32_t now) {
    Reader r(d, len);
    r.u8();
    r.u8();
    r.u16();  // sid, already checked
    const uint16_t seq = r.u16();
    const uint16_t ts = r.u16();
    const uint16_t echo = r.u16();
    const int16_t aim = r.i16();
    const uint8_t flags = r.u8();
    const uint8_t jumps = r.u8();
    if (!r.ok) return;

    lastRxMs_ = now;
    if (rxSeqValid_ && static_cast<int16_t>(seq - rxSeq_) <= 0) return;
    rxSeq_ = seq;
    rxSeqValid_ = true;

    peerTs_ = ts;
    haveEcho_ = true;
    if (flags & kInEcho) noteEcho(echo, now);

    heard_ = true;
    remote_.aim = aim / 100.0f;
    remote_.charge = (flags & kInCharge) != 0;
    remote_.jumpHeld = (flags & kInJumpHeld) != 0;
    remote_.again = (flags & kInAgain) != 0;
    remote_.jumpCount = jumps;
}

void Session::parseSnapshot(const uint8_t* d, size_t len, uint32_t now) {
    Reader r(d, len);
    r.u8();
    r.u8();
    r.u16();  // sid
    const uint16_t seq = r.u16();
    const uint16_t ts = r.u16();
    const uint16_t echo = r.u16();
    const uint8_t stage = r.u8();
    const uint8_t cd = r.u8();
    const uint8_t matchId = r.u8();
    const uint32_t seed = r.u32();
    const uint8_t phase = r.u8();
    const int8_t winner = static_cast<int8_t>(r.u8());
    const uint8_t phaseTimer = r.u8();
    const float clock = r.f32();
    const uint8_t flags = r.u8();
    const uint8_t evCur = r.u8();
    const uint8_t evPrev = r.u8();
    if (!r.ok || stage > static_cast<uint8_t>(Stage::Playing) || phase > static_cast<uint8_t>(Phase::MatchEnd)) return;

    struct PlayerWire {
        int16_t h, vh, aim;
        uint8_t power, hitFlash, cooldown, score, item, flags;
    } pw[2];
    for (auto& p : pw) {
        p.h = r.i16();
        p.vh = r.i16();
        p.aim = r.i16();
        p.power = r.u8();
        p.hitFlash = r.u8();
        p.cooldown = r.u8();
        p.score = r.u8();
        p.item = r.u8();
        p.flags = r.u8();
    }
    const uint8_t count = r.u8();
    if (!r.ok || count > cfg::MAX_PROJ) return;

    struct ProjWire {
        uint8_t flags, typeOwner;
        int16_t x, y, vx, vy, rot;
        uint8_t fuse, blastT;
    } prw[cfg::MAX_PROJ];
    for (int i = 0; i < count; ++i) {
        ProjWire& p = prw[i];
        p.flags = r.u8();
        p.typeOwner = r.u8();
        p.x = r.i16();
        p.y = r.i16();
        p.vx = r.i16();
        p.vy = r.i16();
        p.rot = r.i16();
        p.fuse = r.u8();
        p.blastT = r.u8();
    }
    if (!r.ok) return;

    lastRxMs_ = now;
    int16_t diff = 1;
    if (rxSeqValid_) diff = static_cast<int16_t>(seq - rxSeq_);
    if (diff <= 0) return;  // old or duplicate
    const bool first = !rxSeqValid_;
    rxSeq_ = seq;
    rxSeqValid_ = true;

    peerTs_ = ts;
    haveEcho_ = true;
    if (flags & kFlagEcho) noteEcho(echo, now);

    if (!haveSnap_ || matchId != matchId_) {
        matchId_ = matchId;
        seed_ = seed;
        view_.begin(GameMode::Versus, seed);
        myAgain_ = false;
        haveSnap_ = true;
    }

    stage_ = static_cast<Stage>(stage);
    cd_ = cd / 10.0f;
    oppAgain_ = (flags & kFlagHostAgain) != 0;

    view_.phase = static_cast<Phase>(phase);
    view_.winner = winner;
    view_.phaseTimer = phaseTimer / 100.0f;
    view_.setClock(clock);

    for (int i = 0; i < 2; ++i) {
        Player& p = view_.players[i];
        const PlayerWire& w = pw[i];
        p.h = w.h / 16.0f;
        p.vh = w.vh / 16.0f;
        p.aimDeg = w.aim / 100.0f;
        p.power = w.power / 255.0f;
        p.hitFlash = w.hitFlash / 255.0f;
        p.cooldown = w.cooldown / 255.0f;
        p.score = w.score;
        p.item = static_cast<ItemType>(w.item <= static_cast<uint8_t>(ItemType::Boomerang) ? w.item : 0);
        p.itemUsed = (w.flags & kPlItemUsed) != 0;
        p.charging = (w.flags & kPlCharging) != 0;
        p.hit = (w.flags & kPlHit) != 0;
        p.powerDir = (w.flags & kPlPowerUp) ? 1 : -1;
        p.face = (w.flags & kPlFaceRight) ? 1 : -1;
        p.feetY = view_.platformY(i) - p.h;
    }

    for (auto& p : view_.proj) p = Projectile();
    for (int i = 0; i < count; ++i) {
        const ProjWire& w = prw[i];
        Projectile& p = view_.proj[i];
        p.active = true;
        p.exploding = (w.flags & kPrExploding) != 0;
        p.fuseOn = (w.flags & kPrFuseOn) != 0;
        const uint8_t type = w.typeOwner & 0x0F;
        p.type = static_cast<ItemType>(type <= static_cast<uint8_t>(ItemType::Boomerang) ? type : 0);
        p.owner = (w.typeOwner >> 4) & 1;
        p.x = w.x / 16.0f;
        p.y = w.y / 16.0f;
        p.vx = w.vx / 8.0f;
        p.vy = w.vy / 8.0f;
        p.rot = w.rot / 256.0f;
        p.fuse = w.fuse / 100.0f;
        p.blastT = w.blastT / 300.0f;
    }

    if (!first) pendingEv_ |= diff == 1 ? evCur : static_cast<uint32_t>(evCur | evPrev);
}

void Session::onReceive(const uint8_t* mac, const uint8_t* data, size_t len, uint32_t now) {
    if (!tx_ || len < 2 || data[0] != kMagic) return;
    Reader r(data, len);
    r.u8();
    const uint8_t type = r.u8();

    switch (type) {
        case PT_HELLO: {
            const uint8_t ver = r.u8();
            const uint32_t nonce = r.u32();
            if (!r.ok || ver != kVersion || state_ != State::Searching || nonce >= nonce_) break;
            // The higher nonce hosts, so the host reacts to the lower device's hello.
            memcpy(peer_, mac, 6);
            candNonce_ = nonce;
            sid_ = static_cast<uint16_t>(mix(nonce_, nonce) & 0xFFFF);
            role_ = Role::Host;
            state_ = State::Offering;
            stateSince_ = now;
            lastSend_ = now;
            sendOffer();
            break;
        }
        case PT_OFFER: {
            const uint32_t hostNonce = r.u32();
            const uint32_t clientNonce = r.u32();
            const uint16_t sid = r.u16();
            if (!r.ok || state_ != State::Searching || clientNonce != nonce_ || hostNonce <= nonce_) break;
            memcpy(peer_, mac, 6);
            candNonce_ = hostNonce;
            sid_ = sid;
            role_ = Role::Client;
            state_ = State::Accepting;
            stateSince_ = now;
            lastSend_ = now;
            sendAccept();
            break;
        }
        case PT_ACCEPT: {
            const uint32_t hostNonce = r.u32();
            const uint32_t clientNonce = r.u32();
            const uint16_t sid = r.u16();
            if (!r.ok || state_ != State::Offering || hostNonce != nonce_ || clientNonce != candNonce_) break;
            if (!fromPeer(mac, sid)) break;
            connect(Role::Host, now);
            break;
        }
        case PT_INPUT: {
            const uint16_t sid = r.u16();
            if (r.ok && state_ == State::Connected && role_ == Role::Host && fromPeer(mac, sid)) {
                parseInput(data, len, now);
            }
            break;
        }
        case PT_SNAP: {
            const uint16_t sid = r.u16();
            if (!r.ok || role_ != Role::Client || !fromPeer(mac, sid)) break;
            if (state_ == State::Accepting) connect(Role::Client, now);
            if (state_ == State::Connected) parseSnapshot(data, len, now);
            break;
        }
        case PT_BYE: {
            const uint16_t sid = r.u16();
            if (r.ok && state_ == State::Connected && fromPeer(mac, sid)) {
                state_ = State::Closed;
                closeReason_ = Status::PeerLeft;
            }
            break;
        }
        default:
            break;
    }
}

uint32_t Session::update(uint32_t now, const LocalInput& in) {
    float dt = (now - lastUpdate_) / 1000.0f;
    if (dt > 0.05f) dt = 0.05f;
    lastUpdate_ = now;

    switch (state_) {
        case State::Searching:
            if (now - lastSend_ >= kHelloMs + (nonce_ % 40u)) {
                lastSend_ = now;
                sendHello();
            }
            return 0;
        case State::Offering:
        case State::Accepting:
            if (now - stateSince_ > kPairTimeoutMs) {
                toSearching(now);
            } else if (now - lastSend_ >= kOfferMs) {
                lastSend_ = now;
                if (state_ == State::Offering) sendOffer();
                else sendAccept();
            }
            return 0;
        case State::Closed:
            return 0;
        case State::Connected:
            break;
    }

    const uint32_t quiet = now - lastRxMs_;
    if (quiet > kTimeoutMs) {
        state_ = State::Closed;
        closeReason_ = Status::TimedOut;
        return 0;
    }
    linkLost_ = quiet > kLostMs;
    return role_ == Role::Host ? updateHost(now, dt, in) : updateClient(now, in);
}

uint32_t Session::updateHost(uint32_t now, float dt, const LocalInput& in) {
    uint32_t ev = 0;
    if (in.jump) hostJump_ = true;
    const bool matchOver = stage_ == Stage::Playing && game_.phase == Phase::MatchEnd;
    if (matchOver && in.again) myAgain_ = true;

    if (!linkLost_) {
        switch (stage_) {
            case Stage::Ready:
                remote_.seenJump = remote_.jumpCount;
                hostJump_ = false;
                if (heard_) {
                    readyMs_ += dt * 1000.0f;
                    if (readyMs_ >= kReadyMs) {
                        stage_ = Stage::Countdown;
                        cd_ = kCountdownSec;
                    }
                }
                break;
            case Stage::Countdown:
                remote_.seenJump = remote_.jumpCount;
                hostJump_ = false;
                cd_ -= dt;
                if (cd_ <= 0.0f) {
                    cd_ = 0.0f;
                    stage_ = Stage::Playing;
                    accum_ = 0.0f;
                }
                break;
            case Stage::Playing:
                accum_ += dt;
                while (accum_ >= kTick) {
                    accum_ -= kTick;
                    PlayerInput pi[2];
                    pi[0].aimDeg = in.aimDeg;
                    pi[0].charge = in.charge;
                    pi[0].jump = hostJump_;
                    pi[0].jumpHeld = in.jumpHeld;
                    hostJump_ = false;
                    pi[1].aimDeg = remote_.aim;
                    pi[1].charge = remote_.charge;
                    pi[1].jumpHeld = remote_.jumpHeld;
                    pi[1].jump = remote_.jumpCount != remote_.seenJump;
                    remote_.seenJump = remote_.jumpCount;
                    ev |= game_.update(kTick, pi);
                }
                if (game_.phase == Phase::MatchEnd) {
                    oppAgain_ = remote_.again;
                    if (myAgain_ && oppAgain_) {
                        newMatch(now);
                        stage_ = Stage::Countdown;
                        cd_ = kCountdownSec;
                    }
                }
                break;
        }
    }

    evAcc_ |= ev;
    if (now - lastSend_ >= kSendMs) {
        lastSend_ = now;
        sendSnapshot(now);
    }
    return ev;
}

uint32_t Session::updateClient(uint32_t now, const LocalInput& in) {
    if (in.jump) ++jumpCount_;
    if (haveSnap_ && view_.phase == Phase::MatchEnd && in.again) myAgain_ = true;
    if (now - lastSend_ >= kSendMs) {
        lastSend_ = now;
        sendInput(in, now);
    }
    if (haveSnap_ && stage_ == Stage::Playing) {
        // Our own aim line follows the local sensor immediately instead of waiting for the host.
        float a = in.aimDeg;
        if (a < cfg::AIM_MIN_DEG) a = cfg::AIM_MIN_DEG;
        if (a > cfg::AIM_MAX_DEG) a = cfg::AIM_MAX_DEG;
        view_.players[1].aimDeg = a;
    }
    const uint32_t ev = pendingEv_;
    pendingEv_ = 0;
    return ev;
}

}  // namespace net
