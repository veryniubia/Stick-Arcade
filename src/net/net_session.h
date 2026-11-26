#pragma once

#include <stddef.h>
#include <stdint.h>

#include "core/game_iface.h"
#include "core/pad.h"

// Online session for one game: pairing, host-authoritative simulation and state snapshots.
// Game independent (the game plugs in through arcade::IGame) and hardware independent: packets go through a
// Transport and the caller passes the time in.
namespace net {

class Transport {
public:
    virtual ~Transport() = default;
    // mac == nullptr broadcasts to everyone in range.
    virtual void send(const uint8_t* mac, const uint8_t* data, size_t len) = 0;
};

enum class Role : uint8_t { None, Host, Client };

enum class Status : uint8_t { Searching, Ready, Countdown, Playing, LinkLost, PeerLeft, TimedOut };

struct LocalInput {
    PadInput pad;
    bool again = false;  // A pressed since the previous call (rematch request at match end)
    bool ready = true;   // false while calibrating before the first countdown
};

constexpr size_t kRandomCapacity = 32;
constexpr uint16_t gameKey(const arcade::GameInfo& info) {
    return static_cast<uint16_t>((info.wireId << 8) | info.protoVer);
}
size_t randomIndex(size_t count, uint32_t (*randomWord)());

class Session {
public:
    // `game` is the one game object of this device: the host steps it, the client only decodes snapshots into it.
    void begin(Transport* tx, uint32_t nonce, uint32_t nowMs, const arcade::GameInfo& info, arcade::IGame* game);
    using LoadGame = arcade::IGame* (*)(uint16_t key);
    void beginRandom(Transport* tx, uint32_t nonce, uint32_t nowMs, uint32_t (*randomWord)(), LoadGame loadGame);
    void setRandomPool(const uint16_t* keys, size_t count);
    void onReceive(const uint8_t* mac, const uint8_t* data, size_t len, uint32_t nowMs);
    // Call every frame. Returns the game events to play sounds for.
    uint32_t update(uint32_t nowMs, const LocalInput& in);
    // Tells the peer we are leaving.
    void leave();

    // While not joinable the session keeps announcing itself and notices other players (peerNearby) but does
    // not pair. Solo play turns it on when the user accepts an opponent.
    void setJoinable(bool on) { joinable_ = on; }
    bool joinable() const { return joinable_; }
    // A device running the same game and protocol announced itself in the last second and a half.
    bool peerNearby(uint32_t nowMs) const;

    Status status() const;
    Role role() const { return role_; }
    int localIndex() const { return role_ == Role::Client ? 1 : 0; }
    arcade::IGame& game() const { return *game_; }
    float countdownSec() const { return cd_; }
    int rttMs() const { return rtt_; }  // -1 until measured
    bool myAgain() const;
    bool oppAgain() const;

private:
    enum class State : uint8_t { Searching, Offering, Accepting, Connected, Closed };
    enum class Stage : uint8_t { Ready, Countdown, Playing };

    const arcade::GameInfo* info_ = nullptr;
    arcade::IGame* game_ = nullptr;
    Transport* tx_ = nullptr;
    uint32_t nonce_ = 1;
    uint32_t candNonce_ = 0;
    State state_ = State::Searching;
    Role role_ = Role::None;
    Status closeReason_ = Status::PeerLeft;
    uint8_t peer_[6] = {};
    uint16_t sid_ = 0;
    bool joinable_ = true;
    bool peerSeen_ = false;
    uint32_t peerSeenMs_ = 0;
    uint32_t (*randomWord_)() = nullptr;
    LoadGame loadGame_ = nullptr;
    uint16_t randomPool_[kRandomCapacity] = {};
    size_t randomCount_ = 0;
    uint16_t selectedKey_ = 0;
    bool remoteReady_ = true;

    uint32_t lastUpdate_ = 0;
    uint32_t lastSend_ = 0;
    uint32_t stateSince_ = 0;
    uint32_t lastRxMs_ = 0;
    bool linkLost_ = false;
    int rtt_ = -1;
    uint16_t peerTs_ = 0;
    bool haveEcho_ = false;
    uint16_t txSeq_ = 0;
    uint16_t rxSeq_ = 0;
    bool rxSeqValid_ = false;

    Stage stage_ = Stage::Ready;
    float cd_ = 0.0f;
    uint8_t matchId_ = 0;
    uint32_t seed_ = 0;
    bool myAgain_ = false;
    bool oppAgain_ = false;

    // Host side.
    PadInput remotePad_;
    bool remoteAgain_ = false;
    bool heard_ = false;
    float readyMs_ = 0.0f;
    float accum_ = 0.0f;
    uint32_t evAcc_ = 0;
    uint8_t evLast_ = 0;
    uint16_t snapSeq_ = 0;

    // Client side.
    bool haveSnap_ = false;
    uint32_t pendingEv_ = 0;

    void toSearching(uint32_t now);
    bool connect(Role role, uint32_t now);
    bool hasRandomGame(uint16_t key) const;
    void newMatch(uint32_t now);
    uint32_t updateHost(uint32_t now, float dt, const LocalInput& in);
    uint32_t updateClient(uint32_t now, const LocalInput& in);
    void noteEcho(uint16_t echo, uint32_t now);

    void sendHello();
    void sendOffer();
    void sendAccept();
    void sendInput(const LocalInput& in, uint32_t now);
    void sendSnapshot(uint32_t now);
    bool fromPeer(const uint8_t* mac, uint16_t sid) const;
    void parseInput(const uint8_t* d, size_t len, uint32_t now);
    void parseSnapshot(const uint8_t* d, size_t len, uint32_t now);
};

}  // namespace net
