#include "games/springring/springring.h"
#include <algorithm>
#include <cmath>
#include <new>

namespace {
arcade::IGame* create(void* memory, arcade::Arena&) { return new(memory) springring::Match(); }
float limit(float x, float lo, float hi) { return std::max(lo, std::min(x, hi)); }
float approach(float x, float target, float dist) { return x + limit(target-x, -dist, dist); }
}
const arcade::GameInfo kSpringRingInfo = {"springring", 8, 1, sizeof(springring::Match), 0,
                                         create, 2, false, false, false, false};

namespace springring {
namespace {
constexpr float kInertia = 260.f; // moment of inertia per unit mass, in square pixels
constexpr float kGravity = 340.f;
constexpr float kPi = 3.14159265f;
constexpr int kPause = 45;
float side(int i) { return i ? 1.f : -1.f; }
bool onPad(float x) { return x >= kPadLeft && x <= kPadRight; }
float surface(float x) { return onPad(x) ? kPadY : kMat; }
bool bounded(float x, float lo, float hi) { return std::isfinite(x) && x >= lo && x <= hi; }

// Normal impulse through the actual contact point. A boot beyond the centre
// of mass turns an otherwise vertical push into a roll.
void support(Clinch& p, Point boot, float speed, float penetration) {
    const float arm = boot.x - p.x;
    const float effective = 1 + arm * arm / kInertia;
    const float push = std::max(0.f, p.vy + arm*p.spin + speed) / effective;
    p.vy -= push;
    p.spin -= arm*push/kInertia;
    const float correction = std::max(0.f, penetration) / effective;
    p.y -= correction;
    p.angle -= arm*correction/kInertia;
    const float height = boot.y-p.y;
    const float friction = limit((p.vx-height*p.spin)/(1+height*height/kInertia), -push*.6f, push*.6f);
    p.vx -= friction;
    p.spin += height*friction/kInertia;
}
}

Point Clinch::joint(float across, float down) const {
    float c = cosf(angle), s = sinf(angle);
    return {x + across*c-down*s, y + across*s+down*c};
}
Point Clinch::head(int i) const { return joint(side(i)*13, -12); }
Point Clinch::hip(int i) const { return joint(side(i)*15, 3); }
Point Clinch::boot(int i) const {
    const Leg& l = legs[i];
    // Bent knees tuck towards the chest. Even when tipped sideways, the
    // lower leg can unfold towards the mat, with a limited world-space reach.
    Point ankle = joint(side(i)*17, 8 + 8*l.length);
    return {ankle.x+l.reach, ankle.y+4+10*l.length};
}

void Match::begin(arcade::Mode mode, uint32_t) {
    mode_ = mode;
    pair = Clinch{};
    score[0] = score[1] = hitMask = freeze = flash = 0;
}
arcade::MatchPhase Match::phase() const {
    if (!hitMask) return arcade::MatchPhase::Playing;
    if (!freeze && (score[0]==3 || score[1]==3)) return arcade::MatchPhase::MatchEnd;
    return arcade::MatchPhase::RoundEnd;
}
uint32_t Match::step(float dt, const PadInput inputs[2]) {
    if (mode_ == arcade::Mode::Client) return 0;
    if (hitMask) {
        if (freeze && --freeze == 0 && score[0]<3 && score[1]<3) {
            pair = Clinch{};
            hitMask = flash = 0;
        }
        return 0;
    }
    if (flash) --flash;
    uint32_t events = 0;
    // Two short contact integrations within the platform tick limit tunnelling.
    const float h = dt*.5f;
    for (int sub=0; sub<2; ++sub) {
        bool extending[2];
        for (int i=0; i<2; ++i) {
            PadInput in = (i && mode_==arcade::Mode::Solo) ? PadInput{} : inputs[i];
            Leg& l = pair.legs[i];
            const float aim = std::isfinite(in.aim) ? in.aim : 0;
            const float target = copysignf(limit((fabsf(aim)-4)/20, 0, 1)*13, aim);
            l.reach = approach(l.reach, target, h*140);
            extending[i] = in.a && l.length < 1;
            l.length = approach(l.length, in.a ? 1.f : 0.f, h*(in.a ? 4.f : 6.f));
            if (!in.a && l.length < .15f) l.spent = false;
        }
        pair.vy += kGravity*h;
        pair.x += pair.vx*h;
        pair.y += pair.vy*h;
        pair.angle = remainderf(pair.angle + pair.spin*h, 2*kPi);
        pair.spin *= 1-.38f*h;
        pair.vx *= 1-.25f*h;
        // Keep the full pair on screen at the ropes.
        const float wall = limit(pair.x, 39, 201);
        if (wall != pair.x) { pair.x = wall; pair.vx *= -.45f; }
        for (int order=0; order<2; ++order) {
            const int i = order ^ sub;
            Leg& l = pair.legs[i];
            Point foot = pair.boot(i);
            const bool pad = onPad(foot.x);
            const float depth = foot.y + 2 - surface(foot.x);
            const bool contact = depth >= -.10f;
            const float arrival = pair.vy + (foot.x-pair.x)*pair.spin;
            float speed = 0;
            if (contact && extending[i] && !l.spent) {
                speed = pad ? 275 : 205;
                events |= pad ? Spring : Kick;
                l.spent = true;
            } else if (contact && !l.planted && pad && arrival > 48) {
                speed = std::min(128.f, arrival*.78f);
                events |= Spring;
            }
            if (contact) support(pair, foot, speed, depth);
            l.planted = contact;
        }
        if (events & Spring) flash = 13;
        pair.spin = limit(pair.spin, -10, 10);
        // Sample both heads before deciding the round. Accumulate contacts
        // across both integrations so simultaneous means the whole 60 Hz tick.
        for (int i=0; i<2; ++i) {
            Point head = pair.head(i);
            if (head.y+5 >= surface(head.x)) hitMask |= 1u << i;
        }
    }
    if (hitMask) {
        if (hitMask != 3) ++score[hitMask==1 ? 1 : 0];
        freeze = kPause;
        events |= Impact;
    }
    return events;
}

void Match::encode(arcade::Writer& w) const {
    // Only the host integrates velocities. The client needs the pose and cues.
    w.f32(pair.x); w.f32(pair.y); w.f32(pair.angle);
    for (const Leg& l : pair.legs) {
        w.f32(l.length); w.f32(l.reach); w.u8(l.planted);
    }
    w.u8(score[0]); w.u8(score[1]); w.u8(hitMask); w.u8(freeze); w.u8(flash);
}
bool Match::decode(arcade::Reader& r) {
    Clinch p{};
    p.x=r.f32(); p.y=r.f32(); p.angle=r.f32();
    if (!bounded(p.x, 38, 202) || !bounded(p.y, -256, 135) || !bounded(p.angle, -4, 4)) return false;
    for (Leg& l : p.legs) {
        l.length=r.f32(); l.reach=r.f32();
        uint8_t planted=r.u8();
        if (!bounded(l.length,0,1) || !bounded(l.reach,-13,13) || planted>1) return false;
        l.planted=planted!=0;
    }
    const uint8_t blue=r.u8(), red=r.u8(), hit=r.u8(), pause=r.u8(), glow=r.u8();
    const bool won = blue==3 || red==3;
    if (!r.ok || blue>3 || red>3 || hit>3 || pause>kPause || glow>13 ||
        (hit==0 && (pause || won)) || (hit && !pause && !won) ||
        (blue==3 && hit!=2) || (red==3 && hit!=1)) return false;
    pair=p; score[0]=blue; score[1]=red; hitMask=hit; freeze=pause; flash=glow;
    return true;
}
}
