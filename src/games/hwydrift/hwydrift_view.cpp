#include "games/hwydrift/hwydrift.h"
#include "core/display.h"
#include "core/platform.h"
#include "core/sound.h"
#include "ui/gfx.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace hwy {
namespace {
using platform::rgb;
#include "sprite_data.inc"
constexpr uint16_t cream=rgb(255,242,203), ember=rgb(255,136,48), ice=rgb(91,233,245);
constexpr uint16_t ink=rgb(15,28,35), tire=rgb(27,32,39);
constexpr const char* shell[]={
    ".tt..tt....",
    "wTTbbTTbb..",
    "wbblggsbbh.",
    "wsssggssssh",
    "wbblggsbbh.",
    "wTTbbTTbb..",
    ".tt..tt....",
};

// Inverse sampling keeps the tyres and body connected at oblique angles.
void model(int x,int y,float angle,int player,float scale,bool flash=false) {
    auto& c=display::canvas();
    const float cs=std::cos(angle),sn=std::sin(angle);
    const int extent=6*scale;
    c.fillEllipse(x+1,y+2,5*scale,3*scale,ink);
    for (int py=-extent;py<=extent;++py) {
        for (int px=-extent;px<=extent;++px) {
            const int col=static_cast<int>(std::floor((px*cs+py*sn)/scale+5.5f));
            const int row=static_cast<int>(std::floor((py*cs-px*sn)/scale+3.5f));
            if (col<0 || col>=11 || row<0 || row>=7) continue;
            uint16_t color=0;
            switch(shell[row][col]) {
                case 't': color=tire; break;
                case 'T': color=palette[7]; break;
                case 'b': color=flash ? cream : player ? ice : ember; break;
                case 'g': color=palette[13]; break;
                case 'l': color=palette[15]; break;
                case 's': case 'h': color=cream; break;
                case 'w': color=player ? palette[14] : palette[11]; break;
                default: continue;
            }
            c.drawPixel(x+px,y+py,color);
        }
    }
}

constexpr float pi=3.14159265f;
struct Camera {
    Vec focus, right, forward;
    float zoom, anchorY;
    Vec screen(Vec pos) const {
        const Vec delta=pos-focus;
        return {67+zoom*delta.dot(right),anchorY-zoom*delta.dot(forward)};
    }
};

void terrain(const Camera& cam) {
    auto& c=display::canvas();
    uint16_t colors[16];
    for (int i=0;i<16;++i) {
#ifdef MIDDIFF_HOST_CANVAS
        colors[i]=palette[i];
#else
        colors[i]=uint16_t((palette[i]<<8)|(palette[i]>>8));
#endif
    }
    // Inverse-map each scanline with fixed-point steps into the flash atlas.
    // Both display backends provide contiguous 16-bit sprite rows.
    auto* dst=static_cast<uint16_t*>(c.getBuffer());
    const int dx=std::lround(cam.right.x*65536/cam.zoom);
    const int dy=std::lround(cam.right.y*65536/cam.zoom);
    for (int y=16;y<225;++y) {
        const Vec start=cam.focus+(cam.forward*(cam.anchorY-y)-cam.right*67)*(1/cam.zoom);
        int u=std::lround(start.x*65536),v=std::lround(start.y*65536);
        for (int x=0;x<135;++x,u+=dx,v+=dy) {
            const int wx=u>>16,wy=v>>16;
            int color=2;
            if (unsigned(wx)<kMapWidth && unsigned(wy)<kMapHeight) {
                const uint8_t pack=mapPixels[wy*(kMapWidth/2)+(wx>>1)];
                color=(wx&1) ? pack&15 : pack>>4;
            } else if (((unsigned(wx)*37u+unsigned(wy)*19u)&127u)<3) color=3;
            dst[y*135+x]=colors[color];
        }
    }
}
void obstacle(Vec p,float zoom,bool crate,Vec right,Vec up) {
    auto& c=display::canvas();
    const int x=std::lround(p.x),y=std::lround(p.y);
    if (x< -12 || x>147 || y<4 || y>239) return;
    if (crate) {
        // The slats stay aligned to the crate as the camera turns.
        const int extent=std::ceil(5*zoom);
        for (int j=-extent;j<=extent;++j) for (int i=-extent;i<=extent;++i) {
            const Vec q=(right*float(i)-up*float(j))*(1/zoom);
            if (std::fabs(q.x)>3.5f || std::fabs(q.y)>3.5f) continue;
            const bool rim=std::max(std::fabs(q.x),std::fabs(q.y))>2.5f;
            const int color=rim ? 12 : std::fabs(q.x+q.y)<.6f ? 10 : 11;
            c.drawPixel(x+i,y+j,palette[color]);
        }
    } else {
        const int radius=std::max(1,int(std::lround(3*zoom)));
        c.fillCircle(x+1,y+1,radius,ink);
        c.fillCircle(x,y,radius,tire);
        c.drawFastHLine(x-radius/2,y-radius+1,radius,palette[8]);
        c.fillCircle(x,y,std::max(1,radius/3),palette[7]);
    }
}
void clockText(char* text,size_t size,uint32_t ticks) {
    std::snprintf(text,size,"%u:%02u.%02u",unsigned(ticks/3600),unsigned(ticks/60%60),unsigned(ticks%60*100/60));
}

void speedometer(Vec velocity) {
    auto& c=display::canvas();
    constexpr int x=112, y=228, radius=17;
    const float fraction=std::min(1.f,std::sqrt(velocity.dot(velocity))*kPoweredDrag/kEngineAcceleration);
    c.fillRoundRect(90,207,44,33,5,ink);
    constexpr float start=-2.86f, sweep=2.58f;
    for (int i=0;i<=20;++i) {
        const float angle=start+sweep*i/20.f;
        const float cs=std::cos(angle),sn=std::sin(angle);
        const int len=i%5==0 ? 4 : 2;
        const uint16_t color=i>=17 ? ember : palette[9];
        c.drawLine(x+std::lround((radius-len)*cs),y+std::lround((radius-len)*sn),
                   x+std::lround(radius*cs),y+std::lround(radius*sn),color);
    }
    const float angle=start+sweep*fraction;
    const int tipX=x+std::lround(13*std::cos(angle)),tipY=y+std::lround(13*std::sin(angle));
    c.fillTriangle(x-2,y,x+2,y,tipX,tipY,ice);
    c.drawLine(x,y,tipX,tipY,ice);
    char value[4];
    std::snprintf(value,sizeof(value),"%02d",int(std::lround(100*fraction)));
    ui::shadowText(value,x,235,1,cream);
}

void startLine(int x,int y,int height,int tile) {
    auto& c=display::canvas();
    for (int row=0;row*tile<height;++row) {
        for (int col=0;col<2;++col) {
            c.fillRect(x+col*tile,y+row*tile,tile,std::min(tile,height-row*tile),
                       (row+col)%2 ? palette[5] : cream);
        }
    }
}
}

void Race::draw(int local,const arcade::DrawCtx& ctx) {
    auto& c=display::canvas();
    const Kart& me=cars[local];
    const int count=solo ? 1 : 2;
    const float left=solo ? introTicks/60.f : ctx.countdownLeft;
    float blend=std::max(0.f,std::min(1.f,(kCountTicks+kFocusTicks-left*60.f)/kFocusTicks));
    blend=blend*blend*(3-2*blend);
    if (!cameraMs_ || tick<drawTick_) {
        cameraAngle_=me.heading;
        cameraPos_=me.pos;
    }
    const float elapsed=std::min(.1f,(ctx.nowMs-cameraMs_)*.001f);
    cameraMs_=ctx.nowMs;
    cameraPos_=cameraPos_+(me.pos-cameraPos_)*(1-std::exp(-elapsed*18));
    // Velocity points along the actual slide. Unwrap before easing across +/-pi.
    if (me.vel.dot(me.vel)>16) {
        float error=std::remainder(std::atan2(me.vel.y,me.vel.x)-cameraAngle_,2*pi);
        cameraAngle_=std::remainder(cameraAngle_+error*(1-std::exp(-elapsed*4.3f)),2*pi);
    }
    const float sn=std::sin(cameraAngle_),cs=std::cos(cameraAngle_);
    const Vec middle={kMapWidth*.5f,kMapHeight*.5f};
    const Camera cam={middle+(cameraPos_-middle)*blend,{-sn,cs},{cs,sn},
                      .345f*std::pow(1.8f/.345f,blend),120+44*blend};
    terrain(cam);
    if (tick!=drawTick_) {
        if (tick<drawTick_) for (auto& mark:marks_) mark.used=false;
        drawTick_=tick;
        for (int i=0;i<count;++i) {
            const Kart& k=cars[i];
            if (std::fabs(k.slip)<.055f || k.vel.dot(k.vel)<4 || tick-lastMark_[i]<2) continue;
            lastMark_[i]=tick;
            const Vec rear=k.pos-Vec{std::cos(k.heading),std::sin(k.heading)}*3.7f;
            marks_[markIndex_]={rear,k.heading,tick,true};
            markIndex_=(markIndex_+1)%kMarks;
        }
    }
    for (const auto& mark:marks_) {
        if (!mark.used || tick-mark.at>75) continue;
        const Vec side={-std::sin(mark.heading)*2,std::cos(mark.heading)*2};
        for (int sign:{-1,1}) {
            const Vec p=cam.screen(mark.pos+side*float(sign));
            c.fillRect(std::lround(p.x),std::lround(p.y),2,2,palette[5]);
        }
    }
    for (int i=0;i<kProps;++i)
        obstacle(cam.screen(props[i].pos),cam.zoom,i%2,cam.right,cam.forward);
    for (int i=0;i<count;++i) {
        const Kart& k=cars[i];
        const Vec p=cam.screen(k.pos);
        if (p.x< -12 || p.x>147 || p.y<4 || p.y>237) continue;
        if (cam.zoom<.7f) {
            c.fillCircle(std::lround(p.x),std::lround(p.y),3,ink);
            c.fillCircle(std::lround(p.x),std::lround(p.y),2,i ? ice : ember);
        } else model(std::lround(p.x),std::lround(p.y),k.heading-cameraAngle_-pi/2,i,cam.zoom,k.impact>8);
    }
    c.fillRect(0,0,135,16,ink);
    c.fillRect(0,225,135,15,ink);
    c.fillRect(38,6,3,8,local ? ice : ember);
    char label[20],time[16];
    std::snprintf(label,sizeof(label),"%d/3",std::min(3,std::max(1,me.laps+1)));
    ui::shadowText(label,55,10,1,cream);
    clockText(time,sizeof(time),me.finish ? me.finish : tick);
    if (left>0) {
        ui::shadowText("COPPER MILL",67,233,1,cream);
        if (left<=kCountTicks/60.f) {
            std::snprintf(label,sizeof(label),"%d",int(std::ceil(left)));
            ui::shadowText(label,67,40,2,ember);
        }
    } else {
        ui::shadowText(time,30,233,1,cream);
        if (me.drift || std::fabs(me.slip)>.055f) ui::shadowText("DRIFT",69,233,1,ember);
        speedometer(me.vel);
        if (tick<39) ui::shadowText("GO",67,40,2,ice);
    }
    if (me.wrongWay) ui::shadowText("WRONG WAY",67,34,1,cream);
    if (me.finish) {
        const bool done=phase()==arcade::MatchPhase::MatchEnd;
        const char* result=solo ? "FINISH" : !done ? "FINISHED" :
            me.finish==cars[1-local].finish ? "TIE" : me.finish<cars[1-local].finish ? "YOU WIN" : "2ND PLACE";
        c.fillRoundRect(6,86,123,45,4,ink);
        ui::shadowText(result,67,100,2,local ? ice : ember);
        ui::shadowText(solo ? "A = AGAIN" : done ? time : "WAIT FOR RIVAL",67,119,1,cream);
    }
}

void Race::sfx(uint32_t events) {
    events=(events>>(4*localPlayer_))&15;
    static const sound::Note lap[]={{650,45},{980,70}}, slide[]={{230,20},{180,20}};
    static const sound::Note hit[]={{95,65}}, finish[]={{660,70},{880,70},{1320,135}};
    if (events&Finished) sound::play(finish,3,4);
    else if (events&Hit) sound::play(hit,1,3);
    else if (events&Lap) sound::play(lap,2,2);
    else if (events&Slide) sound::play(slide,2,1);
}
void drawCard(int x,int y,int w,int h,uint32_t) {
    auto& c=display::canvas();
    c.setClipRect(x,y,w,h);
    c.fillRect(x,y,w,h,palette[6]);
    const int tile=std::max(3,h/11);
    startLine(x+w/2-tile,y,h,tile);
    model(x+w/2,y+h/2,-.46f,0,std::min(w/16.f,h/11.f));
    c.clearClipRect();
}
void drawLobby(uint32_t) {
    auto& c=display::canvas();
    c.fillScreen(ink);
    startLine(13,0,79,5);
    model(25,43,-.46f,0,3);
    ui::shadowText("TILT STEER   A GAS   B DRIFT",120,98,1,cream);
}
}

