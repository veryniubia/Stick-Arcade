#include "ui/play.h"

#include <M5Unified.h>
#include <esp_random.h>

#include "core/debug.h"
#include "core/display.h"
#include "core/input.h"
#include "core/platform.h"
#include "core/settings.h"
#include "core/sound.h"
#include "net/net_espnow.h"
#include "net/net_session.h"
#include "ui/gfx.h"

namespace play {

namespace {

using platform::rgb;

enum class Stage : uint8_t { Lobby, Solo, Online };

constexpr uint32_t kClosedShowMs = 1500;
constexpr uint32_t kPingShowMs = 5000;
constexpr uint32_t kSoloHintMs = 10000;   // after this long without an opponent the SOLO hint starts to blink
constexpr uint32_t kExitAskMs = 3000;     // the confirmation closes by itself
constexpr uint32_t kJoinWindowMs = 10000; // how long a solo player stays joinable after pressing A on OPP FOUND

const registry::GameEntry* g_entry = nullptr;
arcade::GameInstance g_gi;
net::Session g_session;
bool g_radioOk = false;
Stage g_stage = Stage::Lobby;

uint32_t g_beginMs = 0;
uint32_t g_closedAtMs = 0;
uint32_t g_playingSinceMs = 0;
uint32_t g_joinUntilMs = 0;
int g_lastDigit = -1;
net::Status g_lastStatus = net::Status::Searching;
float g_accum = 0.0f;

bool g_aArmed = false;  // A must have been released once after entering, so the press that opened the game is not a charge

bool g_exitAsk = false;
uint32_t g_exitAskMs = 0;
bool g_frozenA = false;
bool g_frozenB = false;
uint8_t g_frozenACount = 0;
uint8_t g_frozenBCount = 0;
uint8_t g_aSkip = 0;  // presses made while the confirmation was open belong to the confirmation, not to the game
uint8_t g_bSkip = 0;

arcade::IGame& game() { return *g_gi.game; }

// Hands over the local player's hands. While the exit confirmation is open the held keys are frozen so that a
// charge in progress neither fires nor stops, and presses made for the confirmation never reach the game.
PadInput makePad() {
    PadInput p;
    p.aim = debug::aim(input::tiltDeg() * settings::aimGain());
    if (g_exitAsk) {
        p.a = g_frozenA;
        p.b = g_frozenB;
        p.aCount = g_frozenACount - g_aSkip;
        p.bCount = g_frozenBCount - g_bSkip;
    } else {
        p.a = (input::aHeld() && g_aArmed) || debug::charging();
        p.b = input::bHeld() || debug::charging();
        p.aCount = input::aCount() - g_aSkip;
        p.bCount = input::bCount() + debug::extraB() - g_bSkip;
    }
    return p;
}

void openExitAsk(uint32_t now) {
    g_exitAsk = true;
    g_exitAskMs = now;
    g_frozenA = (input::aHeld() && g_aArmed) || debug::charging();
    g_frozenB = input::bHeld() || debug::charging();
    g_frozenACount = input::aCount();
    g_frozenBCount = input::bCount() + debug::extraB();
}

void closeExitAsk() {
    g_exitAsk = false;
    g_aSkip += input::aCount() - g_frozenACount;
    g_bSkip += input::bCount() + debug::extraB() - g_frozenBCount;
}

void pumpRadio(uint32_t now) {
    if (!g_radioOk) return;
    uint8_t mac[6];
    uint8_t buf[250];
    size_t len = 0;
    while (net_espnow::poll(mac, buf, &len)) {
#ifdef DEBUG_REMOTE
        if (debug::g.lossPct > 0 && static_cast<int>(esp_random() % 100) < debug::g.lossPct) continue;
#endif
        g_session.onReceive(mac, buf, len, now);
    }
}

void startSolo() {
    game().begin(arcade::Mode::Solo, esp_random());
    g_stage = Stage::Solo;
    g_accum = 0.0f;
    g_joinUntilMs = 0;
    g_exitAsk = false;
    g_session.setJoinable(false);
}

void enterOnline() {
    g_stage = Stage::Online;
    g_lastDigit = -1;
    g_lastStatus = net::Status::Searching;
    g_playingSinceMs = 0;
    g_closedAtMs = 0;
    g_exitAsk = false;
    g_joinUntilMs = 0;
}

// Shake-to-exit confirmation. Returns true when the user confirmed.
bool updateExitAsk(uint32_t now, bool shake, bool aPress, bool bPress) {
    if (!g_exitAsk) {
        if (shake) openExitAsk(now);
        return false;
    }
    if (aPress) {
        closeExitAsk();
        return true;
    }
    if (bPress || now - g_exitAskMs > kExitAskMs) closeExitAsk();
    return false;
}

void drawDots(int x, int y, uint32_t ms) {
    auto& c = display::canvas();
    const int dots = static_cast<int>(ms / 300) % 4;
    for (int i = 0; i < dots; ++i) c.fillRect(x + i * 8, y, 4, 4, ui::yellow());
}

void drawPing(const net::Session& s) {
    char buf[16];
    if (s.rttMs() >= 0) snprintf(buf, sizeof(buf), "PING %d", s.rttMs());
    else snprintf(buf, sizeof(buf), "PING --");
    auto& c = display::canvas();
    c.setTextDatum(top_right);
    c.setTextSize(1);
    c.setTextColor(ui::kBlack);
    c.drawString(buf, platform::kScreenW - 3, 7);
    c.setTextColor(ui::kWhite);
    c.drawString(buf, platform::kScreenW - 4, 6);
}

// SOLO marker: coloured bar along the top and a tag, so nobody mistakes it for an online match.
void drawSoloMark() {
    auto& c = display::canvas();
    const uint16_t orange = rgb(255, 150, 40);
    c.fillRect(0, 0, platform::kScreenW, 3, orange);
    c.fillRoundRect(5, 5, 29, 11, 3, orange);
    c.setTextSize(1);
    c.setTextDatum(middle_center);
    c.setTextColor(ui::kBlack);
    c.drawString("SOLO", 19, 11);
}

void drawBox(int w, int h, int y) {
    auto& c = display::canvas();
    const int x = (platform::kScreenW - w) / 2;
    c.fillRoundRect(x, y, w, h, 5, rgb(24, 20, 26));
    c.drawRoundRect(x, y, w, h, 5, ui::kWhite);
}

void drawExitAsk() {
    auto& c = display::canvas();
    drawBox(140, 46, 40);
    ui::shadowText("EXIT?", platform::kScreenW / 2, 54, 2, ui::yellow());
    c.setTextSize(1);
    c.setTextDatum(middle_center);
    c.setTextColor(ui::kWhite);
    c.fillRoundRect(62, 71, 14, 12, 3, ui::keyRed());
    c.drawString("A", 69, 77);
    c.setTextDatum(middle_left);
    c.drawString("YES", 80, 77);
    c.setTextDatum(middle_center);
    c.fillRoundRect(130, 71, 14, 12, 3, ui::keyBlue());
    c.drawString("B", 137, 77);
    c.setTextDatum(middle_left);
    c.drawString("NO", 148, 77);
}

void drawLobby() {
    const uint32_t ms = millis();
    g_entry->drawLobby(ms);
    ui::shadowText(g_entry->name, platform::kScreenW / 2, 20, 2, ui::yellow());
    ui::shadowText(g_radioOk ? "SEARCHING" : "RADIO OFF", platform::kScreenW / 2, 52, 2, ui::kWhite);
    if (g_radioOk) drawDots(platform::kScreenW / 2 - 12, 68, ms - g_beginMs);
    ui::hintBar("SOLO", "BACK", ms - g_beginMs > kSoloHintMs || !g_radioOk);
}

void drawOnlineOverlays(uint32_t now) {
    const net::Status st = g_session.status();
    const bool pre = st == net::Status::Ready || st == net::Status::Countdown;
    const bool earlyPlay = g_playingSinceMs != 0 && now - g_playingSinceMs < kPingShowMs;
    if (pre || earlyPlay) drawPing(g_session);

    const int cx = platform::kScreenW / 2;
    switch (st) {
        case net::Status::Ready:
            ui::shadowText("READY", cx, 40, 3, ui::yellow());
            break;
        case net::Status::Countdown: {
            char d[2] = {static_cast<char>('0' + static_cast<int>(g_session.countdownSec() + 0.99f)), 0};
            ui::shadowText(d, cx, 44, 5, ui::yellow());
            break;
        }
        case net::Status::LinkLost:
            ui::shadowText("LINK LOST", cx, 40, 2, ui::red());
            break;
        case net::Status::PeerLeft:
            ui::shadowText("OPP LEFT", cx, 40, 2, ui::red());
            break;
        case net::Status::TimedOut:
            ui::shadowText("TIMEOUT", cx, 40, 2, ui::red());
            break;
        default:
            break;
    }
    if (st == net::Status::Playing && game().phase() == arcade::MatchPhase::MatchEnd) {
        ui::shadowText(g_session.myAgain() ? "WAITING" : "A = AGAIN", cx, 70, 2, ui::kWhite);
        if (g_session.oppAgain() && !g_session.myAgain()) ui::shadowText("OPP READY", cx, 90, 1, ui::green());
    }
}

void drawSoloOverlays(uint32_t now) {
    drawSoloMark();
    if (!g_radioOk) return;
    if (g_joinUntilMs != 0) {
        ui::shadowText("JOINING", platform::kScreenW / 2, 96, 1, ui::green());
    } else if (g_session.peerNearby(now) && game().idle(0)) {
        ui::shadowText("OPP FOUND", platform::kScreenW / 2, 82, 2, ui::green());
        ui::shadowText("A = JOIN", platform::kScreenW / 2, 100, 1, ui::kWhite);
    }
}

}  // namespace

void begin(const registry::GameEntry& entry) {
    g_entry = &entry;
    g_gi = arcade::gameCreate(*entry.info);
    g_stage = Stage::Lobby;
    g_beginMs = millis();
    g_closedAtMs = 0;
    g_playingSinceMs = 0;
    g_joinUntilMs = 0;
    g_lastDigit = -1;
    g_lastStatus = net::Status::Searching;
    g_aArmed = false;
    g_exitAsk = false;
    g_aSkip = g_bSkip = 0;
    g_radioOk = false;
    if (!g_gi.game) return;
    g_radioOk = net_espnow::begin();
    if (g_radioOk) {
        g_session.begin(&net_espnow::transport(), esp_random(), millis(), *entry.info, g_gi.game);
        g_session.setJoinable(true);
    }
}

void end() {
    net_espnow::end();
    g_radioOk = false;
    arcade::gameDestroy(g_gi);
}

bool update(float dt) {
    if (!g_gi.game) return true;  // out of memory: back to the launcher
    const uint32_t now = millis();
    bool aPress = input::aPressed() || debug::aPress();
    bool bPress = input::bPressed() || debug::bPress();
    const bool shake = input::shaken() || debug::shake();
    if (!input::aHeld()) g_aArmed = true;

    pumpRadio(now);
    const bool askWasOpen = g_exitAsk;

    net::LocalInput li;
    li.pad = makePad();

    if (g_stage == Stage::Lobby) {
        if (g_radioOk) {
            g_session.update(now, li);
            if (g_session.status() != net::Status::Searching) {
                enterOnline();
                return false;
            }
        }
        if (aPress) return true;
        if (bPress) startSolo();
        return false;
    }

    // Solo and online share the exit confirmation; while it is open A and B belong to it.
    const bool confirmed = updateExitAsk(now, shake, aPress, bPress);
    if (askWasOpen) aPress = bPress = false;
    if (confirmed) {
        if (g_stage == Stage::Online) g_session.leave();
        return true;
    }
    li.pad = makePad();
    li.again = aPress;

    if (g_stage == Stage::Solo) {
        if (g_radioOk) {
            g_session.update(now, li);
            if (g_session.status() != net::Status::Searching) {
                enterOnline();
                return false;
            }
            if (g_joinUntilMs == 0 && aPress && g_session.peerNearby(now) && game().idle(0) && !g_exitAsk) {
                g_session.setJoinable(true);
                g_joinUntilMs = now + kJoinWindowMs;
            } else if (g_joinUntilMs != 0 && now > g_joinUntilMs) {
                g_session.setJoinable(false);
                g_joinUntilMs = 0;
            }
        }
        g_accum += dt;
        while (g_accum >= platform::kTick) {
            g_accum -= platform::kTick;
            const PadInput pads[2] = {li.pad, PadInput()};
            game().sfx(game().step(platform::kTick, pads));
        }
        return false;
    }

    // Online.
    game().sfx(g_session.update(now, li));
    const net::Status st = g_session.status();
    if (st == net::Status::Countdown) {
        const int digit = static_cast<int>(g_session.countdownSec() + 0.99f);
        if (digit != g_lastDigit && digit > 0) sound::playUi(sound::Ui::Move);
        g_lastDigit = digit;
    }
    if (st == net::Status::Playing && g_lastStatus != net::Status::Playing && g_lastStatus != net::Status::LinkLost) {
        g_playingSinceMs = now;
        sound::playUi(sound::Ui::Refresh);
    }
    g_lastStatus = st;

    if (st == net::Status::PeerLeft || st == net::Status::TimedOut) {
        if (g_closedAtMs == 0) {
            g_closedAtMs = now;
            sound::playUi(sound::Ui::Denied);
        }
        return now - g_closedAtMs > kClosedShowMs;
    }
    return false;
}

void draw() {
    if (!g_gi.game) return;
    const uint32_t now = millis();
    arcade::DrawCtx ctx;
    ctx.nowMs = now;
    if (g_stage == Stage::Lobby) {
        drawLobby();
        return;
    }
    if (g_stage == Stage::Solo) {
        ctx.solo = true;
        game().draw(0, ctx);
        drawSoloOverlays(now);
    } else {
        game().draw(g_session.localIndex(), ctx);
        drawOnlineOverlays(now);
    }
    if (g_exitAsk) drawExitAsk();
}

#ifdef DEBUG_REMOTE
void debugLine() {
    static const char* names[] = {"SEARCH", "READY", "COUNT", "PLAY", "LOST", "LEFT", "TIMEOUT"};
    if (!g_gi.game || !g_radioOk) return;
    Serial.printf("NET role=%d st=%s rtt=%d stage=%d phase=%d rxdrop=%u nearby=%d\n",
                  static_cast<int>(g_session.role()), names[static_cast<int>(g_session.status())], g_session.rttMs(),
                  static_cast<int>(g_stage), static_cast<int>(game().phase()),
                  static_cast<unsigned>(net_espnow::overflowCount()), g_session.peerNearby(millis()) ? 1 : 0);
}

bool debugKey(char key) {
    if (!g_gi.game) return false;
    if (key == 'd') {
        arcade::Writer w;
        game().encode(w);
        Serial.print("STATE ");
        for (size_t i = 0; i < w.n; ++i) Serial.printf("%02x", w.buf[i]);
        Serial.println();
        return true;
    }
    return game().debugKey(key);
}
#endif

}  // namespace play
