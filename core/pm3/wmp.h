// WMP background scene metadata (port of py/wmplib.py).  Payload image = decode_wmp().
#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>

namespace pm3 {

struct WmpRec { uint16_t dstx, dsty, page, z3, srcx, srcy, w, h, z8, z9; };  // blit (page*256+srcx, srcy, w, h) -> dst
struct WmpInfo {
    uint32_t agp_off = 0;                   // offset just after 'AGP_' of the 24bpp image
    int n = 0, flag = 0;
    std::vector<uint8_t> ids;
    std::vector<std::vector<WmpRec>> groups;  // group0 = background tiles, >=1 = animated overlay frames
};
// Throws std::runtime_error on malformed data.
WmpInfo wmp_parse(const uint8_t* r, size_t n);

}  // namespace pm3
