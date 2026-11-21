#pragma once
#include "core/game_iface.h"

class BumperDuelGame final : public arcade::IGame {
 public:
    static constexpr float kRadius = 7, kBigRadius = 16;
    static constexpr float kLeft = 3, kRight = 132, kTop = 25, kBottom = 222;
    struct Fighter {
        float x = 0, y = 0, vx = 0, vy = 0, angle = 0;
        float protection = 0, cooldown = 0, pulse = 0, recoil = 0;
        float burger = 0, shoes = 0, shield = 0, slide = 0;
        uint8_t control = 0;
        bool struck = false;
    };
    struct Point { float x, y; };
    struct Pillar { float x, y, r; };
    enum ItemType : uint8_t { None, Burger, Shoes, Shield, Banana };
    struct Item {
        float x = 0, y = 0, warning = 0, ttl = 0;
        ItemType type = None;
    };
    Item items[2];
    uint32_t randomState = 1;
    float spawnTimer = 4;
    static const Point cage[8];
    static const Pillar pillars[2];
    enum Event : uint32_t { Impact = 1, Hit0 = 2, Hit1 = 4, Finish = 8, Inflate = 16, Pickup = 32, Block = 64, Slide = 128 };
    Fighter cars[2]; // Existing debug/network fixture integration uses this array name.
    uint8_t score[2] = {};
    uint32_t seed = 0;
    bool solo = false, swapped = false;
    int8_t winner = -1;
    float elapsed = 0, flash = 0;
    arcade::MatchPhase phase_ = arcade::MatchPhase::Playing;
    void begin(arcade::Mode, uint32_t) override;
    void prime(const PadInput[2]) override;
    uint32_t step(float, const PadInput[2]) override;
    arcade::MatchPhase phase() const override { return phase_; }
    bool idle(int) const override { return phase_ == arcade::MatchPhase::MatchEnd; }
    void encode(arcade::Writer&) const override;
    bool decode(arcade::Reader&) override;
    void draw(int, const arcade::DrawCtx&) override;
    void sfx(uint32_t) override;
    static float radius(const Fighter&);
    float heading(int player, float tilt) const;
    void settleScores(uint8_t);
    void fixture(char);
#ifdef DEBUG_REMOTE
    bool debugKey(char) override;
#endif
 private:
    arcade::Mode mode_ = arcade::Mode::Solo;
    bool begun_ = false;
    uint8_t lastB_[2] = {};
    bool heldB_[2] = {};
    float renderX_[2] = {}, renderY_[2] = {}, renderA_[2] = {};
    uint32_t drawnAt_ = 0;
    int viewLocal_ = 0;
    void drive(int, const PadInput&, float);
    bool environment(Fighter&);
    uint32_t collide();
    uint32_t random();
    bool clearAt(float x, float y, float margin) const;
    void spawnItem();
    uint32_t updateItems(float dt);
    uint32_t collect(int player, ItemType type);
};
extern const arcade::GameInfo kBumperDuelInfo;
