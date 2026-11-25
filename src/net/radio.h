#pragma once
#include "net/net_session.h"

namespace radio {
// tick services the link even when no game owns a session.
void tick();
bool beginGame();
void endGame();
net::Transport& transport();
bool poll(uint8_t id[6], uint8_t* data, size_t* len);
uint32_t overflowCount();

#ifdef ESP_PLATFORM
bool phoneMode();
bool setPhoneMode(bool on);
const char* ssid();
const char* password();
const char* phoneStatus();
#endif
}
