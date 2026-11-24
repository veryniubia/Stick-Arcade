#pragma once
#include <cstdio>
#include <cstdlib>
#include <string>
// Each virtual device has its own NVS file beside the executable.
class Preferences {
    std::string path_;
public:
    bool begin(const char*, bool) {
        const char* p = std::getenv("STICKS3_SETTINGS");
        path_ = p ? p : "sticks3-settings.bin"; return true;
    }
    size_t getBytes(const char*, void* out, size_t n) {
        FILE* f = std::fopen(path_.c_str(), "rb"); if (!f) return 0;
        size_t got = std::fread(out, 1, n, f); std::fclose(f); return got;
    }
    size_t putBytes(const char*, const void* data, size_t n) {
        FILE* f = std::fopen(path_.c_str(), "wb"); if (!f) return 0;
        size_t put = std::fwrite(data, 1, n, f); std::fclose(f); return put;
    }
};
