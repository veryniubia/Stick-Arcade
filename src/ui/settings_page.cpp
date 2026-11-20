#include "ui/settings_page.h"

#include <M5Unified.h>
#include <math.h>

#include "core/debug.h"
#include "core/display.h"
#include "core/input.h"
#include "core/platform.h"
#include "core/settings.h"
#include "core/sound.h"
#include "ui/gfx.h"

namespace settings_page {

namespace {

using platform::rgb;

enum Row { kSound, kBright, kAim, kCalibrate, kBattery, kVersion, kReset, kBack, kRowCount };

constexpr int kVisible = 5;
constexpr int kRowH = 20;
constexpr int kTop = 16;
constexpr float kCalibWarmupSec = 0.5f;
constexpr float kCalibSec = 2.0f;
constexpr uint32_t kNoteMs = 1200;

int g_sel = 0;
bool g_calibrating = false;
float g_calibT = 0.0f;
float g_calibSum = 0.0f;
int g_calibN = 0;
uint32_t g_noteUntil = 0;
const char* g_note = "";

void note(const char* text) {
    g_note = text;
    g_noteUntil = millis() + kNoteMs;
}

void valueText(int row, char* out, size_t n) {
    const settings::Data& s = settings::get();
    switch (row) {
        case kSound:
            if (s.volume == 0) snprintf(out, n, "OFF");
            else snprintf(out, n, "%d/%d", s.volume, settings::kVolumeLevels - 1);
            break;
        case kBright: snprintf(out, n, "%d/%d", s.bright + 1, settings::kBrightLevels); break;
        case kAim: snprintf(out, n, "%.1f", settings::kAimValue[s.aim]); break;
        case kCalibrate: snprintf(out, n, "%+.1f", s.tiltOffsetX10 / 10.0f); break;
        case kBattery: {
            const int level = M5.Power.getBatteryLevel();
            const bool usb = M5.Power.getVBUSVoltage() > 4000;
            if (level < 0) snprintf(out, n, "%s", usb ? "USB" : "--");
            else snprintf(out, n, "%d%%%s", level, usb ? " USB" : "");
            break;
        }
        case kVersion: snprintf(out, n, "%s", platform::kVersion); break;
        default: out[0] = 0; break;
    }
}

const char* labelOf(int row) {
    static const char* kLabels[kRowCount] = {"SOUND", "BRIGHT", "AIM", "CALIBRATE", "BATTERY", "VERSION", "RESET", "BACK"};
    return kLabels[row];
}

const char* actionOf(int row) {
    switch (row) {
        case kSound: case kBright: case kAim: return "CHANGE";
        case kCalibrate: return "SET";
        case kReset: return "RESET";
        case kBack: return "BACK";
        default: return nullptr;
    }
}

}  // namespace

void begin() {
    g_sel = 0;
    g_calibrating = false;
    g_noteUntil = 0;
}

bool update(float dt) {
    if (g_calibrating) {
        g_calibT += dt;
        if (g_calibT > kCalibWarmupSec) {
            g_calibSum += input::rawTiltDeg();
            ++g_calibN;
        }
        if (g_calibT >= kCalibSec) {
            g_calibrating = false;
            float deg = g_calibN ? g_calibSum / g_calibN : 0.0f;
            if (deg > 40.0f) deg = 40.0f;
            if (deg < -40.0f) deg = -40.0f;
            settings::Data s = settings::get();
            s.tiltOffsetX10 = static_cast<int16_t>(lroundf(deg * 10.0f));
            settings::set(s);
            sound::playUi(sound::Ui::Select);
            note("DONE");
        }
        return false;
    }

    if (input::bPressed() || debug::bPress()) {
        g_sel = (g_sel + 1) % kRowCount;
        sound::playUi(sound::Ui::Move);
    }
    if (!(input::aPressed() || debug::aPress())) return false;

    settings::Data s = settings::get();
    switch (g_sel) {
        case kSound:
            s.volume = static_cast<uint8_t>((s.volume + 1) % settings::kVolumeLevels);
            settings::set(s);
            sound::playUi(sound::Ui::Select);  // lets the user hear the new volume
            break;
        case kBright:
            s.bright = static_cast<uint8_t>((s.bright + 1) % settings::kBrightLevels);
            settings::set(s);
            sound::playUi(sound::Ui::Select);
            break;
        case kAim:
            s.aim = static_cast<uint8_t>((s.aim + 1) % settings::kAimLevels);
            settings::set(s);
            sound::playUi(sound::Ui::Select);
            break;
        case kCalibrate:
            g_calibrating = true;
            g_calibT = 0.0f;
            g_calibSum = 0.0f;
            g_calibN = 0;
            sound::playUi(sound::Ui::Move);
            break;
        case kReset:
            settings::resetDefaults();
            sound::playUi(sound::Ui::Select);
            note("DONE");
            break;
        case kBack:
            sound::playUi(sound::Ui::Select);
            return true;
        default:
            sound::playUi(sound::Ui::Denied);
            break;
    }
    return false;
}

void draw() {
    auto& c = display::canvas();
    c.fillRect(0, 0, platform::kScreenW, platform::kHintY, rgb(20, 22, 36));
    const bool noteOn = !g_calibrating && millis() < g_noteUntil;
    ui::shadowText(noteOn ? g_note : "SETTINGS", platform::kScreenW / 2, 7, 1, noteOn ? ui::green() : ui::yellow());

    int first = g_sel - kVisible / 2;
    if (first < 0) first = 0;
    if (first > kRowCount - kVisible) first = kRowCount - kVisible;

    char value[16];
    c.setTextSize(2);
    for (int i = 0; i < kVisible; ++i) {
        const int row = first + i;
        const int y = kTop + i * kRowH;
        const bool sel = row == g_sel;
        if (sel) {
            c.fillRoundRect(10, y - 1, platform::kScreenW - 20, kRowH - 2, 4, rgb(40, 44, 70));
            c.fillTriangle(3, y + 2, 3, y + 12, 8, y + 7, ui::yellow());
        }
        c.setTextDatum(middle_left);
        c.setTextColor(ui::kWhite);
        c.drawString(labelOf(row), 16, y + 8);
        valueText(row, value, sizeof(value));
        c.setTextDatum(middle_right);
        c.setTextColor(sel ? ui::yellow() : rgb(190, 192, 200));
        c.drawString(value, platform::kScreenW - 16, y + 8);
    }
    c.setTextSize(1);

    // Scroll marks
    if (first > 0) c.fillTriangle(232, 20, 236, 20, 234, 17, rgb(150, 154, 176));
    if (first + kVisible < kRowCount) c.fillTriangle(232, 112, 236, 112, 234, 115, rgb(150, 154, 176));

    if (g_calibrating) {
        const int w = 150, h = 46;
        const int x = (platform::kScreenW - w) / 2;
        c.fillRoundRect(x, 36, w, h, 5, rgb(24, 20, 26));
        c.drawRoundRect(x, 36, w, h, 5, ui::kWhite);
        ui::shadowText("KEEP LEVEL", platform::kScreenW / 2, 52, 2, ui::yellow());
        const float f = g_calibT / kCalibSec;
        c.drawRect(x + 10, 66, w - 20, 8, ui::kWhite);
        c.fillRect(x + 11, 67, static_cast<int>((w - 22) * (f > 1.0f ? 1.0f : f)), 6, ui::yellow());
    }

    ui::hintBar("NEXT", actionOf(g_sel));
}

}  // namespace settings_page
