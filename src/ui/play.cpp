#include "ui/play.h"

#include <M5Unified.h>
#include <algorithm>
#include <esp_random.h>

#include "core/debug.h"
#include "core/display.h"
#include "core/input.h"
#include "core/joystick.h"
#include "core/offline_trace.h"
#include "core/platform.h"
#include "core/settings.h"
#include "core/sound.h"
#include "net/radio.h"
#include "net/net_session.h"
#include "ui/gfx.h"
#include "ui/launcher.h"

namespace play {

namespace {

using platform::rgb;

enum class Calibration { Off, Prompt, Release, Sampling };
Calibration g_cal = Calibration::Off;
bool g_scoped = false, g_trackingB = false;
uint32_t g_scopePressMs = 0;
uint8_t g_calA = 0, g_calJump = 0;

enum class Stage : uint8_t { Lobby, Solo, Online };

constexpr uint32_t kClosedShowMs = 1500;
constexpr uint32_t kPingShowMs = 5000;
constexpr uint32_t kSoloHintMs = 10000;   // after this long without an opponent the SOLO hint starts to blink
constexpr uint32_t kOnlineExitAskMs = 3000;
constexpr uint32_t kJoinWindowMs = 10000; // how long a solo player stays joinable after pressing A on OPP FOUND
constexpr uint32_t kRandomStickProbeMs = 700;

const registry::GameEntry* g_entry = nullptr;
arcade::GameInstance g_gi;
net::Session g_session;
bool g_radioOk = false;
bool g_randomSearching = false;
bool g_randomSoloPending = false;
uint32_t g_noEligibleSince = 0;
Stage g_stage = Stage::Lobby;

uint32_t g_beginMs = 0;
uint32_t g_closedAtMs = 0;
uint32_t g_playingSinceMs = 0;
uint32_t g_joinUntilMs = 0;
int g_lastDigit = -1;
net::Status g_lastStatus = net::Status::Searching;
float g_accum = 0.0f;

bool g_bArmed = true;
bool g_aArmed = false;  // A must have been released once after entering, so the press that opened the game is not a charge

bool g_exitAsk = false;
uint32_t g_exitAskMs = 0;
bool g_frozenA = false;
bool g_frozenB = false;
uint8_t g_frozenACount = 0;
uint8_t g_frozenBCount = 0;
uint8_t g_aSkip = 0;  // presses made while the confirmation was open belong to the confirmation, not to the game
uint8_t g_bSkip = 0;
uint8_t g_frozenJumpCount = 0;
uint8_t g_jumpSkip = 0;
uint32_t g_stickLostMs = 0;  // when a game that needs the joystick noticed it was gone (0 = present)

arcade::IGame& game() { return *g_gi.game; }

bool liveB() {
    const auto& info=*g_entry->info;
    return (input::bHeld() && (g_bArmed || info.tiltWheel)) || debug::braking() ||
           (!info.portrait && !info.tiltWheel && debug::charging());
}

// Hands over the local player's hands. While the exit confirmation is open the held keys are frozen so that a
// charge in progress neither fires nor stops, and presses made for the confirmation never reach the game.
PadInput makePad() {
    PadInput p;
    if (g_entry->info->needsJoystick) {
        p.aim = input::lookYawDeg();
        p.pitch = input::lookPitchDeg();
        p.jumpX = joystick::pressX();
        p.jumpY = joystick::pressY();
        if (!g_exitAsk) {
            p.stickX = joystick::x();
            p.stickY = joystick::y();
            p.jumpCount = joystick::pressCount() - g_jumpSkip;
        } else {
            p.jumpCount = g_frozenJumpCount - g_jumpSkip;
        }
    } else if (g_entry->info->tiltWheel) {
        p.aim = debug::aim(input::wheelTiltDeg());
    } else {
        p.aim = debug::aim(g_entry->info->portrait ? input::driveTiltDeg() : input::tiltDeg() * settings::aimGain());
    }
    if (g_exitAsk) {
        p.a = g_frozenA;
        p.b = g_frozenB;
        p.aCount = g_frozenACount - g_aSkip;
        p.bCount = g_frozenBCount - g_bSkip;
    } else {
        p.a = (input::aHeld() && g_aArmed) || debug::charging();
        p.b = liveB();
        p.aCount = input::aCount() - g_aSkip;
        p.bCount = input::bCount() + debug::extraB() - g_bSkip;
    }
    return p;
}

void openExitAsk(uint32_t now) {
    offline_trace::record("exit_prompt");
    g_exitAsk = true;
    g_exitAskMs = now;
    g_frozenA = (input::aHeld() && g_aArmed) || debug::charging();
    g_frozenB = liveB();
    g_frozenACount = input::aCount();
    g_frozenBCount = input::bCount() + debug::extraB();
    g_frozenJumpCount = joystick::pressCount();
}

void closeExitAsk() {
    g_exitAsk = false;
    const uint8_t aPresses = input::aCount() - g_frozenACount;
    const uint8_t bPresses = input::bCount() + debug::extraB() - g_frozenBCount;
    g_aSkip += aPresses;
    g_bSkip += bPresses;
    g_jumpSkip += joystick::pressCount() - g_frozenJumpCount;
    // A new press used in the dialog must also stay out of play while held.
    if (aPresses && input::aHeld()) g_aArmed = false;
    if (bPresses && input::bHeld()) g_bArmed = false;
}

void pumpRadio(uint32_t now) {
    if (!g_radioOk) return;
    uint8_t mac[6];
    uint8_t buf[250];
    size_t len = 0;
    for (int i = 0; i < 24 && radio::poll(mac, buf, &len); ++i) {
#ifdef DEBUG_REMOTE
        if (debug::g.lossPct > 0 && static_cast<int>(esp_random() % 100) < debug::g.lossPct) continue;
#endif
        g_session.onReceive(mac, buf, len, now);
    }
}

void startSolo() {
    offline_trace::record("solo");
    if (g_entry->info->portrait && !g_entry->info->tiltWheel) g_bArmed = false;
    display::setPortrait(g_entry->info->portrait);
    game().begin(arcade::Mode::Solo, esp_random());
    // The B press that selected SOLO belongs to the waiting screen, as do older press counters.
    const PadInput pads[2] = {makePad(), PadInput()};
    game().prime(pads);
    g_stage = Stage::Solo;
    g_accum = 0.0f;
    g_joinUntilMs = 0;
    g_exitAsk = false;
    g_session.setJoinable(false);
}

void enterOnline() {
    display::setPortrait(g_entry->info->portrait);
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
        if (shake && g_cal == Calibration::Off) openExitAsk(now);
        return false;
    }
    if (aPress || input::aCount() != g_frozenACount) {
        closeExitAsk();
        return true;
    }
    if (bPress || (g_stage == Stage::Online && now - g_exitAskMs > kOnlineExitAskMs)) closeExitAsk();
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
    c.drawString(buf, display::width() - 3, 7);
    c.setTextColor(ui::kWhite);
    c.drawString(buf, display::width() - 4, 6);
}

// SOLO marker: coloured bar along the top and a tag, so nobody mistakes it for an online match.
void drawSoloMark() {
    auto& c = display::canvas();
    const uint16_t orange = rgb(255, 150, 40);
    c.fillRect(0, 0, display::width(), 3, orange);
    c.fillRoundRect(5, 5, 29, 11, 3, orange);
    c.setTextSize(1);
    c.setTextDatum(middle_center);
    c.setTextColor(ui::kBlack);
    c.drawString("SOLO", 19, 11);
}

void drawBox(int w, int h, int y) {
    w = std::min(w, display::width()-8);
    auto& c = display::canvas();
    const int x = (display::width() - w) / 2;
    c.fillRoundRect(x, y, w, h, 5, rgb(24, 20, 26));
    c.drawRoundRect(x, y, w, h, 5, ui::kWhite);
}

void updateCalibration(bool bPress) {
    switch (g_cal) {
        case Calibration::Prompt:
            if (bPress) g_cal = Calibration::Release;
            break;
        case Calibration::Release:
            if (!input::bHeld()) {
                input::calibrateLook();
                g_cal = Calibration::Sampling;
            }
            break;
        case Calibration::Sampling:
            if (input::calibrationProgress() >= 1) g_cal = Calibration::Off;
            break;
        case Calibration::Off: break;
    }
}

void drawCalibration() {
    drawBox(196, 58, 39);
    const bool sampling = g_cal == Calibration::Sampling;
    ui::shadowText(sampling ? "CENTERING" : "FACE FORWARD", 120, 54, 2, ui::yellow());
    ui::shadowText(sampling ? "MEASURING GYRO BIAS" :
                   g_cal == Calibration::Release ? "RELEASE B" : "TAP B TO CENTER",
                   120, 75, 1, ui::kWhite);
    if (sampling) {
        auto& c = display::canvas();
        c.drawRect(60, 84, 120, 5, ui::kWhite);
        c.fillRect(61, 85, static_cast<int>(118 * input::calibrationProgress()), 3, ui::green());
    }
}

void drawExitAsk() {
    auto& c = display::canvas();
    if (g_entry->info->portrait) {
        drawBox(126, 54, 91);
        ui::shadowText("EXIT?", 67, 105, 2, ui::yellow());
        ui::shadowText("A YES  B NO", 67, 130, 1, ui::kWhite);
        return;
    }
    const int cx=display::width()/2;
    const int y=display::height()/2-25;
    drawBox(140, 50, y);
    ui::shadowText("EXIT?", cx, y+14, 2, ui::yellow());
    ui::shadowText("A YES   B NO", cx, y+37, 1, ui::kWhite);
}

void drawLobby() {
    const uint32_t ms = millis();
    if (g_randomSearching) {
        display::canvas().fillScreen(rgb(20, 22, 36));
        launcher::drawRandomCard(60, 75, 120, 40, ms);
    } else {
        g_entry->drawLobby(ms);
    }
    ui::shadowText(g_randomSearching ? "RANDOM" : g_entry->name, display::width() / 2, 20, 2, ui::yellow());
    ui::shadowText(g_radioOk ? "SEARCHING" : "RADIO OFF", display::width() / 2, 52, 2, ui::kWhite);
    if (g_radioOk) drawDots(display::width() / 2 - 12, 68, ms - g_beginMs);
    if (g_randomSearching && g_noEligibleSince && ms - g_noEligibleSince < 1600) {
        ui::shadowText("NO JOYSTICK FOUND", display::width() / 2, 106, 1, ui::red());
    }
    ui::hintBar("SOLO", "BACK", ms - g_beginMs > kSoloHintMs || !g_radioOk);
}

void drawOnlineOverlays(uint32_t now) {
    const net::Status st = g_session.status();
    const bool pre = st == net::Status::Ready || st == net::Status::Countdown;
    const bool earlyPlay = g_playingSinceMs != 0 && now - g_playingSinceMs < kPingShowMs;
    if (pre || earlyPlay) drawPing(g_session);

    const int cx = display::width() / 2;
    const int overlaySize = g_entry->info->portrait ? 1 : 2;
    switch (st) {
        case net::Status::Ready:
            if (game().gridPreviewSeconds()>0) break;
            ui::shadowText("READY", cx, 40, 3, ui::yellow());
            break;
        case net::Status::Countdown: {
            if (game().gridPreviewSeconds()>0) break; // game draws its grid sequence
            char d[2] = {static_cast<char>('0' + static_cast<int>(g_session.countdownSec() + 0.99f)), 0};
            ui::shadowText(d, cx, 44, 5, ui::yellow());
            break;
        }
        case net::Status::LinkLost:
            ui::shadowText("LINK LOST", cx, 40, overlaySize, ui::red());
            break;
        case net::Status::PeerLeft:
            ui::shadowText("OPP LEFT", cx, 40, overlaySize, ui::red());
            break;
        case net::Status::TimedOut:
            ui::shadowText("TIMEOUT", cx, 40, overlaySize, ui::red());
            break;
        default:
            break;
    }
    if (st == net::Status::Playing && game().phase() == arcade::MatchPhase::MatchEnd) {
        ui::shadowText(g_session.myAgain() ? "WAITING" : "A = AGAIN", cx, g_entry->info->portrait ? 149 : 70, overlaySize, ui::kWhite);
        if (g_session.oppAgain() && !g_session.myAgain()) ui::shadowText("OPP READY", cx, g_entry->info->portrait ? 164 : 90, 1, ui::green());
    }
}

void drawSoloOverlays(uint32_t now) {
    drawSoloMark();
    if (!g_radioOk) return;
    if (g_joinUntilMs != 0) {
        ui::shadowText("JOINING", display::width() / 2, 96, 1, ui::green());
    } else if (g_session.peerNearby(now) && game().idle(0)) {
        ui::shadowText("OPP FOUND", display::width() / 2, 82, 2, ui::green());
        ui::shadowText("A = JOIN", display::width() / 2, 100, 1, ui::kWhite);
    }
}

}  // namespace

static void resetPlay() {
    debug::releaseControls();
    display::setPortrait(false);
    g_stage = Stage::Lobby;
    g_scoped = g_trackingB = false;
    g_cal = Calibration::Off;
    g_randomSearching = g_randomSoloPending = false;
    g_noEligibleSince = 0;
    g_beginMs = millis();
    g_closedAtMs = 0;
    g_playingSinceMs = 0;
    g_joinUntilMs = 0;
    g_lastDigit = -1;
    g_lastStatus = net::Status::Searching;
    g_aArmed = false;
    g_bArmed = true;
    g_exitAsk = false;
    g_aSkip = g_bSkip = 0;
    g_jumpSkip = 0;
    g_stickLostMs = 0;
    g_radioOk = false;
}

static void load(const registry::GameEntry& entry) {
    g_entry = &entry;
    g_gi = arcade::gameCreate(*entry.info);
    g_cal = entry.info->gyroScope ? Calibration::Prompt : Calibration::Off;
    offline_trace::record("lobby", entry.info->wireId);
    if (entry.info->gyroScope) {
        const float gain = settings::aimGain();
        input::beginLook(0.33f * gain, gain);
    }
}

static arcade::IGame* loadRandomGame(uint16_t key) {
    for (int i = 0; i < registry::count(); ++i) {
        const auto& entry = registry::get(i);
        if (net::gameKey(*entry.info) != key) continue;
        joystick::setPower(entry.info->needsJoystick);
        load(entry);
        return g_gi.game;
    }
    return nullptr;
}

void begin(const registry::GameEntry& entry) {
    resetPlay();
    load(entry);
    if (!g_gi.game) {
        offline_trace::record("game_alloc_fail");
        return;
    }
    g_radioOk = radio::beginGame();
    offline_trace::record("radio", g_radioOk);
    if (g_radioOk) {
        g_session.begin(&radio::transport(), esp_random(), millis(), *entry.info, g_gi.game);
        g_session.setJoinable(true);
    }
}

void beginRandom() {
    resetPlay();
    g_randomSearching = true;
    joystick::setPower(true);
    g_radioOk = radio::beginGame();
    offline_trace::record("radio", g_radioOk);
    if (g_radioOk) {
        g_session.beginRandom(&radio::transport(), esp_random(), millis(), esp_random, loadRandomGame);
    }
}

void end() {
    offline_trace::record("play_end");
    debug::releaseControls();
    input::endLook();
    display::setPortrait(false);
    if (g_radioOk) g_session.leave();
    radio::endGame();
    g_entry = nullptr;
    g_radioOk = false;
    g_randomSearching = g_randomSoloPending = false;
    arcade::gameDestroy(g_gi);
    display::setPortrait(false);
}

static bool updateRandom(uint32_t now) {
    if (input::aPressed() || debug::aPress()) return true;
    if (!joystick::ok() && now - g_beginMs < kRandomStickProbeMs) {
        g_randomSoloPending |= input::bPressed() || debug::bPress();
        return false;
    }

    uint16_t keys[net::kRandomCapacity];
    size_t count = 0;
    for (int i = 0; i < registry::count() && count < net::kRandomCapacity; ++i) {
        const auto& info = *registry::get(i).info;
        if (!info.needsJoystick || joystick::ok()) keys[count++] = net::gameKey(info);
    }
    if (g_radioOk) {
        g_session.setRandomPool(keys, count);
        pumpRadio(now);
        if (g_session.status() != net::Status::Searching) {
            if (!g_gi.game) return true;
            g_randomSearching = g_randomSoloPending = false;
            enterOnline();
            return false;
        }
        g_session.update(now, {});
    }
    if (!g_randomSoloPending && !input::bPressed() && !debug::bPress()) return false;
    g_randomSoloPending = false;
    if (!count) {
        g_noEligibleSince = now;
        sound::playUi(sound::Ui::Denied);
        return false;
    }
    g_randomSearching = false;
    if (g_radioOk) radio::endGame();
    g_radioOk = false;
    if (!loadRandomGame(keys[net::randomIndex(count, esp_random)])) return true;
    if (g_cal == Calibration::Off) startSolo();
    else g_randomSoloPending = true;
    return false;
}

bool update(float dt) {
    const uint32_t now = millis();
    if (g_randomSearching) return updateRandom(now);
    if (!g_gi.game) return true;  // out of memory: back to the launcher
    // A game that needs the joystick cannot go on without it: say so, tell the opponent, and go back.
    if (g_entry->info->needsJoystick && !joystick::ok()) {
        if (g_stickLostMs == 0) {
            g_stickLostMs = now;
            offline_trace::record("stick_lost", static_cast<int>(g_stage));
            sound::playUi(sound::Ui::Denied);
            if (g_stage == Stage::Online) g_session.leave();
        }
        return now - g_stickLostMs > kClosedShowMs;
    }
    bool aPress = input::aPressed() || debug::aPress();
    bool bPress = input::bPressed() || debug::bPress();
    const bool shake = input::shaken() || debug::shake();
    if (!input::aHeld()) g_aArmed = true;
    if (!input::bHeld()) g_bArmed = true;

    const bool askWasOpen = g_exitAsk;

    net::LocalInput li;
    li.pad = makePad();

    if (g_stage == Stage::Lobby) {
        // Calibrate before advertising or accepting a peer. Both devices reach the
        // existing matchmaking handshake only after their local pose is ready.
        if (g_cal != Calibration::Off) {
            updateCalibration(bPress);
            if (g_randomSoloPending && g_cal == Calibration::Off && !aPress) {
                g_randomSoloPending = false;
                startSolo();
            }
            return aPress;
        }
        pumpRadio(now);
        if (g_radioOk) {
            g_session.update(now, li);
            if (g_session.status() != net::Status::Searching) {
                enterOnline();
                return false;
            }
        }
        if (aPress) {
            offline_trace::record("lobby_back");
            return true;
        }
        if (bPress) startSolo();
        return false;
    }

    // Solo and online share the exit confirmation; while it is open A and B belong to it.
    const bool confirmed = updateExitAsk(now, shake, aPress, bPress);
    if (askWasOpen) aPress = bPress = false;
    if (confirmed) {
        offline_trace::record("exit_confirm", g_stage == Stage::Online);
        if (g_stage == Stage::Online) g_session.leave();
        return true;
    }
    bool blockControls = g_cal != Calibration::Off;
    if (g_entry->info->gyroScope) {
        const bool playing = g_stage == Stage::Solo || g_session.status() == net::Status::Playing;
        if (blockControls) {
            updateCalibration(bPress);
        } else if (!playing || g_exitAsk || askWasOpen || game().phase() != arcade::MatchPhase::Playing) {
            g_trackingB = false;
        } else if (bPress) {
            g_trackingB = true;
            g_scopePressMs = now;
        } else if (g_trackingB && now - g_scopePressMs >= 650) {
            g_trackingB = false;
            g_scoped = false;
            g_cal = Calibration::Release;
            // Suppress actions on the frame that crosses the hold threshold as well.
            g_calA = input::aCount() - uint8_t(input::aPressed());
            g_calJump = joystick::pressCount();
            blockControls = true;
        } else if (g_trackingB && !input::bHeld()) {
            g_trackingB = false;
            g_scoped = !g_scoped;
        }
    }
    pumpRadio(now);
    li.pad = makePad();
    if (blockControls) {
        // Consume presses during calibration, including the completion frame.
        g_aSkip += uint8_t(input::aCount() - g_calA);
        g_jumpSkip += uint8_t(joystick::pressCount() - g_calJump);
        g_calA = input::aCount();
        g_calJump = joystick::pressCount();
        li.pad = makePad();
        li.pad.stickX = li.pad.stickY = 0;
        li.pad.a = li.pad.b = false;
        aPress = false;
    }
    li.again = aPress;
    li.ready = g_cal == Calibration::Off;

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
        if (aPress && g_joinUntilMs == 0 && game().phase()==arcade::MatchPhase::MatchEnd && !g_exitAsk) {
            startSolo();
            return false;
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
        if (digit != g_lastDigit && digit > 0 && digit <= 3) sound::playUi(sound::Ui::Move);
        g_lastDigit = digit;
    }
    if (st == net::Status::Playing && g_lastStatus != net::Status::Playing && g_lastStatus != net::Status::LinkLost) {
        g_playingSinceMs = now;
        sound::playUi(sound::Ui::Refresh);
        g_scoped = false;
    }
    if (st != g_lastStatus) {
        offline_trace::record("online_status", static_cast<int>(st), static_cast<int>(g_session.role()));
        g_lastStatus = st;
    }

    if (st == net::Status::PeerLeft || st == net::Status::TimedOut) {
        if (g_closedAtMs == 0) {
            g_closedAtMs = now;
            sound::playUi(sound::Ui::Denied);
        }
        return now - g_closedAtMs > kClosedShowMs;
    }
    return false;
}

#ifdef __ANDROID__
const char* gameName() { return g_randomSearching ? "RANDOM" : g_entry ? g_entry->name : ""; }
int rttMs() { return g_radioOk && g_stage == Stage::Online ? g_session.rttMs() : -1; }
const char* stateText() {
    if (g_randomSearching) return "Searching";
    if (!g_entry) return "Menu";
    if (g_stage == Stage::Lobby) return "Lobby";
    if (g_stage == Stage::Solo) return "Solo";
    static const char* states[] = {"Searching","Ready","Countdown","Playing","Link lost","Peer left","Timeout"};
    return states[static_cast<int>(g_session.status())];
}
const char* roleText() {
    if (!g_entry || g_stage != Stage::Online) return "-";
    return g_session.role() == net::Role::Host ? "Host" : "Client";
}
bool needsJoystick() { return g_entry && g_entry->info->needsJoystick; }
#endif

void draw() {
    if (g_randomSearching) {
        drawLobby();
        return;
    }
    if (!g_gi.game) return;
    const uint32_t now = millis();
    arcade::DrawCtx ctx;
    ctx.nowMs = now;
    ctx.scoped = g_scoped;
    if (g_stage == Stage::Lobby) {
        if (g_cal != Calibration::Off) {
            g_entry->drawLobby(now);
            drawCalibration();
            if (g_stickLostMs) ui::shadowText("JOYSTICK LOST", 120, 110, 1, ui::red());
        } else drawLobby();
        return;
    }
    if (g_stage == Stage::Solo) {
        ctx.solo = true;
        game().draw(0, ctx);
        drawSoloOverlays(now);
    } else {
        const net::Status st=g_session.status();
        if (st==net::Status::Ready) ctx.countdownLeft=3.f+game().gridPreviewSeconds();
        if (st==net::Status::Countdown) ctx.countdownLeft=std::max(.01f,g_session.countdownSec());
        game().draw(g_session.localIndex(), ctx);
        drawOnlineOverlays(now);
    }
    if (g_cal != Calibration::Off) drawCalibration();
    if (g_exitAsk) drawExitAsk();
    if (g_stickLostMs != 0) {
        const int cx = display::width() / 2;
        drawBox(150, 44, 46);
        ui::shadowText("JOYSTICK LOST", cx, 62, 1, ui::red());
        ui::shadowText("BACK TO MENU", cx, 78, 1, ui::kWhite);
    }
}

#ifdef DEBUG_REMOTE
void debugLine() {
    static const char* names[] = {"SEARCH", "READY", "COUNT", "PLAY", "LOST", "LEFT", "TIMEOUT"};
    if (!g_gi.game || !g_radioOk) return;
    Serial.printf("NET role=%d st=%s rtt=%d stage=%d phase=%d rxdrop=%u nearby=%d\n",
                  static_cast<int>(g_session.role()), names[static_cast<int>(g_session.status())], g_session.rttMs(),
                  static_cast<int>(g_stage), static_cast<int>(game().phase()),
                  static_cast<unsigned>(radio::overflowCount()), g_session.peerNearby(millis()) ? 1 : 0);
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
