#include "versus.h"

#include <M5Unified.h>
#include <esp_random.h>

#include "net_espnow.h"
#include "net_session.h"
#include "render.h"
#include "sound.h"

namespace versus {

namespace {

constexpr float kExitHoldSec = 0.8f;
constexpr uint32_t kClosedShowMs = 1500;
constexpr uint32_t kPingShowMs = 5000;

net::Session g_session;
bool g_radioOk = false;
uint32_t g_beginMs = 0;
uint32_t g_playingSinceMs = 0;
uint32_t g_closedAtMs = 0;
float g_exitHold = 0.0f;
int g_lastDigit = -1;
int g_lossPct = 0;
net::Status g_lastStatus = net::Status::Searching;

uint16_t col(uint8_t r, uint8_t g, uint8_t b) { return render::rgb(r, g, b); }

void shadowText(const char* text, int x, int y, int size, uint16_t color) {
    auto& c = render::canvas;
    c.setTextDatum(middle_center);
    c.setTextSize(size);
    c.setTextColor(col(0, 0, 0));
    c.drawString(text, x + size / 2 + 1, y + size / 2 + 1);
    c.setTextColor(color);
    c.drawString(text, x, y);
    c.setTextSize(1);
}

void drawExitHold() {
    if (g_exitHold <= 0.0f) return;
    auto& c = render::canvas;
    constexpr int w = 110, h = 34;
    const int x = (cfg::SCREEN_W - w) / 2;
    const int y = 38;
    c.fillRoundRect(x, y, w, h, 5, col(24, 20, 26));
    c.drawRoundRect(x, y, w, h, 5, col(255, 255, 255));
    c.setTextDatum(middle_center);
    c.setTextSize(2);
    c.setTextColor(col(255, 220, 60));
    c.drawString("MENU", cfg::SCREEN_W / 2, y + 12);
    c.setTextSize(1);
    const float f = g_exitHold / kExitHoldSec;
    c.drawRect(x + 8, y + 23, w - 16, 7, col(255, 255, 255));
    c.fillRect(x + 9, y + 24, static_cast<int>((w - 18) * (f > 1.0f ? 1.0f : f)), 5, col(255, 220, 60));
}

void drawSearching() {
    auto& c = render::canvas;
    render::drawBackground();
    render::drawPlatform(24, 92);
    render::drawPlatform(216, 92);
    render::drawFigure(24, 92, 1, 0, false, false, 20.0f);
    render::drawFigure(216, 92, -1, 1, false, false, -20.0f);

    const uint32_t t = millis() - g_beginMs;
    const char* text = g_radioOk ? "SEARCHING" : "RADIO FAIL";
    shadowText("VERSUS", cfg::SCREEN_W / 2, 20, 2, col(255, 220, 60));
    shadowText(text, cfg::SCREEN_W / 2, 52, 2, col(255, 255, 255));
    if (g_radioOk) {
        const int dots = static_cast<int>(t / 300) % 4;
        for (int i = 0; i < dots; ++i) c.fillRect(cfg::SCREEN_W / 2 - 12 + i * 8, 68, 4, 4, col(255, 220, 60));
    }

    render::drawHudStrip();
    c.setTextSize(1);
    c.setTextDatum(middle_center);
    c.setTextColor(col(255, 255, 255));
    c.drawString("HOLD A+B = MENU", cfg::SCREEN_W / 2, 127);
    drawExitHold();
}

void drawPing(const net::Session& s) {
    char buf[16];
    if (s.rttMs() >= 0) snprintf(buf, sizeof(buf), "PING %d", s.rttMs());
    else snprintf(buf, sizeof(buf), "PING --");
    auto& c = render::canvas;
    c.setTextDatum(top_right);
    c.setTextSize(1);
    c.setTextColor(col(0, 0, 0));
    c.drawString(buf, cfg::SCREEN_W - 3, 7);
    c.setTextColor(col(255, 255, 255));
    c.drawString(buf, cfg::SCREEN_W - 4, 6);
}

}  // namespace

void begin() {
    g_beginMs = millis();
    g_playingSinceMs = 0;
    g_closedAtMs = 0;
    g_exitHold = 0.0f;
    g_lastDigit = -1;
    g_lastStatus = net::Status::Searching;
    g_radioOk = net_espnow::begin();
    if (g_radioOk) {
        g_session.begin(&net_espnow::transport(), esp_random(), millis());
    } else {
        g_closedAtMs = millis();
    }
}

void end() {
    net_espnow::end();
    g_radioOk = false;
}

bool update(float dt, const Input& in) {
    const uint32_t now = millis();
    if (!g_radioOk) return now - g_closedAtMs > kClosedShowMs;

    uint8_t mac[6];
    uint8_t buf[250];
    size_t len = 0;
    while (net_espnow::poll(mac, buf, &len)) {
#ifdef DEBUG_REMOTE
        if (g_lossPct > 0 && static_cast<int>(esp_random() % 100) < g_lossPct) continue;
#endif
        g_session.onReceive(mac, buf, len, now);
    }

    if (in.exitHeld) {
        g_exitHold += dt;
        if (g_exitHold >= kExitHoldSec) {
            g_session.leave();
            return true;
        }
    } else {
        g_exitHold = 0.0f;
    }

    net::LocalInput li;
    li.aimDeg = in.aimDeg;
    li.charge = in.charge;
    li.jump = in.jump;
    li.jumpHeld = in.jumpHeld;
    li.again = in.again;
    sound::playEvents(g_session.update(now, li));

    const net::Status st = g_session.status();
    if (st == net::Status::Countdown) {
        const int digit = static_cast<int>(g_session.countdownSec() + 0.99f);
        if (digit != g_lastDigit && digit > 0) sound::play(sound::Sfx::Move);
        g_lastDigit = digit;
    }
    if (st == net::Status::Playing && g_lastStatus != net::Status::Playing && g_lastStatus != net::Status::LinkLost) {
        g_playingSinceMs = now;
        sound::play(sound::Sfx::Refresh);
    }
    g_lastStatus = st;

    if (st == net::Status::PeerLeft || st == net::Status::TimedOut) {
        if (g_closedAtMs == 0) {
            g_closedAtMs = now;
            sound::play(sound::Sfx::Denied);
        }
        return now - g_closedAtMs > kClosedShowMs;
    }
    return false;
}

void draw() {
    auto& c = render::canvas;
    const net::Status st = g_radioOk ? g_session.status() : net::Status::Searching;
    if (st == net::Status::Searching) {
        drawSearching();
        return;
    }

    const uint32_t now = millis();
    const bool earlyPlay = g_playingSinceMs != 0 && now - g_playingSinceMs < kPingShowMs;
    render::drawGame(g_session.game(), g_session.localIndex(), g_exitHold / kExitHoldSec, earlyPlay);

    const bool pre = st == net::Status::Ready || st == net::Status::Countdown;
    if (pre || earlyPlay) drawPing(g_session);

    const Game& g = g_session.game();
    switch (st) {
        case net::Status::Ready:
            shadowText("READY", cfg::SCREEN_W / 2, 40, 3, col(255, 220, 60));
            break;
        case net::Status::Countdown: {
            char d[2] = {static_cast<char>('0' + static_cast<int>(g_session.countdownSec() + 0.99f)), 0};
            shadowText(d, cfg::SCREEN_W / 2, 44, 5, col(255, 220, 60));
            break;
        }
        case net::Status::LinkLost:
            shadowText("LINK LOST", cfg::SCREEN_W / 2, 40, 2, col(255, 90, 80));
            break;
        case net::Status::PeerLeft:
            shadowText("OPP LEFT", cfg::SCREEN_W / 2, 40, 2, col(255, 90, 80));
            break;
        case net::Status::TimedOut:
            shadowText("TIMEOUT", cfg::SCREEN_W / 2, 40, 2, col(255, 90, 80));
            break;
        default:
            break;
    }
    if (st == net::Status::Playing && g.phase == Phase::MatchEnd) {
        shadowText(g_session.myAgain() ? "WAITING" : "A = AGAIN", cfg::SCREEN_W / 2, 70, 2, col(255, 255, 255));
        if (g_session.oppAgain() && !g_session.myAgain()) {
            shadowText("OPP READY", cfg::SCREEN_W / 2, 90, 1, col(120, 255, 120));
        }
    }
    drawExitHold();
}

#ifdef DEBUG_REMOTE
void debugLine() {
    static const char* names[] = {"SEARCH", "READY", "COUNT", "PLAY", "LOST", "LEFT", "TIMEOUT"};
    const Game& g = g_session.game();
    Serial.printf("NET role=%d st=%s rtt=%d score=%d:%d phase=%d rxdrop=%u\n", static_cast<int>(g_session.role()),
                  names[static_cast<int>(g_session.status())], g_session.rttMs(), g.players[0].score,
                  g.players[1].score, static_cast<int>(g.phase),
                  static_cast<unsigned>(net_espnow::overflowCount()));
}

void debugSetRxLoss(int pct) { g_lossPct = pct; }
#endif

}  // namespace versus
