#pragma once
#include "Arduino.h"
#include <algorithm>
#include <vector>
#include "sim.h"
namespace simfont {
#include "lgfx/Fonts/glcdfont.h"
}
enum textdatum_t { top_left, top_right, middle_left, middle_center, middle_right };
constexpr uint16_t TFT_BLACK=0;
namespace lgfx { constexpr int palette_4bit=4; using rgb565_t=uint16_t; }
// RGB565 software implementation of the M5GFX operations used by this project.
class M5Canvas {
    int w_=240,h_=135,l_=0,t_=0,r_=240,b_=135,datum_=middle_center,size_=1;
    int pivotX_=0,pivotY_=0;
    uint16_t ink_=65535;
    std::vector<uint16_t> pixels_=std::vector<uint16_t>(240*135);
public:
    explicit M5Canvas(void* =nullptr) {}
    void* createSprite(int w,int h) { w_=w;h_=h;pixels_.assign(w*h,0);clearClipRect();return pixels_.data(); }
    void setColorDepth(int) {}
    int getColorDepth() const {return 16;}
    int width() const {return w_;} int height() const {return h_;}
    size_t bufferLength() const {return pixels_.size()*2;}
    void* getBuffer() {return pixels_.data();}
    const std::vector<uint16_t>& pixels() const {return pixels_;}
    void setClipRect(int x,int y,int w,int h){l_=x;t_=y;r_=x+w;b_=y+h;}
    void clearClipRect(){l_=t_=0;r_=w_;b_=h_;}
    void drawPixel(int x,int y,uint16_t c){if(x>=std::max(0,l_)&&x<std::min(w_,r_)&&y>=std::max(0,t_)&&y<std::min(h_,b_))pixels_[y*w_+x]=c;}
    void fillRect(int x,int y,int w,int h,uint16_t c){
        for(int yy=std::max({0,t_,y});yy<std::min({h_,b_,y+h});++yy)
            for(int xx=std::max({0,l_,x});xx<std::min({w_,r_,x+w});++xx)pixels_[yy*w_+xx]=c;
    }
    void fillScreen(uint16_t c){fillRect(0,0,w_,h_,c);}
    void drawFastHLine(int x,int y,int w,uint16_t c){fillRect(x,y,w,1,c);}
    void drawFastVLine(int x,int y,int h,uint16_t c){fillRect(x,y,1,h,c);}
    void drawRect(int x,int y,int w,int h,uint16_t c){fillRect(x,y,w,1,c);fillRect(x,y+h-1,w,1,c);fillRect(x,y,1,h,c);fillRect(x+w-1,y,1,h,c);}
    void drawLine(int x,int y,int ex,int ey,uint16_t c){
        int dx=abs(ex-x),dy=-abs(ey-y),sx=x<ex?1:-1,sy=y<ey?1:-1,e=dx+dy;
        for(;;){drawPixel(x,y,c);if(x==ex&&y==ey)break;int e2=2*e;if(e2>=dy){e+=dy;x+=sx;}if(e2<=dx){e+=dx;y+=sy;}}
    }
    void fillCircle(int x,int y,int r,uint16_t c){for(int yy=-r;yy<=r;++yy)for(int xx=-r;xx<=r;++xx)if(xx*xx+yy*yy<=r*r)drawPixel(x+xx,y+yy,c);}
    void drawCircle(int x,int y,int r,uint16_t c){int xx=r,yy=0,e=1-r;while(xx>=yy){for(int s:{-1,1})for(int t:{-1,1}){drawPixel(x+s*xx,y+t*yy,c);drawPixel(x+s*yy,y+t*xx,c);}++yy;if(e<0)e+=2*yy+1;else{--xx;e+=2*(yy-xx)+1;}}}
    void fillEllipse(int x,int y,int rx,int ry,uint16_t c){if(rx<=0||ry<=0)return;for(int yy=-ry;yy<=ry;++yy)for(int xx=-rx;xx<=rx;++xx)if(double(xx)*xx/(rx*rx)+double(yy)*yy/(ry*ry)<=1)drawPixel(x+xx,y+yy,c);}
    void fillRoundRect(int x,int y,int w,int h,int r,uint16_t c){r=std::min({r,w/2,h/2});fillRect(x+r,y,w-2*r,h,c);fillRect(x,y+r,w,h-2*r,c);for(int a:{x+r,x+w-r-1})for(int b:{y+r,y+h-r-1})fillCircle(a,b,r,c);}
    void drawRoundRect(int x,int y,int w,int h,int r,uint16_t c){
        r=std::min({r,w/2,h/2});drawFastHLine(x+r,y,w-2*r,c);drawFastHLine(x+r,y+h-1,w-2*r,c);drawFastVLine(x,y+r,h-2*r,c);drawFastVLine(x+w-1,y+r,h-2*r,c);
        for(int yy=0;yy<=r;++yy)for(int xx=0;xx<=r;++xx)if(abs(xx*xx+yy*yy-r*r)<=r)for(int sx:{-1,1})for(int sy:{-1,1})drawPixel(sx<0?x+r-xx:x+w-r-1+xx,sy<0?y+r-yy:y+h-r-1+yy,c);
    }
    void fillTriangle(int ax,int ay,int bx,int by,int cx,int cy,uint16_t c){
        auto edge=[](int x,int y,int a,int b,int d,int e){return (d-a)*(y-b)-(e-b)*(x-a);};
        if(edge(cx,cy,ax,ay,bx,by)==0){drawLine(ax,ay,bx,by,c);drawLine(bx,by,cx,cy,c);return;}
        for(int y=std::max(0,std::min({ay,by,cy}));y<=std::min(h_-1,std::max({ay,by,cy}));++y)
            for(int x=std::max(0,std::min({ax,bx,cx}));x<=std::min(w_-1,std::max({ax,bx,cx}));++x){int a=edge(x,y,ax,ay,bx,by),b=edge(x,y,bx,by,cx,cy),d=edge(x,y,cx,cy,ax,ay);if((a>=0&&b>=0&&d>=0)||(a<=0&&b<=0&&d<=0))drawPixel(x,y,c);}
    }
    void setTextDatum(int d){datum_=d;} void setTextSize(int s){size_=s;} void setTextColor(uint16_t c){ink_=c;}
    int drawString(const char* str,int x,int y){int w=int(strlen(str))*6*size_;if(datum_==middle_center)x-=w/2;else if(datum_==middle_right||datum_==top_right)x-=w;if(datum_!=top_left&&datum_!=top_right)y-=4*size_;
        for(int i=0;str[i];++i)for(int col=0;col<5;++col)for(int row=0;row<8;++row)if(simfont::font[uint8_t(str[i])*5+col]&(1<<row))fillRect(x+(i*6+col)*size_,y+row*size_,size_,size_,ink_);return w;}
    void pushImage(int x,int y,int w,int h,const uint8_t* p,int,const uint16_t* pal){for(int i=0;i<w*h;++i)drawPixel(x+i%w,y+i/w,pal[(i&1)?p[i/2]&15:p[i/2]>>4]);}
    void setPivot(int x,int y){pivotX_=x;pivotY_=y;}
    void pushRotateZoom(M5Canvas* dest,float x,float y,float angle,float zx,float zy,uint16_t key){
        float a=angle*.01745329252f,c=cosf(a),s=sinf(a);int extent=int(std::ceil((w_+h_)*std::max(zx,zy)));
        for(int yy=-extent;yy<=extent;++yy)for(int xx=-extent;xx<=extent;++xx){int sx=int(std::floor((c*xx+s*yy)/zx+pivotX_+.5f)),sy=int(std::floor((-s*xx+c*yy)/zy+pivotY_+.5f));if(sx>=0&&sx<w_&&sy>=0&&sy<h_){auto col=pixels_[sy*w_+sx];if(col!=key)dest->drawPixel(int(std::lround(x))+xx,int(std::lround(y))+yy,col);}}
    }
};
namespace lgfx { using LovyanGFX=M5Canvas; }
namespace m5 { struct IMU_Class { enum {sensor_mask_accel=1,sensor_mask_gyro=2}; }; }
struct SimButton {
    bool held=false,pressed=false;
    bool pending=false;
    void update(bool v){pressed=pending||(v&&!held);held=v;pending=false;}
    bool isPressed() const{return held;} bool wasPressed() const{return pressed;}
};
struct SimDisplay {
    int rotation=1,brightness=255;
    void setRotation(int r){rotation=r;} void fillScreen(uint16_t){}
    void setBrightness(int b){brightness=b;}
};
struct SimImu {
    struct Vec {float x=0,y=0,z=0;};
    struct Sample {Vec accel,gyro;uint32_t usec=0;} sample;
    bool portrait=false;
    float previousYaw=0;
    uint32_t previousUs=0;
    bool isEnabled() const{return true;}
#ifdef __ANDROID__
    int fresh=0;
    int update(){int mask=fresh;fresh=0;return mask;}
#else
    int update(){
        float t=sim::controls.tilt*.01745329252f,p=sim::controls.pitch*.01745329252f;
        sample.accel={cosf(t)*cosf(p),sinf(t)*cosf(p),sinf(p)};
        if(portrait)sample.accel={sinf(t)*cosf(p),cosf(t)*cosf(p),sinf(p)};
        // Pose controls describe angles, like a held physical device. Only an angle
        // change produces a gyro rate; holding an off-centre pose must stay still.
        const uint32_t now=micros();
        const float dt=(now-previousUs)*1e-6f;
        const float rate=previousUs && dt>0 ? (sim::controls.yaw-previousYaw)/dt : 0;
        previousYaw=sim::controls.yaw;previousUs=now;
        sample.gyro={-rate*sample.accel.x,-rate*sample.accel.y,-rate*sample.accel.z};
        if(sim::shakeAt){uint32_t age=millis()-sim::shakeAt;if(age<50||(age>=180&&age<230)){sample.accel.x*=5;sample.accel.y*=5;sample.accel.z*=5;}if(age>250)sim::shakeAt=0;}
        sample.usec=micros();return 3;
    }
#endif
    const Sample& getImuData() const{return sample;}
};
struct SimI2C {
    bool begin(){return true;}
    bool readRegister(uint8_t,uint8_t reg,uint8_t* out,size_t n,uint32_t){
        if(!sim::controls.connected)return false;
        if(reg==0x10&&n==2){out[0]=uint8_t(128+sim::controls.joyY*96/100);out[1]=uint8_t(128+sim::controls.joyX*96/100);return true;}
        if(reg==0x20&&n==1){out[0]=sim::controls.stick?0:1;return true;}return false;
    }
};
struct SimSpeaker {
    unsigned volume=140,channelVolume[3]={255,255,255};
    void setVolume(unsigned v){volume=v;} void stop(){sim::audioStop();}
    void tone(unsigned hz,unsigned ms,int,bool){sim::audioTone(hz,ms,volume);}
    void setChannelVolume(int ch,unsigned v){if(ch>=0&&ch<3)channelVolume[ch]=v;}
    void playRaw(const uint8_t* p,unsigned n,unsigned rate,bool,int,int ch,bool){sim::audioSample(p,n,rate,volume*(ch<3?channelVolume[ch]:255)/255,ch);}
};
struct SimPower {
    int level=100, usbMv=5000;
    void setExtOutput(bool){} int getBatteryLevel(){return level;} int getVBUSVoltage(){return usbMv;}
};
struct SimM5 {
    struct Config {bool pmic_button=false;};
    SimDisplay Display;SimButton BtnA,BtnB;SimImu Imu;SimI2C Ex_I2C;SimPower Power;SimSpeaker Speaker;
    Config config(){return {};}
    void begin(Config){}
    void update(){BtnA.update(sim::controls.a);BtnB.update(sim::controls.b);}
};
inline SimM5 M5;
