#include "core/display.h"
#include "core/platform.h"
#include "core/sound.h"
#include "games/middiff/middiff.h"
#include "games/middiff/view.h"
#include "ui/gfx.h"
#include <chrono>
#include <cstring>
#include <string>
#define PROGMEM
#include "lgfx/Fonts/glcdfont.h"

namespace { M5Canvas canvas; }
namespace display { M5Canvas& canvas(){return ::canvas;} }
namespace sound { void play(const Note*,int,uint8_t){} }
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
using G = MidDiffGame;
static G fresh(arcade::Mode m = arcade::Mode::Host) { G g; g.begin(m, 5); return g; }
static void look(G& g, int i, float yaw, float pitch) { g.players[i].yaw = yaw; g.players[i].pitch = pitch; }
int main(int argc,char** argv) {
    if(argc!=2)return 1;
    auto save=[&](const char* name){canvas.save((std::string(argv[1])+"/"+name+".ppm").c_str());};
    arcade::DrawCtx ctx;ctx.nowMs=1200;
    G g=fresh();
    g.draw(0,ctx);save("start");
    look(g,0,0,-0.6f);g.draw(0,ctx);save("aimed");
    g.players[1].x=2.1f;g.players[1].crouch=1;look(g,0,8,-1);g.draw(0,ctx);save("peek");
    g=fresh();g.players[1].crouch=1;g.draw(0,ctx);save("hidden");
    g=fresh();look(g,0,-80,0);g.draw(0,ctx);save("left");
    look(g,0,80,0);g.draw(0,ctx);save("right");
    look(g,0,0,25);g.draw(0,ctx);save("up");
    look(g,0,0,-25);g.draw(0,ctx);save("down");
    g=fresh();g.players[0].x=3;g.players[0].z=-6;g.players[0].crouch=1;look(g,0,20,0);g.draw(0,ctx);save("close_crouch");
    g=fresh();g.players[0].flash=.1f;g.players[0].shotX=0;g.players[0].shotY=1.5f;g.players[0].shotZ=7.5f;
    g.players[0].marker=.2f;g.players[0].cooldown=.6f;g.players[1].hurt=.2f;g.players[1].flash=.05f;
    g.players[1].shotX=0;g.players[1].shotY=1.2f;g.players[1].shotZ=-7.5f;g.draw(0,ctx);save("shot");
    g=fresh();g.players[0].hurt=.3f;g.players[0].hp=35;g.players[0].winded=true;g.players[0].stamina=0;g.draw(0,ctx);save("hurt");
    g=fresh();look(g,1,15,3);g.draw(1,ctx);save("p2_view");
    g=fresh();g.score[0]=1;g.score[1]=2;g.phase_=arcade::MatchPhase::RoundEnd;g.roundWinner=0;g.phaseTimer=G::kRoundPause;g.draw(0,ctx);save("point");
    g.phase_=arcade::MatchPhase::MatchEnd;g.score[0]=3;g.winner=g.roundWinner=0;g.draw(0,ctx);save("match_end");
    g=fresh(arcade::Mode::Solo);g.score[0]=12;ctx.solo=true;g.draw(0,ctx);
    canvas.fillRect(0,0,240,3,platform::rgb(255,150,40));
    canvas.fillRect(5,5,29,11,platform::rgb(255,150,40));ui::shadowText("SOLO",19,11,1,0);
    save("solo");
    ctx.solo=false;
    middiff_view::drawLobby(1200);save("lobby");
    for(int k=0;k<3;++k){canvas.fillRect(0,0,240,135,0);middiff_view::drawCard(60,28,120,78,600+k*1100);save(("card"+std::to_string(k)).c_str());}
    // Frame cost of the renderer on this PC (indicative only).
    g=fresh();look(g,0,10,-1);
    auto t0=std::chrono::steady_clock::now();
    for(int i=0;i<200;++i)g.draw(0,ctx);
    double ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-t0).count()/200;
    printf("host draw: %.3f ms/frame\n",ms);
    return 0;
}
