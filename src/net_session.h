#pragma once

#include <stddef.h>
#include <stdint.h>

#include "game.h"

// Online versus session: pairing, host-authoritative simulation and state snapshots.
// Hardware independent: packets go through a Transport, time is passed in by the caller.
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
    float aimDeg = 0.0f;
    bool charge = false;
    bool jump = false;      // button B pressed since the previous call
    bool jumpHeld = false;
    bool again = false;     // button A pressed since the previous call (rematch request at match end)
};

class Session {
public:
    void begin(Transport* tx, uint32_t nonce, uint32_t nowMs);
    void onReceive(const uint8_t* mac, const uint8_t* data, size_t len, uint32_t nowMs);
    // Call every frame. Returns the game events (EV_*) to play sounds for.
    uint32_t update(uint32_t nowMs, const LocalInput& in);
    // Tells the peer we are leaving.
    void leave();

    Status status() const;
    Role role() const { return role_; }
    int localIndex() const { return role_ == Role::Client ? 1 : 0; }
    const Game& game() const { return role_ == Role::Client ? view_ : game_; }
    float countdownSec() const { return cd_; }
    int rttMs() const { return rtt_; }  // -1 until measured
    bool myAgain() const;
    bool oppAgain() const;

private:
    enum class State : uint8_t { Searching, Offering, Accepting, Connected, Closed };
    enum class Stage : uint8_t { Ready, Countdown, Playing };

    struct Remote {
        float aim = 0.0f;
        bool charge = false;
        bool jumpHeld = false;
        bool again = false;
        uint8_t jumpCount = 0;
        uint8_t seenJump = 0;
    };

    Transport* tx_ = nullptr;
    uint32_t nonce_ = 1;
    uint32_t candNonce_ = 0;
    State state_ = State::Searching;
    Role role_ = Role::None;
    Status closeReason_ = Status::PeerLeft;
    uint8_t peer_[6] = {};
    uint16_t sid_ = 0;

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
    Game game_;
    Remote remote_;
    bool heard_ = false;
    bool hostJump_ = false;
    float readyMs_ = 0.0f;
    float accum_ = 0.0f;
    uint32_t evAcc_ = 0;
    uint8_t evLast_ = 0;
    uint16_t snapSeq_ = 0;

    // Client side.
    Game view_;
    bool haveSnap_ = false;
    uint8_t jumpCount_ = 0;
    uint32_t pendingEv_ = 0;

    void toSearching(uint32_t now);
    void connect(Role role, uint32_t now);
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
