#include "games/bumperduel/bumperduel.h"
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <initializer_list>
static int failures=0,checks=0;
#define CHECK(x) do { ++checks; if(!(x)){++failures;printf("FAIL %d: %s\n",__LINE__,#x);} }while(0)
void BumperDuelGame::draw(int,const arcade::DrawCtx&){}
void BumperDuelGame::sfx(uint32_t){}
using G=BumperDuelGame;
static PadInput p[2];
static G fresh(){p[0]=p[1]=PadInput();G g;g.begin(arcade::Mode::Host,123);return g;}
static void run(G& g,int n){for(int i=0;i<n;++i)g.step(1.f/60,p);}
static float speed(const G::Fighter& c){return hypotf(c.vx,c.vy);}
static void controls(){
 auto g=fresh();g.cars[0].x=67;g.cars[0].y=116;
 for(float tilt:{-45.f,-22.5f,-11.25f,0.f,11.25f,22.5f,45.f}){
  p[0].aim=tilt;run(g,1);CHECK(fabsf(g.cars[0].angle-g.heading(0,tilt))<.001f);
  run(g,30);CHECK(fabsf(g.cars[0].angle-g.heading(0,tilt))<.001f);
 }
 CHECK(fabsf(g.heading(0,11.25f)-g.heading(0,0)-.785398f)<.001f);
 for(float roll:{-179.f,-100.f,-46.f,46.f,100.f,179.f}){
  float expected=1.5707963f+roll*.06981317008f;
  CHECK(fabsf(atan2f(sinf(g.heading(0,roll)-expected),cosf(g.heading(0,roll)-expected)))<.001f);
 }
 CHECK(fabsf(g.heading(0,60)-g.heading(0,-30))<.001f); // full sensor angles are periodic, never clamped

 p[0].aim=std::numeric_limits<float>::quiet_NaN();run(g,1);CHECK(std::isfinite(g.cars[0].angle));
 p[0].aim=0;p[0].a=true;run(g,12);float v=speed(g.cars[0]);CHECK(v>50 && v<65);
 p[0].a=false;run(g,12);CHECK(speed(g.cars[0])<v*.12f);
 g=fresh();p[0].b=true;++p[0].bCount;run(g,9);CHECK(G::radius(g.cars[0])>13.7f);CHECK(g.cars[0].cooldown>2.8f);
 run(g,25);CHECK(G::radius(g.cars[0])==7);run(g,180);CHECK(g.cars[0].cooldown==0 && g.cars[0].pulse==0); // hold never repeats
 p[0].b=false;run(g,1);p[0].b=true;++p[0].bCount;run(g,1);CHECK(g.cars[0].pulse>0);
 auto pulse=g.cars[0].pulse;p[0].b=false;run(g,1);p[0].b=true;++p[0].bCount;run(g,1);CHECK(g.cars[0].pulse<pulse); // cooldown rejects
 g=fresh();p[0].bCount=255;g.prime(p);run(g,1);CHECK(g.cars[0].pulse==0);
 p[0].bCount=0;run(g,1);CHECK(g.cars[0].pulse>0); // lost held state, counter wrap still delivers edge
 g=fresh();p[0].b=true;p[0].bCount=1;g.prime(p);run(g,2);CHECK(g.cars[0].pulse==0);
 g=fresh();g.cars[0].x=67;g.cars[0].y=116;p[0].a=p[0].b=true;++p[0].bCount;run(g,8);CHECK(speed(g.cars[0])>40 && g.cars[0].pulse>0);
}
static void attacks(){
 for(int n=0;n<8;++n){
  auto g=fresh();float a=n*.78539816f;g.cars[0].x=67;g.cars[0].y=123;
  g.cars[1].x=67+cosf(a)*19;g.cars[1].y=123+sinf(a)*19;
  p[0].b=true;++p[0].bCount;run(g,6);
  CHECK(g.cars[0].struck);CHECK(g.cars[1].vx*cosf(a)+g.cars[1].vy*sinf(a)>90);
  CHECK(g.score[0]==0 && g.score[1]==0); // player contact never directly scores
 }
 auto g=fresh();g.fixture('p');run(g,9);CHECK(g.cars[0].vx< -90 && g.cars[1].vx>90);CHECK(g.cars[0].struck && g.cars[1].struck);
 g=fresh();g.fixture('h');g.cars[0].vx=50;run(g,12);CHECK(g.score[0]==0 && g.score[1]==0);
 // A burst from the middle corridor sends its victim into a side wall.
 g=fresh();g.cars[0].x=85;g.cars[0].y=122;g.cars[1].x=104;g.cars[1].y=122;
 p[0].b=true;++p[0].bCount;run(g,25);CHECK(g.score[0]==1 && g.cars[1].protection>0);CHECK(g.cars[1].vx<0);
 // Immunity prevents points but does not prevent further physical bursts.
 g=fresh();g.fixture('t');g.cars[1].protection=1;run(g,9);CHECK(g.cars[1].vx>90 && g.score[0]==0);
}
static void hazards(){
 auto g=fresh();g.fixture('c');run(g,1);CHECK(g.score[0]==1 && g.cars[1].vx< -90 && g.cars[1].protection>.95f);
 float x=g.cars[1].x;run(g,12);CHECK(g.cars[1].x<x-10 && g.score[0]==1); // no round restart
 g.cars[1].x=126;g.cars[1].y=120;run(g,1);CHECK(g.score[0]==1); // immune wall contact
 g.cars[1].protection=0;g.cars[1].x=126;g.cars[1].y=120;run(g,1);CHECK(g.score[0]==2);
 g=fresh();g.cars[0].x=G::pillars[0].x+13;g.cars[0].y=G::pillars[0].y;run(g,1);CHECK(g.score[1]==1 && g.cars[0].vx>90);
 // Initially safe at base size, expansion itself reaches the spikes.
 g=fresh();g.cars[0].x=121;g.cars[0].y=120;run(g,1);CHECK(g.score[1]==0);
 p[0].b=true;++p[0].bCount;run(g,9);CHECK(g.score[1]==1);
 g=fresh();g.cars[0].x=17;g.cars[0].y=35;run(g,1);CHECK(g.score[1]==1); // diagonal cage edge
 g=fresh();g.fixture('y');run(g,1);CHECK(g.score[0]==5 && g.score[1]==5 && g.winner==-1 && g.phase()==arcade::MatchPhase::MatchEnd);
 float t=g.elapsed;run(g,20);CHECK(g.elapsed==t);g.settleScores(3);CHECK(g.score[0]==5 && g.score[1]==5);
 g=fresh();g.fixture('w');run(g,1);CHECK(g.winner==0 && g.score[0]==5);
 g=fresh();g.score[1]=4;g.cars[0].x=9;g.cars[0].y=120;run(g,1);CHECK(g.winner==1);
}
static void stability(){
 for(int n=0;n<80;++n){
  auto g=fresh();g.cars[0].x=10+n%6*22;g.cars[0].y=32+n%8*24;g.cars[0].vx=n%2 ? 170:-170;g.cars[0].vy=40;
  p[0].a=true;
  for(int tick=0;tick<600;++tick){
   p[0].aim=45*sinf((tick+n)*.013f);if(tick%190==0)++p[0].bCount;
   g.step(1.f/60,p);
   if(g.phase()==arcade::MatchPhase::MatchEnd){g.score[0]=g.score[1]=0;g.winner=-1;g.phase_=arcade::MatchPhase::Playing;}
   for(auto c:g.cars){
    CHECK(std::isfinite(c.x) && std::isfinite(c.angle) && speed(c)<=180.01f);
    for(int k=0;k<8;++k){auto a=G::cage[k],b=G::cage[(k+1)%8];float dx=b.x-a.x,dy=b.y-a.y;
     CHECK(((c.x-a.x)*(-dy)+(c.y-a.y)*dx)/hypotf(dx,dy)>=G::radius(c)-.08f);
    }
    for(auto v:G::pillars)CHECK(hypotf(c.x-v.x,c.y-v.y)>=v.r+G::radius(c)-.08f);
   }
  }
 }
 auto g=fresh();g.fixture('r');run(g,1);CHECK(hypotf(g.cars[0].x-g.cars[1].x,g.cars[0].y-g.cars[1].y)>13.9f);
 g=fresh();auto start=g.cars[1];g.begin(arcade::Mode::Host,234);CHECK(g.swapped && g.cars[0].x==start.x && g.cars[0].y==start.y);
 g=fresh();g.begin(arcade::Mode::Solo,1);auto bot=g.cars[1];p[1].a=p[1].b=true;p[1].bCount=20;p[1].aim=30;run(g,180);
 CHECK(g.cars[1].x==bot.x && g.cars[1].y==bot.y && g.cars[1].angle==bot.angle && g.cars[1].cooldown==0 && g.cars[1].control==0);
 g.fixture('t');float bx=g.cars[1].x;run(g,18);CHECK(g.cars[1].x>bx+10); // passive body still moves
}
static void pickups(){
 auto give=[](G& g,int who,G::ItemType type){g.items[0].x=g.cars[who].x;g.items[0].y=g.cars[who].y;g.items[0].type=type;g.items[0].ttl=12;};
 auto g=fresh();give(g,0,G::Burger);run(g,1);CHECK(g.cars[0].burger>5.98f && G::radius(g.cars[0])==9 && g.items[0].type==G::None);
 g.cars[0].pulse=.35f;CHECK(G::radius(g.cars[0])==19);
 g.cars[0].pulse=0;run(g,120);give(g,0,G::Burger);run(g,1);CHECK(g.cars[0].burger>5.98f); // refresh, never stack
 give(g,0,G::Shoes);run(g,1);CHECK(g.cars[0].shoes>5.98f && g.cars[0].burger>5.9f);
 g.cars[0].x=26;g.cars[0].y=120;p[0].aim=-22.5f;p[0].a=true;run(g,12);CHECK(g.cars[0].vx>65);
 p[0].a=false;run(g,12);CHECK(speed(g.cars[0])<9); // shoes preserve fast stopping
 g=fresh();give(g,0,G::Shield);run(g,1);CHECK(g.cars[0].shield>5.98f);
 g.cars[0].x=9;g.cars[0].y=120;run(g,1);CHECK(g.score[1]==0 && g.cars[0].shield==0 && g.cars[0].vx>90 && g.cars[0].protection>0);
 g.cars[0].protection=0;g.cars[0].x=9;g.cars[0].y=120;run(g,1);CHECK(g.score[1]==1); // consumed shield cannot save a second injury
 g=fresh();give(g,0,G::Shield);run(g,1);run(g,361);CHECK(g.cars[0].shield==0);
 g=fresh();g.cars[0].x=30;g.cars[0].y=120;p[0].aim=-22.5f;give(g,0,G::Banana);run(g,1);
 CHECK(g.cars[0].slide>0 && g.cars[0].vx>150);float x=g.cars[0].x;run(g,18);CHECK(g.cars[0].x>x+35 && g.cars[0].x<x+50);
 g=fresh();give(g,0,G::Shoes);run(g,1);run(g,361);CHECK(g.cars[0].shoes==0);
 g=fresh();give(g,0,G::Burger);run(g,1);run(g,361);CHECK(g.cars[0].burger==0 && G::radius(g.cars[0])==7);
 // Hamburger reduces received burst impulse, without making it immune.
 g=fresh();g.fixture('t');g.cars[1].burger=6;run(g,9);float heavy=g.cars[1].vx;
 g=fresh();g.fixture('t');run(g,9);CHECK(heavy>50 && heavy<g.cars[1].vx*.8f);
 // One inflated body is not displaced or accelerated by a non-inflated opponent.
 g=fresh();g.cars[0].x=67;g.cars[0].y=123;g.cars[0].pulse=.3f;g.cars[0].struck=true;
 g.cars[1].x=87;g.cars[1].y=123;g.cars[1].vx=-90;g.cars[1].recoil=.5f;run(g,1);
 CHECK(g.cars[0].x==67 && g.cars[0].y==123 && g.cars[0].vx==0 && g.cars[0].vy==0);
 g.cars[0].vx=-90;g.cars[0].recoil=.5f;float old=g.cars[0].x;run(g,1);CHECK(g.cars[0].x<old && g.cars[0].vx< -80);
 // The new side passage accommodates even an inflated hamburger body.
 g=fresh();g.cars[0].x=25;g.cars[0].y=90;g.cars[0].burger=6;g.cars[0].pulse=.35f;run(g,1);CHECK(g.score[1]==0);
 // Warning prevents instant pickup; expiration and simultaneous pickup are authoritative.
 g=fresh();give(g,0,G::Burger);g.items[0].warning=.5f;run(g,20);CHECK(g.cars[0].burger==0);run(g,12);CHECK(g.cars[0].burger>5.9f);
 g=fresh();g.items[0].type=G::Burger;g.items[0].x=30;g.items[0].y=120;g.items[0].ttl=.01f;run(g,1);CHECK(g.items[0].type==G::None);
 g=fresh();g.cars[0].x=60;g.cars[0].y=123;g.cars[1].x=75;g.cars[1].y=123;
 g.items[0].type=G::Burger;g.items[0].x=67.5f;g.items[0].y=123;g.items[0].ttl=12;run(g,1);
 CHECK((g.cars[0].burger>0)!=(g.cars[1].burger>0));
 // Actual random placement: every active item is away from spikes, with at most two slots.
 g=fresh();run(g,239);CHECK(g.items[0].type==G::None);run(g,3);CHECK(g.items[0].type!=G::None && g.items[0].warning>0);
 for(int tick=0;tick<3600;++tick){
  run(g,1);
  for(auto item:g.items)if(item.type!=G::None){
   CHECK(item.type>=G::Burger && item.type<=G::Banana);
   for(int k=0;k<8;++k){auto a=G::cage[k],b=G::cage[(k+1)%8];float dx=b.x-a.x,dy=b.y-a.y;
    CHECK(((item.x-a.x)*(-dy)+(item.y-a.y)*dx)/hypotf(dx,dy)>=24);
   }
   for(auto v:G::pillars)CHECK(hypotf(item.x-v.x,item.y-v.y)>=v.r+24);
  }
 }
}
static void codec(){
 auto g=fresh();g.fixture('p');g.cars[0].burger=5.5f;g.cars[0].shield=3;g.cars[1].shoes=4;g.items[0].type=G::Banana;g.items[0].x=30;g.items[0].y=120;g.items[0].ttl=10;g.items[0].warning=.4f;run(g,7);arcade::Writer w;g.encode(w);CHECK(w.n==84 && w.n+20<250);
 G c;c.begin(arcade::Mode::Client,1);arcade::Reader r(w.buf,w.n);CHECK(c.decode(r));
 arcade::Writer copy;c.encode(copy);CHECK(copy.n==w.n && !memcmp(copy.buf,w.buf,w.n));
 for(size_t n=0;n<w.n;++n){arcade::Reader bad(w.buf,n);CHECK(!c.decode(bad));arcade::Writer unchanged;c.encode(unchanged);CHECK(!memcmp(unchanged.buf,copy.buf,copy.n));}
 for(size_t off:{size_t(4),size_t(5),size_t(6),size_t(11),size_t(12),size_t(13),size_t(24),size_t(26),size_t(27),size_t(28),size_t(29),size_t(30),size_t(32),size_t(34),size_t(36),size_t(37),size_t(67),size_t(72),size_t(73),size_t(75)}){
  arcade::Writer bad=w;bad.buf[off]=off==5 ? 127:255;arcade::Reader reader(bad.buf,bad.n);CHECK(!c.decode(reader));arcade::Writer unchanged;c.encode(unchanged);CHECK(!memcmp(unchanged.buf,copy.buf,copy.n));
 }
 arcade::Writer bad=w;float nan=std::numeric_limits<float>::quiet_NaN();memcpy(bad.buf+7,&nan,4);arcade::Reader nr(bad.buf,bad.n);CHECK(!c.decode(nr));
 bad=w;bad.u8(0);arcade::Reader extra(bad.buf,bad.n);CHECK(!c.decode(extra));
 float t=c.elapsed;run(c,10);CHECK(c.elapsed==t);
 CHECK(kBumperDuelInfo.wireId==4 && kBumperDuelInfo.protoVer==5 && kBumperDuelInfo.portrait);
}
int main(){controls();attacks();hazards();pickups();stability();codec();printf("BUMPER: %d checks, %d failures\n",checks,failures);return failures?1:0;}
