#include "core/display.h"
#include "core/platform.h"
#include "core/sound.h"
#include "games/hwydrift/hwydrift.h"
#include "ui/gfx.h"
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#define PROGMEM
#include "lgfx/Fonts/glcdfont.h"

namespace { M5Canvas canvas; }
namespace display { M5Canvas& canvas(){return ::canvas;} }
namespace sound { void play(const Note*,int,uint8_t,uint16_t){} }
namespace ui {
void shadowText(const char* text,int x,int y,int size,uint16_t color) {
    int start=x-int(strlen(text))*6*size/2,top=y-4*size;
    auto draw=[&](int offset,uint16_t col) {
        for(int i=0;text[i];++i)for(int cx=0;cx<5;++cx)for(int cy=0;cy<8;++cy)
            if(font[uint8_t(text[i])*5+cx]&(1<<cy))
                ::canvas.fillRect(start+(i*6+cx)*size+offset,top+cy*size+offset,size,size,col);
    };
    draw(size/2+1,0);draw(0,color);
}
}

// Drives the real simulation around the circuit with a simple pilot and saves portrait frames.
int main(int argc,char** argv) {
    if(argc!=2)return 1;
    auto save=[&](const std::string& name){canvas.save((std::string(argv[1])+"/"+name+".ppm").c_str());};
    canvas.setSize(240,135);
    for (uint32_t ms : {1200u,1700u}) {
        canvas.fillScreen(0);
        hwy::drawCard(60,28,120,78,ms);
        save("card_"+std::to_string(ms));
    }
    hwy::drawLobby(1200);save("lobby");
    canvas.setSize(135,240);
    hwy::Race race;
    PadInput pads[2]={};
    race.begin(arcade::Mode::Solo,0);
    race.introTicks=0;
    arcade::DrawCtx ctx;
    ctx.solo=true;
    uint32_t ms=1000;
    float topSlip=0;
    for (int frame=0;frame<2400;++frame) {
        const auto& k=race.cars[0];
        const int ahead=(int(std::lround(k.section))+4)%hwy::sections();
        const hwy::Vec target=hwy::node(ahead)-k.pos;
        const float error=std::remainder(std::atan2(target.y,target.x)-k.heading,6.2831853f);
        pads[0].a=true;
        pads[0].aim=std::max(-24.f,std::min(24.f,error*40));
        pads[0].b=std::fabs(error)>.3f;
        race.step(1.f/60,pads);
        ms+=17;ctx.nowMs=ms;
        race.draw(0,ctx);
        topSlip=std::max(topSlip,std::fabs(k.slip));
        if (frame%40==0) save("run_"+std::to_string(frame));
    }
    std::printf("laps %d, top slip %.2f rad\n",race.cars[0].laps,topSlip);
    return 0;
}
