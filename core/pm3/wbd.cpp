#include "pm3/wbd.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <stdexcept>

#include "pm3/abp.h"

namespace pm3 {
namespace {
uint32_t u32(const uint8_t* p) { return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24); }
uint16_t u16(const uint8_t* p) { return (uint16_t)(p[0] | (p[1] << 8)); }
[[noreturn]] void bad(const char* m) { throw std::runtime_error(m); }
}  // namespace

Wbd wbd_load(const uint8_t* r, size_t n) {
    if (n < 28) bad("WBD too small");
    uint32_t a = u32(r);
    if (a >= n) bad("bad WBD sheet offset");
    std::vector<size_t> ps;
    for (size_t i = 0; i + 4 <= a; i++)
        if (std::memcmp(r + i, "ABP_", 4) == 0) ps.push_back(i);
    if (ps.size() < 2) bad("WBD blocks missing");
    Bytes b1, b2;
    abp_decompress(r + ps[0], n - ps[0], b1);
    abp_decompress(r + ps[1], n - ps[1], b2);

    Wbd w;
    Image im = decode_gdt8(r + a, n - a).toRgba();
    for (size_t p = 0; p + 3 < im.rgba.size(); p += 4)
        if (im.rgba[p] == 255 && im.rgba[p + 1] == 0 && im.rgba[p + 2] == 255) im.rgba[p + 3] = 0;
    w.sheet = std::move(im);

    if (b1.size() < 4) bad("blk1 too small");
    uint32_t ng = u32(&b1[0]);
    if (4 + (size_t)ng * 8 > b1.size()) bad("blk1 group table truncated");
    for (uint32_t g = 0; g < ng; g++) {
        uint32_t nr = u32(&b1[4 + 8 * g]), off = u32(&b1[8 + 8 * g]);
        if ((size_t)off + (size_t)nr * 20 > b1.size()) bad("blk1 recs truncated");
        std::vector<WbdRec> recs;
        for (uint32_t i = 0; i < nr; i++) {
            const uint8_t* q = &b1[off + 20 * i];
            WbdRec rc; uint16_t* f = &rc.dstx;
            for (int k = 0; k < 10; k++) f[k] = u16(q + 2 * k);
            recs.push_back(rc);
        }
        w.groups.push_back(std::move(recs));
    }
    if (b2.size() < 8) bad("blk2 too small");
    uint32_t tl = u32(&b2[4]) / 4;
    if ((size_t)tl * 4 > b2.size()) bad("blk2 table truncated");
    for (uint32_t i = 0; i < tl; i++) w.tab.push_back(u32(&b2[4 * i]));
    w.b2 = std::move(b2);
    return w;
}

std::vector<std::pair<int, int>> Wbd::seq(size_t i) const {
    std::vector<std::pair<int, int>> out;
    if (i >= tab.size()) return out;
    size_t p = tab[i];
    while (p + 1 < b2.size()) {
        int f = b2[p], t = b2[p + 1];
        if (f == 0xFE) break;
        if (f != 0xFD) out.push_back({f, t});
        p += 2;
    }
    return out;
}

void Wbd::bbox(int& x0, int& y0, int& x1, int& y1) const {
    x0 = y0 = 1 << 30; x1 = y1 = -(1 << 30);
    for (auto& g : groups) for (auto& r : g) {
        x0 = std::min<int>(x0, r.dstx); y0 = std::min<int>(y0, r.dsty);
        x1 = std::max<int>(x1, r.dstx + r.w); y1 = std::max<int>(y1, r.dsty + r.h);
    }
}

Image Wbd::render(int frame) const {
    int x0, y0, x1, y1; bbox(x0, y0, x1, y1);
    Image c; c.w = std::max(0, x1 - x0); c.h = std::max(0, y1 - y0);
    c.rgba.assign((size_t)c.w * c.h * 4, 0);
    auto blit = [&](const WbdRec& r) {
        for (int yy = 0; yy < r.h; yy++) {
            int cy = r.dsty - y0 + yy, sy = r.srcy + yy;
            if (cy < 0 || cy >= c.h) continue;
            for (int xx = 0; xx < r.w; xx++) {
                int cx = r.dstx - x0 + xx, sx = r.srcx + xx;
                if (cx < 0 || cx >= c.w || sx < 0 || sx >= sheet.w || sy < 0 || sy >= sheet.h) continue;
                const uint8_t* sp = &sheet.rgba[((size_t)sy * sheet.w + sx) * 4];
                if (!sp[3]) continue;
                std::memcpy(&c.rgba[((size_t)cy * c.w + cx) * 4], sp, 4);
            }
        }
    };
    if (groups.empty() || groups[0].empty()) return c;
    const auto& g0 = groups[0];
    blit(g0[0]);
    if (frame > 0 && frame < (int)groups.size())
        for (auto& r : groups[frame]) blit(r);
    for (size_t i = 1; i < g0.size(); i++) blit(g0[i]);
    return c;
}

bool write_pam(const char* path, const Image& im) {
    FILE* f = std::fopen(path, "wb");
    if (!f) return false;
    std::fprintf(f, "P7\nWIDTH %d\nHEIGHT %d\nDEPTH 4\nMAXVAL 255\nTUPLTYPE RGB_ALPHA\nENDHDR\n", im.w, im.h);
    std::fwrite(im.rgba.data(), 1, im.rgba.size(), f);
    std::fclose(f);
    return true;
}

}  // namespace pm3
