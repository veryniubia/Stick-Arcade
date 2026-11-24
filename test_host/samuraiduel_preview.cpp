#include "core/display.h"
#include "core/platform.h"
#include "core/sound.h"
#include "games/samuraiduel/samuraiduel.h"
#include "games/samuraiduel/view.h"
#include "ui/gfx.h"
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
int main(int argc,char** argv) {
    if(argc!=2)return 1;
    auto save=[&](const char* name){canvas.save((std::string(argv[1])+"/"+name+".ppm").c_str());};
    SamuraiDuelGame g;PadInput p[2]={};g.begin(arcade::Mode::Host,122);
    arcade::DrawCtx ctx;ctx.nowMs=1200;
    g.draw(0,ctx);save("arena");
    g.begin(arcade::Mode::Host,122);
    g.fighters[0].x=112;g.fighters[0].y=72;
    g.fighters[1].x=250;g.fighters[1].y=106;
    ++p[1].aCount;g.step(1.f/60,p);g.draw(0,ctx);save("platforms");
    g.begin(arcade::Mode::Host,122);p[0]=p[1]=PadInput();
    for (int direction : {1, -1}) {
        g.fighters[0].x = direction > 0 ? 89 : 231;
        g.fighters[1].x = direction > 0 ? 276 : 44;
        g.fighters[0].facing = direction;
        const char* side = direction > 0 ? "right" : "left";
        const float times[] = {.035f, .10f, .15f, .23f};
        for (int i=0;i<4;++i) {
            g.fighters[0].action = SamuraiDuelGame::kSlashDuration - times[i];
            g.draw(0,ctx);
            save((std::string("slash_") + side + "_" + std::to_string(i)).c_str());
        }
    }
    g.begin(arcade::Mode::Host,122);
    g.fighters[0].x=84;g.fighters[0].action=.29f;
    auto& b=g.bullets[0];b.active=true;b.x=109;b.y=SamuraiDuelGame::kFloor-8;b.direction=-1;b.owner=1;
    g.step(1.f/60,p);g.draw(0,ctx);save("parry");
    g.fighters[1].grounded=false;g.fighters[1].x=177;g.fighters[1].y=70;
    g.fighters[1].action=.10f;g.draw(1,ctx);save("air_shot");
    g.phase_=arcade::MatchPhase::RoundEnd;g.score[0]=1;g.roundWinner=0;g.phaseTimer=g.kRoundPause;g.draw(0,ctx);save("point");
    g.phase_=arcade::MatchPhase::MatchEnd;g.score[0]=5;g.winner=g.roundWinner=0;g.draw(0,ctx);save("match_end");
    g.begin(arcade::Mode::Solo,123);g.score[0]=260;ctx.solo=true;g.draw(0,ctx);
    // Include the platform's solo badge to verify reserved HUD space.
    canvas.fillRect(0,0,240,3,platform::rgb(255,150,40));
    canvas.fillRect(5,5,29,11,platform::rgb(255,150,40));ui::shadowText("SOLO",19,11,1,0);
    save("solo");
    ctx.solo=false;g.begin(arcade::Mode::Host,123);g.draw(1,ctx);
    ui::shadowText("PING 24",215,10,1,65535);save("online_ping");
    samuraiduel_view::drawLobby(1200);save("lobby");
    canvas.fillRect(0,0,240,135,0);samuraiduel_view::drawCard(60,28,120,78,1200);save("card");
    return 0;
}
