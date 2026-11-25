#include "core/display.h"
#include "sim.h"
#include <fstream>
namespace display {
namespace { M5Canvas cv; bool portrait=false; int fps=60; }
bool begin(){cv.createSprite(width(),height());return true;}
M5Canvas& canvas(){return cv;}
int width(){return portrait?135:240;} int height(){return portrait?240:135;}
void setPortrait(bool p){if(p==portrait)return;portrait=p;M5.Imu.portrait=p;M5.Display.setRotation(p?0:1);cv.createSprite(width(),height());}
void setTargetFps(int f){fps=f>0?f:60;}
#ifdef __ANDROID__
int targetFps(){return fps;}
void present(){}
#else
void present(){sim::present();sim::timeUs+=1000000/fps;}
#endif
const void* lastFrame(){return cv.getBuffer();}
bool dmaActive(){return false;} uint32_t pushUs(){return 0;} uint32_t waitUs(){return 0;}
}
namespace sim {
void saveFrame(const std::string& path){
    std::ofstream f(path,std::ios::binary);f<<"P6\n"<<display::width()<<" "<<display::height()<<"\n255\n";
    for(auto c:display::canvas().pixels()){char rgb[]={char(((c>>11)&31)*255/31),char(((c>>5)&63)*255/63),char((c&31)*255/31)};f.write(rgb,3);}
}
}
