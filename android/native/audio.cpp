#include <aaudio/AAudio.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <mutex>
#include "sim.h"

namespace sim {
namespace {
AAudioStream* stream = nullptr;
int deviceRate = 48000;
std::mutex guard;
struct Voice {
    const uint8_t* sample = nullptr; // Game PCM is static.
    unsigned length = 0;
    double cursor = 0, step = 0, gain = 0;
};
Voice voices[3];

aaudio_data_callback_result_t render(AAudioStream*, void*, void* output, int32_t frames) {
    auto* samples = static_cast<float*>(output);
    std::fill_n(samples, frames, 0.f);
    std::unique_lock<std::mutex> lock(guard, std::try_to_lock);
    if (!lock.owns_lock()) return AAUDIO_CALLBACK_RESULT_CONTINUE;
    for (auto& v : voices) {
        for (int i=0; i<frames && v.cursor < v.length; ++i, v.cursor+=v.step) {
            const double amplitude = v.sample ? (v.sample[unsigned(v.cursor)]-128)/128.0
                                              : 0.35*std::sin(v.cursor*6.283185307);
            samples[i] = std::clamp(float(samples[i]+v.gain*amplitude), -1.f, 1.f);
        }
    }
    return AAUDIO_CALLBACK_RESULT_CONTINUE;
}
}
void openAudio() {
    if (stream) return;
    AAudioStreamBuilder* b = nullptr;
    if (AAudio_createStreamBuilder(&b) != AAUDIO_OK) return;
    AAudioStreamBuilder_setFormat(b, AAUDIO_FORMAT_PCM_FLOAT);
    AAudioStreamBuilder_setChannelCount(b, 1);
    AAudioStreamBuilder_setDataCallback(b, render, nullptr);
    if (AAudioStreamBuilder_openStream(b, &stream) == AAUDIO_OK) {
        deviceRate = AAudioStream_getSampleRate(stream);
        AAudioStream_requestStart(stream);
    }
    AAudioStreamBuilder_delete(b);
}
void audioStop() {
    std::lock_guard<std::mutex> lock(guard);
    for (auto& v : voices) v = {};
}
void closeAudio() {
    if (stream) { AAudioStream_close(stream); stream = nullptr; }
    audioStop();
}
void audioTone(unsigned hz, unsigned ms, unsigned volume) {
    std::lock_guard<std::mutex> lock(guard);
    voices[0] = {nullptr, (hz*ms+999)/1000, 0, double(hz)/deviceRate, volume/255.0};
}
void audioSample(const uint8_t* data, unsigned n, unsigned rate, unsigned volume, int ch) {
    if (ch < 0 || ch >= 3) return;
    std::lock_guard<std::mutex> lock(guard);
    voices[ch] = {data, n, 0, double(rate)/deviceRate, volume/255.0};
}
}
