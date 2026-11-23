// MSB-first bit reader over little-endian 32-bit words (shared by ABP and GDT/AGP codecs).
// Matches the Python reference: when fewer than 4 bytes remain, the word reads as 0 (and keeps
// reading 0 forever), so truncated input terminates instead of faulting.
#pragma once
#include <algorithm>
#include <cstddef>
#include <cstdint>

namespace pm3 {

class BitReader {
public:
    BitReader(const uint8_t* buf, size_t len, size_t pos) : b_(buf), n_(len), p_(pos) {}

    // n in [0, 24]
    uint32_t bits(int n) {
        uint32_t v = 0;
        while (n > 0) {
            if (left_ == 0) fill();
            int t = std::min(n, left_);
            uint32_t part = (cur_ >> (left_ - t)) & ((1u << t) - 1u);
            v = (v << t) | part;
            left_ -= t;
            n -= t;
        }
        return v;
    }
    uint32_t bit() { return bits(1); }

private:
    void fill() {
        if (p_ + 4 <= n_) {
            cur_ = (uint32_t)b_[p_] | ((uint32_t)b_[p_ + 1] << 8) | ((uint32_t)b_[p_ + 2] << 16) |
                   ((uint32_t)b_[p_ + 3] << 24);
        } else {
            cur_ = 0;
        }
        p_ += 4;
        left_ = 32;
    }
    const uint8_t* b_;
    size_t n_, p_;
    uint32_t cur_ = 0;
    int left_ = 0;
};

}  // namespace pm3
