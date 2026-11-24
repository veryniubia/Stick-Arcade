// ESP-NOW packet transport replacement. Two local virtual devices, unchanged session protocol.
#include <winsock2.h>
#include "net/net_espnow.h"
#include "sim.h"
#include <cstring>
#include <stdexcept>
namespace net_espnow {
namespace {
SOCKET sock=INVALID_SOCKET;
Stats counters{};
class LocalTransport:public net::Transport {
public:
    void send(const uint8_t* mac,const uint8_t* data,size_t len) override {
        if(sock==INVALID_SOCKET||len>250)return;
        int peer=sim::device==1?2:1;
        if(mac && mac[5]!=peer)return;
        sockaddr_in dst{};dst.sin_family=AF_INET;dst.sin_addr.s_addr=htonl(INADDR_LOOPBACK);dst.sin_port=htons(39170+peer);
        uint8_t packet[256]={2,0,0,0,0,uint8_t(sim::device)};memcpy(packet+6,data,len);
        int n=sendto(sock,reinterpret_cast<const char*>(packet),int(len+6),0,reinterpret_cast<sockaddr*>(&dst),sizeof(dst));
        ++counters.txPackets;counters.txBytes+=uint32_t(len);if(n>=0)++counters.sendOk;else ++counters.sendFail;
    }
} tx;
}
bool begin(){
    if(active())return true;
    static bool ready=[](){WSADATA w{};return WSAStartup(MAKEWORD(2,2),&w)==0;}();if(!ready)return false;
    sock=socket(AF_INET,SOCK_DGRAM,IPPROTO_UDP);if(sock==INVALID_SOCKET)return false;
    sockaddr_in addr{};addr.sin_family=AF_INET;addr.sin_addr.s_addr=htonl(INADDR_LOOPBACK);addr.sin_port=htons(39170+sim::device);
    if(bind(sock,reinterpret_cast<sockaddr*>(&addr),sizeof(addr))!=0){end();return false;}
    u_long nonblock=1;ioctlsocket(sock,FIONBIO,&nonblock);counters={};return true;
}
void end(){if(sock!=INVALID_SOCKET)closesocket(sock);sock=INVALID_SOCKET;}
bool active(){return sock!=INVALID_SOCKET;}
net::Transport& transport(){return tx;}
bool poll(uint8_t mac[6],uint8_t* data,size_t* len){
    if(!active())return false;char packet[256];int n=recv(sock,packet,sizeof(packet),0);if(n<=6)return false;
    memcpy(mac,packet,6);*len=n-6;memcpy(data,packet+6,*len);++counters.rxPackets;counters.rxBytes+=uint32_t(*len);return true;
}
uint32_t overflowCount(){return 0;} Stats stats(){return counters;}
}
