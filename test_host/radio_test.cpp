// Compile the firmware lifecycle against two recording backends; the AP and
// ESP-NOW hardware bindings are the only pieces replaced by this test.
#define ESP_PLATFORM 1
#include "net/arcade_radio.cpp"
#include <cassert>
#include <cstdio>

namespace {
class RecordingLink : public phone::Link {
    int read(phone::Endpoint&,uint8_t*,size_t) override { return -1; }
    void write(const phone::Endpoint&,const uint8_t*,size_t) override {}
};
RecordingLink esp,udp;
bool espOn=false,apOn=false,failStart=false;
int apStarts=0, apStops=0, ticks=0;
}
namespace net_espnow {
bool begin() { assert(!apOn); return espOn=true; }
void end() { espOn=false; }
net::Transport& transport() { return esp; }
bool poll(uint8_t*,uint8_t*,size_t*) { return false; }
uint32_t overflowCount() { return 0; }
}
namespace phone_ap {
bool start() { assert(!espOn); ++apStarts; apOn=true; return !failStart; }
void stop() { ++apStops; apOn=false; }
void service() { assert(apOn); ++ticks; }
phone::Link& link() { return udp; }
}
int main() {
    assert(!radio::phoneMode());
    radio::tick(); assert(ticks==0);
    assert(radio::beginGame() && espOn && &radio::transport()==&esp);
    assert(radio::setPhoneMode(true) && !espOn && apOn);
    assert(radio::setPhoneMode(true) && apStarts==1);
    radio::tick(); // launcher
    assert(radio::beginGame() && &radio::transport()==&udp);
    radio::endGame(); radio::tick();
    assert(!espOn && apOn && ticks==2 && apStops==0);
    assert(radio::beginGame());
    radio::endGame();
    assert(radio::setPhoneMode(false) && !apOn && !espOn);
    radio::tick(); assert(ticks==2);
    assert(radio::beginGame() && espOn);
    radio::endGame(); assert(!espOn);
    failStart=true;
    assert(!radio::setPhoneMode(true) && !radio::phoneMode() && !apOn);
    assert(radio::beginGame() && espOn);
    puts("PASS boot default, exclusive radios, launcher/game lifetime, AP failure cleanup and ESP-NOW restore");
}
