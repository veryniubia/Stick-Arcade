#include "games/bumperduel/bumperduel.h"
#include <algorithm>
#include <cmath>
#include <new>
using std::min;
using std::max;
namespace {
constexpr float pi = 3.14159265359f;
float clamp(float v, float a, float b) { return max(a, min(b, v)); }
float wrap(float a) { return atan2f(sinf(a), cosf(a)); }
void bound(BumperDuelGame::Fighter& c) {
    float speed = hypotf(c.vx, c.vy);
    if (speed > 180) { c.vx *= 180 / speed; c.vy *= 180 / speed; }
}
arcade::IGame* create(void* m, arcade::Arena&) { return new (m) BumperDuelGame(); }
}
const arcade::GameInfo kBumperDuelInfo = {"bumperduel", 4, 5, sizeof(BumperDuelGame), 0, create, 2, true};
const BumperDuelGame::Point BumperDuelGame::cage[8] = {
    {24,25}, {111,25}, {132,46}, {132,201}, {111,222}, {24,222}, {3,201}, {3,46}};
const BumperDuelGame::Pillar BumperDuelGame::pillars[2] = {{54,90,7}, {81,157,7}};
float BumperDuelGame::radius(const Fighter& c) {
    float small = c.burger > 0 ? 9.f : kRadius;
    float large = c.burger > 0 ? 19.f : kBigRadius;
    if (c.pulse <= 0) return small;
    float age = .5f - c.pulse;
    float growth = age < .15f ? age / .15f : age < .35f ? 1 : c.pulse / .15f;
    return small + (large - small) * clamp(growth, 0, 1);
}
float BumperDuelGame::heading(int i, float tilt) const {
    if (!std::isfinite(tilt)) tilt = 0;
    int slot = i ^ (swapped ? 1 : 0);
    return wrap((slot ? -pi / 2 : pi / 2) + std::remainder(tilt, 90.f) * pi / 45);
}
void BumperDuelGame::begin(arcade::Mode mode, uint32_t s) {
    if (mode != arcade::Mode::Client) { swapped = begun_ ? !swapped : false; begun_ = true; }
    mode_ = mode; solo = mode == arcade::Mode::Solo; seed = s;
    elapsed = flash = 0; score[0] = score[1] = 0; winner = -1;
    phase_ = arcade::MatchPhase::Playing; drawnAt_ = 0;
    randomState = seed ? seed : 1; spawnTimer = 4;
    for (auto& item : items) item = Item();
    for (int i = 0; i < 2; ++i) {
        cars[i] = Fighter();
        int slot = i ^ (swapped ? 1 : 0);
        cars[i].x = slot ? 78 : 57; cars[i].y = slot ? 199 : 48;
        cars[i].angle = heading(i, 0);
        lastB_[i] = 0; heldB_[i] = false;
    }
}
void BumperDuelGame::prime(const PadInput p[2]) {
    for (int i = 0; i < 2; ++i) { lastB_[i] = p[i].bCount; heldB_[i] = p[i].b; }
}
void BumperDuelGame::drive(int i, const PadInput& p, float dt) {
    auto& c = cars[i];
    // The idle solo opponent has no sensor input and keeps its initial heading.
    if (!(solo && i == 1)) c.angle = heading(i, p.aim);
    c.control = (p.a ? 1 : 0) | (p.b ? 2 : 0);
    float drag = c.recoil > 0 ? 1.6f : 12.f;
    float target = c.slide > 0 ? 0 : p.a ? (c.shoes > 0 ? 80.f : 64.f) : 0;
    float blend = 1 - expf(-drag * dt);
    c.vx += (cosf(c.angle) * target - c.vx) * blend;
    c.vy += (sinf(c.angle) * target - c.vy) * blend;
    c.protection = max(0.f, c.protection - dt);
    c.recoil = max(0.f, c.recoil - dt);
    c.pulse = max(0.f, c.pulse - dt);
    c.slide = max(0.f, c.slide - dt);
    bound(c); c.x += c.vx * dt; c.y += c.vy * dt;
}
bool BumperDuelGame::environment(Fighter& c) {
    bool hit = false;
    float r = radius(c);
    auto contact = [&](float nx, float ny, float depth) {
        if (depth <= 0) return;
        hit = true; c.slide = 0; c.x += nx * (depth + .02f); c.y += ny * (depth + .02f);
        float vn = c.vx * nx + c.vy * ny;
        if (vn < 0) { c.vx -= 1.55f * vn * nx; c.vy -= 1.55f * vn * ny; }
        // A scoring injury always knocks inward, even from a stationary/expanding contact.
        if (c.protection <= 0) {
            float inward = c.vx * nx + c.vy * ny;
            c.vx += nx * max(0.f, 105 - inward); c.vy += ny * max(0.f, 105 - inward);
        }
        c.recoil = max(c.recoil, .65f);
    };
    auto walls = [&]() {
        for (int n = 0; n < 8; ++n) {
            auto a = cage[n], b = cage[(n + 1) % 8];
            float dx = b.x - a.x, dy = b.y - a.y, length = hypotf(dx, dy);
            float nx = -dy / length, ny = dx / length;
            contact(nx, ny, r - ((c.x - a.x) * nx + (c.y - a.y) * ny));
        }
    };
    walls();
    for (const auto& p : pillars) {
        float dx = c.x - p.x, dy = c.y - p.y, d = hypotf(dx, dy);
        contact(d > .001f ? dx / d : 1, d > .001f ? dy / d : 0, p.r + r - d);
    }
    walls(); bound(c); return hit;
}
uint32_t BumperDuelGame::collide() {
    auto& a = cars[0]; auto& b = cars[1];
    float dx = b.x - a.x, dy = b.y - a.y, d = hypotf(dx, dy);
    float depth = radius(a) + radius(b) - d;
    if (depth < 0) return 0;
    float nx = d > .001f ? dx / d : 1, ny = d > .001f ? dy / d : 0;
    // A sole inflated body cannot receive new opponent displacement/impulse.
    // Existing velocity is retained, so inflating never cancels an earlier knockback.
    float massA = a.pulse > 0 && b.pulse <= 0 ? 0 : a.burger > 0 ? .7f : 1.f;
    float massB = b.pulse > 0 && a.pulse <= 0 ? 0 : b.burger > 0 ? .7f : 1.f;
    float total = massA + massB;
    a.x -= nx * (depth + .02f) * massA / total;
    a.y -= ny * (depth + .02f) * massA / total;
    b.x += nx * (depth + .02f) * massB / total;
    b.y += ny * (depth + .02f) * massB / total;
    float closing = (a.vx - b.vx) * nx + (a.vy - b.vy) * ny;
    if (closing > 0) {
        float impulse = closing * 1.3f / total;
        a.vx -= nx * impulse * massA; a.vy -= ny * impulse * massA;
        b.vx += nx * impulse * massB; b.vy += ny * impulse * massB;
        if (massA > 0) a.recoil = max(a.recoil, .18f);
        if (massB > 0) b.recoil = max(b.recoil, .18f);
    }
    bool attackA = a.pulse > .15f && !a.struck;
    bool attackB = b.pulse > .15f && !b.struck;
    // Both bursts are evaluated together; hamburger reduces incoming burst impulse by 30%.
    if (attackA) { a.struck = true; float kick = b.burger > 0 ? 101.5f : 145.f; b.vx += nx*kick; b.vy += ny*kick; b.recoil = .65f; }
    if (attackB) { b.struck = true; float kick = a.burger > 0 ? 101.5f : 145.f; a.vx -= nx*kick; a.vy -= ny*kick; a.recoil = .65f; }
    bound(a); bound(b);
    return attackA || attackB || closing > 20 ? Impact : 0;
}
void BumperDuelGame::settleScores(uint8_t hits) {
    if (phase_ != arcade::MatchPhase::Playing) return;
    for (int i = 0; i < 2; ++i) if (hits & (1 << i)) score[i] = min(5, int(score[i]) + 1);
    if (score[0] >= 5 || score[1] >= 5) {
        winner = score[0] >= 5 ? (score[1] >= 5 ? -1 : 0) : 1;
        phase_ = arcade::MatchPhase::MatchEnd;
    }
}
uint32_t BumperDuelGame::step(float dt, const PadInput in[2]) {
    if (mode_ == arcade::Mode::Client || phase_ != arcade::MatchPhase::Playing || !std::isfinite(dt) || dt <= 0) return 0;
    dt = min(dt, .05f); elapsed += dt; flash = max(0.f, flash - dt);
    PadInput p[2] = {in[0], solo ? PadInput() : in[1]};
    uint32_t ev = 0; uint8_t hits = 0;
    for (int i = 0; i < 2; ++i) {
        auto& c = cars[i]; c.cooldown = max(0.f, c.cooldown - dt);
        c.burger = max(0.f, c.burger - dt);
        c.shoes = max(0.f, c.shoes - dt);
        c.shield = max(0.f, c.shield - dt);
        bool press = p[i].bCount != lastB_[i] || (p[i].b && !heldB_[i]);
        lastB_[i] = p[i].bCount; heldB_[i] = p[i].b;
        if (press && c.cooldown <= 0) { c.pulse = .5f; c.cooldown = 3; c.struck = false; ev |= Inflate; }
    }
    int steps = max(4, int(ceilf(dt * 240)));
    float sub = dt / steps;
    auto hazards = [&](int i) {
        auto& c = cars[i];
        if (environment(c) && c.protection <= 0) {
            if (c.shield > 0) {
                c.shield = 0; c.protection = .35f; ev |= Block;
                return;
            }
            c.protection = 1; hits |= 1 << (1 - i); flash = .24f;
            ev |= i ? Hit0 : Hit1;
        }
    };
    for (int n = 0; n < steps; ++n) {
        for (int i = 0; i < 2; ++i) { drive(i, p[i], sub); hazards(i); }
        ev |= collide();
        for (int i = 0; i < 2; ++i) hazards(i);
        ev |= updateItems(sub);
        for (int i = 0; i < 2; ++i) hazards(i);
    }
    settleScores(hits);
    if (phase_ == arcade::MatchPhase::MatchEnd) ev |= Finish;
    return ev;
}
uint32_t BumperDuelGame::random() {
    randomState ^= randomState << 13;
    randomState ^= randomState >> 17;
    randomState ^= randomState << 5;
    return randomState;
}
bool BumperDuelGame::clearAt(float x, float y, float margin) const {
    for (int n = 0; n < 8; ++n) {
        auto a = cage[n], b = cage[(n+1)%8];
        float dx = b.x-a.x, dy = b.y-a.y;
        if (((x-a.x)*(-dy)+(y-a.y)*dx)/hypotf(dx,dy) < margin) return false;
    }
    for (auto p : pillars) if (hypotf(x-p.x,y-p.y) < p.r+margin) return false;
    return true;
}
void BumperDuelGame::spawnItem() {
    for (auto& item : items) {
        if (item.type != None) continue;
        for (int attempt = 0; attempt < 96; ++attempt) {
            float x = 25 + random()%86, y = 50 + random()%148;
            if (!clearAt(x,y,24)) continue;
            bool free = true;
            for (const auto& c : cars) if (hypotf(c.x-x,c.y-y) < radius(c)+10) free = false;
            for (const auto& other : items) if (other.type != None && hypotf(other.x-x,other.y-y) < 28) free = false;
            if (!free) continue;
            item.x=x; item.y=y; item.type=ItemType(1+random()%4); item.warning=.5f; item.ttl=12.5f;
            return;
        }
        return;
    }
}
uint32_t BumperDuelGame::collect(int i, ItemType type) {
    auto& c = cars[i];
    if (type == Burger) c.burger = 6;
    if (type == Shoes) c.shoes = 6;
    if (type == Shield) c.shield = 6;
    if (type == Banana) {
        c.vx = cosf(c.angle)*170; c.vy = sinf(c.angle)*170;
        c.slide = .35f; c.recoil = .4f;
    }
    return Pickup | (type == Banana ? Slide : 0);
}
uint32_t BumperDuelGame::updateItems(float dt) {
    uint32_t events = 0;
    for (auto& item : items) {
        if (item.type == None) continue;
        item.ttl = max(0.f,item.ttl-dt);
        if (item.ttl <= .001f) { item=Item(); continue; }
        item.warning = max(0.f,item.warning-dt);
        if (item.warning > 0) continue;
        float dist[2] = {hypotf(cars[0].x-item.x,cars[0].y-item.y),hypotf(cars[1].x-item.x,cars[1].y-item.y)};
        bool near[2] = {dist[0] <= radius(cars[0])+4,dist[1] <= radius(cars[1])+4};
        if (!near[0] && !near[1]) continue;
        int winner = near[0] ? 0 : 1;
        if (near[0] && near[1]) winner = fabsf(dist[0]-dist[1]) < .01f ? int(random()&1) : dist[0]<dist[1] ? 0:1;
        events |= collect(winner,item.type); item=Item();
    }
    spawnTimer = max(0.f,spawnTimer-dt);
    if (spawnTimer <= 0) { spawnItem(); spawnTimer = 6 + (random()%2001)/1000.f; }
    return events;
}
void BumperDuelGame::encode(arcade::Writer& w) const {
    w.u32(seed); w.u8(uint8_t(phase_)); w.u8(uint8_t(winner));
    w.u8((solo ? 1 : 0) | (swapped ? 2 : 0)); w.f32(elapsed);
    w.u8(score[0]); w.u8(score[1]); w.u8(arcade::q8(flash, 100));
    for (const auto& c : cars) {
        w.i16(arcade::q16(c.x,64)); w.i16(arcade::q16(c.y,64));
        w.i16(arcade::q16(c.vx,64)); w.i16(arcade::q16(c.vy,64));
        w.i16(arcade::q16(c.angle,8192)); w.u8(arcade::q8(c.protection,100));
        w.u16(uint16_t(lroundf(c.cooldown * 1000))); w.u8(arcade::q8(c.pulse, 250));
        w.u8(arcade::q8(c.recoil,250)); w.u8(c.control); w.u8(c.struck ? 1 : 0);
        w.u16(uint16_t(lroundf(c.burger*1000))); w.u16(uint16_t(lroundf(c.shoes*1000)));
        w.u16(uint16_t(lroundf(c.shield*1000))); w.u8(arcade::q8(c.slide,250));
    }
    w.u32(randomState); w.u16(uint16_t(lroundf(spawnTimer*1000)));
    for (const auto& item : items) {
        w.i16(arcade::q16(item.x,64)); w.i16(arcade::q16(item.y,64)); w.u8(uint8_t(item.type));
        w.u8(arcade::q8(item.warning,250)); w.u16(uint16_t(lroundf(item.ttl*1000)));
    }
}
bool BumperDuelGame::decode(arcade::Reader& r) {
    BumperDuelGame next = *this;
    next.seed = r.u32(); auto phase = r.u8(); next.winner = int8_t(r.u8());
    auto flags = r.u8(); next.elapsed = r.f32();
    next.score[0] = r.u8(); next.score[1] = r.u8(); next.flash = r.u8() / 100.f;
    if ((phase != 0 && phase != 2) || flags > 3 || !std::isfinite(next.elapsed) || next.elapsed < 0 ||
        next.score[0] > 5 || next.score[1] > 5 || next.flash > .25f || next.winner < -1 || next.winner > 1) return false;
    next.phase_ = arcade::MatchPhase(phase); next.solo = flags & 1; next.swapped = flags & 2;
    bool end = next.score[0] == 5 || next.score[1] == 5;
    int expected = next.score[0] == 5 ? (next.score[1] == 5 ? -1 : 0) : 1;
    if (end != (phase == 2) || (!end && next.winner != -1) || (end && next.winner != expected)) return false;
    for (auto& c : next.cars) {
        c.x = r.i16()/64.f; c.y = r.i16()/64.f; c.vx = r.i16()/64.f; c.vy = r.i16()/64.f;
        c.angle = r.i16()/8192.f; c.protection = r.u8()/100.f; c.cooldown = r.u16()/1000.f;
        c.pulse = r.u8()/250.f; c.recoil = r.u8()/250.f; c.control = r.u8(); auto struck = r.u8(); c.struck = struck;
        c.burger = r.u16()/1000.f; c.shoes = r.u16()/1000.f; c.shield = r.u16()/1000.f; c.slide = r.u8()/250.f;
        if (c.burger>6 || c.shoes>6 || c.shield>6 || c.slide>.352f) return false;
        if (c.x < kLeft || c.x > kRight || c.y < kTop || c.y > kBottom || hypotf(c.vx,c.vy) > 180.03f ||
            fabsf(c.angle) > pi+.001f || c.protection > 1.01f || c.cooldown > 3 || c.pulse > .5f ||
            c.recoil > .66f || c.control > 3 || struck > 1) return false;
    }
    next.randomState = r.u32(); next.spawnTimer = r.u16()/1000.f;
    if (!next.randomState || next.spawnTimer>8) return false;
    for (auto& item : next.items) {
        item.x=r.i16()/64.f; item.y=r.i16()/64.f; auto type=r.u8(); item.type=ItemType(type);
        item.warning=r.u8()/250.f; item.ttl=r.u16()/1000.f;
        if (type>Banana || item.warning>.5f || item.ttl>12.5f) return false;
        if (type==None) {
            if (item.x!=0 || item.y!=0 || item.warning!=0 || item.ttl!=0) return false;
        } else if (item.x<kLeft || item.x>kRight || item.y<kTop || item.y>kBottom || item.ttl<=0) return false;
    }
    if (!r.ok || r.pos != r.len) return false;
    *this = next; return true;
}
void BumperDuelGame::fixture(char key) {
    // Hazard fixtures use real simulation; y deliberately puts both bodies on spikes in the same tick.
    uint8_t s0 = score[0], s1 = score[1]; bool swap = swapped;
    uint8_t edges[2] = {lastB_[0], lastB_[1]}; bool held[2] = {heldB_[0], heldB_[1]};
    begin(mode_, seed); swapped = swap; score[0] = s0; score[1] = s1;
    for (int i = 0; i < 2; ++i) { lastB_[i] = edges[i]; heldB_[i] = held[i]; }
    for (int i = 0; i < 2; ++i) cars[i].angle = heading(i,0);
    if (key == 'c') score[0] = score[1] = 0;
    if (key == 'w') { score[0] = 4; score[1] = 0; }
    if (key == 'y') score[0] = score[1] = 4;
    if (key == 'c' || key == 's' || key == 'w' || key == 'y') { cars[1].x = 126; cars[1].y = 120; }
    if (key == 'y') { cars[0].x = 9; cars[0].y = 120; }
    if (key == 'r') { cars[0].x = cars[1].x = 67; cars[0].y = cars[1].y = 123; }
    if (key == '9') {
        for (int i=0;i<2;++i) {
            cars[i].x=i ? 92:43; cars[i].y=123;
            cars[i].burger=cars[i].shoes=cars[i].shield=6;
            cars[i].pulse=.35f; cars[i].cooldown=3;
        }
        for (int i=0;i<2;++i) { items[i].x=i ? 103:32; items[i].y=i ? 185:60; items[i].type=i ? Banana:Shoes; items[i].ttl=12; }
    }
    if (key >= '5' && key <= '8') {
        items[0] = Item(); items[0].type=ItemType(key-'4');
        items[0].x=cars[0].x; items[0].y=cars[0].y; items[0].ttl=12;
    }
    if (key == 'h' || key == 't' || key == 'p') {
        cars[0].x = 57; cars[0].y = 123; cars[1].x = 77; cars[1].y = 123;
        if (key == 't') { cars[0].pulse = .5f; cars[0].cooldown = 3; }
        if (key == 'p') { cars[0].pulse = cars[1].pulse = .5f; cars[0].cooldown = cars[1].cooldown = 3; }
    }
}
#ifdef DEBUG_REMOTE
bool BumperDuelGame::debugKey(char key) {
    if (key >= '5' && key <= '9') { fixture(key); return true; }
    if (key == 'C') { fixture('s'); return true; }
    if (key == 'c' || key == 'h' || key == 't' || key == 'p' || key == 'w' || key == 'r' || key == 'y') { fixture(key); return true; }
    return false;
}
#endif
