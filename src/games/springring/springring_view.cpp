#include "games/springring/springring.h"
#include "games/springring/harbor.h"
#include "games/springring/harbor_card.h"
#include "core/display.h"
#include "core/platform.h"
#include "core/sound.h"
#include "ui/gfx.h"
#include <algorithm>
#include <cmath>

namespace springring {
namespace {
using platform::rgb;
constexpr uint16_t ink=rgb(37,39,68), teal=rgb(32,100,111), mint=rgb(110,211,189),
    lightMint=rgb(170,235,201), ivory=rgb(255,240,203), gold=rgb(255,194,56),
    orange=rgb(215,109,54), blue=rgb(44,126,230), red=rgb(231,72,83);
constexpr uint16_t costume[2][10] = {
    {ink,rgb(44,65,135),blue,rgb(93,205,249),ivory,rgb(255,196,140),rgb(211,129,107),rgb(155,79,84),gold,teal},
    {ink,rgb(150,49,81),red,rgb(255,133,107),ivory,rgb(248,179,116),rgb(196,109,83),rgb(145,70,79),gold,teal}
};

// Separate original masks: a breaking wave and a crab claw. They rotate with
// the shoulders so the face, crest and boots read even through a full flip.
constexpr char wave[][15] = {
    ".....0000.....",
    "...00334000...",
    "..0322344300..",
    ".032223433210.",
    ".022222433220.",
    ".012204440200.",
    "..01240040550.",
    "..01225555550.",
    "..0112544460..",
    "...011226600..",
    "....0011100...",
    "......000....."
};
constexpr char claw[][15] = {
    "...00....00...",
    "..0880..0880..",
    ".028820288820.",
    ".022222222220.",
    ".032233322210.",
    ".012288888820.",
    "..01240040550.",
    "..01225555550.",
    "..0112554460..",
    "...001226600..",
    "....0011100...",
    "......000....."
};
constexpr char torso[][17] = {
    ".....000000.....",
    "...0055555500...",
    "..055555555550..",
    ".0655556555650..",
    ".0655665555650..",
    ".076655655560...",
    "..07666666660...",
    "..01222233320...",
    "..01224442220...",
    "...012282210....",
    "...011111110....",
    "....0000000....."
};
int pix(float f) { return int(lroundf(f)); }
struct View {
    const Clinch& p;
    float x,y,zoom;
    Point map(Point q) const { return {x+(q.x-p.x)*zoom,y+(q.y-p.y)*zoom}; }
    Point joint(float across,float down) const { return map(p.joint(across,down)); }
};

template<size_t H,size_t W>
void stamp(Point pos,float angle,bool mirror,float zoom,const char (&sprite)[H][W],int team) {
    const float co=cosf(angle), si=sinf(angle);
    const float hw=(W-1)*.5f, hh=H*.5f;
    int ex=int(ceilf((fabsf(co)*hw+fabsf(si)*hh)*zoom));
    int ey=int(ceilf((fabsf(si)*hw+fabsf(co)*hh)*zoom));
    auto& c=display::canvas();
    for(int dy=-ey;dy<=ey;++dy) for(int dx=-ex;dx<=ex;++dx) {
        float u=(dx*co+dy*si)/zoom;
        const int sx=int(floorf(hw+(mirror ? -u : u)));
        const int sy=int(floorf(hh+(-dx*si+dy*co)/zoom));
        if (unsigned(sx)>=W-1 || unsigned(sy)>=H) continue;
        const char index=sprite[sy][sx];
        if(index>='0' && index<='9') c.drawPixel(pix(pos.x)+dx,pix(pos.y)+dy,costume[team][index-'0']);
    }
}
void stroke(Point a,Point b,float width,uint16_t color) {
    auto& c=display::canvas();
    const int r=std::max(1,pix(width*.5f));
    const int steps=1+int(fmaxf(fabsf(a.x-b.x),fabsf(a.y-b.y)));
    for(int j=0;j<=steps;++j) {
        float t=float(j)/steps;
        c.fillCircle(pix(a.x+(b.x-a.x)*t),pix(a.y+(b.y-a.y)*t),r,color);
    }
}
void fighter(const View& v,int i) {
    auto& c=display::canvas();
    const float side=i ? 1.f : -1.f, z=v.zoom;
    const auto& colors=costume[i];
    Point hip=v.map(v.p.hip(i)), foot=v.map(v.p.boot(i));
    Point knee={(hip.x+foot.x)*.5f+side*(5-3*v.p.legs[i].length)*z,(hip.y+foot.y)*.5f-2*z};
    stroke(hip,knee,6*z,ink); stroke(knee,foot,5*z,ink);
    stroke(hip,knee,3*z,colors[5]); stroke(knee,foot,2.6f*z,colors[2]);
    // The boot's bright welt ends at the same contact height as the solver.
    int bx=pix(foot.x),by=pix(foot.y),w=pix(9*z),h=pix(6*z);
    c.fillRoundRect(bx-w/2,by-h+pix(3*z),w,h,pix(2*z),ink);
    c.fillRect(bx-w/2+1,by-pix(2*z),w-2,pix(3*z),colors[2]);
    c.drawFastHLine(bx-w/2+1,by+pix(z),w-2,ivory);
    c.fillRect(bx,by-pix(2*z),pix(2*z),pix(z),colors[3]);

    stamp(v.joint(side*14,-1),v.p.angle,i!=0,z,torso,i);
    Point head=v.map(v.p.head(i));
    if(i) stamp(head,v.p.angle,true,z,claw,i);
    else stamp(head,v.p.angle,false,z,wave,i);
}
void clinch(const View& v) {
    fighter(v,0); fighter(v,1);
    for(int i=0;i<2;++i) {
        float side=i ? 1.f : -1.f, z=v.zoom;
        Point shoulder=v.joint(side*10,-5), elbow=v.joint(side*7,i ? -1 : -2);
        Point wrist=v.joint(side*1.5f,i ? -4 : -3);
        stroke(shoulder,elbow,6*z,ink); stroke(elbow,wrist,5*z,ink);
        stroke(shoulder,elbow,3*z,costume[i][5]); stroke(elbow,wrist,2.5f*z,costume[i][6]);
        stroke(v.joint(side*4,-3),wrist,2.8f*z,costume[i][3]);
    }
    Point grip=v.joint(0,-3);
    auto& c=display::canvas();
    c.fillCircle(pix(grip.x),pix(grip.y),pix(2.4f*v.zoom),ink);
    c.fillRect(pix(grip.x-v.zoom),pix(grip.y-v.zoom),pix(3*v.zoom),pix(2*v.zoom),ivory);
}
void venue(int x,int y,int w,int h,int floor,bool flash) {
    auto& c=display::canvas();
    bool card=w<=120;
    const auto& art=card ? assets::harborCard : assets::harbor;
    ui::drawBitmap(art,x,y);
    const int left=x+w/2-13, right=x+w/2+13;
    const int back=floor-(card ? 6 : 10);
    // The far ropes sit well behind the contact line on the canvas.
    for (int level=0;level<2;++level) {
        int ry=back-9-level*7;
        c.drawFastHLine(x+3,ry,w-6,ivory);
        c.drawFastHLine(x+3,ry+1,w-6,teal);
    }
    c.fillRect(x,back,w,floor-back+1,lightMint);
    c.drawFastHLine(x,back,w,mint);
    for(int i=0;i<2;++i) {
        int post=x+(i ? w-8 : 3);
        c.fillRect(post,back-22,5,23,ink);
        c.fillRect(post+1,back-21,3,21,i ? red : blue);
        for(int k=0;k<2;++k) {
            c.fillRect(post-2,back-18+k*7,9,4,ink);
            c.fillRect(post-1,back-18+k*7,7,2,i ? costume[1][3] : costume[0][3]);
        }
    }
    // Front apron, with a canvas seam and two stitched corner stripes.
    c.fillRect(x,floor,w,y+h-floor,teal);
    c.drawFastHLine(x,floor,w,ink);
    c.fillRect(x,floor+1,w,3,mint);
    c.drawFastHLine(x,floor+4,w,ivory);
    for(int i=0;i<3;++i) {
        int off=6+i*7;
        c.fillTriangle(x+off,floor+6,x+off+5,floor+6,x+off,floor+12,mint);
        c.fillTriangle(x+w-off,floor+6,x+w-off-5,floor+6,x+w-off,floor+12,mint);
    }
    // Keep the tread at the collision height. The exposed zigzags below it
    // distinguish the board from the flat canvas even between bounces.
    c.fillRect(left-1,floor-2,29,8,ink);
    for(int j=left+3;j<right-5;j+=10) {
        c.drawLine(j,floor,j+5,floor+1,ivory);
        c.drawLine(j+5,floor+1,j,floor+3,gold);
        c.drawLine(j,floor+3,j+5,floor+4,ivory);
    }
    c.drawFastHLine(left-2,floor+6,31,teal);
    c.fillRect(left-1,floor-4,29,4,ink);
    c.fillRect(left,floor-4,27,3,flash ? ivory : gold);
    c.drawFastHLine(left+1,floor-2,25,orange);
    for(int j=left+2;j<right-1;j+=6) c.drawPixel(j,floor-3,teal);
}
void burst(Point p,int radius,uint16_t color) {
    auto& c=display::canvas();
    for(int i=0;i<8;++i) {
        float a=i*.785398f, dx=cosf(a),dy=sinf(a);
        c.drawLine(pix(p.x+8*dx),pix(p.y+8*dy),pix(p.x+radius*dx),pix(p.y+radius*dy),ink);
        c.drawLine(pix(p.x+9*dx),pix(p.y+9*dy)-1,pix(p.x+(radius-1)*dx),pix(p.y+(radius-1)*dy)-1,color);
    }
}
void scoreFlag(int x,int score,int i,bool own) {
    auto& c=display::canvas();
    const uint16_t color=i ? red : blue;
    c.fillRoundRect(x,5,44,13,3,ink);
    c.fillRect(x+2,6,19,10,color);
    ui::shadowText(i ? "RED" : "BLU",x+11,11,1,ivory);
    for(int n=0;n<3;++n) {
        int cx=x+25+7*n;
        c.fillCircle(cx,11,2,n<score ? gold : costume[i][1]);
        if(n<score)c.drawPixel(cx,10,ivory);
    }
    if(own)c.drawFastHLine(x+4,17,15,gold);
}
void ribbon(const char* label,int x,int y,int width,int size,uint16_t face) {
    auto& c=display::canvas();
    const int h=size==2 ? 22 : 12;
    c.fillRoundRect(x-width/2,y-h/2+1,width,h,3,ink);
    c.fillRoundRect(x-width/2+1,y-h/2,width-2,h-2,2,face);
    ui::shadowText(label,x,y-1,size,ivory);
}
}
void Match::draw(int local,const arcade::DrawCtx& ctx) {
    auto& c=display::canvas();
    venue(0,0,240,135,kMat,flash!=0);
    // A shallow contact shadow keeps the boot's height readable against the water.
    for(int i=0;i<2;++i) {
        Point foot=pair.boot(i);
        int width=std::max(2,9-int((kMat-foot.y)*.09f));
        c.fillEllipse(pix(foot.x),kMat-1,width,1,mint);
    }
    clinch({pair,pair.x,pair.y,1});
    if(flash) {
        for(int i=-1;i<=1;++i) {
            int x=120+i*9;
            c.drawLine(x,kPadY-4,x+3*i,kPadY-9,orange);
            c.drawLine(x-1,kPadY-4,x-1+3*i,kPadY-9,ivory);
        }
    }
    scoreFlag(38,score[0],0,local==0); scoreFlag(158,score[1],1,local==1);
    ribbon("FIRST 3",120,12,55,1,teal);
    for(int i=0;i<2;++i) if(hitMask & (1u<<i)) burst(pair.head(i),14,gold);
    if(phase()==arcade::MatchPhase::MatchEnd) {
        bool won=(score[0]==3)==(local==0);
        ribbon(won ? "YOU WIN" : "YOU LOSE",120,42,116,2,won ? teal : ink);
        if(ctx.solo) ribbon("A = AGAIN",120,64,74,1,teal);
    } else if(hitMask) {
        ribbon(hitMask==3 ? "DOUBLE DOWN" : hitMask==1 ? "RED +1" : "BLUE +1",
               120,42,hitMask==3 ? 145 : 100,2,hitMask==3 ? teal : hitMask==1 ? costume[1][1] : costume[0][1]);
    }
}
void Match::sfx(uint32_t e) {
    static const sound::Note impact[]={{175,35},{90,75}}, spring[]={{330,24},{650,27},{1060,35}}, kick[]={{210,22},{320,24}};
    if(e&Impact) sound::play(impact,2,4);
    else if(e&Spring) sound::play(spring,3,3);
    else if(e&Kick) sound::play(kick,2,2);
}
void drawCard(int x,int y,int w,int h,uint32_t ms) {
    auto& c=display::canvas();
    c.setClipRect(x,y,w,h);
    int floor=y+h-16;
    venue(x,y,w,h,floor,false);
    Clinch p;
    float wave=sinf(ms*.0028f);
    p.angle=.42f*wave; p.legs[0].length=.7f; p.legs[1].reach=-4;
    const float lowest=std::max(p.boot(0).y,p.boot(1).y)-p.y+2;
    const float highest=std::min(p.head(0).y,p.head(1).y)-p.y-8;
    const float hop=1+wave;
    const float zoom=std::min(1.3f,(floor-y-hop-3)/(lowest-highest));
    clinch({p,x+w*.5f,floor-zoom*lowest-hop,zoom});
    c.clearClipRect();
}
void drawLobby(uint32_t ms) {
    venue(0,0,240,135,104,false);
    display::canvas().fillRoundRect(61,10,118,19,4,teal);
    Clinch p;
    p.angle=.25f*sinf(ms*.003f); p.legs[1].length=.4f;
    clinch({p,120,84,1});
    ribbon("HOLD A: EXTEND   TILT: REACH",120,111,184,1,teal);
}
}
