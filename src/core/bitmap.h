#pragma once

#include <stddef.h>
#include <stdint.h>

// Palette bitmaps for card art and other pictures: at most 16 colours, 4 bits per pixel (two pixels per byte,
// high nibble first), optionally run-length coded. Made by tools/png2c.py, stored as const arrays in flash.
// RLE format: pairs of bytes (count 1..255, packed byte); the packed byte is repeated `count` times.
namespace arcade {

struct Bitmap {
    uint16_t w;
    uint16_t h;
    const uint16_t* palette;  // RGB565, `colors` entries
    uint8_t colors;
    const uint8_t* data;
    size_t size;  // bytes in `data`
    bool rle;
};

namespace detail {

// Sequential reader of the 4-bit pixel stream (undoes the RLE when needed).
class PixelReader {
public:
    explicit PixelReader(const Bitmap& b) : b_(&b) {}

    uint8_t next() {
        if (!haveNibble_) {
            packed_ = nextByte();
            haveNibble_ = true;
            return packed_ >> 4;
        }
        haveNibble_ = false;
        return packed_ & 0x0F;
    }

private:
    const Bitmap* b_;
    size_t pos_ = 0;
    uint8_t run_ = 0;
    uint8_t value_ = 0;
    uint8_t packed_ = 0;
    bool haveNibble_ = false;

    uint8_t nextByte() {
        if (!b_->rle) return pos_ < b_->size ? b_->data[pos_++] : 0;
        if (run_ == 0) {
            if (pos_ + 1 < b_->size) {
                run_ = b_->data[pos_];
                value_ = b_->data[pos_ + 1];
                pos_ += 2;
            } else {
                return 0;  // data ended early: the rest stays colour 0
            }
        }
        --run_;
        return value_;
    }
};

}  // namespace detail

// Calls f(x, y, length, colourIndex) for every horizontal run of equal colour, row by row.
template <class F>
void forEachRun(const Bitmap& b, F&& f) {
    detail::PixelReader r(b);
    for (int y = 0; y < b.h; ++y) {
        int x = 0;
        while (x < b.w) {
            const uint8_t c = r.next();
            int len = 1;
            // Peek by reading ahead: runs are merged while the next pixel has the same colour.
            while (x + len < b.w) {
                detail::PixelReader probe = r;
                if (probe.next() != c) break;
                r = probe;
                ++len;
            }
            f(x, y, len, c < b.colors ? c : 0);
            x += len;
        }
    }
}

}  // namespace arcade
