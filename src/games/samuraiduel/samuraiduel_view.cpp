#include "games/samuraiduel/samuraiduel.h"
#include "games/samuraiduel/view.h"
#include "core/display.h"
#include "core/platform.h"
#include "core/sound.h"
#include "ui/gfx.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace {
using platform::rgb;
constexpr uint16_t sky = rgb(12, 17, 35), deep = rgb(19, 27, 48), mist = rgb(35, 46, 66);
constexpr uint16_t moon = rgb(231, 230, 194), wood = rgb(83, 58, 57), edge = rgb(226, 173, 103);
constexpr uint16_t cyan = rgb(70, 182, 255), gold = rgb(255, 194, 100), ink = rgb(8, 12, 25);
constexpr uint16_t white = rgb(240, 246, 231), red = rgb(241, 102, 108);

int screen(float world) { return int(lroundf(world*.75f)); }
void background(int x, int y, int w, int h, uint32_t ms) {
    auto& c=display::canvas();
    c.fillRect(x,y,w,h,sky);
    // Broad, quiet moon disc with a low-contrast halo: no block-shaped craters.
    int mx=x+w*58/100, my=y+h*39/100, r=h*30/100;
    for(int halo=3;halo>=0;--halo) {
        int radius=r+halo*2;
        uint16_t color=halo==3 ? rgb(22,29,43):halo==2 ? rgb(33,41,52):halo==1 ? rgb(58,65,70):rgb(184,185,164);
        for(int dy=-radius;dy<=radius;++dy) {
            int half=int(sqrtf(float(radius*radius-dy*dy)));
            c.drawFastHLine(mx-half,my+dy,half*2+1,color);
        }
    }
    // Thin drifting cloud bands veil the moon; distant ridges stay behind the arena.
    for(int n=0;n<4;++n) {
        int cy=y+h*(25+n*12)/100;
        int cx=x+int((ms/220+n*61)%(w+70))-70;
        c.fillRect(cx,cy,w/3,2,rgb(62,70,77));
        c.drawFastHLine(cx+8,cy+2,w/3,rgb(39,48,61));
    }
    for(int layer=0;layer<2;++layer) for(int dx=0;dx<w;dx+=2) {
        int base=y+h*(layer ? 75:67)/100;
        int height=int((sinf(dx*.035f+layer*2)+1)*h*.06f);
        c.fillRect(x+dx,base-height,2,y+h-base+height,layer ? deep:rgb(38,48,62));
    }
    int deck=y+int(h*SamuraiDuelGame::kFloor/SamuraiDuelGame::kWorldHeight);
    // Like Throwhalla's lava, moving stepped crests give the surface a readable rhythm.
    // Water uses layered lateral currents and broken moonlit foam instead of rising embers.
    const int bottom=y+h, waterTop=deck+8;
    c.fillRect(x,deck+4,w,bottom-deck-4,rgb(15,48,68));
    for(int py=waterTop;py<bottom;py+=4) {
        int depth=py-waterTop;
        c.fillRect(x,py,w,std::min(4,bottom-py),
            depth<7 ? rgb(27,99,124):depth<15 ? rgb(20,77,105):rgb(16,56,84));
    }
    const int flow=int(ms/110);
    for(int layer=0;layer<3;++layer) {
        int base=waterTop+layer*7;
        if(base+3>=bottom)break;
        for(int dx=0;dx<w;dx+=3) {
            int wave=abs(((dx/3+(layer==1 ? -flow:flow)+layer*17)%12+12)%12-6)/2;
            int top=base+wave;
            uint16_t body=layer==0 ? rgb(43,139,154):layer==1 ? rgb(29,110,138):rgb(28,85,119);
            c.fillRect(x+dx,top,std::min(3,w-dx),std::min(3,bottom-top),body);
            if((dx/3+layer*3)%8<4) {
                bool reflection=abs(x+dx-mx)<w/8+layer*5;
                c.drawFastHLine(x+dx,top,std::min(3,w-dx),reflection ? rgb(174,210,192):rgb(82,170,181));
            }
        }
    }
    // Sparse, staggered glints break up the reflection; no striped triangular spotlight.
    for(int n=0;n<13;++n) {
        int rx=x+int((n*43+ms/(n%2 ? 85:125))%std::max(1,w));
        int ry=waterTop+4+(n*11)%std::max(1,bottom-waterTop-5);
        int length=2+n%4;
        c.drawFastHLine(rx,ry,std::min(length,x+w-rx),rgb(104,174,181));
    }
    // Short supports disappear into the water instead of dividing it into tall columns.
    for(int dx=16;dx<w;dx+=64) {
        c.fillRect(x+dx,deck+3,3,7,rgb(30,29,36));
        c.drawFastVLine(x+dx,deck+4,5,rgb(76,61,57));
    }
    c.fillRect(x,deck,w,2,rgb(171,143,113));
    c.fillRect(x,deck+2,w,3,wood);
    c.drawFastHLine(x,deck+5,w,ink);
    for(int dx=0;dx<w;dx+=13)c.drawFastVLine(x+dx,deck+2,3,ink);
    for(int side=0;side<2;++side) {
        int bx=side ? x+w-20:x+2;
        c.fillRect(bx+3,deck-30,2,30,rgb(73,44,48));
        c.fillRect(bx+14,deck-30,2,30,rgb(73,44,48));
        c.fillRect(bx,deck-31,20,2,rgb(111,64,60));
        c.fillRect(bx+2,deck-26,16,1,rgb(88,49,52));
    }
}
void ledge(int l,int r,int y) {
    auto& c=display::canvas();
    c.fillRect(l,y,r-l+1,2,rgb(189,142,101));
    c.fillRect(l,y+2,r-l+1,4,wood);
    c.drawFastHLine(l+1,y+1,r-l-1,gold);
    c.drawFastHLine(l,y+6,r-l+1,ink);
    for(int x=l+5;x<r;x+=10)c.drawFastVLine(x,y+2,3,ink);
    c.fillRect(l+4,y+5,3,4,wood);c.fillRect(r-6,y+5,3,4,wood);
    for(int x=l+6;x<r-3;x+=16)c.drawPixel(x,y+3,gold);
    c.drawFastHLine(l,y-1,r-l+1,rgb(77,103,117));
}

// Hand-authored native pixel masks: straw hat/robe and wide brim/long coat.
const char* const samurai[18]={
    ".....hh......", "....hhhh.....", "..hhhhhhhh...", ".hhhhhhhhhh..",
    "kkkkkkkkkkkk.", "....ssks.....", "....ssws.....", "....kkkk.....",
    "...tttttt....", "..ttttttts...", "..ttttttts...", "...tttttt....",
    "...gggggg....", "...tttttt....", "..tttttttt...", "..ttt..ttt...",
    "...kk..kk....", "..kkk..kkk..."};
const char* const gunner[18]={
    "....hhhh.....", "...hhhhhh....", "...hhhhhh....", ".hhhhhhhhhhh.",
    "..kkkkkkkkkk.", "....ssks.....", "....ssws.....", "....rrrr.....",
    "...tttttt....", "..ttttttts...", "..ttttttts...", "...ttgttt....",
    "...gggggg....", "...tttttt....", "...tttttt....", "...tt..tt....",
    "...kk..kk....", "..kkk..kkk..."};

void drawSword(int x, int feet, int facing, float action) {
    auto& c = display::canvas();
    using Duel = SamuraiDuelGame;
    constexpr float ready = -.06f, raised = -1.76f, lowered = 1.03f;
    constexpr float sweepEnd = Duel::kSlashDuration - Duel::kSlashFollowThrough;
    const float elapsed = Duel::kSlashDuration - action;
    float angle = ready;
    if (elapsed < Duel::kSlashWindup) {
        angle = ready + (raised - ready) * (elapsed / Duel::kSlashWindup);
    } else if (elapsed < sweepEnd) {
        angle = raised + (lowered - raised) * ((elapsed - Duel::kSlashWindup) / (sweepEnd - Duel::kSlashWindup));
    } else if (action > 0) {
        angle = ready + (lowered - ready) * (action / Duel::kSlashFollowThrough);
    }

    const int pivotX = x + facing * 3, pivotY = feet - 7;
    auto bladeX = [&](float a, float radius) { return pivotX + facing * int(lroundf(cosf(a) * radius)); };
    auto bladeY = [&](float a, float radius) { return pivotY + int(lroundf(sinf(a) * radius * (a > 0 ? .55f : 1.f))); };
    constexpr int tipRadius = 14;
    constexpr uint16_t silver = rgb(179, 197, 208), tail = rgb(100, 124, 147);
    if (elapsed > Duel::kSlashWindup && elapsed < sweepEnd) {
        const float start = std::max(raised, angle - 1.0f);
        int prevX = bladeX(start, tipRadius), prevY = bladeY(start, tipRadius);
        for (int i = 1; i <= 6; ++i) {
            const float a = start + (angle - start) * i / 6.f;
            const int px = bladeX(a, tipRadius), py = bladeY(a, tipRadius);
            if (i == 1) c.drawPixel(prevX, prevY, tail);
            if (i >= 3) c.drawLine(prevX, prevY, px, py, i == 6 ? white : silver);
            prevX = px;
            prevY = py;
        }
    }

    const int handX = bladeX(angle, 2), handY = bladeY(angle, 2);
    if (action > 0) c.drawLine(x + facing, feet - 7, handX, handY, rgb(225, 185, 146));
    c.drawLine(handX, handY, bladeX(angle, tipRadius), bladeY(angle, tipRadius), white);
    c.drawLine(bladeX(angle - .22f, 3), bladeY(angle - .22f, 3) + 1,
               bladeX(angle + .22f, 3), bladeY(angle + .22f, 3) - 1, gold);
}

void character(int x,int feet,int facing,bool sword,int player,float walk,bool airborne,bool flash,float action=0) {
    auto& c=display::canvas();
    auto rows=sword ? samurai:gunner;
    uint16_t cloth=player ? rgb(191,72,83):rgb(77,153,173);
    for(int yy=0;yy<18;++yy) for(int xx=0;xx<13;++xx) {
        char p=rows[yy][xx]; if(p=='.' || (sword && action>0 && yy>=9 && p=='s'))continue;
        uint16_t col=ink;
        if(p=='h')col=sword ? (yy<3 ? rgb(236,199,125):rgb(166,115,66)):(yy<3 ? rgb(158,122,104):rgb(97,72,69));
        if(p=='s')col=rgb(225,185,146);
        if(p=='w')col=white;
        if(p=='t')col=xx<5 ? (player ? rgb(162,46,71):rgb(29,91,166)):cloth;
        if(p=='g')col=gold;
        if(p=='r')col=red;
        if(flash)col=white;
        int shift=0;
        if(yy>=15 && airborne)shift=xx<6 ? -1:1;
        else if(yy>=15 && fabsf(walk)>1)shift=(int(walk/6)&1) ? (xx<6 ? -1:1):0;
        c.drawPixel(x+screen(float((xx-6)*facing+shift)),feet+screen(float(yy-18)),col);
    }
    if(sword) {
        c.drawLine(x-facing*4,feet-5,x-facing*8,feet-2,rgb(104,132,147));
        drawSword(x,feet,facing,action);
    } else {
        c.fillRect(facing>0 ? x+3:x-10,feet-8,8,2,rgb(157,172,173));
        c.drawFastHLine(facing>0 ? x+4:x-10,feet-8,7,white);
        c.fillRect(x+facing*4-1,feet-6,2,2,wood);
    }
}
void spark(int x,int y,uint16_t col,int frame) {
    auto& c=display::canvas();
    int r=3+frame%4;
    c.drawLine(x-r-2,y,x+r+2,y,col);c.drawLine(x,y-r-2,x,y+r+2,col);
    c.drawLine(x-r,y-r,x+r,y+r,white);c.drawLine(x-r,y+r,x+r,y-r,white);
    c.fillRect(x-1,y-1,3,3,white);
}

}

void SamuraiDuelGame::draw(int local,const arcade::DrawCtx& ctx) {
    viewLocal_=local;
    auto& c=display::canvas();
    background(0,0,240,135,ctx.nowMs);
    // Arena and artwork share the same floor and platform coordinates.
    for(const auto& p:platforms)ledge(screen(p.left),screen(p.right),screen(p.y));
    if (burgerActive && phase_ == arcade::MatchPhase::Playing) ui::drawBurger(screen(burgerX), screen(burgerY));
    for(int i=0;i<2;++i) {
        const auto& f=fighters[i];int x=screen(f.x),y=screen(f.y);
        if(f.grounded)c.fillRect(x-4,y-1,9,1,ink);
        bool blink=phase_!=arcade::MatchPhase::Playing && roundWinner>=0 && roundWinner!=i && spark>0;
        // Let the blade finish its movement after a point freezes the fighters.
        const float slash = phase_ == arcade::MatchPhase::Playing ? f.action :
                            (spark > 0 ? std::max(0.f, f.action - (.20f - spark)) : 0.f);
        character(x,y,f.facing,isSword(i),i,fabsf(f.vx)>3 ? float(ctx.nowMs)/35:0,!f.grounded,blink,slash);
        if (boostRounds[i]) ui::drawBurger(std::max(5, std::min(234, x)), y-26);
        if(i==local) {
            c.fillRect(x-2,y-18,5,1,white);c.fillRect(x-1,y-17,3,1,white);c.drawPixel(x,y-16,white);
        }
        if(!isSword(i) && f.action>0) {
            int muzzle=x+f.facing*11;
            c.fillRect(muzzle-1,y-8,3,3,gold);c.drawPixel(muzzle+f.facing*2,y-7,white);
        }
        if(f.cooldown>0) {
            float total=isSword(i) ? kSlashDuration:kShotCooldown;
            c.fillRect(x-5,y+2,10,1,ink);
            c.fillRect(x-5,y+2,int(10*(1-f.cooldown/total)),1,isSword(i)?cyan:gold);
        }
    }
    for(const auto& b:bullets)if(b.active) {
        int x=screen(b.x),y=screen(b.y);uint16_t col=b.reflected ? cyan:gold;
        c.drawLine(x-b.direction*7,y,x,y,col);
        c.drawFastHLine(x-1,y-1,3,white);c.drawFastHLine(x-1,y,3,white);
    }
    if (spark > 0) {
        const int x = screen(sparkX), y = screen(sparkY);
        if (phase_ == arcade::MatchPhase::Playing) {
            const int radius = 1 + int(spark * 14);
            c.drawFastHLine(x - radius, y, 2 * radius + 1, gold);
            c.drawFastVLine(x, y - radius, 2 * radius + 1, gold);
            c.drawPixel(x, y, white);
        } else {
            ::spark(x, y, cyan, int(ctx.nowMs/30));
        }
    }
    // HUD is opaque to keep moonlight and jumping sprites out of the text.
    c.fillRect(0,0,240,14,ink);c.drawFastHLine(0,13,240,mist);
    if (ctx.solo) {
        char label[17]; snprintf(label,sizeof(label),"HITS %lu",static_cast<unsigned long>(score[0]));
        ui::shadowText(label,120,7,1,cyan);
    } else {
        for(int i=0;i<2;++i) {
            int center=i ? 161:38;uint16_t col=i ? red:cyan;
            char label[16];snprintf(label,sizeof(label),"P%d %lu",i+1,static_cast<unsigned long>(score[i]));
            ui::shadowText(label,center,7,1,col);
            if(i==local)c.drawFastHLine(center-13,12,26,col);
        }
        ui::shadowText("FIRST 5",112,7,1,moon);
    }
    // Controls live in the lobby; the full lower screen is available during combat.
    if (phase_ == arcade::MatchPhase::MatchEnd) {
        ui::matchResult(winner, local, 120, 40, 3);
    } else if (phase_ == arcade::MatchPhase::RoundEnd) {
        if (roundWinner < 0) {
            ui::shadowText("RESET", 120, 40, 3, white);
        } else if (phaseTimer > kRoundPause - ui::kHitNoticeSeconds) {
            ui::hitNotice(roundWinner != local, 120, 40, 3);
        }
    }
}
void SamuraiDuelGame::sfx(uint32_t ev) {
    using sound::Note;
    static const Note shot[]={{1300,12},{150,25}},slash[]={{750,12},{420,24}},
        parry[]={{1900,18},{2600,25},{1400,25}},hurt[]={{180,30},{90,60}},
        jump[]={{350,12},{550,16}},finish[]={{660,70},{880,70},{1320,130}},
        lose[]={{440,70},{330,80},{220,130}},round[]={{660,40},{990,70}};
    if(ev&Finish)sound::play(winner==viewLocal_ ? finish:lose,3,5);
    else if(ev&Round)sound::play(round,2,4);
    else if(ev&Pickup) {
        static const Note pickup[]={{660,25},{990,45}};
        sound::play(pickup,2,3);
    }
    else if(ev&Parry)sound::play(parry,3,4);
    else if(ev&Hurt)sound::play(hurt,2,3);
    else if(ev&Shot)sound::play(shot,2,2);
    else if(ev&Slash)sound::play(slash,2,2);
    else if(ev&Jump)sound::play(jump,2,1);
}
namespace samuraiduel_view {
void drawCard(int x,int y,int w,int h,uint32_t ms) {
    auto& c=display::canvas();c.setClipRect(x,y,w,h);
    background(x,y,w,h,ms);
    int ground=y+int(h*SamuraiDuelGame::kFloor/SamuraiDuelGame::kWorldHeight);
    if(w>150) {
        for(const auto& p:SamuraiDuelGame::platforms)
            ledge(x+int(p.left*w/SamuraiDuelGame::kWorldWidth),x+int(p.right*w/SamuraiDuelGame::kWorldWidth),y+int(p.y*h/SamuraiDuelGame::kWorldHeight));
    } else ledge(x+w*40/100,x+w*70/100,y+h*56/100);
    int sx=x+w/4,gx=x+w*3/4;
    character(sx,ground,1,true,0,0,false,false);
    character(gx,ground,-1,false,1,0,false,false);
    int travel=std::max(1,gx-sx-20),bx=gx-13-int((ms/7)%travel);
    c.drawFastHLine(bx,ground-8,6,gold);
    if((ms/300)%4==0)::spark(sx+19,ground-8,cyan,int(ms/30));
    c.clearClipRect();
}
void drawLobby(uint32_t ms){
    drawCard(0,0,240,135,ms);
    ui::shadowText("TILT MOVE  B JUMP  A ATTACK",120,113,1,white);
}
}
