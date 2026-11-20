#include <M5Unified.h>
#include <esp_random.h>
#ifdef DEBUG_REMOTE
#include <esp_freertos_hooks.h>
#include <esp_heap_caps.h>
#include <esp_timer.h>
#endif

#include "game.h"
#include "input.h"
#include "menu.h"
#include "net_espnow.h"
#include "render.h"
#include "sound.h"
#include "versus.h"

namespace {

enum class Screen { Menu, Game, Versus };

constexpr float kTick = 1.0f / 60.0f;
constexpr float kExitHoldSec = 0.8f;
constexpr float kExitHintSec = 5.0f;
constexpr int kLocalPlayer = 0;

Game g_game;
Screen g_screen = Screen::Menu;
uint32_t g_lastMs = 0;
float g_accum = 0.0f;
float g_gameClock = 0.0f;
bool g_jumpLatch = false;
bool g_chargeArmed = false;
float g_exitHold = 0.0f;

void startTraining() {
    g_game.begin(GameMode::Training, esp_random());
    g_screen = Screen::Game;
    g_accum = 0.0f;
    g_gameClock = 0.0f;
    g_jumpLatch = false;
    g_chargeArmed = false;  // the A press that picked the menu entry must not start a charge
    g_exitHold = 0.0f;
}

void startVersus() {
    g_screen = Screen::Versus;
    g_jumpLatch = false;
    g_chargeArmed = false;
    g_exitHold = 0.0f;
    versus::begin();
}

void backToMenu() {
    if (g_screen == Screen::Versus) versus::end();
    g_screen = Screen::Menu;
    menu::begin();
}

#ifdef DEBUG_REMOTE
// Development-only serial remote: g=start training, v=start versus, m=menu, f=charge+throw, i=next item, j=jump,
// s=screenshot, 1-4=pick item, u/b=force aim up(90)/backward(180), n=back to IMU aim,
// l/k/L=drop 20%/0%/100% of received packets (versus), a=press A, x=hold A+B for 3 s.
uint32_t g_dbgChargeUntil = 0;
uint32_t g_dbgExitUntil = 0;
float g_dbgAim = -1000.0f;
bool g_dbgJump = false;
bool g_dbgAgain = false;
bool g_dbgShot = false;

bool debugAgain() {
    const bool a = g_dbgAgain;
    g_dbgAgain = false;
    return a;
}

bool debugExitHeld() { return millis() < g_dbgExitUntil; }

float debugAim(float imuAim) { return g_dbgAim > -999.0f ? g_dbgAim : imuAim; }

bool debugCharging() { return millis() < g_dbgChargeUntil; }

bool debugJump() {
    const bool j = g_dbgJump;
    g_dbgJump = false;
    return j;
}

void debugPoll() {
    while (Serial.available()) {
        const int ch = Serial.read();
        if (ch == 'g') startTraining();
        else if (ch == 'v') startVersus();
        else if (ch == 'l') versus::debugSetRxLoss(20);
        else if (ch == 'k') versus::debugSetRxLoss(0);
        else if (ch == 'L') versus::debugSetRxLoss(100);
        else if (ch == 'a') g_dbgAgain = true;
        else if (ch == 'm') backToMenu();
        else if (ch == 'f') g_dbgChargeUntil = millis() + 450;
        else if (ch == 'j') g_dbgJump = true;
        else if (ch == 's') g_dbgShot = true;
        else if (ch == 'u') g_dbgAim = 90.0f;
        else if (ch == 'x') g_dbgExitUntil = millis() + 3000;
        else if (ch == 'b') g_dbgAim = 180.0f;
        else if (ch == 'n') g_dbgAim = -1000.0f;
        else if (ch >= '1' && ch <= '4' && g_screen == Screen::Game) {
            g_game.players[0].item = static_cast<ItemType>(ch - '0');
        } else if (ch == 'i' && g_screen == Screen::Game) {
            auto& item = g_game.players[0].item;
            item = static_cast<ItemType>(1 + static_cast<int>(item) % 4);
        }
    }
}

void debugShot() {
    if (!g_dbgShot) return;
    g_dbgShot = false;
    const size_t bytes = cfg::SCREEN_W * cfg::SCREEN_H * 2;
    Serial.write("SHOT", 4);
    Serial.write(static_cast<const uint8_t*>(render::canvas.getBuffer()), bytes);
    Serial.flush();
}

// Development-only load statistics, printed once per second as a PERF line (and a RADIO line while online).
// Times are microseconds per frame; idle0/idle1 count FreeRTOS idle-task passes per second on each core.
namespace perf {
volatile uint32_t idleHits[2] = {0, 0};
bool idle0() { ++idleHits[0]; return false; }
bool idle1() { ++idleHits[1]; return false; }

uint32_t frames = 0;
uint64_t sumIn = 0, sumLogic = 0, sumDraw = 0, sumPush = 0, sumLoop = 0;
uint32_t maxLoop = 0;
uint32_t lastReportMs = 0;
uint32_t lastIdle[2] = {0, 0};
net_espnow::Stats lastRadio = {};
uint32_t tLogic = 0, tDraw = 0;  // set by the screen code of the current frame

inline uint32_t us() { return static_cast<uint32_t>(esp_timer_get_time()); }

void begin() {
    esp_register_freertos_idle_hook_for_cpu(idle0, 0);
    esp_register_freertos_idle_hook_for_cpu(idle1, 1);
    lastReportMs = millis();
}

void frame(uint32_t in, uint32_t screenUs, uint32_t push, uint32_t loopUs) {
    ++frames;
    sumIn += in;
    sumLogic += tLogic;
    sumDraw += tDraw ? tDraw : screenUs;
    sumPush += push;
    sumLoop += loopUs;
    if (loopUs > maxLoop) maxLoop = loopUs;
    tLogic = tDraw = 0;

    const uint32_t now = millis();
    if (now - lastReportMs < 1000) return;
    const uint32_t span = now - lastReportMs;
    lastReportMs = now;
    const uint32_t n = frames ? frames : 1;
    const uint32_t i0 = idleHits[0], i1 = idleHits[1];
    Serial.printf("PERF scr=%d fps=%u in=%u logic=%u draw=%u push=%u loop=%u maxloop=%u idle0=%u idle1=%u "
                  "heap=%u min=%u big=%u psram=%u stack=%u\n",
                  static_cast<int>(g_screen), static_cast<unsigned>(frames * 1000 / span),
                  static_cast<unsigned>(sumIn / n), static_cast<unsigned>(sumLogic / n),
                  static_cast<unsigned>(sumDraw / n), static_cast<unsigned>(sumPush / n),
                  static_cast<unsigned>(sumLoop / n), static_cast<unsigned>(maxLoop),
                  static_cast<unsigned>((i0 - lastIdle[0]) * 1000 / span),
                  static_cast<unsigned>((i1 - lastIdle[1]) * 1000 / span),
                  static_cast<unsigned>(ESP.getFreeHeap()), static_cast<unsigned>(ESP.getMinFreeHeap()),
                  static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_8BIT)),
                  static_cast<unsigned>(ESP.getFreePsram()),
                  static_cast<unsigned>(uxTaskGetStackHighWaterMark(nullptr)));
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
    sumIn = sumLogic = sumDraw = sumPush = sumLoop = 0;
    maxLoop = 0;
}
}  // namespace perf
#else
bool debugExitHeld() { return false; }
float debugAim(float imuAim) { return imuAim; }
bool debugCharging() { return false; }
bool debugJump() { return false; }
bool debugAgain() { return false; }
void debugPoll() {}
void debugShot() {}
#endif

void runGame(float dt) {
    if (input::bPressed() || debugJump()) g_jumpLatch = true;
    if (!input::aHeld()) g_chargeArmed = true;

    if ((input::aHeld() && input::bHeld()) || debugExitHeld()) {
        g_exitHold += dt;
        if (g_exitHold >= kExitHoldSec) {
            backToMenu();
            return;
        }
    } else {
        g_exitHold = 0.0f;
    }

    if (g_game.phase == Phase::MatchEnd && input::aPressed()) {
        backToMenu();
        return;
    }

    g_gameClock += dt;
    g_accum += dt;
    while (g_accum >= kTick) {
        g_accum -= kTick;
        PlayerInput in[2];
        in[kLocalPlayer].aimDeg = debugAim(input::aimDeg());
        in[kLocalPlayer].charge = (input::aHeld() && g_chargeArmed) || debugCharging();
        in[kLocalPlayer].jump = g_jumpLatch;
        in[kLocalPlayer].jumpHeld = input::bHeld() || debugCharging();
        g_jumpLatch = false;
        sound::playEvents(g_game.update(kTick, in));
    }
    render::drawGame(g_game, kLocalPlayer, g_exitHold / kExitHoldSec, g_gameClock < kExitHintSec);
}

void runVersus(float dt) {
    if (!input::aHeld()) g_chargeArmed = true;

    versus::Input in;
    in.aimDeg = debugAim(input::aimDeg());
    in.charge = (input::aHeld() && g_chargeArmed) || debugCharging();
    in.jump = input::bPressed() || debugJump();
    in.jumpHeld = input::bHeld() || debugCharging();
    in.again = input::aPressed() || debugAgain();
    in.exitHeld = (input::aHeld() && input::bHeld()) || debugExitHeld();
#ifdef DEBUG_REMOTE
    const uint32_t t0 = perf::us();
#endif
    const bool leave = versus::update(dt, in);
#ifdef DEBUG_REMOTE
    perf::tLogic = perf::us() - t0;
#endif
    if (leave) {
        backToMenu();
        return;
    }
#ifdef DEBUG_REMOTE
    const uint32_t t1 = perf::us();
#endif
    versus::draw();
#ifdef DEBUG_REMOTE
    perf::tDraw = perf::us() - t1;
#endif
}

}  // namespace

void setup() {
    auto cfg = M5.config();
    M5.begin(cfg);
    Serial.begin(115200);

    M5.Display.setRotation(1);
    M5.Display.setBrightness(110);
    M5.Display.fillScreen(TFT_BLACK);

    render::begin();
    input::begin();
    sound::begin();
    menu::begin();
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
    const uint32_t tScreen0 = perf::us();
#endif

    const uint32_t now = millis();
    float dt = (now - g_lastMs) / 1000.0f;
    g_lastMs = now;
    if (dt > 0.05f) dt = 0.05f;

    if (g_screen == Screen::Menu) {
        const menu::Choice choice = menu::update();
        if (choice == menu::Choice::Training) {
            startTraining();
        } else if (choice == menu::Choice::Versus) {
            startVersus();
        } else {
            menu::draw();
        }
    } else if (g_screen == Screen::Game) {
        runGame(dt);
    } else {
        runVersus(dt);
    }
#ifdef DEBUG_REMOTE
    const uint32_t tScreen = perf::us() - tScreen0;
    const uint32_t tPush0 = perf::us();
#endif
    render::present();
#ifdef DEBUG_REMOTE
    const uint32_t tPush = perf::us() - tPush0;
#endif
    debugShot();
#ifdef DEBUG_REMOTE
    perf::frame(tIn, tScreen, tPush, perf::us() - tLoop0);
    static uint32_t lastNet = 0;
    if (g_screen == Screen::Versus && now - lastNet >= 1000) {
        lastNet = now;
        versus::debugLine();
    }
#endif
}
