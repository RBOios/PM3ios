#include "pm3/abp.h"

#include <cstring>

#include "pm3/bitreader.h"

namespace pm3 {

bool abp_decompress(const uint8_t* buf, size_t len, Bytes& out, uint32_t* declared) {
    out.clear();
    if (len < 0x1C || std::memcmp(buf, "ABP_", 4) != 0) return false;
    uint32_t usz = (uint32_t)buf[4] | ((uint32_t)buf[5] << 8) | ((uint32_t)buf[6] << 16) |
                   ((uint32_t)buf[7] << 24);
    if (declared) *declared = usz;
    out.reserve(usz + 16);

    BitReader br(buf, len, 0x18);
    uint8_t ring[4096] = {0};
    unsigned r = 1;
    const size_t limit = (size_t)usz + 0x10000;
    for (;;) {
        if (br.bit()) {
            uint8_t c = (uint8_t)br.bits(8);
            out.push_back(c);
            ring[r] = c;
            r = (r + 1) & 0xFFF;
        } else {
            unsigned pos = br.bits(12);
            if (pos == 0) break;
            unsigned ln = br.bits(4) + 2;
            for (unsigned i = 0; i < ln; i++) {
                uint8_t c = ring[(pos + i) & 0xFFF];
                out.push_back(c);
                ring[r] = c;
                r = (r + 1) & 0xFFF;
            }
        }
        if (out.size() > limit) break;
    }
    return true;
}

}  // namespace pm3
