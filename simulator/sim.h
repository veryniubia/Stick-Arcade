#pragma once
#include <cstdint>
#include <string>
namespace sim {
struct Controls {
    bool a=false, b=false, stick=false, connected=true;
    float tilt=0, pitch=0, yaw=0;
    int joyX=0, joyY=0;
};
inline Controls controls;
inline uint64_t timeUs=1000000;
inline uint32_t shakeAt=0;
inline int device=1;
inline uint32_t seed=0;
inline bool headless=false;
inline std::string outputDir;
void present();
void audioTone(unsigned hz, unsigned ms, unsigned volume);
void audioSample(const uint8_t* data, unsigned n, unsigned rate, unsigned volume, int channel);
void audioStop();
void saveFrame(const std::string& path);
}
