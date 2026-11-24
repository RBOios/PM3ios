// WBD bust-up portraits (port of py/wbd_render.py).
// Entry: u32 a (sheet offset) | ABP_ block 1 (parts) | ABP_ block 2 (timeline) | @a: 8bpp GDT sheet (384x480),
// colour key (255,0,255). Group 0 = fixed layers (rec 0 = body, rec 1.. = overlays), group k>=1 = face variant k.
#pragma once
#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

#include "pm3/image.h"

namespace pm3 {

struct WbdRec { uint16_t dstx, dsty, z2, z3, srcx, srcy, w, h, z8, z9; };

struct Wbd {
    Image sheet;                                   // RGBA, key colour transparent
    std::vector<std::vector<WbdRec>> groups;
    std::vector<uint8_t> b2;
    std::vector<uint32_t> tab;                     // timeline offsets
    // (frame, ticks) pairs of timeline `i` (0xFE ends, 0xFD skipped); ticks are 1/30 s.
    std::vector<std::pair<int, int>> seq(size_t i) const;
    // Union bbox of every rec of every group: x0,y0,x1,y1
    void bbox(int& x0, int& y0, int& x1, int& y1) const;
    // Renders `frame` into an RGBA image (transparent background) of bbox size.
    Image render(int frame) const;
};

Wbd wbd_load(const uint8_t* r, size_t n);          // throws std::runtime_error
bool write_pam(const char* path, const Image& im); // RGBA, for pixel comparison

}  // namespace pm3
