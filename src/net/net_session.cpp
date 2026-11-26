#include "net/net_session.h"

#include <math.h>
#include <string.h>
#include <algorithm>

#include "core/platform.h"
#include "core/wire.h"

using arcade::q8;
using arcade::Reader;
using arcade::Writer;

namespace net {

namespace {

constexpr uint8_t kMagic = 0xD7;
constexpr uint8_t kVersion = 5;  // session protocol; the game has its own protoVer
constexpr uint8_t kRoom = 0;     // reserved: devices only pair inside the same room (a future room code)

enum : uint8_t { PT_HELLO = 1, PT_OFFER, PT_ACCEPT, PT_INPUT, PT_SNAP, PT_BYE };

constexpr uint32_t kHelloMs = 120;
constexpr uint32_t kOfferMs = 100;
constexpr uint32_t kPairTimeoutMs = 1500;
constexpr uint32_t kPeerSeenMs = 1500;
constexpr uint32_t kLostMs = 600;
constexpr uint32_t kTimeoutMs = 5000;
constexpr uint32_t kSendMs = 15;
constexpr float kReadyMs = 1000.0f;
constexpr float kCountdownSec = 3.0f;
constexpr int kByeCopies = 4;

// HELLO flag bits.
constexpr uint8_t kHelloJoinable = 1;
constexpr uint8_t kRandomGameId = 0;
constexpr uint8_t kRandomProtocol = 1;

// Snapshot flag bits.
constexpr uint8_t kFlagHostAgain = 1;
constexpr uint8_t kFlagClientAgain = 2;
constexpr uint8_t kFlagEcho = 4;

// Input flag bits.
constexpr uint8_t kInA = 1;
constexpr uint8_t kInB = 2;
constexpr uint8_t kInAgain = 4;
constexpr uint8_t kInEcho = 8;
constexpr uint8_t kInCalibrating = 16;

uint32_t mix(uint32_t a, uint32_t b) {
    uint32_t h = a * 2654435761u ^ (b + 0x9E3779B9u + (a << 6) + (a >> 2));
    h ^= h >> 15;
    h *= 2246822519u;
    h ^= h >> 13;
    return h ? h : 1u;
}

}  // namespace

size_t randomIndex(size_t count, uint32_t (*randomWord)()) {
    const uint32_t bound = static_cast<uint32_t>(count);
    const uint32_t threshold = (0u - bound) % bound;
    uint32_t word;
    do {
        word = randomWord();
    } while (word < threshold);
    return word % bound;
}

void Session::begin(Transport* tx, uint32_t nonce, uint32_t nowMs, const arcade::GameInfo& info,
                    arcade::IGame* game) {
    *this = Session();
    tx_ = tx;
    info_ = &info;
    game_ = game;
    nonce_ = nonce ? nonce : 1u;
    lastUpdate_ = nowMs;
    stateSince_ = nowMs;
    lastSend_ = nowMs - kHelloMs;
}

void Session::beginRandom(Transport* tx, uint32_t nonce, uint32_t nowMs,
                          uint32_t (*randomWord)(), LoadGame loadGame) {
    *this = Session();
    tx_ = tx;
    nonce_ = nonce ? nonce : 1u;
    lastUpdate_ = stateSince_ = nowMs;
    lastSend_ = nowMs - kHelloMs;
    randomWord_ = randomWord;
    loadGame_ = loadGame;
}

void Session::setRandomPool(const uint16_t* keys, size_t count) {
    randomCount_ = std::min(count, kRandomCapacity);
    std::copy_n(keys, randomCount_, randomPool_);
}

bool Session::hasRandomGame(uint16_t key) const {
    return std::find(randomPool_, randomPool_ + randomCount_, key) != randomPool_ + randomCount_;
}

bool Session::peerNearby(uint32_t nowMs) const { return peerSeen_ && nowMs - peerSeenMs_ < kPeerSeenMs; }

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

bool Session::connect(Role role, uint32_t now) {
    if (loadGame_) {
        if (!hasRandomGame(selectedKey_)) {
            toSearching(now);
            return false;
        }
        game_ = loadGame_(selectedKey_);
    }
    state_ = State::Connected;
    role_ = role;
    lastRxMs_ = now;
    stateSince_ = now;
    linkLost_ = false;
    rtt_ = -1;
    haveEcho_ = false;
    rxSeqValid_ = false;
    stage_ = Stage::Ready;
    if (!game_) {
        leave();
        closeReason_ = Status::TimedOut;
        return false;
    }
    if (role == Role::Host) {
        heard_ = false;
        readyMs_ = 0.0f;
        newMatch(now);
    }
    return true;
}

void Session::newMatch(uint32_t now) {
    ++matchId_;
    seed_ = mix(nonce_ ^ (candNonce_ << 1), now + matchId_);
    game_->begin(arcade::Mode::Host, seed_);
    myAgain_ = false;
    oppAgain_ = false;
    remoteAgain_ = false;
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
    w.u8(loadGame_ ? kRandomGameId : info_->wireId);
    w.u8(loadGame_ ? kRandomProtocol : info_->protoVer);
    w.u8(loadGame_ ? 2 : info_->maxPlayers);
    w.u8(kRoom);
    w.u8(joinable_ ? kHelloJoinable : 0);
    w.u32(nonce_);
    if (loadGame_) {
        w.u8(static_cast<uint8_t>(randomCount_));
        for (size_t i = 0; i < randomCount_; ++i) w.u16(randomPool_[i]);
    }
    tx_->send(nullptr, w.buf, w.n);
}

void Session::sendOffer() {
    Writer w;
    w.u8(kMagic);
    w.u8(PT_OFFER);
    w.u32(nonce_);
    w.u32(candNonce_);
    w.u16(sid_);
    if (loadGame_) w.u16(selectedKey_);
    tx_->send(peer_, w.buf, w.n);
}

void Session::sendAccept() {
    Writer w;
    w.u8(kMagic);
    w.u8(PT_ACCEPT);
    w.u32(candNonce_);  // the host's nonce
    w.u32(nonce_);
    w.u16(sid_);
    if (loadGame_) w.u16(selectedKey_);
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
    w.i16(arcade::q16(in.pad.aim, 100.0f));
    uint8_t flags = 0;
    if (in.pad.a) flags |= kInA;
    if (in.pad.b) flags |= kInB;
    if (myAgain_) flags |= kInAgain;
    if (haveEcho_) flags |= kInEcho;
    if (!in.ready) flags |= kInCalibrating;
    w.u8(flags);
    w.u8(in.pad.bCount);
    w.u8(in.pad.aCount);
    w.i16(arcade::q16(in.pad.pitch, 100.0f));
    w.u8(static_cast<uint8_t>(in.pad.stickX));
    w.u8(static_cast<uint8_t>(in.pad.stickY));
    w.u8(in.pad.jumpCount);
    w.u8(static_cast<uint8_t>(in.pad.jumpX));
    w.u8(static_cast<uint8_t>(in.pad.jumpY));
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

    uint8_t flags = 0;
    if (myAgain_) flags |= kFlagHostAgain;
    if (remoteAgain_) flags |= kFlagClientAgain;
    if (haveEcho_) flags |= kFlagEcho;
    w.u8(flags);

    const uint8_t evCur = static_cast<uint8_t>(evAcc_ & 0xFF);
    w.u8(evCur);
    w.u8(evLast_);
    evLast_ = evCur;
    evAcc_ = 0;

    game_->encode(w);
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
    const uint8_t bCount = r.u8();
    const uint8_t aCount = r.u8();
    const int16_t pitch = r.i16();
    const int8_t stickX = static_cast<int8_t>(r.u8());
    const int8_t stickY = static_cast<int8_t>(r.u8());
    const uint8_t jumpCount = r.u8();
    const int8_t jumpX = static_cast<int8_t>(r.u8());
    const int8_t jumpY = static_cast<int8_t>(r.u8());
    if (!r.ok) return;

    lastRxMs_ = now;
    if (rxSeqValid_ && static_cast<int16_t>(seq - rxSeq_) <= 0) return;
    rxSeq_ = seq;
    rxSeqValid_ = true;

    peerTs_ = ts;
    haveEcho_ = true;
    if (flags & kInEcho) noteEcho(echo, now);

    heard_ = true;
    remoteReady_ = !(flags & kInCalibrating);
    remotePad_.aim = aim / 100.0f;
    remotePad_.a = (flags & kInA) != 0;
    remotePad_.b = (flags & kInB) != 0;
    remotePad_.bCount = bCount;
    remotePad_.aCount = aCount;
    remotePad_.pitch = pitch / 100.0f;
    remotePad_.stickX = stickX;
    remotePad_.stickY = stickY;
    remotePad_.jumpCount = jumpCount;
    remotePad_.jumpX = jumpX;
    remotePad_.jumpY = jumpY;
    remoteAgain_ = (flags & kInAgain) != 0;
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
    const uint8_t flags = r.u8();
    const uint8_t evCur = r.u8();
    const uint8_t evPrev = r.u8();
    if (!r.ok || stage > static_cast<uint8_t>(Stage::Playing)) return;

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

    const bool fresh = !haveSnap_ || matchId != matchId_;
    if (fresh) {
        matchId_ = matchId;
        seed_ = seed;
        game_->begin(arcade::Mode::Client, seed);
        myAgain_ = false;
    }
    if (!game_->decode(r)) return;
    haveSnap_ = true;

    stage_ = static_cast<Stage>(stage);
    cd_ = cd / 10.0f;
    oppAgain_ = (flags & kFlagHostAgain) != 0;

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
            const uint8_t gameId = r.u8();
            const uint8_t proto = r.u8();
            const uint8_t maxPlayers = r.u8();
            const uint8_t room = r.u8();
            const uint8_t flags = r.u8();
            const uint32_t nonce = r.u32();
            if (!r.ok || ver != kVersion || room != kRoom ||
                gameId != (loadGame_ ? kRandomGameId : info_->wireId) ||
                proto != (loadGame_ ? kRandomProtocol : info_->protoVer) ||
                maxPlayers != (loadGame_ ? 2 : info_->maxPlayers)) {
                break;
            }
            peerSeen_ = true;
            peerSeenMs_ = now;
            if (state_ != State::Searching || !joinable_ || !(flags & kHelloJoinable) || nonce >= nonce_) break;
            if (loadGame_) {
                const size_t count = r.u8();
                if (count > kRandomCapacity || r.pos + count * 2 != len) break;
                uint16_t common[kRandomCapacity];
                size_t commonCount = 0;
                for (size_t i = 0; i < randomCount_; ++i) {
                    Reader remote = r;
                    for (size_t j = 0; j < count; ++j) {
                        if (remote.u16() != randomPool_[i]) continue;
                        common[commonCount++] = randomPool_[i];
                        break;
                    }
                }
                if (!commonCount) break;
                selectedKey_ = common[randomIndex(commonCount, randomWord_)];
            }
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
            if (!r.ok || state_ != State::Searching || !joinable_ || clientNonce != nonce_ || hostNonce <= nonce_) break;
            const uint16_t key = loadGame_ ? r.u16() : 0;
            if (!r.ok || r.pos != len || (loadGame_ && !hasRandomGame(key))) break;
            selectedKey_ = key;
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
            const uint16_t key = loadGame_ ? r.u16() : 0;
            if (!r.ok || r.pos != len || key != selectedKey_) break;
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
            if (state_ == State::Accepting && !connect(Role::Client, now)) break;
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
    const bool matchOver = stage_ == Stage::Playing && game_->phase() == arcade::MatchPhase::MatchEnd;
    if (matchOver && in.again) myAgain_ = true;

    if (!linkLost_) {
        const PadInput pads[2] = {in.pad, remotePad_};
        switch (stage_) {
            case Stage::Ready:
                game_->prime(pads);
                if (heard_ && in.ready && remoteReady_) {
                    readyMs_ += dt * 1000.0f;
                    if (readyMs_ >= kReadyMs) {
                        stage_ = Stage::Countdown;
                        cd_ = kCountdownSec + game_->gridPreviewSeconds();
                    }
                } else readyMs_ = 0.0f;
                break;
            case Stage::Countdown:
                game_->prime(pads);
                cd_ -= dt;
                if (cd_ <= 0.0f) {
                    cd_ = 0.0f;
                    stage_ = Stage::Playing;
                    accum_ = 0.0f;
                }
                break;
            case Stage::Playing:
                accum_ += dt;
                while (accum_ >= platform::kTick) {
                    accum_ -= platform::kTick;
                    const PadInput live[2] = {in.pad, remotePad_};
                    ev |= game_->step(platform::kTick, live);
                }
                if (game_->phase() == arcade::MatchPhase::MatchEnd) {
                    oppAgain_ = remoteAgain_;
                    if (myAgain_ && oppAgain_) {
                        newMatch(now);
                        stage_ = Stage::Countdown;
                        cd_ = kCountdownSec + game_->gridPreviewSeconds();
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
    if (haveSnap_ && game_->phase() == arcade::MatchPhase::MatchEnd && in.again) myAgain_ = true;
    if (now - lastSend_ >= kSendMs) {
        lastSend_ = now;
        sendInput(in, now);
    }
    if (haveSnap_ && stage_ == Stage::Playing) game_->localInput(1, in.pad);
    const uint32_t ev = pendingEv_;
    pendingEv_ = 0;
    return ev;
}

}  // namespace net
