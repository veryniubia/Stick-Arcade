#include "core/settings.h"

#include <M5Unified.h>
#include <Preferences.h>

#include "core/input.h"
#include "core/sound.h"

namespace settings {

namespace {

Data g_data;
Preferences g_prefs;

void apply() {
    sound::setVolume(kVolumeValue[g_data.volume]);
    M5.Display.setBrightness(kBrightValue[g_data.bright]);
    input::setTiltOffset(g_data.tiltOffsetX10 / 10.0f);
}

void save() {
    uint8_t blob[kBlobSize];
    encode(g_data, blob);
    g_prefs.putBytes("cfg", blob, sizeof(blob));
}

}  // namespace

void begin() {
    g_prefs.begin("arcade", false);
    uint8_t blob[kBlobSize];
    const size_t n = g_prefs.getBytes("cfg", blob, sizeof(blob));
    Data loaded;
    g_data = (n == kBlobSize && decode(blob, n, loaded)) ? loaded : Data();
    apply();
}

const Data& get() { return g_data; }

void set(const Data& d) {
    const bool changed = !(d == g_data);
    g_data = d;
    apply();
    if (changed) save();
}

void resetDefaults() { set(Data()); }

float aimGain() { return kAimValue[g_data.aim]; }
uint8_t brightness() { return kBrightValue[g_data.bright]; }

}  // namespace settings
