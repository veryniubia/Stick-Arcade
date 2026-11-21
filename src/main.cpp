#include <M5Unified.h>
#ifdef DEBUG_REMOTE
#include <esp_freertos_hooks.h>
#include <esp_heap_caps.h>
#include <esp_timer.h>
#endif

#include "core/debug.h"
#include "core/display.h"
#include "core/input.h"
#include "core/platform.h"
#include "core/settings.h"
#include "core/sound.h"
#include "games/registry.h"
#include "net/net_espnow.h"
#include "ui/launcher.h"
#include "ui/play.h"
#include "ui/settings_page.h"

namespace {

enum class Screen : uint8_t { Launcher, Settings, Play };

constexpr int kMenuFps = 30;
constexpr int kPlayFps = 60;

Screen g_screen = Screen::Launcher;
uint32_t g_lastMs = 0;

void goLauncher() {
    g_screen = Screen::Launcher;
    launcher::begin();
    display::setTargetFps(kMenuFps);
}

void startPlay(int gameIndex) {
    g_screen = Screen::Play;
    display::setTargetFps(kPlayFps);
    play::begin(registry::get(gameIndex));
}

void goSettings() {
    g_screen = Screen::Settings;
    settings_page::begin();
}

#ifdef DEBUG_REMOTE
void debugPoll() {
    while (Serial.available()) {
        const int ch = Serial.read();
        auto& d = debug::g;
        if (ch == 'v' || ch == 'g' || ch == 'V' || ch == 'G' || ch == 'z' || ch == 'Z') {
            int index = (ch == 'z' || ch == 'Z' || ch == 'V' || ch == 'G') ? 1 : 0;
#ifdef ONLY_GAME_BUMPERDUEL
            index = 0;
#endif
            if (g_screen == Screen::Play) play::end();
            if (index < registry::count()) {
                startPlay(index);
                if (ch == 'g' || ch == 'G' || ch == 'Z') d.bPress = true;  // B on the waiting screen = solo
            }
        } else if (ch == 'm') {
            if (g_screen == Screen::Play) play::end();
            goLauncher();
        } else if (ch == 'l') d.lossPct = 20;
        else if (ch == 'k') d.lossPct = 0;
        else if (ch == 'L') d.lossPct = 100;
        else if (ch == 'a') d.aPress = true;
        else if (ch == 'o') d.bPress = true;
        else if (ch == 'x') d.shake = true;
        else if (ch == 'e') d.chargeUntil = millis() + 65000;
        else if (ch == 'q') { d.chargeUntil = 0; d.brakeUntil = 0; }
        else if (ch == 'R') d.brakeUntil = millis() + 65000;
        else if (ch == '[') d.aim = -25;
        else if (ch == ']') d.aim = 25;
        else if (ch == '=') d.aim = 0;
        else if (ch == 'f') d.chargeUntil = millis() + 450;
        else if (ch == 'j') ++d.extraB;
        else if (ch == 's') d.shot = true;
        else if (ch == 'u') d.aim = 90.0f;
        else if (ch == 'b') d.aim = 180.0f;
        else if (ch == 'n') d.aim = -1000.0f;
        else if (ch == 'I') {
            const float imuAim = input::tiltDeg() * settings::aimGain();
            Serial.printf("IMU ok=%d raw=%.3f tilt=%.3f override=%d aim=%.3f\n",
                          input::imuOk() ? 1 : 0, input::rawTiltDeg(), input::tiltDeg(),
                          d.aim > -999.0f ? 1 : 0, debug::aim(imuAim));
        }
        else if (g_screen == Screen::Play) play::debugKey(static_cast<char>(ch));
    }
}

void debugShot() {
    if (!debug::g.shot) return;
    debug::g.shot = false;
    const size_t bytes = platform::kScreenW * platform::kScreenH * 2;
    Serial.printf("SIZE %d %d\n", display::width(), display::height());
    Serial.write("SHOT", 4);
    Serial.write(static_cast<const uint8_t*>(display::lastFrame()), bytes);
    Serial.flush();
}

// Development-only load statistics, printed once per second as a PERF line (and a RADIO line while online).
// Times are microseconds per frame; idle0/idle1 count FreeRTOS idle-task passes per second on each core.
namespace perf {
volatile uint32_t idleHits[2] = {0, 0};
bool idle0() { ++idleHits[0]; return false; }
bool idle1() { ++idleHits[1]; return false; }

uint32_t frames = 0;
uint64_t sumIn = 0, sumLogic = 0, sumDraw = 0, sumPush = 0, sumWait = 0, sumLoop = 0;
uint32_t maxLoop = 0;
uint32_t lastReportMs = 0;
uint32_t lastIdle[2] = {0, 0};
net_espnow::Stats lastRadio = {};

inline uint32_t us() { return static_cast<uint32_t>(esp_timer_get_time()); }

void begin() {
    esp_register_freertos_idle_hook_for_cpu(idle0, 0);
    esp_register_freertos_idle_hook_for_cpu(idle1, 1);
    lastReportMs = millis();
}

void frame(uint32_t in, uint32_t logic, uint32_t draw, uint32_t loopUs) {
    ++frames;
    sumIn += in;
    sumLogic += logic;
    sumDraw += draw;
    sumPush += display::pushUs();
    sumWait += display::waitUs();
    sumLoop += loopUs;
    if (loopUs > maxLoop) maxLoop = loopUs;

    const uint32_t now = millis();
    if (now - lastReportMs < 1000) return;
    const uint32_t span = now - lastReportMs;
    lastReportMs = now;
    const uint32_t n = frames ? frames : 1;
    const uint32_t i0 = idleHits[0], i1 = idleHits[1];
    Serial.printf("PERF scr=%d fps=%u in=%u logic=%u draw=%u push=%u wait=%u loop=%u maxloop=%u idle0=%u idle1=%u "
                  "heap=%u min=%u big=%u psram=%u stack=%u dma=%d\n",
                  static_cast<int>(g_screen), static_cast<unsigned>(frames * 1000 / span),
                  static_cast<unsigned>(sumIn / n), static_cast<unsigned>(sumLogic / n),
                  static_cast<unsigned>(sumDraw / n), static_cast<unsigned>(sumPush / n),
                  static_cast<unsigned>(sumWait / n), static_cast<unsigned>(sumLoop / n),
                  static_cast<unsigned>(maxLoop), static_cast<unsigned>((i0 - lastIdle[0]) * 1000 / span),
                  static_cast<unsigned>((i1 - lastIdle[1]) * 1000 / span), static_cast<unsigned>(ESP.getFreeHeap()),
                  static_cast<unsigned>(ESP.getMinFreeHeap()),
                  static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_8BIT)),
                  static_cast<unsigned>(ESP.getFreePsram()), static_cast<unsigned>(uxTaskGetStackHighWaterMark(nullptr)),
                  display::dmaActive() ? 1 : 0);
    if (net_espnow::active()) {
        const net_espnow::Stats r = net_espnow::stats();
        Serial.printf("RADIO tx=%u/s %uB/s rx=%u/s %uB/s ackok=%u ackfail=%u\n",
                      static_cast<unsigned>((r.txPackets - lastRadio.txPackets) * 1000 / span),
                      static_cast<unsigned>((r.txBytes - lastRadio.txBytes) * 1000 / span),
                      static_cast<unsigned>((r.rxPackets - lastRadio.rxPackets) * 1000 / span),
                      static_cast<unsigned>((r.rxBytes - lastRadio.rxBytes) * 1000 / span),
                      static_cast<unsigned>(r.sendOk - lastRadio.sendOk),
                      static_cast<unsigned>(r.sendFail - lastRadio.sendFail));
        lastRadio = r;
    } else {
        lastRadio = {};
    }
    lastIdle[0] = i0;
    lastIdle[1] = i1;
    frames = 0;
    sumIn = sumLogic = sumDraw = sumPush = sumWait = sumLoop = 0;
    maxLoop = 0;
}
}  // namespace perf
#else
void debugPoll() {}
void debugShot() {}
#endif

}  // namespace

void setup() {
    auto cfg = M5.config();
    cfg.pmic_button = false;  // polling the power key over I2C costs 0.8 ms per frame and the platform does not use it
    M5.begin(cfg);
    Serial.begin(115200);

    M5.Display.setRotation(1);
    M5.Display.fillScreen(TFT_BLACK);

    sound::begin();
    settings::begin();  // applies brightness, volume and the calibrated level pose
    display::begin();
    registry::begin();
    input::begin();
    goLauncher();
    g_lastMs = millis();
#ifdef DEBUG_REMOTE
    perf::begin();
#endif
}

void loop() {
#ifdef DEBUG_REMOTE
    const uint32_t tLoop0 = perf::us();
#endif
    M5.update();
    input::update();
    sound::update();
    debugPoll();
#ifdef DEBUG_REMOTE
    const uint32_t tIn = perf::us() - tLoop0;
    const uint32_t tLogic0 = perf::us();
#endif

    const uint32_t now = millis();
    float dt = (now - g_lastMs) / 1000.0f;
    g_lastMs = now;
    if (dt > 0.05f) dt = 0.05f;

    switch (g_screen) {
        case Screen::Launcher: {
            const launcher::Result r = launcher::update(dt);
            if (r == launcher::Result::Game) startPlay(launcher::selectedGame());
            else if (r == launcher::Result::Settings) goSettings();
            break;
        }
        case Screen::Settings:
            if (settings_page::update(dt)) goLauncher();
            break;
        case Screen::Play:
            if (play::update(dt)) {
                play::end();
                goLauncher();
            }
            break;
    }
#ifdef DEBUG_REMOTE
    const uint32_t tDraw0 = perf::us();
    const uint32_t tLogic = tDraw0 - tLogic0;
#endif

    switch (g_screen) {
        case Screen::Launcher: launcher::draw(); break;
        case Screen::Settings: settings_page::draw(); break;
        case Screen::Play: play::draw(); break;
    }
#ifdef DEBUG_REMOTE
    const uint32_t tDraw = perf::us() - tDraw0;
#endif

    display::present();
    debugShot();
#ifdef DEBUG_REMOTE
    perf::frame(tIn, tLogic, tDraw, perf::us() - tLoop0);
    static uint32_t lastNet = 0;
    if (g_screen == Screen::Play && now - lastNet >= 1000) {
        lastNet = now;
        play::debugLine();
    }
#endif
}
