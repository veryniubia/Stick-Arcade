#include "games/samuraiduel/samuraiduel.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>

void SamuraiDuelGame::draw(int, const arcade::DrawCtx&) {}
void SamuraiDuelGame::sfx(uint32_t) {}
static int failures = 0;
#define CHECK(c) do { if (!(c)) { printf("FAIL %d: %s\n",__LINE__,#c); ++failures; } } while(0)
using G = SamuraiDuelGame;
constexpr float tick = 1.f/60;
static void run(G& g, PadInput p[2], int frames) { for(int i=0;i<frames;++i)g.step(tick,p); }
static G fresh() { G g;g.begin(arcade::Mode::Host,122);return g; }
static bool same(const G& a,const G& b) {
    arcade::Writer x,y;a.encode(x);b.encode(y);return x.n==y.n && memcmp(x.buf,y.buf,x.n)==0;
}
static void movementAndPlatforms() {
    G g=fresh();PadInput p[2]={};g.prime(p);
    p[0].aim=32;run(g,p,15);CHECK(g.fighters[0].x>48);
    p[0].aim=0;p[0].b=true;++p[0].bCount;g.step(tick,p);
    CHECK(!g.fighters[0].grounded && g.fighters[0].vy<0);
    float high=g.fighters[0].y;
    for(int i=0;i<70;++i){g.step(tick,p);high=std::min(high,g.fighters[0].y);}
    CHECK(high<106);CHECK(g.fighters[0].grounded && g.fighters[0].y==106);
    // Holding B never produces an automatic second jump.
    run(g,p,40);CHECK(g.fighters[0].y==106 && g.fighters[0].grounded);
    G small=fresh();p[0]=PadInput();++p[0].bCount;small.step(tick,p);
    float low=small.fighters[0].y;
    for(int i=0;i<55;++i){small.step(tick,p);low=std::min(low,small.fighters[0].y);}
    CHECK(low>85);CHECK(small.fighters[0].y==G::kFloor);
    // Walk off the platform; no hovering or automatic attachment to a lower platform.
    p[0].aim=32;run(g,p,90);CHECK(g.fighters[0].x>126);CHECK(g.fighters[0].y==G::kFloor);
    // Land on the upper platform after jumping from a low one.
    g=fresh();g.fighters[0].x=100;g.fighters[0].y=106;
    p[0]=PadInput();p[0].aim=32;p[0].b=true;++p[0].bCount;
    run(g,p,30);p[0].aim=0;run(g,p,30);
    CHECK(g.fighters[0].grounded && g.fighters[0].y==72);
}
static void attacksAndInputs() {
    G g=fresh();PadInput p[2]={};g.prime(p);
    p[0].b=true;++p[0].bCount;g.step(tick,p);
    p[0].a=true;++p[0].aCount;run(g,p,10);
    CHECK(g.fighters[0].action==0 && !g.slashActive(0));
    // A press just before landing is buffered, but has no airborne blade.
    g=fresh();g.fighters[0].x=90;g.fighters[0].y=G::kFloor-1;g.fighters[0].grounded=false;g.fighters[0].vy=80;
    p[0]=PadInput();++p[0].aCount;g.step(tick,p);
    CHECK(g.fighters[0].grounded && g.fighters[0].action>0);
    // Jumping cannot carry an active sword into the air.
    p[0].b=true;++p[0].bCount;run(g,p,4);
    CHECK(g.fighters[0].grounded && g.slashActive(0));
    // Presses during READY/countdown do not leak into play.
    g=fresh();p[0].a=true;p[0].b=true;p[0].aCount=22;p[0].bCount=14;
    g.prime(p);g.step(tick,p);CHECK(g.fighters[0].action==0 && g.fighters[0].grounded);
    // Gun can jump, move, and fire. The shot must wait for the visible windup.
    g=fresh();p[0]=PadInput();p[1].a=true;p[1].b=true;p[1].aim=-32;++p[1].aCount;++p[1].bCount;
    g.step(tick,p);CHECK(!g.bullets[0].active && !g.fighters[1].grounded);
    bool shot=false;
    for(int i=0;i<14;++i)shot|=(g.step(tick,p)&G::Shot)!=0;
    CHECK(shot && g.fighters[1].x<276 && !g.fighters[1].grounded);
    run(g,p,60); // Held A does not autofire.
    int shots=0;for(auto& b:g.bullets)shots+=b.active;
    CHECK(shots==0);
    // Cooldown prevents a fresh press from firing immediately.
    g=fresh();p[1]=PadInput();++p[1].aCount;run(g,p,12);
    ++p[1].aCount;uint32_t ev=0;for(int i=0;i<10;++i)ev|=g.step(tick,p);
    CHECK(!(ev&G::Shot));
}
static void tiltFacing() {
    G g=fresh();PadInput p[2]={};
    p[0].aim=-32;p[1].aim=32;
    ++p[0].aCount;++p[1].aCount;g.step(tick,p);
    CHECK(g.fighters[0].facing==-1 && g.fighters[1].facing==1);
    p[0].aim=32;p[1].aim=-32;
    for(int frame=0;frame<9;++frame) {
        CHECK(!(g.step(tick,p)&G::Shot));
        CHECK(g.fighters[0].facing==-1 && g.fighters[1].facing==1);
    }
    CHECK(g.step(tick,p)&G::Shot);
    CHECK(g.bullets[0].direction==1 && g.fighters[1].facing==-1);
    CHECK(g.fighters[0].facing==-1);
    run(g,p,20);CHECK(g.fighters[0].facing==1);
    p[0].aim=p[1].aim=0;run(g,p,10);
    CHECK(g.fighters[0].facing==1 && g.fighters[1].facing==-1);
}
static void bullet(G& g,float x,float y,int dir,int owner=1) {
    auto& b=g.bullets[0];b=G::Bullet();b.active=true;b.x=x;b.y=y;b.direction=int8_t(dir);b.owner=uint8_t(owner);
}
static void parryAndDamage() {
    PadInput p[2]={};G g=fresh();g.fighters[0].x=100;g.fighters[1].x=190;
    g.fighters[0].action=.29f;g.fighters[0].cooldown=.3f;
    bullet(g,125,G::kFloor-8,-1);uint32_t ev=g.step(tick,p);
    CHECK(ev&G::Parry);CHECK(g.score[1]==0);
    CHECK(g.bullets[0].reflected && g.bullets[0].owner==0 && g.bullets[0].direction==1);
    CHECK(g.fighters[0].recoil>0 && g.fighters[0].vx<0);
    run(g,p,35);CHECK(g.score[0]==1); // Reflected shot really hits the gunner.
    // Wrong-facing blade cannot stop a bullet from behind.
    g=fresh();g.fighters[0].x=100;g.fighters[0].facing=1;g.fighters[0].action=.29f;
    bullet(g,92,G::kFloor-8,1);run(g,p,2);CHECK(g.score[1]==1);
    // Recovery has no parry window.
    g=fresh();g.fighters[0].x=100;g.fighters[0].action=.12f;
    bullet(g,108,G::kFloor-8,-1);g.step(tick,p);CHECK(g.score[1]==1);
    // Swept collision crosses the blade and body in a single long step: parry wins.
    g=fresh();g.fighters[0].x=100;g.fighters[0].action=.29f;
    bullet(g,110,G::kFloor-8,-1);ev=g.step(.05f,p);CHECK(ev&G::Parry);CHECK(g.score[1]==0);
    // A different-height bullet passes harmlessly.
    g=fresh();g.fighters[0].x=100;bullet(g,108,78,-1);g.step(tick,p);CHECK(g.score[1]==0);
    // Repeated hits on a fallen fighter never award another point; other shots stay alive.
    g=fresh();g.fighters[0].x=100;bullet(g,108,G::kFloor-8,-1);g.bullets[1]=g.bullets[0];
    g.step(tick,p);CHECK(g.score[1]==1);
    CHECK(g.phase()==arcade::MatchPhase::RoundEnd);
    CHECK(g.fighters[0].down && g.bullets[1].active);
    run(g,p,30);CHECK(g.score[1]==1);
    // A single slash only damages once, and a subsequent jump cannot turn it into an aerial attack.
    g=fresh();g.fighters[0].x=100;g.fighters[1].x=118;++p[0].aCount;run(g,p,10);
    CHECK(g.score[0]==1);
}
static void roundsAndWire() {
    G g=fresh();PadInput p[2]={};
    const float startingX[2]={g.fighters[0].x,g.fighters[1].x};
    for(int round=0;round<G::kWinningScore;++round) {
        bullet(g,g.fighters[1].x-7,g.fighters[1].y-8,1,0);
        g.bullets[0].reflected=g.isSword(0);
        g.step(tick,p);
        CHECK(g.score[0]==round+1);
        CHECK(g.phase()==arcade::MatchPhase::RoundEnd);int old=g.swordPlayer;
        run(g,p,100);CHECK(g.swordPlayer==old);
        if(round<G::kWinningScore-1) {
            CHECK(g.phase()==arcade::MatchPhase::Playing);
            CHECK(g.fighters[0].x==startingX[0] && g.fighters[1].x==startingX[1]);
        }
    }
    CHECK(g.phase()==arcade::MatchPhase::MatchEnd && g.winner==0);
    G snapshot=g;run(g,p,100);CHECK(same(g,snapshot));
    int opening=g.swordPlayer;g.begin(arcade::Mode::Host,457);CHECK(g.swordPlayer!=opening);
    CHECK(g.fighters[0].x==startingX[1] && g.fighters[1].x==startingX[0]);
    // Time limit resolves a draw without awarding either player a point.
    g.fighters[0].x=110;g.roundTime=45-tick/2;g.step(tick,p);
    CHECK(g.phase()==arcade::MatchPhase::RoundEnd && g.roundWinner==-1);
    run(g,p,100);
    CHECK(g.phase()==arcade::MatchPhase::Playing && g.score[0]==0 && g.score[1]==0);
    CHECK(g.swordPlayer==1-opening && g.fighters[0].x==startingX[1] && g.fighters[1].x==startingX[0]);
    // Transactional decoding, including every truncation and invalid enum/NaN/trailing byte.
    g=fresh();bullet(g,150,G::kFloor-8,-1);arcade::Writer w;g.encode(w);CHECK(w.n<220);
    G client;client.begin(arcade::Mode::Client,1);arcade::Reader r(w.buf,w.n);CHECK(client.decode(r));CHECK(same(g,client));
    for(size_t size=0;size<w.n;++size) {
        G before=client;arcade::Reader truncated(w.buf,size);CHECK(!client.decode(truncated));CHECK(same(before,client));
    }
    for(int variant=0;variant<4;++variant) {
        arcade::Writer bad=w;
        if(variant==0)bad.buf[4]=3;
        if(variant==1)bad.buf[8]=2;
        if(variant==2){float nan=std::numeric_limits<float>::quiet_NaN();memcpy(bad.buf+17,&nan,4);}
        if(variant==3)bad.u8(0);
        G before=client;arcade::Reader invalid(bad.buf,bad.n);CHECK(!client.decode(invalid));CHECK(same(before,client));
    }
    // Client authority: inputs cannot mutate decoded simulation.
    snapshot=client;p[0].aim=32;++p[0].aCount;client.step(tick,p);CHECK(same(client,snapshot));
}
static void deterministicSolo() {
    G a, b;
    a.begin(arcade::Mode::Solo, 95);
    b.begin(arcade::Mode::Solo, 95);
    CHECK(!a.isSword(0));
    PadInput p[2] = {};
    bool shot=false,jump=false;
    for(int i=0;i<2400;++i) {
        p[0].aim=(i/100)%2 ? 32:-32;
        if(i%43==0)++p[0].aCount;
        if(i%103==0)++p[0].bCount;
        p[0].b=(i%103)<15;
        uint32_t ea=a.step(tick,p),eb=b.step(tick,p);CHECK(ea==eb && same(a,b));
        shot|=(ea&G::Shot)!=0;jump|=(ea&G::Jump)!=0;
        arcade::Writer w;a.encode(w);G c;c.begin(arcade::Mode::Client,1);arcade::Reader r(w.buf,w.n);CHECK(c.decode(r));
    }
    CHECK(shot && jump);
    float elapsed=a.elapsed;p[0].aim=std::numeric_limits<float>::quiet_NaN();a.step(tick,p);
    CHECK(std::isfinite(a.fighters[0].x));a.step(-1,p);CHECK(a.elapsed<=elapsed+tick+.001f);
}
static void soloOpponentHasNoInput() {
    G g;
    PadInput pads[2] = {};
    pads[1].aim = 32;
    pads[1].a = pads[1].b = true;
    pads[1].aCount = 7;
    pads[1].bCount = 11;
    for (int sword = 0; sword < 2; ++sword) {
        g.begin(arcade::Mode::Solo, 92 + 2*sword);
        CHECK(g.swordPlayer == sword);
        g.prime(pads);
        const float opponentX = g.fighters[1].x;
        for (int frame = 0; frame < 120; ++frame) {
            ++pads[1].aCount;
            ++pads[1].bCount;
            CHECK(g.step(tick, pads) == 0);
            CHECK(g.fighters[1].x == opponentX && g.fighters[1].y == G::kFloor);
        }
        g.phase_ = arcade::MatchPhase::RoundEnd;
        g.phaseTimer = tick;
        CHECK(g.step(tick, pads) == 0);
        CHECK(g.phase() == arcade::MatchPhase::Playing);
        CHECK(g.swordPlayer == sword && g.fighters[1].x == opponentX);
        CHECK(g.step(tick, pads) == 0);
    }
}
static void soloPracticeKeepsScoring() {
    for (uint32_t seed=0;seed<4;++seed) {
        G g;g.begin(arcade::Mode::Solo,seed);PadInput p[2]={};
        const auto weapon=g.swordPlayer;
        const float playerX=g.fighters[0].x, opponentX=g.fighters[1].x;
        CHECK(weapon==(seed>>1) && (playerX>opponentX)==bool(seed&1));
        for(uint32_t point=1;point<=260;++point) {
            ++p[0].aCount;
            if(g.isSword(0)) g.fighters[0].x=opponentX-15*g.fighters[0].facing;
            uint32_t events=0;
            for(int frame=0;frame<65 && g.phase()==arcade::MatchPhase::Playing;++frame) events|=g.step(tick,p);
            CHECK((events&G::Round) && !(events&G::Finish));
            CHECK(g.score[0]==point && g.winner==-1 && g.phase()==arcade::MatchPhase::RoundEnd);
            run(g,p,100);
            CHECK(g.phase()==arcade::MatchPhase::Playing && g.swordPlayer==weapon);
            CHECK(g.fighters[0].x==playerX && g.fighters[1].x==opponentX);
        }
        arcade::Writer w;g.encode(w);G client;client.begin(arcade::Mode::Client,seed);
        arcade::Reader r(w.buf,w.n);CHECK(client.decode(r) && same(g,client));
    }
}
static void expandedArenaRegression() {
    PadInput p[2]={};
    // Both routes reach the upper deck with the normal held jump.
    for (int side : {-1,1}) {
        G g=fresh();auto& f=g.fighters[0];
        f.x=side<0 ? 110:210;f.y=106;
        p[0]=PadInput();p[0].b=true;++p[0].bCount;
        run(g,p,65);CHECK(f.grounded && f.y==72);
    }
    // Every platform has exits on both sides; a player never gets stuck on a long deck.
    for (const auto& platform:G::platforms) for (int direction : {-1,1}) {
        G g=fresh();auto& f=g.fighters[0];
        f.x=direction<0 ? platform.left+2:platform.right-2;f.y=platform.y;
        p[0]=PadInput();p[0].aim=float(direction*32);
        run(g,p,45);CHECK(f.y>platform.y);
        arcade::Writer w;g.encode(w);G c;c.begin(arcade::Mode::Client,1);
        arcade::Reader r(w.buf,w.n);CHECK(c.decode(r));
    }
    // The middle gap allows a direct descent from the first deck height to the floor.
    G gap=fresh();gap.fighters[0].x=160;gap.fighters[0].y=106;gap.fighters[0].grounded=false;
    p[0]=PadInput();run(gap,p,50);CHECK(gap.fighters[0].y==G::kFloor);
    // Crossing under recoil never turns a fighter, even against their current tilt.
    for (int mirror : {-1,1}) {
        G g=fresh();g.fighters[0].x=120-1.1f*mirror;g.fighters[1].x=120+1.1f*mirror;
        g.fighters[0].vx=83*mirror;g.fighters[1].vx=-83*mirror;
        g.fighters[0].facing=mirror;g.fighters[1].facing=-mirror;
        g.fighters[0].recoil=g.fighters[1].recoil=.1f;
        p[0].aim=-32*mirror;p[1].aim=32*mirror;g.step(.05f,p);
        CHECK(g.fighters[0].facing==mirror && g.fighters[1].facing==-mirror);
    }
}
static void firstToFive() {
    PadInput p[2]={};
    for(int scorer=0;scorer<2;++scorer) {
        G g=fresh();
        for(int point=1;point<=G::kWinningScore;++point) {
            int victim=1-scorer;
            bullet(g,g.fighters[victim].x-7,G::kFloor-8,1,scorer);
            g.bullets[0].reflected=g.isSword(scorer);
            uint32_t events=g.step(tick,p);
            CHECK(events&G::Round);CHECK(g.score[scorer]==point && g.score[victim]==0);
            CHECK(!(events&G::Finish)); // Even match point waits for surviving bullets.
            arcade::Writer w;g.encode(w);G c;c.begin(arcade::Mode::Client,1);
            arcade::Reader r(w.buf,w.n);CHECK(c.decode(r));CHECK(same(g,c));
            auto bad=w;bad.buf[9]=6;arcade::Reader invalid(bad.buf,bad.n);CHECK(!c.decode(invalid));
            // Living fighters can act during the delay; held inputs must not replay at spawn.
            ++p[0].aCount;++p[1].bCount;p[0].a=p[1].b=true;
            run(g,p,100);
            if(point<G::kWinningScore) {
                CHECK(g.phase()==arcade::MatchPhase::Playing);
                for(const auto& f:g.fighters)CHECK(f.y==G::kFloor && f.action==0 && f.cooldown==0);
                for(const auto& b:g.bullets)CHECK(!b.active);
            }
        }
        CHECK(g.winner==scorer);auto frozen=g;run(g,p,120);CHECK(same(g,frozen));
    }
}
static void burgerRoundsAndWeapons() {
    PadInput p[2]={};
    G g=fresh();
    CHECK(g.burgerActive && g.burgerX==160);
    g.burgerX=g.fighters[0].x; g.burgerY=G::kFloor-9;
    CHECK(g.step(tick,p)&G::Pickup);
    CHECK(!g.burgerActive && g.boostRounds[0]==2);
    g.fighters[1].x=g.fighters[0].x+36;
    ++p[0].aCount; run(g,p,16);
    CHECK(g.score[0]==1 && g.fighters[0].x>44);
    run(g,p,100);
    CHECK(g.burgerActive && g.boostRounds[0]==1 && g.burgerX>160 && g.burgerX<190);
    g.roundTime=45; run(g,p,100);
    CHECK(g.burgerActive && g.boostRounds[0]==0);

    g=fresh(); p[0]=PadInput();
    g.burgerX=g.fighters[1].x; g.burgerY=G::kFloor-9;
    CHECK(g.step(tick,p)&G::Pickup);
    ++p[1].aCount;
    int shots=0, lastFrame=0;
    for(int i=0;i<36;++i) {
        if (g.step(tick,p)&G::Shot) {
            if (shots) CHECK(i-lastFrame>=8 && i-lastFrame<=9);
            ++shots; lastFrame=i;
        }
    }
    CHECK(shots==3);
    arcade::Writer w; g.encode(w); G client; client.begin(arcade::Mode::Client,0);
    arcade::Reader r(w.buf,w.n); CHECK(client.decode(r) && same(g,client));
    for (int slot=0;slot<3;++slot) {
        g=fresh(); g.fighters[0].action=.29f;
        bullet(g,g.fighters[0].x+9,G::kFloor-8,-1);
        g.bullets[slot]=g.bullets[0];
        if(slot)g.bullets[0].active=false;
        const auto events=g.step(.05f,p);
        CHECK((events&G::Parry) && !(events&G::Hurt));
    }
}
static void thrustAndUniversalParry() {
    for (int mirror : {-1,1}) for (bool boosted : {false,true}) {
        G g=fresh();PadInput p[2]={};g.burgerActive=false;
        auto& s=g.fighters[0];auto& gun=g.fighters[1];
        s.x=mirror>0 ? 60:260;gun.x=mirror>0 ? 260:60;
        s.facing=mirror;gun.facing=-mirror;
        g.boostRounds[0]=boosted ? 2:0;g.boostRounds[1]=2;
        ++p[1].aCount;
        int shots=0,parries=0;
        // Launch a real three-shot burst, then attack when the first shot is approaching.
        for(int frame=0;frame<100;++frame) {
            if(g.bullets[0].active && (g.bullets[0].x-s.x)*mirror<66 && p[0].aCount==0)++p[0].aCount;
            const auto ev=g.step(tick,p);
            shots+=(ev&G::Shot)!=0;parries+=(ev&G::Parry)!=0;
            arcade::Writer w;g.encode(w);G client;client.begin(arcade::Mode::Client,0);
            arcade::Reader r(w.buf,w.n);CHECK(client.decode(r) && same(g,client));
        }
        CHECK(shots==3);
        CHECK(boosted ? parries==3 && g.score[1]==0 : parries==1 && g.score[1]==1);
        if(boosted)CHECK((s.x-(mirror>0 ? 60:260))*mirror>18);
    }
    for(bool thrust : {false,true}) for(float remaining : {.01f,.035f}) {
        G g=fresh();PadInput p[2]={};auto& s=g.fighters[0];
        s.thrust=thrust;s.action=remaining;
        bullet(g,s.x+7,G::kFloor-8,-1);
        CHECK(!(g.step(tick,p)&G::Parry) && s.down); // Both exposed recoveries.
    }
    // Thrust cannot hover across a platform exit or turn during an attack.
    for(int mirror : {-1,1}) {
        G g=fresh();PadInput p[2]={};auto& s=g.fighters[0];
        s.x=mirror>0 ? 125:25;s.y=106;s.facing=mirror;g.boostRounds[0]=2;
        ++p[0].aCount;g.step(tick,p);p[0].aim=-32.f*mirror;
        for(int frame=0;frame<7;++frame) {
            g.step(tick,p);
            if(s.action>0)CHECK(s.facing==mirror);
        }
        CHECK(!s.grounded && s.action==0);
    }
    // Picking up a boost during a normal slash must not change its timing mid-animation.
    G g=fresh();PadInput p[2]={};++p[0].aCount;g.step(tick,p);
    g.burgerX=g.fighters[0].x;g.burgerY=G::kFloor-9;g.step(tick,p);
    CHECK(g.boostRounds[0]==2 && !g.fighters[0].thrust);
}

static void delayedDeathsAndDraw() {
    G g=fresh();PadInput p[2]={};g.burgerActive=false;
    // A returning bullet kills the shooter; the shooter's existing bullet can still kill later.
    bullet(g,g.fighters[1].x-7,G::kFloor-8,1,0);g.bullets[0].reflected=true;
    auto& later=g.bullets[1];later.active=true;later.owner=1;later.direction=-1;
    later.x=250;later.y=G::kFloor-8;
    CHECK(g.step(tick,p)&G::Hurt);
    CHECK(g.fighters[1].down && g.bullets[1].active && g.score[0]==1);
    const float deadX=g.fighters[1].x;
    p[0].aim=32;++p[0].bCount;p[0].b=true;
    p[1].aim=-32;++p[1].aCount;++p[1].bCount;p[1].a=p[1].b=true;
    run(g,p,15);
    CHECK(g.fighters[0].x>44 && !g.fighters[0].grounded);
    CHECK(g.fighters[1].x>deadX && g.fighters[1].grounded && g.fighters[1].action==0);
    // Put the survivor back into the remaining shot's path, late in the first death timer.
    p[0]=PadInput();g.fighters[0].x=44;g.fighters[0].y=G::kFloor;
    g.fighters[0].grounded=true;g.fighters[0].vx=g.fighters[0].vy=0;
    g.bullets[1].x=50;g.phaseTimer=.15f;
    CHECK(g.step(tick,p)&G::Hurt);
    CHECK(g.score[0]==1 && g.score[1]==1 && g.phaseTimer==G::kRoundPause);
    CHECK(g.fighters[0].down && g.fighters[1].down);
    run(g,p,75);CHECK(g.phase()==arcade::MatchPhase::RoundEnd);
    run(g,p,25);CHECK(g.phase()==arcade::MatchPhase::Playing);
    CHECK(!g.fighters[0].down && !g.fighters[1].down);

    // A dead gunner never emits the remaining rounds of its already-started burst.
    g=fresh();g.boostRounds[1]=2;p[0]=p[1]=PadInput();++p[1].aCount;
    int shots=0;for(int f=0;f<12;++f)shots+=(g.step(tick,p)&G::Shot)!=0;
    CHECK(shots==1);
    bullet(g,g.fighters[1].x-7,G::kFloor-8,1,0);g.bullets[0].reflected=true;
    g.step(tick,p);
    for(int f=0;f<30;++f)CHECK(!(g.step(tick,p)&G::Shot));

    // Match point is provisional until the last death has had 1.5 seconds to play out.
    for(bool reverse : {false,true}) {
        g=fresh();p[0]=p[1]=PadInput();g.score[0]=g.score[1]=4;
        for(int i=0;i<2;++i) {
            const int owner=reverse ? 1-i:i, victim=1-owner;
            auto& b=g.bullets[i];b.active=true;b.owner=owner;b.direction=owner==0 ? 1:-1;
            b.reflected=owner==g.swordPlayer;b.x=g.fighters[victim].x-b.direction*6;b.y=G::kFloor-8;
        }
        CHECK(!(g.step(tick,p)&G::Finish));
        CHECK(g.score[0]==5 && g.score[1]==5 && g.phase()==arcade::MatchPhase::RoundEnd);
        run(g,p,100);CHECK(g.phase()==arcade::MatchPhase::MatchEnd && g.winner==-1);
        arcade::Writer w;g.encode(w);G client;client.begin(arcade::Mode::Client,0);
        arcade::Reader r(w.buf,w.n);CHECK(client.decode(r) && same(g,client));
        const int previous=g.swordPlayer;g.begin(arcade::Mode::Host,123);
        CHECK(g.swordPlayer==1-previous);
    }
}

int main() {
    thrustAndUniversalParry();delayedDeathsAndDraw();
    burgerRoundsAndWeapons();
    movementAndPlatforms();attacksAndInputs();tiltFacing();parryAndDamage();roundsAndWire();deterministicSolo();expandedArenaRegression();
    soloOpponentHasNoInput();soloPracticeKeepsScoring();firstToFive();
    // Five through nine exchanges cover both parity cases and either match winner.
    for(int winner=0;winner<2;++winner) for(int losses=0;losses<5;++losses) {
        G g=fresh();PadInput p[2]={};
        for(int point=0;point<5+losses;++point) {
            int scorer=point<losses ? 1-winner:winner, victim=1-scorer;
            bullet(g,g.fighters[victim].x-7,G::kFloor-8,1,scorer);
            g.bullets[0].reflected=g.isSword(scorer);g.step(tick,p);
            run(g,p,100);
        }
        CHECK(g.winner==winner);
        int previousSword=g.swordPlayer;
        g.begin(arcade::Mode::Host,456);g.prime(p);
        CHECK(g.swordPlayer==1-previousSword && g.score[0]==0 && g.score[1]==0);
        arcade::Writer w;g.encode(w);G client;client.begin(arcade::Mode::Client,456);
        arcade::Reader r(w.buf,w.n);CHECK(client.decode(r));CHECK(client.swordPlayer==g.swordPlayer);
    }
    printf(failures ? "%d SAMURAI FAILURES\n":"SAMURAI ALL PASSED\n",failures);return failures ? 1:0;
}
