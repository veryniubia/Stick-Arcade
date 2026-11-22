#include <algorithm>
#include <cmath>
#include <cstdio>
#include "core/display.h"
#include "core/platform.h"
#include "core/sound.h"
#include "games/bumperduel/bumperduel.h"
#include "games/bumperduel/view.h"
#include "ui/gfx.h"
namespace {
using platform::rgb;
constexpr uint16_t bg = rgb(17,21,34), floorColor = rgb(34,54,62), blue = rgb(57,170,255),
                   red = rgb(255,86,110), gold = rgb(255,199,83), skin = rgb(244,179,124);
constexpr float pi = 3.14159265f;
float delta(float a, float b) { return atan2f(sinf(a-b),cosf(a-b)); }
void fighter(float x, float y, float angle, float radius, int player, bool own, bool shield, uint32_t ms) {
    auto& c = display::canvas();
    const float fx = cosf(angle), fy = sinf(angle), scale = 7 / radius;
    const uint16_t shorts = player ? red : blue;
    // Small rotated pixel mask. The rounded back, front fists, nose and eyes remain readable at every heading.
    c.fillEllipse(int(x), int(y+3), int(radius), int(radius*.7f), rgb(17,34,40));
    const int extent = int(ceilf(radius));
    for (int py = -extent; py <= extent; ++py) for (int px = -extent; px <= extent; ++px) {
        float u = (px*fx+py*fy)*scale, v = (-px*fy+py*fx)*scale;
        float d = u*u+v*v;
        uint16_t color = 0;
        if (d <= 49) {
            color = d > 37 ? rgb(127,76,64) : skin;
            if (u < -2) color = d > 37 ? rgb(30,45,64) : shorts;
            if (u > -2.5f && u < -1.3f) color = rgb(250,222,169); // belt
            if (u > 0 && u < 4.8f && fabsf(v) < 3.5f) color = rgb(255,205,151);
            if (u > 3 && u < 4.6f && fabsf(v) > 1.3f && fabsf(v) < 2.5f) color = bg;
            if (u > 4.5f && fabsf(v) < 1.3f) color = rgb(255,226,175); // nose
            if (u > -0.4f && u < 1 && fabsf(v) > 4.6f) color = shorts; // wristbands
            if (u < -4 && fabsf(v) < 1.2f && own) color = ui::kWhite;
        }
        if (color) {
            if (shield && (ms/80)%2 == 0 && d > 37) color = rgb(165,255,225);
            c.drawPixel(int(x)+px,int(y)+py,color);
        }
    }
    // Feet and a one-pixel direction marker identify the front even while inflated.
    for (int side : {-1,1}) {
        int px = int(x-fx*radius*.65f-fy*side*radius*.6f);
        int py = int(y-fy*radius*.65f+fx*side*radius*.6f);
        c.fillRect(px-1,py-1,3,3,shorts);
    }
    c.fillCircle(int(x+fx*(radius+1)),int(y+fy*(radius+1)),1,gold);
    if (own) c.drawCircle(int(x),int(y),int(radius)+3,shield ? rgb(165,255,225) : rgb(107,159,164));
}
void spike(float x, float y, float nx, float ny, float length = 4) {
    auto& c = display::canvas();
    c.fillTriangle(int(x-ny*2),int(y+nx*2),int(x+ny*2),int(y-nx*2),
                   int(x+nx*length),int(y+ny*length),rgb(223,229,220));
    c.drawPixel(int(x+nx*length),int(y+ny*length),gold);
}
void pillar(int x, int y, int r) {
    auto& c = display::canvas();
    c.fillCircle(x,y+3,r,rgb(15,30,35));
    c.fillCircle(x,y,r-4,rgb(99,66,66));
    c.drawCircle(x,y,r-4,gold);
    c.fillCircle(x,y,3,rgb(45,49,56));
    c.fillRect(x-1,y-1,2,2,rgb(166,140,116));
    for (int i=0;i<10;++i) {
        float a=i*pi/5, nx=cosf(a), ny=sinf(a);
        spike(x+nx*(r-4),y+ny*(r-4),nx,ny);
    }
}
void itemIcon(BumperDuelGame::ItemType type, int x, int y) {
    using G = BumperDuelGame;
    auto& c = display::canvas();
    if (type == G::Burger) {
        ui::drawBurger(x+5, y+5);
    } else if (type == G::Shoes) {
        c.fillRect(x+1,y+1,4,6,rgb(94,224,212));
        c.fillRoundRect(x+1,y+5,9,4,1,rgb(94,224,212));
        c.fillRect(x,y+9,11,1,ui::kWhite);
        c.drawLine(x+4,y+4,x+6,y+6,bg); c.drawLine(x+6,y+4,x+8,y+6,bg);
    } else if (type == G::Shield) {
        c.fillTriangle(x,y+1,x+10,y+1,x+5,y+10,rgb(104,211,255));
        c.fillRect(x,y+1,11,4,rgb(104,211,255));
        c.drawLine(x+5,y+3,x+5,y+7,ui::kWhite);
        c.drawLine(x+3,y+5,x+7,y+5,ui::kWhite);
    } else if (type == G::Banana) {
        c.fillCircle(x+5,y+4,5,gold); c.fillCircle(x+3,y+1,5,floorColor);
        c.drawLine(x,y+2,x+1,y+4,rgb(142,100,39));
        c.drawPixel(x+9,y+2,rgb(142,100,39));
        c.drawLine(x+4,y+7,x+7,y+6,rgb(255,235,136));
    }
}
void emblem(int x, int y) {
    auto& c = display::canvas();
    const auto ink = rgb(63,88,92);
    // Two round wrestlers meeting in a burst: an emblem, with no lettering.
    c.fillCircle(x-8,y,8,ink); c.fillCircle(x+8,y,8,ink);
    c.fillCircle(x-8,y-2,5,floorColor); c.fillCircle(x+8,y-2,5,floorColor);
    c.fillRect(x-14,y+3,12,3,ink); c.fillRect(x+2,y+3,12,3,ink);
    c.fillTriangle(x-3,y-2,x+2,y-8,x+1,y+1,ink);
    c.fillTriangle(x+3,y+2,x-2,y+8,x-1,y-1,ink);
}
void arena() {
    using G = BumperDuelGame;
    auto& c = display::canvas();
    // A convex octagon stretched to the native portrait play area.
    for (int n=0;n<8;++n) {
        auto a=G::cage[n], b=G::cage[(n+1)%8];
        c.fillTriangle(67,123,int(a.x),int(a.y),int(b.x),int(b.y),floorColor);
    }
    for (int y=34;y<216;y+=9) for (int x=10;x<128;x+=9) {
        if (y<46 && (x<26 || x>109)) continue;
        if (y>201 && (x<26 || x>109)) continue;
        c.drawPixel(x,y,rgb(44,66,72));
    }
    c.drawCircle(67,123,24,rgb(58,78,82));
    c.drawCircle(67,123,21,rgb(58,78,82));
    emblem(67,123);
    for (int n=0;n<8;++n) {
        auto a=G::cage[n], b=G::cage[(n+1)%8];
        float dx=b.x-a.x, dy=b.y-a.y, len=hypotf(dx,dy), nx=-dy/len, ny=dx/len;
        // Spike tips lie on the same boundary used by collision; the mesh sits outside it.
        for (int offset=4;offset<=6;++offset)
            c.drawLine(int(a.x-nx*offset),int(a.y-ny*offset),int(b.x-nx*offset),int(b.y-ny*offset),rgb(96,112,119));
        for (float d=4;d<len;d+=8) {
            float x=a.x+dx*d/len, y=a.y+dy*d/len;
            spike(x-nx*4,y-ny*4,nx,ny);
        }
        c.fillCircle(int(a.x-nx*4),int(a.y-ny*4),2,gold);
    }
    for (auto p:G::pillars) pillar(int(p.x),int(p.y),int(p.r));
}
}
void BumperDuelGame::draw(int local,const arcade::DrawCtx& ctx) {
    viewLocal_=local;
    auto& c=display::canvas(); c.fillScreen(bg); arena();
    for (const auto& item : items) {
        if (item.type == None) continue;
        int x=int(item.x), y=int(item.y);
        if (item.warning>0) {
            c.drawLine(x-3,y,x+3,y,gold); c.drawLine(x,y-3,x,y+3,gold);
        } else if (item.ttl>2 || (ctx.nowMs/100)%2) {
            itemIcon(item.type,x-5,y-5);
        }
    }
    float dt=drawnAt_ ? std::min(.05f,(ctx.nowMs-drawnAt_)/1000.f) : 0;
    bool snap=mode_!=arcade::Mode::Client || !drawnAt_ || dt==0;
    drawnAt_=ctx.nowMs;
    for (int i=0;i<2;++i) {
        auto& p=cars[i];
        float error=hypotf(p.x-renderX_[i],p.y-renderY_[i]);
        float alpha=std::min(1.f,dt*40);
        if (snap || error>18) { renderX_[i]=p.x; renderY_[i]=p.y; renderA_[i]=p.angle; }
        else { renderX_[i]+=(p.x-renderX_[i])*alpha; renderY_[i]+=(p.y-renderY_[i])*alpha; renderA_[i]+=delta(p.angle,renderA_[i])*alpha; }
        if (p.slide>0) {
            float speed=hypotf(p.vx,p.vy);
            if (speed>1) for (int n=0;n<3;++n) {
                int x=int(renderX_[i]-p.vx/speed*(radius(p)+4+n*3));
                int y=int(renderY_[i]-p.vy/speed*(radius(p)+4+n*3));
                c.fillRect(x-1,y-1,3,2,gold);
            }
        }
        if (p.shield>0) {
            int r=int(radius(p))+4;
            c.drawCircle(int(renderX_[i]),int(renderY_[i]),r,rgb(104,211,255));
            if ((ctx.nowMs/150)%2) c.drawCircle(int(renderX_[i]),int(renderY_[i]),r+1,rgb(55,116,149));
        }
        fighter(renderX_[i],renderY_[i],renderA_[i],radius(p),i,i==local,p.protection>0,ctx.nowMs);
        int active=(p.burger>0)+(p.shoes>0)+(p.shield>0);
        int bx=int(renderX_[i])-active*6, by=std::max(27,int(renderY_[i]-radius(p)-15));
        const float timers[3]={p.burger,p.shoes,p.shield};
        for (int n=0;n<3;++n) if (timers[n]>0) {
            itemIcon(ItemType(n+1),bx,by);
            c.fillRect(bx,by+11,11,1,bg);
            c.fillRect(bx,by+11,int(11*timers[n]/6),1,n==0 ? gold:n==1 ? rgb(94,224,212):rgb(104,211,255));
            bx+=12;
        }
        if (phase_ == arcade::MatchPhase::Playing && p.protection > .35f) {
            const int x = std::max(16, std::min(118, int(renderX_[i])));
            ui::hitNotice(i == local, x, std::max(28,int(renderY_[i]-radius(p)-(active?22:8))), 1);
        }
        // Both cooldowns are visible; a ready opponent is part of the timing game.
        int x=int(renderX_[i])-8, y=int(renderY_[i]+radius(p))+5;
        c.fillRect(x,y,16,2,rgb(12,24,33));
        c.fillRect(x,y,int(16*(1-p.cooldown/3)),2,p.cooldown>0 ? (i ? red:blue) : gold);
    }
    const int start=ctx.solo ? 52:3;
    for (int i=0;i<2;++i) {
        int x=start+i*33;
        char value[]={char('0'+score[i]),0};
        c.fillRoundRect(x,3,30,16,3,i ? rgb(94,45,61):rgb(29,68,101));
        c.fillRect(x+3,6,3,9,i ? red:blue); ui::shadowText(value,x+17,11,2,ui::kWhite);
        if(i==local)c.fillRect(x+6,20,18,1,ui::kWhite);
    }
    const auto& own=cars[local];
    char hint[24];
    if(own.cooldown>0) snprintf(hint,sizeof(hint),"A MOVE  B %.1fs",ceilf(own.cooldown*10)/10);
    else snprintf(hint,sizeof(hint),"A MOVE  B PUFF");
    c.fillRect(0,228,135,12,bg);
    ui::shadowText(hint,67,234,1,own.cooldown>0 ? rgb(185,205,210):gold);
    if(phase_==arcade::MatchPhase::MatchEnd) {
        ui::matchResult(winner, local, 67, 115, 2);
        if(ctx.solo)ui::shadowText("A AGAIN",67,148,1,ui::kWhite);
    }
}
void BumperDuelGame::sfx(uint32_t ev) {
    using sound::Note;
    static const Note hit[]={{180,30},{100,45},{70,35}}, puff[]={{210,22},{380,28},{520,25}},
        bump[]={{130,22},{85,18}},pickup[]={{660,30},{990,40}},block[]={{1200,20},{500,35}},slide[]={{900,20},{650,25},{400,35}},win[]={{660,80},{880,80},{1320,140}},lose[]={{440,80},{330,100},{220,130}};
    if(ev&Finish)sound::play(winner==viewLocal_ ? win:lose,3,5);
    else if(ev&(Hit0|Hit1))sound::play(hit,3,4);
    else if(ev&Block)sound::play(block,2,4);
    else if(ev&Slide)sound::play(slide,3,3);
    else if(ev&Pickup)sound::play(pickup,2,3);
    else if(ev&Inflate)sound::play(puff,3,3);
    else if(ev&Impact)sound::play(bump,2,2);
}
namespace bumperduel_view {
void drawCard(int x,int y,int w,int h,uint32_t ms) {
    auto& c=display::canvas();c.setClipRect(x,y,w,h);c.fillRect(x,y,w,h,bg);
    int cut=std::min(w,h)/5;
    int px[8]={x+cut,x+w-cut,x+w-2,x+w-2,x+w-cut,x+cut,x+2,x+2};
    int py[8]={y+2,y+2,y+cut,y+h-cut,y+h-2,y+h-2,y+h-cut,y+cut};
    for(int n=0;n<8;++n){int k=(n+1)%8;c.fillTriangle(x+w/2,y+h/2,px[n],py[n],px[k],py[k],floorColor);c.drawLine(px[n],py[n],px[k],py[k],gold);}
    pillar(x+w/2,y+h/2,7);
    float cycle=fmodf(ms*.001f,3), r=7;
    if(cycle<.5f){BumperDuelGame::Fighter p;p.pulse=.5f-cycle;r=BumperDuelGame::radius(p);}
    fighter(x+w/2-23,y+h/2+9,0,r,0,true,false,ms);
    fighter(x+w/2+23+(cycle<.65f ? sinf(cycle*pi/.65f)*8:0),y+h/2-9,pi,7,1,false,false,ms);
    c.clearClipRect();
}
void drawLobby(uint32_t ms){drawCard(0,0,240,135,ms);}
}
