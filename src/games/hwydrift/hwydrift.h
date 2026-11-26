#pragma once
#include "core/game_iface.h"
#include "games/hwydrift/course.h"

namespace hwy {
constexpr int kProps = 5;
constexpr int kLaps = 3;
constexpr int kFocusTicks = 60, kCountTicks = 180, kGridTicks = 90+kFocusTicks+kCountTicks;
constexpr float kEngineAcceleration = 84.f, kPoweredDrag = .95f;
struct Body { Vec pos, vel; };
struct Kart : Body {
    float heading = 0;
    float slip = 0; // chassis yaw relative to the direction of rear-axle traction, radians
    float section = 0;
    int16_t laps = 0;
    bool drift = false, wrongWay = false;
    uint8_t impact = 0;
    uint32_t finish = 0;
};
class Race : public arcade::IGame {
public:
    enum Event { Slide = 1, Lap = 2, Hit = 4, Finished = 8 };
    Kart cars[2];
    Body props[kProps]; // Alternating loose tyre and heavier wooden crate.
    uint32_t tick = 0;
    uint16_t introTicks = 0;
    bool solo = true;
    float gridPreviewSeconds() const override { return (kGridTicks-kCountTicks)/60.f; }
    void prime(const PadInput pads[2]) override;
    void begin(arcade::Mode mode, uint32_t seed) override;
    uint32_t step(float dt, const PadInput pads[2]) override;
    arcade::MatchPhase phase() const override;
    void encode(arcade::Writer& w) const override;
    bool decode(arcade::Reader& r) override;
    void draw(int local, const arcade::DrawCtx& ctx) override;
    void sfx(uint32_t events) override;
private:
    int localPlayer_ = 0;
    Vec cameraPos_;
    float cameraAngle_ = 0;
    uint32_t cameraMs_ = 0;
    struct Mark { Vec pos; float heading; uint32_t at; bool used; };
    static constexpr int kMarks = 24;
    Mark marks_[kMarks] = {};
    uint32_t drawTick_ = 0, lastMark_[2] = {};
    int markIndex_ = 0;
};
void drawCard(int x, int y, int w, int h, uint32_t ms);
void drawLobby(uint32_t ms);
}
extern const arcade::GameInfo kHwyDriftInfo;

