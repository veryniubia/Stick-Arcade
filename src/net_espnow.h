#pragma once

#include <stddef.h>
#include <stdint.h>

#include "net_session.h"

// ESP-NOW radio glue for net::Session. Receive callbacks run on the Wi-Fi task, so packets are queued
// and handed to the main loop through poll().
namespace net_espnow {

constexpr uint8_t kChannel = 1;

bool begin();  // turns Wi-Fi + ESP-NOW on; false if the radio could not be started
void end();    // turns the radio off again (saves power, frees the Wi-Fi task)
bool active();

net::Transport& transport();

// Pops one received packet. `data` must hold at least 250 bytes.
bool poll(uint8_t mac[6], uint8_t* data, size_t* len);
uint32_t overflowCount();

// Radio counters since begin(); `sendFail` counts unicast frames that never got a MAC-level ACK.
struct Stats {
    uint32_t txPackets, txBytes, rxPackets, rxBytes, sendOk, sendFail;
};
Stats stats();

}  // namespace net_espnow
