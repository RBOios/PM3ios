#pragma once
#include <cstdint>
namespace pm3 {
// CPython random.Random(seed) compatible MT19937 (seed < 2^32), randrange(n) for n >= 1.
class PyRandom {
public:
    explicit PyRandom(uint32_t seed) {
        init_genrand(19650218u);
        uint32_t key[1] = {seed};
        const int klen = 1;
        int i = 1, j = 0;
        for (int k = 624 > klen ? 624 : klen; k; k--) {
            mt_[i] = (mt_[i] ^ ((mt_[i - 1] ^ (mt_[i - 1] >> 30)) * 1664525u)) + key[j] + (uint32_t)j;
            i++; j++;
            if (i >= 624) { mt_[0] = mt_[623]; i = 1; }
            if (j >= klen) j = 0;
        }
        for (int k = 623; k; k--) {
            mt_[i] = (mt_[i] ^ ((mt_[i - 1] ^ (mt_[i - 1] >> 30)) * 1566083941u)) - (uint32_t)i;
            i++;
            if (i >= 624) { mt_[0] = mt_[623]; i = 1; }
        }
        mt_[0] = 0x80000000u;
        idx_ = 624;
    }
    uint32_t randrange(uint32_t n) {  // n >= 2
        int k = 0;
        for (uint32_t t = n; t; t >>= 1) k++;
        uint32_t r = next() >> (32 - k);
        while (r >= n) r = next() >> (32 - k);
        return r;
    }

private:
    void init_genrand(uint32_t s) {
        mt_[0] = s;
        for (int i = 1; i < 624; i++) mt_[i] = 1812433253u * (mt_[i - 1] ^ (mt_[i - 1] >> 30)) + (uint32_t)i;
        idx_ = 624;
    }
    uint32_t next() {
        if (idx_ >= 624) {
            for (int k = 0; k < 624; k++) {
                uint32_t y = (mt_[k] & 0x80000000u) | (mt_[(k + 1) % 624] & 0x7fffffffu);
                mt_[k] = mt_[(k + 397) % 624] ^ (y >> 1) ^ ((y & 1) ? 0x9908b0dfu : 0);
            }
            idx_ = 0;
        }
        uint32_t y = mt_[idx_++];
        y ^= y >> 11;
        y ^= (y << 7) & 0x9d2c5680u;
        y ^= (y << 15) & 0xefc60000u;
        y ^= y >> 18;
        return y;
    }
    uint32_t mt_[624];
    int idx_;
};
}  // namespace pm3
