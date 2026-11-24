#include "pm3/wmp.h"

#include <stdexcept>

namespace pm3 {
namespace {
uint32_t u32(const uint8_t* p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
uint16_t u16(const uint8_t* p) { return (uint16_t)(p[0] | (p[1] << 8)); }
}  // namespace

WmpInfo wmp_parse(const uint8_t* r, size_t len) {
    if (len < 24) throw std::runtime_error("WMP too short");
    WmpInfo w; w.agp_off = u32(r);
    const size_t b = 8; w.n = r[b + 2]; w.flag = r[b + 3];
    size_t a = w.agp_off; if (a > len) throw std::runtime_error("bad WMP offset");
    size_t found = 0; uint32_t ng = 0;
    for (size_t p = b + 4 + w.n; p < b + 4 + w.n + 16 && p + 12 <= len; p++) {
        uint32_t g = u32(r + p);
        if (g > 0 && g < 1000 && p + 4 + 8 * (size_t)g <= a && u32(r + p + 8) == p + 4 + 8 * g - b) { found = p; ng = g; break; }
    }
    if (!found) throw std::runtime_error("WMP groups not found");
    w.ids.assign(r + b + 4, r + b + 4 + w.n);
    for (uint32_t i = 0; i < ng; i++) {
        uint32_t nr = u32(r + found + 4 + i * 8), off = u32(r + found + 8 + i * 8);
        std::vector<WmpRec> g;
        for (uint32_t j = 0; j < nr; j++) {
            size_t q = b + off + (size_t)j * 20; if (q + 20 > len) throw std::runtime_error("WMP rec out of range");
            WmpRec rc; uint16_t* f = &rc.dstx; for (int k = 0; k < 10; k++) f[k] = u16(r + q + k * 2);
            g.push_back(rc);
        }
        w.groups.push_back(std::move(g));
    }
    return w;
}

}  // namespace pm3
