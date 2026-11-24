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
namespace sound { void playSample(const uint8_t*,unsigned,unsigned,bool){} void play(const Note*,int,uint8_t,uint16_t){} }
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
static G fresh(arcade::Mode m = arcade::Mode::Host) {
    G g; g.begin(m, 5);
    for (auto& p : g.players) { p.invuln = 0; p.cooldown = 0; }
    g.gate.state = G::GateState::Closed; g.gate.pos = 0; g.gate.timer = 5;
    return g;
}
static void place(G& g, int i, float x, float depth) {
    g.players[i].x = x;
    g.players[i].z = (g.face(i) > 0 ? -1.0f : 1.0f) * depth;
}
static void look(G& g, int i, float yaw, float pitch) { g.players[i].yaw = yaw; g.players[i].pitch = pitch; }
static void openGate(G& g) { g.gate.state = G::GateState::Open; g.gate.pos = 1; g.gate.timer = 4; }
// Aims player i's view at player 1-i's head.
static void aimAtFoe(G& g, int i, float y = 1.6f) {
    const float f = g.face(i);
    const auto& me = g.players[i];
    const auto& foe = g.players[1 - i];
    const float dx = (foe.x - me.x) * f, dz = (foe.z - me.z) * f;
    g.players[i].yaw = std::atan2(dx, dz) * 57.29578f;
    g.players[i].pitch = std::atan2(y - G::kEye, std::hypot(dx, dz)) * 57.29578f;
}
int main(int argc,char** argv) {
    if(argc!=2)return 1;
    auto save=[&](const char* name){canvas.save((std::string(argv[1])+"/"+name+".ppm").c_str());};
    arcade::DrawCtx ctx;ctx.nowMs=1200;
    G g=fresh();
    // Spawn view and the far view.
    place(g,0,0,8.5f);place(g,1,0,8.5f);g.draw(0,ctx);save("start");
    place(g,0,0.5f,12.5f);place(g,1,0.5f,12.5f);aimAtFoe(g,0);g.draw(0,ctx);save("far");
    ctx.scoped=true;g.draw(0,ctx);save("scope");ctx.scoped=false;
    g.players[0].cooldown=.6f;g.draw(0,ctx);save("bolt");
    // Behind the high cover, gate closed and open: the doorways show the other side.
    g=fresh();place(g,0,-3.0f,7.6f);place(g,1,-3.0f,7.6f);aimAtFoe(g,0);g.draw(0,ctx);save("gate_closed");
    openGate(g);aimAtFoe(g,0);g.draw(0,ctx);save("gate_open");
    g.gate.state=G::GateState::Closed;g.gate.pos=0;g.gate.timer=.3f;ctx.nowMs=1230;g.draw(0,ctx);save("gate_warn");ctx.nowMs=1200;
    g.gate.state=G::GateState::Opening;g.gate.pos=.5f;g.gate.timer=.45f;g.draw(0,ctx);save("gate_lifting");
    // Right doorway with the barrels, the centre arch, and looking along the walls.
    g=fresh();place(g,0,3.4f,7.4f);place(g,1,3.4f,7.4f);aimAtFoe(g,0);g.draw(0,ctx);save("barrels");
    g=fresh();place(g,0,0,9.0f);place(g,1,0,9.0f);look(g,0,-G::kYawLimit,2);g.draw(0,ctx);save("left");
    look(g,0,G::kYawLimit,2);g.draw(0,ctx);save("right");
    look(g,0,0,26);g.draw(0,ctx);save("up");
    look(g,0,0,-26);g.draw(0,ctx);save("down");
    // The other team's side: its own cover layout.
    g=fresh();place(g,1,-1.0f,7.6f);place(g,0,-1.0f,8.5f);aimAtFoe(g,1);g.draw(1,ctx);save("team1_view");
    look(g,1,0,-24);g.draw(1,ctx);save("team1_down");
    // The foe up close: aiming at us, aiming aside, hurt, hopping, flashing, dead, protected.
    g=fresh();openGate(g);place(g,0,0,6.4f);place(g,1,0,6.4f);aimAtFoe(g,0);look(g,1,0,0);g.draw(0,ctx);save("foe_at_me");
    look(g,1,-35,5);g.draw(0,ctx);save("foe_left");
    look(g,1,38,-6);g.players[1].flash=.1f;g.draw(0,ctx);save("foe_fires");
    look(g,1,0,0);g.players[1].flash=0;g.players[1].hurt=.2f;g.draw(0,ctx);save("foe_hurt");
    g.players[1].hurt=0;g.players[1].jumpT=G::kJumpTime/2;g.draw(0,ctx);save("foe_hop");
    g.players[1].jumpT=0;g.players[1].dead=1.2f;g.draw(0,ctx);save("foe_falling");
    g.players[1].dead=1.0f;g.draw(0,ctx);save("foe_down");
    g.players[1].dead=0;g.players[1].invuln=1.0f;ctx.nowMs=1300;g.draw(0,ctx);save("foe_protected");ctx.nowMs=1200;
    g.players[1].invuln=0;g.draw(0,ctx);
    // T vs CT: same scene seen by the other team.
    g=fresh();openGate(g);place(g,0,0,6.4f);place(g,1,0,6.4f);aimAtFoe(g,1);g.draw(1,ctx);save("foe_other_team");
    // HUD states.
    g=fresh();openGate(g);place(g,0,0,8.5f);place(g,1,0,8.5f);aimAtFoe(g,0);
    g.players[0].flash=.1f;g.players[0].marker=.2f;g.players[0].cooldown=.6f;g.players[0].shotX=0;g.players[0].shotY=1.6f;g.players[0].shotZ=8.5f;
    g.players[0].stamina=.45f;g.players[1].flash=.05f;g.players[1].shotX=0;g.players[1].shotY=1.2f;g.players[1].shotZ=-8.5f;g.draw(0,ctx);save("shot");
    g.players[0].flash=0;g.players[0].markerHead=true;g.players[0].hp=40;g.players[0].hurt=.3f;g.draw(0,ctx);save("hurt");
    g=fresh();place(g,0,0,8.5f);g.players[0].dead=1.1f;g.players[0].hp=0;g.draw(0,ctx);save("dead");
    g=fresh();place(g,0,0,8.5f);g.players[0].invuln=1.45f;g.draw(0,ctx);save("respawn_flash");
    g.players[0].invuln=.9f;g.draw(0,ctx);save("protected");
    g=fresh();g.score[0]=3;g.score[1]=4;place(g,0,0,8.5f);g.draw(0,ctx);save("score");
    g.phase_=arcade::MatchPhase::MatchEnd;g.score[0]=5;g.winner=0;g.draw(0,ctx);save("match_end");
    g=fresh(arcade::Mode::Solo);g.score[0]=12;ctx.solo=true;g.draw(0,ctx);
    canvas.fillRect(0,0,240,3,platform::rgb(255,150,40));
    canvas.fillRect(5,5,29,11,platform::rgb(255,150,40));ui::shadowText("SOLO",19,11,1,0);
    save("solo");
    ctx.solo=false;
    middiff_view::drawLobby(1200);save("lobby");
    for(int k=0;k<3;++k){canvas.fillRect(0,0,240,135,0);middiff_view::drawCard(60,28,120,78,600+k*1100);save(("card"+std::to_string(k)).c_str());}
    g=fresh();place(g,0,0,8.5f);place(g,1,0,8.5f);
    g.burger.state=G::BurgerState::Pickup;g.burger.x=.45f;g.burger.y=2.3f;g.burger.z=0;
    g.draw(0,ctx);save("burger_pickup");
    g.burger.state=G::BurgerState::Held;g.burger.owner=0;g.draw(0,ctx);save("burger_held");
    g.burger.state=G::BurgerState::Flying;g.burger.x=.4f;g.burger.y=1.9f;g.burger.z=-5.8f;g.burger.timer=1.88f;
    g.draw(0,ctx);save("burger_throw");
    g.burger.y=1.4f;g.burger.z=-4;g.burger.timer=.7f;g.draw(0,ctx);save("burger_flying");
    g.burger.state=G::BurgerState::Blast;g.burger.timer=.36f;g.draw(0,ctx);save("burger_blast");
    g.burger.timer=.16f;g.draw(0,ctx);save("burger_smoke");
    // Capture the real rules and renderer at 30 FPS, including the throw, bounces and blast.
    g=fresh();place(g,0,0,8.5f);place(g,1,0,8.5f);openGate(g);
    g.burger.state=G::BurgerState::Held;g.burger.owner=0;
    PadInput inputs[2]={};g.prime(inputs);
    for(int frame=0;frame<105;++frame) {
        if(frame==20)++inputs[0].aCount;
        if(frame>=20) {g.step(1.f/60,inputs);g.step(1.f/60,inputs);}
        ctx.nowMs=1200+frame*33;g.draw(0,ctx);
        char name[40];std::snprintf(name,sizeof(name),"burger_anim_%03d",frame);save(name);
    }
    // Frame cost of the renderer on this PC (indicative only).
    g=fresh();place(g,0,0,8.5f);place(g,1,0,8.5f);aimAtFoe(g,0);openGate(g);
    auto t0=std::chrono::steady_clock::now();
    for(int i=0;i<200;++i)g.draw(0,ctx);
    double ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-t0).count()/200;
    printf("host draw: %.3f ms/frame\n",ms);
    return 0;
}
