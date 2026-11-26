#include "games/springring/springring.h"
#include <cassert>
#include <initializer_list>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>

using springring::Match;
using arcade::MatchPhase;
using arcade::Mode;
void Match::draw(int,const arcade::DrawCtx&) {}
void Match::sfx(uint32_t) {}
namespace {
constexpr float dt = 1.f/60;
void settle(Match& m, Mode mode) {
    m.begin(mode, 17);
    PadInput in[2] = {};
    for (int i=0;i<30;++i) m.step(dt,in);
}
bool sameView(const Match& a,const Match& b) {
    arcade::Writer x,y; a.encode(x); b.encode(y);
    return x.n==y.n && !memcmp(x.buf,y.buf,x.n);
}
void flatMatch() {
    Match solo,host;
    settle(solo,Mode::Solo); settle(host,Mode::Host);
    PadInput neutral[2]={}, noisy[2]={};
    noisy[1].a=true; noisy[1].aim=24;
    for (int round=1;round<=3;++round) {
        // One level A press after landing can win a round.
        for (int step=0;step<100 && !solo.hitMask;++step) {
            noisy[0].a=neutral[0].a=step<18;
            solo.step(dt,noisy); host.step(dt,neutral);
            assert(sameView(solo,host));
        }
        assert(solo.hitMask==2 && solo.score[0]==round && solo.score[1]==0);
        while (solo.freeze) { solo.step(dt,noisy); host.step(dt,neutral); }
        if (round<3) {
            assert(solo.phase()==MatchPhase::Playing && !solo.hitMask && solo.pair.angle==0);
            neutral[0].a=noisy[0].a=false;
            for(int j=0;j<30;++j) { solo.step(dt,noisy); host.step(dt,neutral); }
        }
    }
    assert(solo.phase()==MatchPhase::MatchEnd);
}
void contacts() {
    Match flat,pad;
    PadInput in[2]={}; in[0].a=true;
    settle(flat,Mode::Host);
    pad=flat; pad.pair.x+=7; pad.pair.y-=4;
    assert(flat.step(dt,in)&Match::Kick);
    assert(pad.step(dt,in)&Match::Spring);
    assert(pad.pair.vy < flat.pair.vy-15);
    // Holding A while extending in mid-air changes pose without adding lift.
    for(float angle : {0.f, 2.f, -2.f}) {
        Match left,right,level;
        left.begin(Mode::Host,0); left.pair.y=32; left.pair.angle=angle;
        right=level=left;
        for (int i=0;i<5;++i) {
            in[0].aim=-25; assert(!left.step(dt,in));
            in[0].aim=25; assert(!right.step(dt,in));
            in[0].aim=3; assert(!level.step(dt,in));
        }
        assert(left.pair.boot(0).x < level.pair.boot(0).x-8);
        assert(right.pair.boot(0).x > level.pair.boot(0).x+8);
        assert(level.pair.legs[0].length>0 && level.pair.legs[0].reach==0);
        assert(level.pair.vy==right.pair.vy && left.pair.vy>0);
    }
    // Press shortly before landing. First contact during extension still kicks.
    flat.begin(Mode::Host,0); flat.pair.y=89;
    uint32_t ev=0;
    for(int i=0;i<12;++i) ev |= flat.step(dt,in);
    assert(ev&Match::Kick);
    for(int i=0;i<15;++i) assert(!(flat.step(dt,in)&Match::Kick));
    flat.begin(Mode::Host,0); flat.pair.x=55; flat.pair.y=95;
    flat.pair.angle=3.14159265f;
    assert(flat.step(dt,in)&Match::Impact);
    assert(flat.hitMask==3 && flat.score[0]==0 && flat.score[1]==0);
    for(int j=0;j<45;++j) assert(!flat.step(dt,in));
    assert(flat.phase()==MatchPhase::Playing && flat.freeze==0 && !flat.hitMask);
}
void edgeSave() {
    Match level,steered; settle(level,Mode::Host);
    PadInput in[2]={};
    // Replay from the ordinary opening. Red kicks once, tipping blue over.
    for(int t=0;t<25;++t) { in[1].a=t<3; level.step(dt,in); }
    assert(level.pair.angle < -1.45f);
    steered=level;
    bool sprung=false;
    for(int t=0;t<30;++t) {
        in[0].a=t>=8;
        in[0].aim=0; uint32_t flat=level.step(dt,in);
        in[0].aim=12; uint32_t tilt=steered.step(dt,in);
        if (t==8) {
            assert((tilt&Match::Spring) && !(flat&Match::Spring));
            assert(steered.pair.boot(0).x>=springring::kPadLeft);
            sprung=true;
        }
    }
    assert(sprung && level.hitMask==1 && steered.hitMask==2);
}
void snapshots() {
    Match host,client;
    settle(host,Mode::Host); client.begin(Mode::Client,0);
    PadInput in[2]={}; in[0].a=true;
    host.step(dt,in);
    arcade::Writer w; host.encode(w);
    arcade::Reader full(w.buf,w.n);
    assert(client.decode(full) && sameView(host,client));
    assert(!client.step(dt,in) && sameView(host,client));
    for(size_t n=0;n<w.n;++n) {
        arcade::Reader shortRead(w.buf,n);
        assert(!client.decode(shortRead) && sameView(host,client));
    }
    w.buf[w.n-3]=4;
    arcade::Reader bad(w.buf,w.n);
    assert(!client.decode(bad) && sameView(host,client));
    arcade::Writer nan; nan.f32(std::numeric_limits<float>::quiet_NaN());
    memcpy(w.buf,nan.buf,4); w.buf[w.n-3]=0;
    arcade::Reader nonfinite(w.buf,w.n);
    assert(!client.decode(nonfinite) && sameView(host,client));
}
}
int main() {
    flatMatch(); contacts(); edgeSave(); snapshots();
    puts("PASS Spring Ring: level A, neutral practice, steering, contacts, edge save, draw, first to three, snapshots");
}
