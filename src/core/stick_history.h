#pragma once

#include <stdint.h>

// Remembers the last stick positions so a press of the stick can be given the direction the stick had just before
// the press: pushing the stick in nudges it, so the reading at the moment of the press is not what the player meant.
// Hardware independent.
namespace arcade {

class StickHistory {
public:
    static constexpr int kSize = 16;
    static constexpr uint32_t kLookBackMs = 100;  // use the position this long before the press
    static constexpr uint32_t kMaxAgeMs = 400;    // older samples are no longer "just before"

    void clear() { count_ = 0; head_ = 0; }

    void push(uint32_t ms, int8_t x, int8_t y) {
        t_[head_] = ms; x_[head_] = x; y_[head_] = y;
        head_ = (head_ + 1) % kSize;
        if (count_ < kSize) ++count_;
    }

    // Direction to use for a press detected at `ms`: the newest sample at least kLookBackMs old; failing that the
    // oldest one still recent enough; failing that, centred.
    void latch(uint32_t ms, int8_t& x, int8_t& y) const {
        x = y = 0;
        int best = -1;
        for (int n = 0; n < count_; ++n) {  // newest first
            const int i = (head_ - 1 - n + 2 * kSize) % kSize;
            const uint32_t age = ms - t_[i];
            if (age > kMaxAgeMs) break;
            best = i;
            if (age >= kLookBackMs) break;
        }
        if (best >= 0) { x = x_[best]; y = y_[best]; }
    }

private:
    uint32_t t_[kSize] = {};
    int8_t x_[kSize] = {};
    int8_t y_[kSize] = {};
    int head_ = 0, count_ = 0;
};

}  // namespace arcade
