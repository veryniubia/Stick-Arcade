#pragma once

#include <stddef.h>
#include <stdint.h>

#include "core/pad.h"
#include "core/wire.h"

// The contract between the platform and a game. A game is hardware independent except for draw() and sfx(),
// which live in a separate *_view.cpp so the rules and the network encoding can be tested on a PC.
namespace arcade {

enum class Mode : uint8_t {
    Solo,    // local only; the second player sends no input
    Host,    // authoritative simulation, the second player is remote
    Client,  // view only: state arrives through decode()
};

enum class MatchPhase : uint8_t { Playing, RoundEnd, MatchEnd };

struct DrawCtx {
    bool solo = false;
    uint32_t nowMs = 0;
};

class IGame {
public:
    virtual ~IGame() = default;

    // Starts a fresh match. `seed` is identical on both devices so static decoration lines up.
    virtual void begin(Mode mode, uint32_t seed) = 0;
    // One fixed platform::kTick step (Solo and Host). Returns event bits; only bits 0-7 reach the client.
    virtual uint32_t step(float dt, const PadInput in[2]) = 0;
    // Called every frame while the match has not started yet (READY, countdown) so press counters can be
    // synchronised: a press made before the first step() must not count as a press during play.
    virtual void prime(const PadInput in[2]) { (void)in; }
    virtual MatchPhase phase() const = 0;

    // Full game state for the client's view. decode() must not change the state when it returns false.
    virtual void encode(Writer& w) const = 0;
    virtual bool decode(Reader& r) = 0;

    // Client only, every frame: lets the local player's own cursor/aim follow the sensor without the host round trip.
    virtual void localInput(int local, const PadInput& in) { (void)local; (void)in; }
    // True while the local player has nothing to do (between rounds, after the throw): the platform may then
    // use A for its own prompts such as joining an opponent that showed up.
    virtual bool idle(int local) const { (void)local; return false; }

    virtual void draw(int local, const DrawCtx& ctx) = 0;
    virtual void sfx(uint32_t events) = 0;

#ifdef DEBUG_REMOTE
    virtual bool debugKey(char key) { (void)key; return false; }
#endif
};

// Linear allocator for everything a game needs besides its own object. It lives and dies with the game.
class Arena {
public:
    Arena(void* mem, size_t size) : base_(static_cast<uint8_t*>(mem)), size_(size) {}
    void* alloc(size_t bytes, size_t align = 8) {
        const size_t at = (used_ + align - 1) & ~(align - 1);
        if (at + bytes > size_) return nullptr;
        used_ = at + bytes;
        return base_ + at;
    }
    size_t used() const { return used_; }
    size_t size() const { return size_; }

private:
    uint8_t* base_;
    size_t size_;
    size_t used_ = 0;
};

struct GameInfo {
    const char* id;          // short lowercase name, also the settings namespace
    uint8_t wireId;          // goes into HELLO: devices running another game ignore each other
    uint8_t protoVer;        // bump when encode()/decode() change
    size_t instanceSize;     // sizeof the game object
    size_t arenaBytes;       // extra memory the game wants from its Arena (0 if none)
    IGame* (*create)(void* mem, Arena& arena);  // placement-new into mem
    uint8_t maxPlayers;      // announced in HELLO; the session itself only pairs two devices so far
};

// One block per running game: [object][arena]. Internal RAM on the device, malloc on a PC.
struct GameInstance {
    void* block = nullptr;
    IGame* game = nullptr;
};

GameInstance gameCreate(const GameInfo& info);
void gameDestroy(GameInstance& inst);

}  // namespace arcade
