#include "games/hwydrift/hwydrift.h"
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstring>

void hwy::Race::draw(int,const arcade::DrawCtx&) {}
void hwy::Race::sfx(uint32_t) {}
using namespace hwy;
constexpr float dt=1.f/60;
constexpr float pi=3.14159265f;

static void checkSnapshot(const Race& source) {
    arcade::Writer data;
    source.encode(data);
    Race copy;
    arcade::Reader good(data.buf,data.n);
    assert(copy.decode(good));
    arcade::Writer before;
    copy.encode(before);
    assert(before.n==data.n && !std::memcmp(before.buf,data.buf,data.n));
    for (size_t size=0;size<data.n;++size) {
        arcade::Reader truncated(data.buf,size);
        assert(!copy.decode(truncated));
    }
    data.buf[7]=data.buf[8]=0xff; // Position outside the atlas.
    arcade::Reader invalid(data.buf,data.n);
    assert(!copy.decode(invalid));
    arcade::Writer after;
    copy.encode(after);
    assert(after.n==before.n && !std::memcmp(after.buf,before.buf,after.n));
}

static void follow(Race& race,PadInput pads[2],int direction) {
    const Kart& k=race.cars[0];
    const int n=sections();
    const int ahead=(int(std::lround(k.section))+direction*3+n)%n;
    const Vec target=node(ahead)-k.pos;
    const float error=std::remainder(std::atan2(target.y,target.x)-k.heading,2*pi);
    pads[0].a=true;
    pads[0].aim=error*65;
    race.step(dt,pads);
}
static float turningRadius(float speed,bool drift) {
    Race r;
    r.begin(arcade::Mode::Host,0);
    PadInput pads[2];
    pads[0].aim=28;
    pads[0].b=drift;
    r.cars[0].vel={speed,0};
    r.step(dt,pads);
    return speed*dt/r.cars[0].heading;
}

static float slideAngle(const Kart& k) {
    return std::remainder(k.heading-std::atan2(k.vel.y,k.vel.x),2*pi)*180.f/pi;
}

static void checkDriftExit() {
    Race r;
    r.begin(arcade::Mode::Host,0);
    auto& k=r.cars[0];
    k.pos.x+=60;
    k.vel={70,0};
    PadInput pads[2];
    pads[0].a=true;
    pads[0].aim=20;
    Race grip=r;
    for (int i=0;i<18;++i) grip.step(dt,pads);
    pads[0].b=true;
    for (int i=0;i<18;++i) r.step(dt,pads);
    const float held=slideAngle(k);
    assert(held>slideAngle(grip.cars[0])+10 && held<55);
    checkSnapshot(r);
    Race opposite=r;
    pads[0].b=false;
    pads[0].aim=0;
    for (int i=0;i<6;++i) r.step(dt,pads);
    const float released=slideAngle(k);
    assert(!k.drift && released>4 && released<held);
    checkSnapshot(r);
    pads[0].aim=-16;
    for (int i=0;i<6;++i) opposite.step(dt,pads);
    const float caught=slideAngle(opposite.cars[0]);
    assert(std::fabs(caught)<released);
    pads[0].aim=0;
    for (int i=0;i<60;++i) r.step(dt,pads);
    assert(std::fabs(k.slip)<.03f);
    std::printf("Slide angle: held %.1f deg, 0.1s after release %.1f, with countersteer %.1f\n",held,released,caught);
}

static float magnitude(Vec v) { return std::sqrt(v.dot(v)); }

static void checkSlideControl() {
    Race r;
    r.begin(arcade::Mode::Host,0);
    auto& k=r.cars[0];
    k.pos.x+=60;
    k.vel={76,0};
    PadInput pads[2];
    pads[0].a=true;
    pads[0].aim=12;
    for (int i=0;i<12;++i) r.step(dt,pads);
    // Pressing B must not step the turning radius: the line bends a little more, not twice as much.
    auto course=[](const Kart& car) { return std::atan2(car.vel.y,car.vel.x); };
    float before=course(k);
    r.step(dt,pads);
    const float grip=course(k)-before;
    pads[0].b=true;
    before=course(k);
    r.step(dt,pads);
    const float first=course(k)-before;
    assert(first>0 && first<grip*1.5f);
    const float flick=std::fabs(k.slip);
    assert(flick>.08f && flick<.3f);
    for (int i=0;i<25;++i) r.step(dt,pads);
    const float held=k.slip;
    assert(held>.2f && std::fabs(k.pos.y-node(0).y)<10);
    // Light opposite lock trims the slide without ending it.
    Race trim=r;
    PadInput light[2]={};
    light[0].a=light[0].b=true;
    light[0].aim=-6.5f;
    for (int i=0;i<15;++i) trim.step(dt,light);
    assert(trim.cars[0].slip>.25f*held && trim.cars[0].slip<held);
    // Full opposite lock flicks the tail across.
    Race flicked=r;
    light[0].aim=-22;
    for (int i=0;i<30;++i) flicked.step(dt,light);
    assert(flicked.cars[0].slip<-.15f);
    std::printf("Slide control: kick %.2f rad, held %.2f, trimmed %.2f, flicked %.2f\n",
                flick,held,trim.cars[0].slip,flicked.cars[0].slip);
}

static void checkWalls() {
    const Vec start=node(0)+Vec{40,0};
    Race r;
    PadInput pads[2];
    pads[0].a=true;
    // Head on: stopped by the drawn wall, with an impact event.
    r.begin(arcade::Mode::Host,0);
    auto& k=r.cars[0];
    k.pos=start;
    k.heading=pi/2;
    k.vel={0,45};
    uint32_t events=0;
    for (int i=0;i<60;++i) events|=r.step(dt,pads)&15;
    assert(events&Race::Hit);
    assert(std::fabs(k.pos.y-start.y)<=kHalfWidth);
    // Glancing: the kart keeps most of its speed and is steered along the wall.
    r.begin(arcade::Mode::Host,0);
    k.pos=start+Vec{0,10};
    k.heading=.2f;
    k.vel={78,15.9f};
    pads[0].a=true;
    for (int i=0;i<40;++i) r.step(dt,pads);
    std::printf("Wall: glancing exit speed %.1f, heading %.3f, y offset %.1f\n",magnitude(k.vel),k.heading,k.pos.y-start.y);
    assert(std::fabs(k.pos.y-start.y)<=kHalfWidth);
    assert(std::fabs(k.heading)<.2f && magnitude(k.vel)>50);
}
int main() {
    checkDriftExit();
    checkSlideControl();
    checkWalls();
    Race race;
    PadInput pads[2];
    race.begin(arcade::Mode::Solo,0);
    const Vec grid=race.cars[0].pos;
    pads[0].stickX=pads[0].stickY=100;
    pads[0].b=pads[0].a=true;
    race.prime(pads);
    assert(race.cars[0].drift);
    assert(race.introTicks==330);
    while (race.introTicks) race.step(dt,pads);
    auto& k=race.cars[0];
    assert(k.pos.x==grid.x && k.pos.y==grid.y && k.heading==0 && race.tick==0 && k.drift);
    race.step(dt,pads);
    assert(k.drift && k.vel.x>0 && k.heading==0);
    pads[0].b=false;
    assert(turningRadius(12,false)<turningRadius(76,false));
    assert(turningRadius(76,true)<turningRadius(76,false));
    pads[0].a=true;
    for (int n=0;n<45;++n) race.step(dt,pads);
    assert(k.pos.x>grid.x+9 && k.heading==0);
    pads[0].b=true;
    pads[0].aim=22;
    float previous=k.heading;
    assert(race.step(dt,pads)&Race::Slide);
    assert(k.drift && k.heading>previous && std::atan2(k.vel.y,k.vel.x)<k.heading);
    pads[0].aim=0;
    previous=k.heading;
    pads[0].b=false;
    pads[0].a=false;
    float speed=k.vel.dot(k.vel);
    race.step(dt,pads);
    assert(std::fabs(k.heading-previous)<.01f && !k.drift && k.vel.dot(k.vel)<speed);
    race.begin(arcade::Mode::Solo,0);
    pads[0].a=true;
    for (int n=0;n<650 && k.pos.x<470;++n) {
        pads[0].aim=n%2 ? 3.9f : -3.9f;
        race.step(dt,pads);
        assert(k.heading==0 && k.pos.y==grid.y && !k.impact);
    }
    assert(k.pos.x>=470);
    pads[0]={};
    for (int j=0;j<2;++j) {
        race.begin(arcade::Mode::Solo,0);
        while (race.introTicks) race.step(dt,pads);
        Body& prop=race.props[j];
        prop.pos=grid+Vec{55,0};
        Vec initial=prop.pos;
        k.pos=prop.pos-Vec{7,0}; k.vel={45,0};
        uint32_t events=0;
        for (int n=0;n<8;++n) events|=race.step(dt,pads);
        assert((events&Race::Hit) && prop.pos.x>initial.x+1 && k.vel.x<40);
        checkSnapshot(race);
    }
    race.begin(arcade::Mode::Host,0);
    race.cars[1].pos=k.pos+Vec{5.8f,0};
    k.vel={32,0};
    const unsigned bump=race.step(dt,pads);
    assert((bump&Race::Hit) && ((bump>>4)&Race::Hit));
    assert(race.cars[1].vel.x>10 && k.vel.x<25);
    race.begin(arcade::Mode::Solo,0);
    bool warned=false;
    while (race.tick<1800 && k.laps>=0) {
        follow(race,pads,-1);
        warned|=k.wrongWay;
    }
    assert(warned && k.laps==-1 && !k.finish);
    // Complete a whole wrong-way lap, then turn around and earn back the distance.
    while (race.tick<4000 && k.laps>-2) follow(race,pads,-1);
    assert(k.laps==-2 && !k.finish);
    while (race.tick<12000 && !k.finish) follow(race,pads,1);
    assert(k.finish && race.phase()==arcade::MatchPhase::MatchEnd);
    checkSnapshot(race);
    race.begin(arcade::Mode::Solo,0);
    while (!k.finish && race.tick<6000) follow(race,pads,1);
    assert(k.finish && k.laps==3 && race.cars[1].pos.x==grid.x);
    const uint32_t finish=k.finish;
    race.step(dt,pads);
    assert(race.tick==finish);
    std::printf("Copper Mill: three laps %.2fs, wheel, coast, slide, collisions, reverse lap and snapshot passed\n",finish/60.f);
}

