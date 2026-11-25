#ifdef ESP_PLATFORM
#include <WiFi.h>
#include "net/phone_ap.h"
#include "net/phone_socket.h"
#include "net/radio.h"

namespace {
phone::Socket udp;
char name[20], key[12];
}
namespace phone_ap {
bool start() {
    WiFi.persistent(false);
    snprintf(name, sizeof(name), "StickArcade-%04X", unsigned(ESP.getEfuseMac() >> 32));
    if (!key[0]) snprintf(key, sizeof(key), "%08X", unsigned(esp_random()));
    IPAddress ip;
    ip.fromString(phone::kGateway);
    if (!WiFi.mode(WIFI_AP) || !WiFi.softAPConfig(ip, ip, IPAddress(255,255,255,0)) ||
        !WiFi.softAP(name, key, 1, false, 1)) return false;
    udp.fd = socket(AF_INET, SOCK_DGRAM, 0);
    auto addr = phone::address(phone::kGateway);
    if (udp.fd < 0 || bind(udp.fd, (sockaddr*)&addr, sizeof(addr)) != 0) return false;
    udp.reset(true);
    return true;
}
void stop() {
    udp.stop();
    WiFi.softAPdisconnect(true);
    WiFi.mode(WIFI_OFF);
}
void service() { udp.service(millis()); }
phone::Link& link() { return udp; }
}
namespace radio {
const char* ssid() { return name; }
const char* password() { return key; }
const char* phoneStatus() {
    if (!phoneMode()) return "OFF";
    if (!WiFi.softAPgetStationNum()) return "WAITING FOR PHONE";
    return udp.connected(millis()) ? "APP CONNECTED" : "WI-FI JOINED";
}
}
#endif
