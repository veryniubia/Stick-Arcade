#include "net/radio.h"
#if !defined(__ANDROID__)
#include "net/net_espnow.h"
#ifdef ESP_PLATFORM
#include "net/phone_ap.h"

namespace radio {
namespace { bool phoneOn = false; }

bool phoneMode() { return phoneOn; }
bool setPhoneMode(bool on) {
    if (on == phoneOn) return true;
    if (on) {
        net_espnow::end();
        phoneOn = phone_ap::start();
    } else {
        phoneOn = false;
    }
    if (!phoneOn) phone_ap::stop(); // Also clean up a partially started AP/socket.
    return on == phoneOn;
}
void tick() { if (phoneOn) phone_ap::service(); }
bool beginGame() {
    if (!phoneOn) return net_espnow::begin();
    phone_ap::link().clearInbox();
    return true;
}
void endGame() { if (!phoneOn) net_espnow::end(); }
net::Transport& transport() { return phoneOn ? static_cast<net::Transport&>(phone_ap::link()) : net_espnow::transport(); }
bool poll(uint8_t id[6], uint8_t* data, size_t* len) {
    return phoneOn ? phone_ap::link().poll(id, data, len) : net_espnow::poll(id, data, len);
}
uint32_t overflowCount() { return phoneMode() ? 0 : net_espnow::overflowCount(); }
}
#else
namespace radio {
void tick() {}
bool beginGame() { return net_espnow::begin(); }
void endGame() { net_espnow::end(); }
net::Transport& transport() { return net_espnow::transport(); }
bool poll(uint8_t id[6], uint8_t* data, size_t* len) { return net_espnow::poll(id, data, len); }
uint32_t overflowCount() { return net_espnow::overflowCount(); }
}
#endif
#endif
