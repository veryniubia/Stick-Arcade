// POSIX/Android loopback integration. Run both shipping UDP endpoints with the
// real sessions and rules. A simulated clock keeps disconnect tests deterministic.
#include <cassert>
#include <initializer_list>
#include <cstdio>
#include "net/phone_socket.h"
#include "games/throwhalla/throwhalla.h"
#include "games/bumperduel/bumperduel.h"
#include "games/samuraiduel/samuraiduel.h"
#include "games/middiff/middiff.h"

void ThrowhallaGame::draw(int,const arcade::DrawCtx&) {}
void ThrowhallaGame::sfx(uint32_t) {}
void BumperDuelGame::draw(int,const arcade::DrawCtx&) {}
void BumperDuelGame::sfx(uint32_t) {}
void SamuraiDuelGame::draw(int,const arcade::DrawCtx&) {}
void SamuraiDuelGame::sfx(uint32_t) {}
void MidDiffGame::draw(int,const arcade::DrawCtx&) {}
void MidDiffGame::sfx(uint32_t) {}

struct Station {
    phone::Socket udp;
    net::Session session;
    arcade::GameInstance game;
    sockaddr_in addr;
    Station(const char* ip,bool ap) : addr(phone::address(ip)) {
        udp.fd=socket(AF_INET,SOCK_DGRAM,0);
        assert(bind(udp.fd,reinterpret_cast<sockaddr*>(&addr),sizeof(addr))==0);
        udp.reset(ap,phone::endpoint(phone::address("127.0.0.1")));
    }
    ~Station() { udp.stop(); arcade::gameDestroy(game); }
    void begin(const arcade::GameInfo& info,uint32_t nonce,uint32_t now) {
        arcade::gameDestroy(game);
        game=arcade::gameCreate(info);
        udp.clearInbox();
        session.begin(&udp,nonce,now,info,game.game);
    }
    void frame(uint32_t now) {
        udp.service(now);
        uint8_t id[6], bytes[250];
        size_t n;
        while (udp.poll(id,bytes,&n)) session.onReceive(id,bytes,n,now);
        session.update(now,{});
    }
    void raw(Station& to,const uint8_t* bytes,size_t len) {
        assert(sendto(udp.fd,bytes,len,0,reinterpret_cast<sockaddr*>(&to.addr),sizeof(addr))==ssize_t(len));
    }
};

int main() {
    Station ap("127.0.0.1",true), mobile("127.0.0.2",false), stranger("127.0.0.3",false);
    mobile.udp.service(0); ap.udp.service(1); mobile.udp.service(2);
    assert(ap.udp.connected(2) && mobile.udp.connected(2));
    uint8_t data[320]={0xd7}, received[250], id[6];
    size_t size;
    mobile.raw(ap,data,0);
    mobile.raw(ap,data,sizeof(data));
    stranger.raw(ap,data,250);
    mobile.raw(ap,data,250);
    ap.udp.service(3);
    assert(ap.udp.poll(id,received,&size) && size==250 && !memcmp(data,received,250));
    assert(!ap.udp.poll(id,received,&size));

    uint32_t now=10;
    for (const arcade::GameInfo* info : {&kThrowhallaInfo,&kBumperDuelInfo,&kSamuraiDuelInfo,&kMidDiffInfo}) {
        for (bool phoneHosts : {false,true}) {
            ap.begin(*info,phoneHosts?1:2,now);
            mobile.begin(*info,phoneHosts?2:1,now);
            auto run=[&](int frames) {
                for (int i=0;i<frames;++i,now+=16) { ap.frame(now); mobile.frame(now); }
            };
            run(400);
            assert(ap.session.status()==net::Status::Playing && mobile.session.status()==net::Status::Playing);
            assert(mobile.session.role()==(phoneHosts?net::Role::Host:net::Role::Client));
            assert(ap.session.rttMs()>=0 && mobile.session.rttMs()>=0);
            now+=750;
            // Drain a previously queued heartbeat before simulating a missed link.
            ap.frame(now);
            ap.frame(now+=750);
            assert(ap.session.status()==net::Status::LinkLost);
            run(50);
            assert(ap.session.status()==net::Status::Playing);
            mobile.session.leave();
            run(4);
            assert(ap.session.status()==net::Status::PeerLeft);
            assert(ap.udp.connected(now));
        }
    }
    now+=3000;
    ap.udp.service(now);
    assert(!ap.udp.connected(now));
    stranger.udp.service(now); ap.udp.service(now); stranger.udp.service(now);
    assert(stranger.udp.connected(now));
    puts("PASS UDP datagram bounds, peer filter, AP idle/rejoin, four games x both host roles, RTT, loss, recovery and leave");
}
