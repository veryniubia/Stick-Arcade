#include "core/offline_trace.h"

#ifdef DEBUG_OFFLINE_TRACE
#include <M5Unified.h>
#include <Preferences.h>
#include <esp_system.h>

namespace offline_trace {
namespace {

constexpr size_t kCapacity = 24;
constexpr char kKey[] = "events-v1";

struct Entry {
    uint32_t ms;
    int16_t batteryMv;
    int16_t usbMv;
    int32_t value;
    int32_t detail;
    char event[16];
};

struct Log {
    size_t next;
    size_t count;
    Entry entries[kCapacity];
};

Preferences g_prefs;
Log g_log = {};
bool g_open = false;
bool g_saved = false;
bool g_usbPresent = false;
uint32_t g_lastPowerMs = 0;

}  // namespace

void begin() {
    g_open = g_prefs.begin("link-trace", false);
    if (g_open && g_prefs.isKey(kKey)) {
        if (g_prefs.getBytes(kKey, &g_log, sizeof(g_log)) != sizeof(g_log) ||
            g_log.next >= kCapacity || g_log.count > kCapacity) {
            g_log = {};
        }
    }
    record("boot", esp_reset_reason());
    g_usbPresent = M5.Power.getVBUSVoltage() > 4000;
}

void record(const char* event, int value, int detail) {
    if (!g_open) return;
    Entry& entry = g_log.entries[g_log.next];
    entry.ms = millis();
    entry.batteryMv = M5.Power.getBatteryVoltage();
    entry.usbMv = M5.Power.getVBUSVoltage();
    entry.value = value;
    entry.detail = detail;
    snprintf(entry.event, sizeof(entry.event), "%s", event);

    g_log.next = (g_log.next + 1) % kCapacity;
    if (g_log.count < kCapacity) ++g_log.count;
    g_saved = g_prefs.putBytes(kKey, &g_log, sizeof(g_log)) == sizeof(g_log);
}

void poll() {
    const uint32_t now = millis();
    if (now - g_lastPowerMs >= 1000) {
        g_lastPowerMs = now;
        const bool usbPresent = M5.Power.getVBUSVoltage() > 4000;
        if (usbPresent != g_usbPresent) {
            g_usbPresent = usbPresent;
            record("usb_power", usbPresent);
        }
    }
    if (Serial.read() != '?') return;
    Serial.printf("OFFLINE TRACE %u ready=%d saved=%d\n",
                  static_cast<unsigned>(g_log.count), g_open, g_saved);
    const size_t first = (g_log.next + kCapacity - g_log.count) % kCapacity;
    for (size_t i = 0; i < g_log.count; ++i) {
        const Entry& entry = g_log.entries[(first + i) % kCapacity];
        Serial.printf("TRACE ms=%lu event=%.16s value=%ld detail=%ld battery=%d usb=%d\n",
                      static_cast<unsigned long>(entry.ms), entry.event,
                      static_cast<long>(entry.value), static_cast<long>(entry.detail),
                      entry.batteryMv, entry.usbMv);
    }
    Serial.println("OFFLINE TRACE END");
}

}  // namespace offline_trace
#endif
