#include "games/hwydrift/hwydrift.h"
#include <algorithm>
#include <cmath>
#include <new>

namespace {
arcade::IGame* create(void* mem, arcade::Arena&) { return new (mem) hwy::Race(); }
}
const arcade::GameInfo kHwyDriftInfo = {"hwydrift",7,4,sizeof(hwy::Race),0,create,2,true,false,false,true};

namespace hwy {
namespace {
constexpr float pi = 3.14159265f, carRadius = 3.1f*kKartScale;
float magnitude(Vec v) { return std::sqrt(v.dot(v)); }
float propRadius(int i) { return i%2 ? 3.5f : 3.f; }
float inverseMass(int i) { return i%2 ? .55f : 1.6f; }

void confine(Body& body, float radius, const Road& road) {
    const Vec offset = body.pos-road.center;
    const float margin = kHalfWidth-radius;
    const float dist = magnitude(offset);
    if (dist<=margin) return;
    const Vec normal = offset*(1.f/dist);
    body.pos = road.center+normal*margin;
    body.vel = body.vel-normal*(1.15f*std::max(0.f,body.vel.dot(normal)));
}

// Probe circles at the nose and tail match the drawn body. The kart is pushed off the wall, loses some speed and
// is steered along it, so scraping is slower than a clean line but never sticks. Returns the closing speed.
float scrape(Kart& car, const Road& road) {
    constexpr float probe = 1.7f*kKartScale, radius = 2.7f*kKartScale;
    const Vec dir = {std::cos(car.heading), std::sin(car.heading)};
    const float margin = kHalfWidth-radius;
    float impact = 0;
    bool touched = false;
    Vec along;
    for (int end=-1; end<=1; end+=2) {
        const Vec offset = car.pos+dir*(probe*end)-road.center;
        const float dist = magnitude(offset);
        if (dist<=margin) continue;
        const Vec normal = offset*(1.f/dist);
        car.pos = car.pos-normal*(dist-margin);
        const float closing = std::max(0.f,car.vel.dot(normal));
        car.vel = car.vel-normal*(1.1f*closing);
        if (!touched || closing>impact) {
            impact = closing;
            along = {-normal.y,normal.x};
        }
        touched = true;
    }
    if (!touched) return 0;
    car.vel = car.vel*(1.f-std::min(.28f,.004f+.006f*impact));
    if (impact>=8) {
        // A real hit swings the nose along the wall; light scrapes leave steering alone.
        if (car.vel.dot(along)<0) along = along*-1.f;
        const float error = std::remainder(std::atan2(along.y,along.x)-car.heading,2*pi);
        car.heading = std::remainder(car.heading+error*std::min(.3f,.012f*impact),2*pi);
    }
    return impact;
}

// Separate overlapping bodies and apply impulse only while approaching.
float contact(Body& a, Body& b, float separation, float invA, float invB) {
    const Vec delta = b.pos-a.pos;
    const float d2 = delta.dot(delta);
    if (d2>=separation*separation) return 0;
    const float d = std::sqrt(d2);
    const Vec normal = d>.001f ? delta*(1.f/d) : Vec{1,0};
    const float correction = (separation-d)/(invA+invB);
    a.pos = a.pos-normal*(correction*invA);
    b.pos = b.pos+normal*(correction*invB);
    const float closing = std::max(0.f,(a.vel-b.vel).dot(normal));
    const float impulse = closing*1.18f/(invA+invB);
    a.vel = a.vel-normal*(impulse*invA);
    b.vel = b.vel+normal*(impulse*invB);
    return closing;
}

unsigned drive(Kart& car, const PadInput& pad, float dt) {
    const float tilt = std::isfinite(pad.aim) ? pad.aim : 0;
    constexpr float deadband=4.f, fullLock=22.f, maxSlip=.67f;
    const float travel = std::min(1.f,std::max(0.f,(std::fabs(tilt)-deadband)/(fullLock-deadband)));
    const float wheel = std::copysign(travel*(.7f+.3f*travel),tilt);
    const float speed = magnitude(car.vel);
    const bool slide = pad.b;
    const bool entering = slide && !car.drift;
    car.drift = slide;
    const float oldSlip=car.slip;
    const float speedRatio=std::min(1.f,speed/68.f);
    // The tail kicks out as B goes down, so the response is felt before the slide builds.
    if (entering && travel>0)
        car.slip=std::copysign(std::min(maxSlip,std::fabs(car.slip)+.12f*speedRatio*std::min(1.f,travel*3.f)),tilt);
    const bool countersteer=wheel*oldSlip<0 && std::fabs(oldSlip)>.025f;
    float targetSlip=0,response=slide ? 5.5f : 4.6f;
    if (countersteer) {
        // Opposite lock unwinds the slide in proportion to how far the wheel is turned. A held
        // drift can be trimmed with light lock, and full lock flicks the tail over to the other side.
        const float lock=std::fabs(wheel);
        if (slide) targetSlip=oldSlip*(1.f-lock);
        response=slide ? 6.f+11.f*lock*lock : 10.f;
    } else if (slide && travel>0) {
        targetSlip=std::copysign((.26f+.41f*travel)*speedRatio,tilt);
    }
    car.slip+=(targetSlip-car.slip)*std::min(1.f,dt*response);
    car.slip=std::max(-maxSlip,std::min(maxSlip,car.slip));
    // Grip follows the visible slide, so pressing or releasing B never steps the turning radius.
    const float looseness=std::min(1.f,std::fabs(car.slip)/.30f);
    const float radius = 8.f+speed*speed*(.0068f-.0044f*looseness);
    const float turn = (speed+(pad.a ? 2.f : 0.f))/radius;
    // A sliding rear axle carries momentum around the bend after the wheel
    // centres. Opposite lock both changes the line and brings the tail back.
    const float axle=car.heading-oldSlip+(wheel*turn+.7f*car.slip*speedRatio)*dt;
    car.heading=std::remainder(axle+car.slip,2*pi);
    const float forceAngle=axle+.18f*car.slip;
    const Vec traction={std::cos(forceAngle),std::sin(forceAngle)};
    const float forward = car.vel.dot(traction);
    const Vec side = car.vel-traction*forward;
    const float drag = (pad.a ? kPoweredDrag : 1.65f)+.20f*std::fabs(car.slip);
    const float damping = std::max(0.f,1.f-(13.f-7.2f*looseness)*dt);
    float along = forward+((pad.a ? kEngineAcceleration : 0.f)-drag*forward)*dt;
    if (pad.a) {
        // Tyres bleeding off sideways speed hand part of it back, and a clean exit from a deep slide pulls forward.
        along += .3f*magnitude(side)*(1.f-damping);
        if (!slide) along += 100.f*std::max(0.f,std::fabs(car.slip)-.12f)*dt;
    }
    if (!pad.a && std::fabs(along)<.4f) along=0;
    car.vel = traction*along+side*damping;
    car.pos = car.pos+car.vel*dt;
    if (car.impact) --car.impact;
    return entering ? Race::Slide : 0;
}

void hit(Kart& car, float closing, unsigned& events) {
    if (closing<9 || car.impact) return;
    car.impact=14;
    events |= Race::Hit;
}

unsigned score(Kart& car, const Road& road, uint32_t tick) {
    const float difference=road.section-car.section;
    car.section=road.section;
    car.wrongWay=car.vel.dot(road.tangent)<-5;
    if (difference>sections()*.5f && car.laps>-32000) --car.laps;
    if (difference>=-sections()*.5f) return 0;
    if (++car.laps<kLaps) return car.laps>0 ? Race::Lap : 0;
    car.finish=tick;
    car.vel={};
    car.slip=0;
    car.drift=car.wrongWay=false;
    return Race::Finished;
}

void writeBody(arcade::Writer& w,const Body& body) {
    w.u16(uint16_t(std::lround(body.pos.x*64.f)));
    w.u16(uint16_t(std::lround(body.pos.y*64.f)));
    w.i16(arcade::q16(body.vel.x,100));
    w.i16(arcade::q16(body.vel.y,100));
}
bool readBody(arcade::Reader& r,Body& body) {
    body.pos={r.u16()/64.f,r.u16()/64.f};
    body.vel={r.i16()*.01f,r.i16()*.01f};
    return body.pos.x<kMapWidth && body.pos.y<kMapHeight && body.vel.dot(body.vel)<16000;
}
}

void Race::begin(arcade::Mode mode, uint32_t) {
    *this=Race();
    solo=mode==arcade::Mode::Solo;
    introTicks=solo ? kGridTicks : 0;
    localPlayer_=mode==arcade::Mode::Client;
    for (int i=0;i<2;++i) {
        cars[i].pos=node(0)+Vec{3,solo ? 0.f : (i ? 4.f : -4.f)};
        cars[i].section=nearestRoad(cars[i].pos).section;
    }
    const Vec positions[kProps]={{425,272},{550,175},{365,194},{220,92},{90,188}};
    for (int i=0;i<kProps;++i) props[i].pos=positions[i];
}

void Race::prime(const PadInput pads[2]) {
    for (int i=0;i<(solo ? 1 : 2);++i) cars[i].drift=pads[i].b;
}

arcade::MatchPhase Race::phase() const {
    return cars[0].finish && (solo || cars[1].finish) ? arcade::MatchPhase::MatchEnd : arcade::MatchPhase::Playing;
}

uint32_t Race::step(float dt, const PadInput pads[2]) {
    if (phase()==arcade::MatchPhase::MatchEnd) return 0;
    if (introTicks) {
        prime(pads);
        --introTicks;
        return 0;
    }
    ++tick;
    unsigned events[2]={};
    const int count=solo ? 1 : 2;
    for (int i=0;i<count;++i)
        if (!cars[i].finish) events[i]=drive(cars[i],pads[i],dt);
    for (int j=0;j<kProps;++j) {
        Body& p=props[j];
        p.vel=p.vel*std::max(0.f,1.f-dt*(j%2 ? 5.5f : 2.7f));
        if (p.vel.dot(p.vel)<.1f) p.vel={};
        p.pos=p.pos+p.vel*dt;
    }
    // Rails and loose obstacles can touch simultaneously at the edge of a bend.
    for (int pass=0;pass<2;++pass) {
        for (int j=0;j<kProps;++j) {
            for (int n=0;n<j;++n)
                contact(props[n],props[j],propRadius(n)+propRadius(j),inverseMass(n),inverseMass(j));
            for (int i=0;i<count;++i) {
                if (cars[i].finish) continue;
                float closing=contact(cars[i],props[j],carRadius+propRadius(j),1,inverseMass(j));
                hit(cars[i],closing,events[i]);
            }
            confine(props[j],propRadius(j),nearestRoad(props[j].pos));
        }
        if (!solo && !cars[0].finish && !cars[1].finish) {
            float closing=contact(cars[0],cars[1],2*carRadius,1,1);
            for (int i=0;i<2;++i) hit(cars[i],closing,events[i]);
        }
        for (int i=0;i<count;++i) {
            if (cars[i].finish) continue;
            const Road road=nearestRoad(cars[i].pos);
            hit(cars[i],scrape(cars[i],road),events[i]);
            if (pass==1) events[i] |= score(cars[i],road,tick);
        }
    }
    return events[0] | (events[1]<<4);
}

void Race::encode(arcade::Writer& w) const {
    w.u32(tick);
    w.u8(solo);
    w.u16(introTicks);
    for (const Kart& k:cars) {
        writeBody(w,k);
        w.i16(arcade::q16(k.heading,1000));
        w.i16(arcade::q16(k.slip,1000));
        w.u16(static_cast<uint16_t>(std::lround(k.section*100)));
        w.i16(k.laps);
        w.u8(static_cast<unsigned>(k.drift) | (static_cast<unsigned>(k.wrongWay)<<1));
        w.u8(k.impact);
        w.u32(k.finish);
    }
    for (const Body& b:props) writeBody(w,b);
}

bool Race::decode(arcade::Reader& r) {
    Race next=*this;
    next.tick=r.u32();
    const unsigned single=r.u8();
    if (single>1) return false;
    next.solo=single!=0;
    next.introTicks=r.u16();
    if (next.introTicks>kGridTicks || (!next.solo && next.introTicks)) return false;
    for (Kart& k:next.cars) {
        if (!readBody(r,k)) return false;
        k.heading=r.i16()*.001f;
        k.slip=r.i16()*.001f;
        k.section=r.u16()*.01f;
        k.laps=r.i16();
        const unsigned flags=r.u8();
        k.drift=flags&1;
        k.wrongWay=flags&2;
        k.impact=r.u8();
        k.finish=r.u32();
        if (std::fabs(k.heading)>3.143f || std::fabs(k.slip)>.671f || k.section>sections() || k.laps>kLaps ||
            flags>3 || k.impact>14 || k.finish>next.tick || bool(k.finish)!=(k.laps==kLaps)) return false;
    }
    for (Body& b:next.props) if (!readBody(r,b)) return false;
    if (!r.ok || r.pos!=r.len) return false;
    *this=next;
    return true;
}
}

