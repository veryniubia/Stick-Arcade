#pragma once
// Minimal host raster canvas for previewing the actual MoonDuel view without hardware.
// Deliberately isolated on the preview compiler's include path; never used in firmware.
#define MIDDIFF_HOST_CANVAS 1
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>

class M5Canvas {
    std::array<uint16_t,240*135> pixels_{};
    int w_=240,h_=135;
    int left_=0,top_=0,right_=240,bottom_=135;
public:
    // The pixel budget is fixed, so a portrait 135x240 canvas fits as well.
    void setSize(int w,int h) { w_=w;h_=h;pixels_.fill(0);clearClipRect(); }
    int width() const { return w_; }
    int height() const { return h_; }
    void* getBuffer() { return pixels_.data(); }
    void fillScreen(uint16_t col) { pixels_.fill(col); }
    void setClipRect(int x,int y,int w,int h) { left_=x;top_=y;right_=x+w;bottom_=y+h; }
    void clearClipRect() {left_=top_=0;right_=w_;bottom_=h_;}
    void drawPixel(int x,int y,uint16_t col) {
        if(x>=std::max(0,left_) && x<std::min(w_,right_) && y>=std::max(0,top_) && y<std::min(h_,bottom_)) pixels_[y*w_+x]=col;
    }
    void fillRect(int x,int y,int w,int h,uint16_t col) {
        for(int yy=y;yy<y+h;++yy)for(int xx=x;xx<x+w;++xx)drawPixel(xx,yy,col);
    }
    void fillRoundRect(int x,int y,int w,int h,int,uint16_t col) { fillRect(x,y,w,h,col); }
    void fillEllipse(int cx,int cy,int rx,int ry,uint16_t col) {
        for(int yy=-ry;yy<=ry;++yy)for(int xx=-rx;xx<=rx;++xx)
            if(rx>0 && ry>0 && float(xx*xx)/(rx*rx)+float(yy*yy)/(ry*ry)<=1.f) drawPixel(cx+xx,cy+yy,col);
    }
    void drawFastHLine(int x,int y,int w,uint16_t col){fillRect(x,y,w,1,col);}
    void drawFastVLine(int x,int y,int h,uint16_t col){fillRect(x,y,1,h,col);}
    void fillCircle(int cx,int cy,int r,uint16_t col){
        for(int yy=-r;yy<=r;++yy)for(int xx=-r;xx<=r;++xx)if(xx*xx+yy*yy<=r*r)drawPixel(cx+xx,cy+yy,col);
    }
    void drawRect(int x,int y,int w,int h,uint16_t col){
        drawFastHLine(x,y,w,col);drawFastHLine(x,y+h-1,w,col);
        drawFastVLine(x,y,h,col);drawFastVLine(x+w-1,y,h,col);
    }
    void drawLine(int x,int y,int endX,int endY,uint16_t col) {
        int dx=abs(endX-x),dy=-abs(endY-y),sx=x<endX ? 1:-1,sy=y<endY ? 1:-1,error=dx+dy;
        for(;;){drawPixel(x,y,col);if(x==endX && y==endY)break;int e=2*error;if(e>=dy){error+=dy;x+=sx;}if(e<=dx){error+=dx;y+=sy;}}
    }
    void fillTriangle(int ax, int ay, int bx, int by, int cx, int cy, uint16_t col) {
        auto side = [](int x, int y, int x1, int y1, int x2, int y2) {
            return (x2-x1)*(y-y1) - (y2-y1)*(x-x1);
        };
        for (int y = std::min({ay,by,cy}); y <= std::max({ay,by,cy}); ++y) {
            for (int x = std::min({ax,bx,cx}); x <= std::max({ax,bx,cx}); ++x) {
                const int a = side(x,y,ax,ay,bx,by), b = side(x,y,bx,by,cx,cy), c = side(x,y,cx,cy,ax,ay);
                if ((a >= 0 && b >= 0 && c >= 0) || (a <= 0 && b <= 0 && c <= 0)) drawPixel(x,y,col);
            }
        }
    }
    void save(const char* file) {
        FILE* f=fopen(file,"wb");if(!f)return;
        fprintf(f,"P6\n%d %d\n255\n",w_,h_);
        for(int i=0;i<w_*h_;++i) {
            const uint16_t col=pixels_[i];
            uint8_t r=((col>>11)&31)*255/31,g=((col>>5)&63)*255/63,b=(col&31)*255/31;
            fputc(r,f);fputc(g,f);fputc(b,f);
        }
        fclose(f);
    }
};
