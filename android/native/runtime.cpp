#include <android/bitmap.h>
#include <android/multinetwork.h>
#include <jni.h>
#include <ctime>
#include <cstdlib>
#include <cerrno>
#include "core/display.h"
#include "core/input.h"
#include "core/joystick.h"
#include "net/phone_socket.h"
#include "net/radio.h"
#include "ui/play.h"

void setup();
void loop();
void suspendGame();
namespace display { int targetFps(); }
namespace sim { void openAudio(); void closeAudio(); }

namespace {
phone::Socket udp;
net_handle_t route = 0;
uint32_t nextAttempt = 0;
int socketError = 0;
bool initialized = false;

uint64_t timeUs() {
    timespec ts{};
    clock_gettime(CLOCK_BOOTTIME, &ts);
    return uint64_t(ts.tv_sec)*1000000 + ts.tv_nsec/1000;
}
void openRoute() {
    udp.fd = socket(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC, 0);
    // A brief Wi-Fi loss can then recover with the same six-byte session identity.
    auto local = phone::address("0.0.0.0");
    if (udp.fd >= 0 && android_setsocknetwork(route, udp.fd) == 0 &&
        bind(udp.fd, (sockaddr*)&local, sizeof(local)) == 0) {
        auto gateway = phone::address(phone::kGateway);
        udp.reset(false, phone::endpoint(gateway));
        socketError = 0;
        return;
    }
    socketError = errno;
    udp.stop();
    nextAttempt = millis() + 1000;
}
}
uint32_t micros() { return uint32_t(timeUs()); }
uint32_t millis() { return uint32_t(timeUs()/1000); }
uint32_t esp_random() { return arc4random(); }

namespace radio {
void tick() {
    if (route && udp.fd < 0 && int32_t(millis() - nextAttempt) >= 0) openRoute();
    if (udp.fd >= 0) udp.service(millis());
}
// A disconnected lobby still runs discovery so a later Wi-Fi join can pair.
bool beginGame() { udp.clearInbox(); return true; }
void endGame() { udp.clearInbox(); }
net::Transport& transport() { return udp; }
bool poll(uint8_t id[6], uint8_t* data, size_t* len) { return udp.poll(id, data, len); }
uint32_t overflowCount() { return 0; }
}

// The Android UI thread owns the game, the socket and all input callbacks.
extern "C" JNIEXPORT void JNICALL Java_dev_stickarcade_player_Arcade_resume(JNIEnv* env, jclass, jstring path) {
    if (!initialized) {
        const char* p = env->GetStringUTFChars(path, nullptr);
        setenv("STICKS3_SETTINGS", p, 1);
        env->ReleaseStringUTFChars(path, p);
        setup();
        initialized = true;
    }
    sim::openAudio();
}
extern "C" JNIEXPORT void JNICALL Java_dev_stickarcade_player_Arcade_pause(JNIEnv*, jclass) {
    suspendGame(); // Send BYE before closing the bound socket.
    sim::controls = {};
    M5.BtnA = {}; M5.BtnB = {};
    joystick::update();
    M5.Imu.fresh = 0;
    M5.Imu.sample = {};
    route = 0;
    udp.stop();
    sim::closeAudio();
}
extern "C" JNIEXPORT void JNICALL Java_dev_stickarcade_player_Arcade_network(JNIEnv*, jclass, jlong handle) {
    if (net_handle_t(handle) == route) return;
    udp.stop();
    route = net_handle_t(handle);
    socketError = 0;
    nextAttempt = millis();
}
extern "C" JNIEXPORT void JNICALL Java_dev_stickarcade_player_Arcade_heading(JNIEnv*, jclass, jfloat deg, jlong ns) {
    input::headingSample(deg, uint32_t(ns / 1000000));
}
extern "C" JNIEXPORT void JNICALL Java_dev_stickarcade_player_Arcade_motion(JNIEnv*, jclass,
        jboolean gyro, jfloat x, jfloat y, jfloat z, jlong ns, jboolean invertWheelAxis) {
    auto& imu = M5.Imu;
    imu.sample.usec = uint32_t(ns / 1000);
    if (gyro) imu.sample.gyro = {x,y,z};
    else imu.sample.accel = {x,y,z};
    imu.fresh = gyro ? m5::IMU_Class::sensor_mask_gyro : m5::IMU_Class::sensor_mask_accel;
    input::updateMotion(uint32_t(ns / 1000000), invertWheelAxis);
}
extern "C" JNIEXPORT void JNICALL Java_dev_stickarcade_player_Arcade_controls(JNIEnv*, jclass,
        jboolean a, jboolean b, jboolean jump, jint x, jint y) {
    auto& ctl = sim::controls;
    M5.BtnA.pending |= a && !ctl.a;
    M5.BtnB.pending |= b && !ctl.b;
    ctl.a = a; ctl.b = b;
    ctl.stick = jump;
    ctl.joyX = x; ctl.joyY = y;
    joystick::update();
}
extern "C" JNIEXPORT void JNICALL Java_dev_stickarcade_player_Arcade_clearControls(JNIEnv*, jclass) {
    sim::controls = {};
    M5.BtnA = {}; M5.BtnB = {};
    joystick::update();
}
extern "C" JNIEXPORT jint JNICALL Java_dev_stickarcade_player_Arcade_step(JNIEnv*, jclass) {
    loop();
    return display::targetFps();
}
extern "C" JNIEXPORT jint JNICALL Java_dev_stickarcade_player_Arcade_width(JNIEnv*, jclass) { return display::width(); }
extern "C" JNIEXPORT jboolean JNICALL Java_dev_stickarcade_player_Arcade_stick(JNIEnv*, jclass) { return play::needsJoystick(); }
extern "C" JNIEXPORT jint JNICALL Java_dev_stickarcade_player_Arcade_brightness(JNIEnv*, jclass) { return M5.Display.brightness; }
extern "C" JNIEXPORT void JNICALL Java_dev_stickarcade_player_Arcade_battery(JNIEnv*, jclass, jint pct, jboolean usb) {
    M5.Power.level = pct;
    M5.Power.usbMv = usb ? 5000 : 0;
}
extern "C" JNIEXPORT void JNICALL Java_dev_stickarcade_player_Arcade_blit(JNIEnv* env, jclass, jobject bitmap) {
    AndroidBitmapInfo info{};
    void* pixels = nullptr;
    if (AndroidBitmap_getInfo(env, bitmap, &info) || info.format != ANDROID_BITMAP_FORMAT_RGB_565 ||
        int(info.width) != display::width() || int(info.height) != display::height() ||
        AndroidBitmap_lockPixels(env, bitmap, &pixels)) return;
    const auto* src = static_cast<const uint16_t*>(display::lastFrame());
    for (unsigned y=0; y<info.height; ++y)
        memcpy(static_cast<uint8_t*>(pixels)+y*info.stride, src+y*info.width, info.width*2);
    AndroidBitmap_unlockPixels(env, bitmap);
}
extern "C" JNIEXPORT jstring JNICALL Java_dev_stickarcade_player_Arcade_status(JNIEnv* env, jclass) {
    char text[144], ping[16] = "--";
    const int rtt = play::rttMs();
    if (rtt >= 0) snprintf(ping, sizeof(ping), "%d ms", rtt);
    const char* link = !route ? "Offline" : socketError ? "Socket error" :
        udp.connected(millis()) ? "UDP OK" : "Wi-Fi / probing";
    snprintf(text, sizeof(text), "%s | %s %s %s | RTT %s", link, play::gameName(), play::stateText(), play::roleText(), ping);
    if (socketError) snprintf(text+strlen(text), sizeof(text)-strlen(text), " (errno %d)", socketError);
    if (play::needsJoystick()) snprintf(text+strlen(text), sizeof(text)-strlen(text), " | %s %.2f",
                                        input::hasHeading() ? "HDG" : "GYRO", input::lookYawDeg());
    return env->NewStringUTF(text);
}
