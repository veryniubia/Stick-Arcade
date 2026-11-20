#include "net_espnow.h"

#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <string.h>

namespace net_espnow {

namespace {

constexpr int kQueueSize = 24;
constexpr int kMaxPeers = 4;
const uint8_t kBroadcast[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

struct Rx {
    uint8_t mac[6];
    uint8_t len;
    uint8_t data[250];
};

Rx g_queue[kQueueSize];
volatile int g_head = 0;
volatile int g_tail = 0;
volatile uint32_t g_overflow = 0;
volatile uint32_t g_rxPackets = 0;
volatile uint32_t g_rxBytes = 0;
volatile uint32_t g_sendOk = 0;
volatile uint32_t g_sendFail = 0;
uint32_t g_txPackets = 0;
uint32_t g_txBytes = 0;
portMUX_TYPE g_lock = portMUX_INITIALIZER_UNLOCKED;
bool g_active = false;
uint8_t g_peers[kMaxPeers][6];
int g_peerCount = 0;

void onSent(const uint8_t* mac, esp_now_send_status_t status) {
    (void)mac;
    if (status == ESP_NOW_SEND_SUCCESS) ++g_sendOk;
    else ++g_sendFail;
}

void onRecv(const uint8_t* mac, const uint8_t* data, int len) {
    if (len <= 0 || len > 250) return;
    ++g_rxPackets;
    g_rxBytes += len;
    portENTER_CRITICAL(&g_lock);
    const int next = (g_head + 1) % kQueueSize;
    if (next == g_tail) {
        ++g_overflow;
    } else {
        Rx& r = g_queue[g_head];
        memcpy(r.mac, mac, 6);
        r.len = static_cast<uint8_t>(len);
        memcpy(r.data, data, len);
        g_head = next;
    }
    portEXIT_CRITICAL(&g_lock);
}

bool addPeer(const uint8_t* mac) {
    if (esp_now_is_peer_exist(mac)) return true;
    esp_now_peer_info_t info = {};
    memcpy(info.peer_addr, mac, 6);
    info.channel = 0;  // follow the current channel
    info.ifidx = WIFI_IF_STA;
    info.encrypt = false;
    return esp_now_add_peer(&info) == ESP_OK;
}

class EspNowTransport : public net::Transport {
public:
    void send(const uint8_t* mac, const uint8_t* data, size_t len) override {
        if (!g_active) return;
        const uint8_t* dst = mac ? mac : kBroadcast;
        if (mac && !esp_now_is_peer_exist(mac)) {
            // Only a few peers are ever needed (the paired device); recycle the oldest slot.
            if (g_peerCount == kMaxPeers) {
                esp_now_del_peer(g_peers[0]);
                memmove(g_peers[0], g_peers[1], 6 * (kMaxPeers - 1));
                --g_peerCount;
            }
            if (!addPeer(mac)) return;
            memcpy(g_peers[g_peerCount++], mac, 6);
        }
        ++g_txPackets;
        g_txBytes += len;
        esp_now_send(dst, data, len);
    }
};

EspNowTransport g_transport;

}  // namespace

bool begin() {
    if (g_active) return true;
    g_head = g_tail = 0;
    g_overflow = 0;
    g_rxPackets = g_rxBytes = g_sendOk = g_sendFail = 0;
    g_txPackets = g_txBytes = 0;
    g_peerCount = 0;

    WiFi.mode(WIFI_STA);
    WiFi.disconnect(false, true);
    esp_wifi_set_ps(WIFI_PS_NONE);  // modem sleep makes the radio miss incoming frames
    esp_wifi_set_channel(kChannel, WIFI_SECOND_CHAN_NONE);

    if (esp_now_init() != ESP_OK) {
        WiFi.mode(WIFI_OFF);
        return false;
    }
    esp_now_register_recv_cb(onRecv);
    esp_now_register_send_cb(onSent);
    esp_wifi_config_espnow_rate(WIFI_IF_STA, WIFI_PHY_RATE_24M);
    if (!addPeer(kBroadcast)) {
        esp_now_deinit();
        WiFi.mode(WIFI_OFF);
        return false;
    }
    g_active = true;
    return true;
}

void end() {
    if (!g_active) return;
    g_active = false;
    esp_now_unregister_recv_cb();
    esp_now_unregister_send_cb();
    esp_now_deinit();
    WiFi.mode(WIFI_OFF);
}

bool active() { return g_active; }

net::Transport& transport() { return g_transport; }

bool poll(uint8_t mac[6], uint8_t* data, size_t* len) {
    bool got = false;
    portENTER_CRITICAL(&g_lock);
    if (g_tail != g_head) {
        const Rx& r = g_queue[g_tail];
        memcpy(mac, r.mac, 6);
        memcpy(data, r.data, r.len);
        *len = r.len;
        g_tail = (g_tail + 1) % kQueueSize;
        got = true;
    }
    portEXIT_CRITICAL(&g_lock);
    return got;
}

uint32_t overflowCount() { return g_overflow; }

Stats stats() {
    return {g_txPackets, g_txBytes, g_rxPackets, g_rxBytes, g_sendOk, g_sendFail};
}

}  // namespace net_espnow
