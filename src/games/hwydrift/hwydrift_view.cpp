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
constexpr uint16_t ink=rgb(15,28,35), tire=rgb(27,32,39), fadedMark=rgb(52,66,74);

// A kart is drawn as polygons in its own frame (u forward, v sideways, one unit = one world unit), so it
// stays crisp at every angle and scale.
struct Frame {
    float x,y,ax,ay,bx,by;
    Frame(float x,float y,float hx,float hy,float scale)
        : x(x),y(y),ax(hx*scale),ay(hy*scale),bx(-hy*scale),by(hx*scale) {}
    void at(float u,float v,int& px,int& py) const {
        px=int(std::lround(x+ax*u+bx*v));
        py=int(std::lround(y+ay*u+by*v));
    }
};
template<size_t N>
void polygon(const Frame& f,const float (&pts)[N][2],uint16_t color) {
    auto& c=display::canvas();
    int px[N],py[N];
    for (size_t i=0;i<N;++i) f.at(pts[i][0],pts[i][1],px[i],py[i]);
    for (size_t i=1;i+1<N;++i) c.fillTriangle(px[0],py[0],px[i],py[i],px[i+1],py[i+1],color);
}
void box(const Frame& f,float u0,float u1,float v0,float v1,uint16_t color) {
    const float pts[4][2]={{u0,v0},{u1,v0},{u1,v1},{u0,v1}};
    polygon(f,pts,color);
}

#ifdef MIDDIFF_HOST_CANVAS
inline uint16_t order(uint16_t p) { return p; }
#else
inline uint16_t order(uint16_t p) { return uint16_t((p<<8)|(p>>8)); }
#endif

// Darkens the pixels under a rounded footprint. `power` 1 is an ellipse, 3 an almost square box.
// Working on the existing pixels keeps the road and kerb visible through the shadow.
void shade(float cx,float cy,float ax,float ay,float halfLen,float halfWid,int power,
           int left,int top,int right,int bottom) {
    auto& c=display::canvas();
    auto* dst=static_cast<uint16_t*>(c.getBuffer());
    const int stride=c.width();
    const float reach=std::max(halfLen,halfWid)+1;
    const int x0=std::max(left,int(std::floor(cx-reach))),x1=std::min(right,int(std::ceil(cx+reach)));
    const int y0=std::max(top,int(std::floor(cy-reach))),y1=std::min(bottom,int(std::ceil(cy+reach)));
    for (int y=y0;y<y1;++y) {
        for (int x=x0;x<x1;++x) {
            const float dx=x+.5f-cx,dy=y+.5f-cy;
            const float u=(dx*ax+dy*ay)/halfLen,v=(-dx*ay+dy*ax)/halfWid;
            float a=u*u,b=v*v;
            for (int i=1;i<power;++i) { a*=a; b*=b; }
            if (a+b>1) continue;
            const uint16_t p=order(dst[y*stride+x]);
            dst[y*stride+x]=order(uint16_t(((p>>1)&0x7BEF)+((p>>3)&0x18E3)));
        }
    }
}

void kartShadow(const Frame& f,float dx,float dy,int left,int top,int right,int bottom) {
    const float len=std::sqrt(f.ax*f.ax+f.ay*f.ay);
    shade(f.x+dx,f.y+dy,f.ax/len,f.ay/len,4.7f*len,2.9f*len,2,left,top,right,bottom);
}

void kart(const Frame& f,int player,bool flash=false) {
    auto& c=display::canvas();
    const uint16_t main=flash ? cream : player ? ice : ember;
    const uint16_t dark=flash ? cream : player ? palette[14] : palette[11];
    for (float u:{-3.f,2.7f})
        for (float v:{-2.1f,2.1f}) box(f,u-1.1f,u+1.1f,v-.8f,v+.8f,tire);
    box(f,-4.7f,-3.8f,-2.7f,2.7f,dark);
    const float body[6][2]={{-4.f,-1.7f},{1.4f,-1.7f},{4.4f,-.7f},{4.4f,.7f},{1.4f,1.7f},{-4.f,1.7f}};
    polygon(f,body,main);
    box(f,-3.6f,3.6f,-.3f,.3f,flash ? main : dark);
    box(f,-1.9f,.3f,-1.f,1.f,palette[13]);
    int hx,hy;
    f.at(-.7f,0,hx,hy);
    const float scale=std::sqrt(f.ax*f.ax+f.ay*f.ay);
    c.fillCircle(hx,hy,std::max(1,int(std::lround(.95f*scale))),cream);
    int vx,vy;
    f.at(-.2f,0,vx,vy);
    if (scale>=2.5f) c.fillCircle(vx,vy,std::max(1,int(std::lround(.4f*scale))),palette[13]);
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
        c.fillCircle(x,y,radius,tire);
        c.drawFastHLine(x-radius/2,y-radius+1,radius,palette[8]);
        c.fillCircle(x,y,std::max(1,radius/3),palette[7]);
    }
}
void obstacleShadow(Vec p,float zoom,bool crate,const Camera& cam) {
    if (p.x< -12 || p.x>147 || p.y<4 || p.y>239) return;
    const float dx=.9f*zoom,dy=1.3f*zoom;
    if (crate) {
        shade(p.x+dx,p.y+dy,cam.right.x,-cam.forward.x,3.5f*zoom,3.5f*zoom,3,0,16,135,225);
    } else {
        shade(p.x+dx,p.y+dy,1,0,3*zoom,3*zoom,1,0,16,135,225);
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

void bandOf(float cx,float cy,const Vec& d,float u0,float u1,float v0,float v1,uint16_t color) {
    auto& c=display::canvas();
    const Vec n={-d.y,d.x};
    int px[4],py[4];
    const float corner[4][2]={{u0,v0},{u1,v0},{u1,v1},{u0,v1}};
    for (int i=0;i<4;++i) {
        px[i]=int(std::lround(cx+d.x*corner[i][0]+n.x*corner[i][1]));
        py[i]=int(std::lround(cy+d.y*corner[i][0]+n.y*corner[i][1]));
    }
    c.fillTriangle(px[0],py[0],px[1],py[1],px[2],py[2],color);
    c.fillTriangle(px[0],py[0],px[2],py[2],px[3],py[3],color);
}

// A sliding kart on a scrolling stretch of the circuit, shared by the launcher card and the waiting screen.
void showcase(int x,int y,int w,int h,uint32_t ms,bool lobby) {
    auto& c=display::canvas();
    const float t=ms*.001f;
    c.setClipRect(x,y,w,h);
    c.fillRect(x,y,w,h,palette[2]);
    const float cx=x+w*.5f,cy=y+h*.55f;
    const float angle=-.34f;
    const Vec d={std::cos(angle),std::sin(angle)};
    const float half=h*(lobby ? .2f : .3f),k=half/15.4f,reach=(w+h)*.6f;
    const float scroll=t*h*1.5f,stripe=7*k;
    bandOf(cx,cy,d,-reach,reach,-half-4*k,half+4*k,palette[8]);
    bandOf(cx,cy,d,-reach,reach,-half-2*k,half+2*k,palette[5]);
    for (int side=-1;side<=1;side+=2) {
        int index=0;
        for (float u=-reach-std::fmod(scroll,2*stripe);u<reach;u+=stripe,++index) {
            const float edge=side*half;
            bandOf(cx,cy,d,u,u+stripe,std::min(edge,edge+side*2*k),std::max(edge,edge+side*2*k),
                   index%2 ? palette[10] : palette[11]);
        }
    }
    bandOf(cx,cy,d,-reach,reach,-half,half,palette[6]);
    for (float u=-reach-std::fmod(scroll,8*k);u<reach;u+=8*k) bandOf(cx,cy,d,u,u+4*k,-.5f*k,.5f*k,palette[9]);
    const float gate=reach-std::fmod(scroll*.8f,2*reach),cell=half/3;
    for (int row=0;row<6;++row)
        for (int col=0;col<2;++col)
            bandOf(cx,cy,d,gate+col*cell,gate+(col+1)*cell,-half+row*cell,-half+(row+1)*cell,
                   (row+col)%2 ? palette[5] : cream);
    const float scale=lobby ? h/26.f : h/18.f;
    const float u0=lobby ? -w*.22f : 0.f;
    const float yaw=angle-.62f+.05f*std::sin(t*3.1f);
    const Vec heading={std::cos(yaw),std::sin(yaw)},side={-heading.y,heading.x};
    const Vec centre={cx+d.x*u0,cy+d.y*u0};
    const Vec rear=centre-heading*(3.f*scale);
    for (int wheel=-1;wheel<=1;wheel+=2) {
        const Vec start=rear+side*(2.1f*scale*wheel);
        int lastX=int(std::lround(start.x)),lastY=int(std::lround(start.y));
        for (int i=1;i<=14;++i) {
            const float s=i/14.f;
            const Vec p=start-d*(s*w*.55f)+side*(s*s*scale*3.f*wheel);
            const int px=int(std::lround(p.x)),py=int(std::lround(p.y));
            if (i%5) c.drawLine(lastX,lastY,px,py,palette[5]);
            lastX=px;lastY=py;
        }
    }
    const Frame f(centre.x,centre.y,heading.x,heading.y,scale);
    kartShadow(f,.9f*scale,1.3f*scale,x,y,x+w,y+h);
    for (int wheel=-1;wheel<=1;wheel+=2) {
        for (int puff=0;puff<5;++puff) {
            const float age=std::fmod(t*1.3f+puff*.2f+(wheel>0 ? .1f : 0.f),1.f);
            const Vec p=rear+side*(2.1f*scale*wheel)-d*(age*scale*14.f)+side*(age*scale*4.f*wheel);
            c.fillCircle(int(std::lround(p.x)),int(std::lround(p.y)),
                         std::max(1,int(std::lround((1.f+age*2.6f)*scale*.35f))),
                         age<.33f ? palette[9] : age<.66f ? palette[8] : palette[7]);
        }
    }
    kart(f,0);
    c.clearClipRect();
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
        cameraZoom_=1.8f;
    }
    const float elapsed=std::min(.1f,(ctx.nowMs-cameraMs_)*.001f);
    cameraMs_=ctx.nowMs;
    cameraPos_=cameraPos_+(me.pos-cameraPos_)*(1-std::exp(-elapsed*18));
    const float speed=std::sqrt(me.vel.dot(me.vel));
    // Faster travel pulls the view back and sends the kart lower, so corners are seen sooner.
    const float pace=std::min(1.f,speed/88.f);
    cameraZoom_+=(1.8f-.4f*pace-cameraZoom_)*(1-std::exp(-elapsed*2.5f));
    // The view follows the line of travel, but part of the body's yaw shows through so a slide stays readable.
    // Unwrap before easing across +/-pi.
    if (speed>4) {
        const float travel=std::atan2(me.vel.y,me.vel.x);
        const float aim=travel+.35f*std::remainder(me.heading-travel,2*pi);
        const float error=std::remainder(aim-cameraAngle_,2*pi);
        cameraAngle_=std::remainder(cameraAngle_+error*(1-std::exp(-elapsed*7.5f)),2*pi);
    }
    const float sn=std::sin(cameraAngle_),cs=std::cos(cameraAngle_);
    const Vec middle={kMapWidth*.5f,kMapHeight*.5f};
    const float zoom=.345f*std::pow(cameraZoom_/.345f,blend);
    const Camera cam={middle+(cameraPos_-middle)*blend,{-sn,cs},{cs,sn},
                      zoom,120+(44+14*(1.8f-cameraZoom_)/.4f)*blend};
    terrain(cam);
    if (tick!=drawTick_) {
        if (tick<drawTick_) {
            for (auto& car:marks_) for (auto& mark:car) mark.at=0;
            lastMark_[0]=lastMark_[1]=0;
        }
        drawTick_=tick;
        for (int i=0;i<count;++i) {
            const Kart& k=cars[i];
            if (std::fabs(k.slip)<.055f || k.vel.dot(k.vel)<4) continue;
            const Vec heading={std::cos(k.heading),std::sin(k.heading)};
            const Vec rear=k.pos-heading*(3.f*kKartScale),side={-heading.y*2.1f*kKartScale,heading.x*2.1f*kKartScale};
            marks_[i][markHead_[i]]={rear+side,rear-side,tick,lastMark_[i] && tick-lastMark_[i]<=4};
            markHead_[i]=(markHead_[i]+1)%kMarks;
            lastMark_[i]=tick;
        }
    }
    for (int i=0;i<count;++i) {
        for (int n=0;n<kMarks;++n) {
            const Mark& mark=marks_[i][n];
            const Mark& before=marks_[i][(n+kMarks-1)%kMarks];
            if (!mark.at || !mark.link || before.at>=mark.at || tick-mark.at>=kMarks) continue;
            const uint16_t color=tick-mark.at<30 ? palette[5] : fadedMark;
            for (int wheel=0;wheel<2;++wheel) {
                const Vec a=cam.screen(wheel ? before.right : before.left),b=cam.screen(wheel ? mark.right : mark.left);
                for (int pixel=0;pixel<2;++pixel)
                    c.drawLine(std::lround(a.x)+pixel,std::lround(a.y),std::lround(b.x)+pixel,std::lround(b.y),color);
            }
        }
    }
    if (cam.zoom>=.7f) {
        const float dx=.9f*cam.zoom,dy=1.3f*cam.zoom;
        for (int i=0;i<kProps;++i) obstacleShadow(cam.screen(props[i].pos),cam.zoom,i%2,cam);
        for (int i=0;i<count;++i) {
            const Vec p=cam.screen(cars[i].pos);
            if (p.x< -12 || p.x>147 || p.y<4 || p.y>237) continue;
            const Vec h={std::cos(cars[i].heading),std::sin(cars[i].heading)};
            kartShadow(Frame(p.x,p.y,h.dot(cam.right),-h.dot(cam.forward),cam.zoom*kKartScale),dx,dy,0,16,135,225);
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
        } else {
            const Vec h={std::cos(k.heading),std::sin(k.heading)};
            kart(Frame(p.x,p.y,h.dot(cam.right),-h.dot(cam.forward),cam.zoom*kKartScale),i,k.impact>8);
        }
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
void drawCard(int x,int y,int w,int h,uint32_t ms) { showcase(x,y,w,h,ms,false); }
void drawLobby(uint32_t ms) {
    showcase(0,0,240,135,ms,true);
    ui::shadowText("TILT STEER   A GAS   B DRIFT",120,113,1,cream);
}
}

