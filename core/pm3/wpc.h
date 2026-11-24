// WPC: sprite sheets (up to 4 8bpp GDT), sprite/rect/group/anim tables (ABP block 1) and slot
// scripts (ABP block 2).  Port of py/wpc_meta.py, py/wpc_script.py, py/wpc_play.py, py/wpc_render.py.
// Coordinates in rects / slot positions / anim offsets are half-resolution units; screen = value * 2.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "pm3/abp.h"
#include "pm3/image.h"

namespace pm3 {

constexpr int kWpcScale = 2;

struct WpcRect {          // 10 x u16  [dstx,dsty,0,0,srcx,srcy,w,h,0,prio]
    uint16_t dstx, dsty, img, z3, srcx, srcy, w, h, z8, prio;
};
struct WpcObject {        // 6 x u32 sheet ("sprite object") record
    uint32_t anim0;       // first anim entry
    uint32_t animN;       // number of anim entries
    uint32_t v2, v3;
    uint32_t group0;      // first group of this object
    uint32_t groupN;
};
struct WpcGroup { uint32_t n, first; };
struct WpcAnim { uint16_t group; uint16_t ticks; int16_t dx, dy; };

struct Wpc {
    Image sheets[4];                  // RGBA, black pixels transparent; w == 0 -> sheet absent
    std::vector<WpcObject> objs;
    std::vector<WpcRect> rects;
    std::vector<WpcGroup> groups;
    std::vector<WpcAnim> anims;
    Bytes code;                       // block 2 bytecode (8-byte instructions)
    std::vector<uint16_t> starts;     // per slot start offset, 0xFFFF = unused slot
};

// Throws std::runtime_error on malformed data.
Wpc wpc_load(const uint8_t* r, size_t n);

struct WpcItem { int param, slot, obj, cur, x, y; };       // one drawn sprite object in a frame
struct WpcFx { int tick, slot, op, a, b, c; };             // particle/overlay ops (logged only)
struct WpcRun {
    std::vector<std::vector<WpcItem>> frames;              // one entry per tick
    std::vector<WpcFx> fx;
};

// Deterministic simulation (slot scripts + movement + sprite animation). seed feeds a
// CPython-compatible Mersenne Twister so results match py/wpc_play.py exactly.
WpcRun wpc_run(const Wpc& w, int ticks, uint32_t seed);

struct WpcBounds { int x0 = 0, y0 = 0, x1 = 0, y1 = 0; bool valid = false; };  // screen px (x2 units)
WpcBounds wpc_bounds(const Wpc& w, const std::vector<std::vector<WpcItem>>& frames, size_t first = 0,
                     size_t step = 1);
// Draws one frame into `canvas` (RGBA) whose top-left is at screen position (b.x0, b.y0).
void wpc_draw(const Wpc& w, const std::vector<WpcItem>& items, Image& canvas, int ox, int oy);

}  // namespace pm3
