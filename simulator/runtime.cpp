#include <windows.h>
#include <commctrl.h>
#include <mmsystem.h>
#include "sim.h"
#include "core/display.h"
#include "core/input.h"
#include "core/joystick.h"
#include "core/offline_trace.h"
#include "core/settings_data.h"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <iostream>
#include <map>
#include <memory>
#include <random>
#include <vector>
void setup();void loop();
uint32_t millis(){return uint32_t(sim::timeUs/1000);}
uint32_t micros(){return uint32_t(sim::timeUs);}
void delay(uint32_t ms){sim::timeUs+=uint64_t(ms)*1000;}
uint32_t esp_random(){static std::mt19937 rng(sim::seed?sim::seed:std::random_device{}());return rng();}
namespace offline_trace {
namespace {std::ofstream log;}
void begin(){log.open(std::filesystem::path(sim::outputDir)/("events-"+std::to_string(sim::device)+".csv"));log<<"ms,event,value,detail\n";}
void poll(){}
void record(const char* event,int value,int detail){log<<millis()<<','<<event<<','<<value<<','<<detail<<'\n';log.flush();}
}
namespace {
HWND window=nullptr,tiltSlider=nullptr,pitchSlider=nullptr,yawSlider=nullptr,stickCheck=nullptr,statusLabel=nullptr;
bool running=true,paused=false,singleStep=false,reboot=false;
bool keys[256]{};
bool keyPresses[256]{}; // Preserve a quick tap even if down/up arrive between two frames.
uint32_t pulseA=0,pulseB=0;
int lastMouseX=0,lastMouseY=0;
bool dragging=false;
struct Wave {WAVEHDR header{};std::vector<uint8_t> bytes;};
HWAVEOUT audio[3]{};
std::vector<std::unique_ptr<Wave>> waves[3];
void cleanAudio(){for(int c=0;c<3;++c)for(auto it=waves[c].begin();it!=waves[c].end();){if((*it)->header.dwFlags&WHDR_DONE){waveOutUnprepareHeader(audio[c],&(*it)->header,sizeof(WAVEHDR));it=waves[c].erase(it);}else ++it;}}
void playWave(const uint8_t* data,unsigned n,unsigned rate,unsigned volume,int channel){
    if(sim::headless||!volume||!n)return;channel=std::clamp(channel,0,2);cleanAudio();
    if(!audio[channel]){WAVEFORMATEX fmt{};fmt.wFormatTag=WAVE_FORMAT_PCM;fmt.nChannels=1;fmt.nSamplesPerSec=22050;fmt.wBitsPerSample=8;fmt.nBlockAlign=1;fmt.nAvgBytesPerSec=22050;if(waveOutOpen(&audio[channel],WAVE_MAPPER,&fmt,0,0,CALLBACK_NULL)!=MMSYSERR_NOERROR){audio[channel]=nullptr;return;}}
    // Bounded buffers; a new sample on the same channel interrupts its predecessor.
    waveOutReset(audio[channel]);cleanAudio();
    auto w=std::make_unique<Wave>();size_t count=uint64_t(n)*22050/std::max(1u,rate);w->bytes.resize(count);
    for(size_t i=0;i<count;++i)w->bytes[i]=uint8_t(128+(int(data[std::min<size_t>(n-1,i*rate/22050)])-128)*int(volume)/255);
    w->header.lpData=reinterpret_cast<char*>(w->bytes.data());w->header.dwBufferLength=DWORD(count);
    waveOutPrepareHeader(audio[channel],&w->header,sizeof(WAVEHDR));waveOutWrite(audio[channel],&w->header,sizeof(WAVEHDR));waves[channel].push_back(std::move(w));
}
// The hardware backlight's PWM values are not desktop pixel intensities.
// Keep every saved brightness level useful on a monitor, with full colour at maximum.
int desktopBrightness(){
    constexpr int levels[settings::kBrightLevels]={128,166,204,240,255};
    const int value=M5.Display.brightness;
    if(value<=0)return 0;
    for(int i=0;i<settings::kBrightLevels;++i)
        if(value<=settings::kBrightValue[i])return levels[i];
    return 255;
}
RECT viewport(){RECT rect{};GetClientRect(window,&rect);rect.right=std::min<LONG>(740,rect.right);return rect;}
void paint(){
    PAINTSTRUCT ps;HDC dc=BeginPaint(window,&ps);
    RECT area=viewport();
    // Paint the viewport off-screen, then copy it in one operation. Never erase under controls.
    HDC buffer=CreateCompatibleDC(dc);
    HBITMAP bitmap=CreateCompatibleBitmap(dc,area.right,area.bottom);
    HGDIOBJ oldBitmap=SelectObject(buffer,bitmap);
    FillRect(buffer,&area,static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
    int w=display::width(),h=display::height();std::vector<uint32_t> frame(w*h);
    int bright=desktopBrightness();
    const auto& pixels=display::canvas().pixels();
    for(int i=0;i<w*h;++i){auto c=pixels[i];int r=((c>>11)&31)*255/31*bright/255,g=((c>>5)&63)*255/63*bright/255,b=(c&31)*255/31*bright/255;frame[i]=uint32_t((r<<16)|(g<<8)|b);}
    BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);info.bmiHeader.biWidth=w;info.bmiHeader.biHeight=-h;info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;info.bmiHeader.biCompression=BI_RGB;
    int scale=std::max(1,std::min((int(area.right)-20)/w,(int(area.bottom)-40)/h));
    int x=std::max(0,(int(area.right)-w*scale)/2),y=std::max(0,(int(area.bottom)-h*scale)/2);
    SetStretchBltMode(buffer,COLORONCOLOR);StretchDIBits(buffer,x,y,w*scale,h*scale,0,0,w,h,frame.data(),&info,DIB_RGB_COLORS,SRCCOPY);
    BitBlt(dc,0,0,area.right,area.bottom,buffer,0,0,SRCCOPY);
    SelectObject(buffer,oldBitmap);DeleteObject(bitmap);DeleteDC(buffer);
    EndPaint(window,&ps);
}
void recenter(){SendMessage(tiltSlider,TBM_SETPOS,TRUE,180);SendMessage(pitchSlider,TBM_SETPOS,TRUE,90);SendMessage(yawSlider,TBM_SETPOS,TRUE,90);sim::controls.tilt=sim::controls.pitch=sim::controls.yaw=0;}
LRESULT CALLBACK proc(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp){
    switch(msg){
    case WM_DESTROY:running=false;PostQuitMessage(0);return 0;
    case WM_PAINT:paint();return 0;
    case WM_ERASEBKGND:return 1;
    case WM_KILLFOCUS:std::fill(std::begin(keys),std::end(keys),false);std::fill(std::begin(keyPresses),std::end(keyPresses),false);dragging=false;return 0;
    case WM_KEYDOWN:case WM_KEYUP:{bool down=msg==WM_KEYDOWN;bool edge=down&&!(lp&(1LL<<30));if(wp<256){keys[wp]=down;if(edge)keyPresses[wp]=true;}
        if(edge){if(wp=='P')paused=!paused;if(wp==VK_F10){paused=true;singleStep=true;}if(wp==VK_ESCAPE)sim::shakeAt=millis();if(wp=='R')recenter();if(wp==VK_F5)reboot=true;if(wp==VK_F12)sim::saveFrame((std::filesystem::path(sim::outputDir)/"screenshot.ppm").string());}return 0;}
    case WM_RBUTTONDOWN:dragging=true;lastMouseX=short(LOWORD(lp));lastMouseY=short(HIWORD(lp));SetCapture(hwnd);return 0;
    case WM_RBUTTONUP:dragging=false;ReleaseCapture();return 0;
    case WM_MOUSEMOVE:if(dragging){int x=short(LOWORD(lp)),y=short(HIWORD(lp));int tilt=int(SendMessage(tiltSlider,TBM_GETPOS,0,0))+(x-lastMouseX)/2;int yaw=int(SendMessage(yawSlider,TBM_GETPOS,0,0))+(x-lastMouseX)/2;int pitch=int(SendMessage(pitchSlider,TBM_GETPOS,0,0))+(y-lastMouseY)/2;if(keys[VK_SHIFT])SendMessage(tiltSlider,TBM_SETPOS,TRUE,std::clamp(tilt,0,360));SendMessage(pitchSlider,TBM_SETPOS,TRUE,std::clamp(pitch,10,170));if(!keys[VK_SHIFT])SendMessage(yawSlider,TBM_SETPOS,TRUE,std::clamp(yaw,0,180));lastMouseX=x;lastMouseY=y;}return 0;
    case WM_COMMAND:{switch(LOWORD(wp)){case 1:pulseA=millis()+100;break;case 2:pulseB=millis()+100;break;case 3:sim::shakeAt=millis();break;case 4:paused=!paused;break;case 5:paused=true;singleStep=true;break;case 6:reboot=true;break;case 7:recenter();break;}SetFocus(hwnd);return 0;}
    case WM_HSCROLL:SetFocus(hwnd);return 0;
    }return DefWindowProc(hwnd,msg,wp,lp);
}
HWND control(const char* cls,const char* label,int x,int y,int w,int h,int id,DWORD style=0){return CreateWindowA(cls,label,WS_CHILD|WS_VISIBLE|style,x,y,w,h,window,reinterpret_cast<HMENU>(INT_PTR(id)),GetModuleHandle(nullptr),nullptr);}
HWND slider(const char* label,int y,int max,int initial){control("STATIC",label,760,y,295,20,0);auto s=control(TRACKBAR_CLASSA,"",760,y+22,295,30,0,TBS_HORZ);SendMessage(s,TBM_SETRANGE,TRUE,MAKELONG(0,max));SendMessage(s,TBM_SETPOS,TRUE,initial);return s;}
void createWindow(){
    InitCommonControls();WNDCLASSA wc{};wc.lpfnWndProc=proc;wc.hInstance=GetModuleHandle(nullptr);wc.lpszClassName="StickS3Simulator";wc.hCursor=LoadCursor(nullptr,IDC_ARROW);wc.hbrBackground=static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));RegisterClassA(&wc);
    std::string title="StickS3 Simulator - Device "+std::to_string(sim::device);
    window=CreateWindowA(wc.lpszClassName,title.c_str(),WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU|WS_MINIMIZEBOX|WS_CLIPCHILDREN,sim::device==1?60:220,40,1100,820,nullptr,nullptr,wc.hInstance,nullptr);
    control("STATIC","StickS3 virtual device",760,18,295,24,0);
    control("BUTTON","A  [Z / Enter]",760,50,140,32,1);control("BUTTON","B  [X / Backspace]",910,50,145,32,2);
    tiltSlider=slider("Tilt / portrait roll (-180..180 deg)",100,360,180);
    pitchSlider=slider("Device pitch (-90..90 deg)",165,180,90);
    yawSlider=slider("Device yaw (-90..90 deg; hold to aim)",230,180,90);
    stickCheck=control("BUTTON","Joystick2 connected",760,300,295,26,8,BS_AUTOCHECKBOX);SendMessage(stickCheck,BM_SETCHECK,BST_CHECKED,0);
    control("BUTTON","Shake twice [Esc]",760,340,295,30,3);
    control("BUTTON","Pause / run [P]",760,380,140,30,4);control("BUTTON","Step [F10]",910,380,145,30,5);
    control("BUTTON","Reboot [F5]",760,420,140,30,6);control("BUTTON","Level pose [R]",910,420,145,30,7);
    control("STATIC","Arrows: tilt / pitch\r\nQ / E: hold device yaw -15 / +15\r\nWASD: joystick  |  Space: stick press\r\nRight-drag: yaw / pitch; Shift: tilt\r\nF12: save screenshot.ppm\r\n\r\nA: select / attack  |  B: next / solo\r\nMidDiff: tap B, release, hold still.\r\nAfter calibration, B starts solo.\r\n\r\nLocal link: open devices 1 and 2,\r\nselect the same game and wait.",760,470,295,210,0);
    statusLabel=control("STATIC","",760,690,295,80,0);
    ShowWindow(window,SW_SHOW);UpdateWindow(window);SetFocus(window);
}
void readControls(){
    auto& c=sim::controls;c.a=keys['Z']||keys[VK_RETURN]||keyPresses['Z']||keyPresses[VK_RETURN]||millis()<pulseA;c.b=keys['X']||keys[VK_BACK]||keyPresses['X']||keyPresses[VK_BACK]||millis()<pulseB;
    c.tilt=float(SendMessage(tiltSlider,TBM_GETPOS,0,0))-180;c.pitch=float(SendMessage(pitchSlider,TBM_GETPOS,0,0))-90;c.yaw=float(SendMessage(yawSlider,TBM_GETPOS,0,0))-90;
    if(keys[VK_LEFT]||keys[VK_RIGHT])c.tilt=(int(keys[VK_RIGHT])-int(keys[VK_LEFT]))*30.f;
    if(keys[VK_UP]||keys[VK_DOWN])c.pitch=(int(keys[VK_DOWN])-int(keys[VK_UP]))*20.f;
    if(keys['Q']||keys['E'])c.yaw=(int(keys['E'])-int(keys['Q']))*15.f;
    c.joyX=(int(keys['D'])-int(keys['A']))*100;c.joyY=(int(keys['W'])-int(keys['S']))*100;c.stick=keys[VK_SPACE]||keyPresses[VK_SPACE];c.connected=SendMessage(stickCheck,BM_GETCHECK,0,0)==BST_CHECKED;
    static ULONGLONG nextStatus=0;
    static std::string lastStatus;
    const auto now=GetTickCount64();
    if(now>=nextStatus){
        nextStatus=now+100;
        char info[300];snprintf(info,sizeof(info),"%s  t=%.2fs  A=%d B=%d\r\nTilt=%.1f Pitch=%.1f  Joy=%d,%d\r\nIMU look=%.1f,%.1f  Stick=%s",paused?"PAUSED":"RUNNING",millis()/1000.,c.a,c.b,c.tilt,c.pitch,joystick::x(),joystick::y(),input::lookYawDeg(),input::lookPitchDeg(),joystick::ok()?"READY":"OFF");
        if(lastStatus!=info){SetWindowTextA(statusLabel,info);lastStatus=info;}
    }
}
struct Action {std::string key,value;};
std::map<int,std::vector<Action>> loadScript(const std::string& path){std::map<int,std::vector<Action>> out;if(path.empty())return out;std::ifstream in(path);if(!in)throw std::runtime_error("Cannot open script: "+path);std::string line;while(std::getline(in,line)){if(line.empty()||line[0]=='#')continue;std::istringstream s(line);int frame;Action a;if(!(s>>frame>>a.key>>a.value))throw std::runtime_error("Invalid script line: "+line);out[frame].push_back(a);}return out;}
void action(const Action& a){float v=a.key=="shot"?0:std::stof(a.value);auto& c=sim::controls;
    if(a.key=="a")c.a=v!=0;else if(a.key=="b")c.b=v!=0;else if(a.key=="tilt")c.tilt=v;else if(a.key=="pitch")c.pitch=v;else if(a.key=="yaw")c.yaw=v;else if(a.key=="joyX")c.joyX=int(v);else if(a.key=="joyY")c.joyY=int(v);else if(a.key=="stick")c.stick=v!=0;else if(a.key=="connected")c.connected=v!=0;else if(a.key=="shake")sim::shakeAt=millis();else if(a.key=="shot")sim::saveFrame((std::filesystem::path(sim::outputDir)/a.value).string());else throw std::runtime_error("Unknown script action: "+a.key);
}
}
namespace sim {
void present(){if(window){RECT area=viewport();InvalidateRect(window,&area,FALSE);}}
void audioTone(unsigned hz,unsigned ms,unsigned volume){if(headless||!hz)return;unsigned n=22050*ms/1000;std::vector<uint8_t> pcm(n);for(unsigned i=0;i<n;++i)pcm[i]=uint8_t(128+48*std::sin(i*6.283185307*hz/22050));playWave(pcm.data(),n,22050,volume,0);}
void audioSample(const uint8_t* p,unsigned n,unsigned rate,unsigned volume,int ch){playWave(p,n,rate,volume,ch);}
void audioStop(){for(auto h:audio)if(h)waveOutReset(h);cleanAudio();}
}
int main(int argc,char** argv){try{
    SetProcessDPIAware();int frames=-1;std::string script;bool realtime=false;
    char exe[MAX_PATH];GetModuleFileNameA(nullptr,exe,MAX_PATH);sim::outputDir=std::filesystem::path(exe).parent_path().string();
    for(int i=1;i<argc;++i){std::string arg=argv[i];if(arg=="--headless")sim::headless=true;else if(arg=="--realtime")realtime=true;else if(arg=="--device"&&i+1<argc)sim::device=std::stoi(argv[++i]);else if(arg=="--seed"&&i+1<argc)sim::seed=uint32_t(std::stoul(argv[++i]));else if(arg=="--frames"&&i+1<argc)frames=std::stoi(argv[++i]);else if(arg=="--script"&&i+1<argc)script=argv[++i];else if(arg=="--output"&&i+1<argc)sim::outputDir=argv[++i];else throw std::runtime_error("Usage: sticks3-simulator [--device 1|2] [--seed N] [--headless --frames N --script file --output dir --realtime]");}
    if(sim::headless&&frames<0)frames=600;
    if(sim::device<1||sim::device>2)throw std::runtime_error("Device must be 1 or 2");
    std::filesystem::create_directories(sim::outputDir);std::string prefs=(std::filesystem::path(sim::outputDir)/("settings-"+std::to_string(sim::device)+".bin")).string();_putenv_s("STICKS3_SETTINGS",prefs.c_str());
    auto actions=loadScript(script);setup();if(!sim::headless)createWindow();
    std::ofstream trace;if(sim::headless){trace.open(std::filesystem::path(sim::outputDir)/"input-trace.csv");trace<<"frame,ms,a,b,aCount,bCount,tilt,driveTilt,yaw,pitch,joyX,joyY,stickCount,width,height\n";}
    auto nextTick=std::chrono::steady_clock::now();
    for(int frame=0;running&&(frames<0||frame<frames);){uint64_t before=sim::timeUs;
        if(window){MSG msg;while(PeekMessage(&msg,nullptr,0,0,PM_REMOVE)){TranslateMessage(&msg);DispatchMessage(&msg);}readControls();}
        if(reboot){std::string cmd="\""+std::string(exe)+"\" --device "+std::to_string(sim::device)+" --output \""+sim::outputDir+"\"";STARTUPINFOA si{};si.cb=sizeof(si);PROCESS_INFORMATION pi{};if(CreateProcessA(nullptr,cmd.data(),nullptr,nullptr,FALSE,0,nullptr,nullptr,&si,&pi)){CloseHandle(pi.hThread);CloseHandle(pi.hProcess);break;}throw std::runtime_error("Reboot failed");}
        if(!running)break;
        if((!sim::headless||realtime)&&!singleStep){
            auto now=std::chrono::steady_clock::now();
            if(now<nextTick){auto wait=std::chrono::duration_cast<std::chrono::milliseconds>(nextTick-now).count();MsgWaitForMultipleObjects(0,nullptr,FALSE,DWORD(std::max<int64_t>(1,wait)),QS_ALLINPUT);continue;}
        }
        auto tickStart=std::chrono::steady_clock::now();
        if(!paused||singleStep){for(const auto& a:actions[frame])action(a);loop();singleStep=false;std::fill(std::begin(keyPresses),std::end(keyPresses),false);
            if(trace)trace<<frame<<','<<millis()<<','<<input::aHeld()<<','<<input::bHeld()<<','<<int(input::aCount())<<','<<int(input::bCount())<<','<<input::tiltDeg()<<','<<input::driveTiltDeg()<<','<<input::lookYawDeg()<<','<<input::lookPitchDeg()<<','<<int(joystick::x())<<','<<int(joystick::y())<<','<<int(joystick::pressCount())<<','<<display::width()<<','<<display::height()<<'\n';++frame;
        }
        cleanAudio();nextTick=tickStart+std::chrono::microseconds(paused?16000:sim::timeUs-before);
    }
    if(sim::headless)sim::saveFrame((std::filesystem::path(sim::outputDir)/"final.ppm").string());
    sim::audioStop();for(auto h:audio)if(h)waveOutClose(h);return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
