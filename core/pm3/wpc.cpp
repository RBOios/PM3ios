#include "pm3/wpc.h"
#include "pm3/pyrandom.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <map>
#include <stdexcept>

namespace pm3 {
namespace {

[[noreturn]] void bad(const char* m) { throw std::runtime_error(m); }

uint16_t u16(const uint8_t* p) { return (uint16_t)(p[0] | (p[1] << 8)); }
uint32_t u32(const uint8_t* p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

using pm3::PyRandom;


inline int64_t floordiv(int64_t a, int64_t b) {  // Python // semantics (b > 0)
    int64_t q = a / b;
    if ((a % b) != 0 && (a < 0)) q--;
    return q;
}

struct Slot {
    int obj = -1;
    int64_t x = 0, y = 0, tx = 0, ty = 0, vx = 0, vy = 0;
    int cnt = 0, fl = 0;
    int start = 0, base = 0, count = 1, last = 0, param = 0, pc = 0;
};
struct ObjState { int a0, an, cur, cnt; };

}  // namespace

Wpc wpc_load(const uint8_t* r, size_t n) {
    if (n < 24) bad("WPC too small");
    Wpc w;
    // sheets
    uint32_t offs[4];
    for (int i = 0; i < 4; i++) offs[i] = u32(r + i * 4);
    for (int i = 0; i < 4; i++) {
        if (!offs[i]) continue;
        if (offs[i] >= n) bad("bad sheet offset");
        Image im = decode_gdt8(r + offs[i], n - offs[i]).toRgba();
        for (size_t p = 0; p + 3 < im.rgba.size(); p += 4)
            if (im.rgba[p] == 0 && im.rgba[p + 1] == 0 && im.rgba[p + 2] == 0) im.rgba[p + 3] = 0;
        w.sheets[i] = std::move(im);
    }
    // ABP blocks: block 1 at +20, block 2 at the next "ABP_" occurrence
    std::vector<size_t> ps;
    for (size_t i = 0; i + 4 <= n; i++)
        if (std::memcmp(r + i, "ABP_", 4) == 0) ps.push_back(i);
    if (ps.empty() || ps[0] != 20) bad("block 1 not at +20");
    Bytes b1, b2;
    abp_decompress(r + 20, n - 20, b1);
    if (ps.size() > 1) abp_decompress(r + ps[1], n - ps[1], b2);

    if (b1.size() < 0x10) bad("blk1 too small");
    unsigned A = u16(&b1[8]), nr = u16(&b1[10]), ng = u16(&b1[12]), na = u16(&b1[14]);
    size_t p = 0x10;
    if (p + 24 * A + 20 * nr + 8 * ng + 8 * na > b1.size()) bad("blk1 tables truncated");
    for (unsigned i = 0; i < A; i++, p += 24) {
        WpcObject o;
        o.anim0 = u32(&b1[p]); o.animN = u32(&b1[p + 4]); o.v2 = u32(&b1[p + 8]); o.v3 = u32(&b1[p + 12]);
        o.group0 = u32(&b1[p + 16]); o.groupN = u32(&b1[p + 20]);
        w.objs.push_back(o);
    }
    for (unsigned i = 0; i < nr; i++, p += 20) {
        const uint8_t* q = &b1[p];
        w.rects.push_back({u16(q), u16(q + 2), u16(q + 4), u16(q + 6), u16(q + 8), u16(q + 10), u16(q + 12),
                           u16(q + 14), u16(q + 16), u16(q + 18)});
    }
    for (unsigned i = 0; i < ng; i++, p += 8) w.groups.push_back({u32(&b1[p]), u32(&b1[p + 4])});
    for (unsigned i = 0; i < na; i++, p += 8)
        w.anims.push_back({u16(&b1[p]), u16(&b1[p + 2]), (int16_t)u16(&b1[p + 4]), (int16_t)u16(&b1[p + 6])});

    // block 2: +10 u16 n (0 -> 16), +16 n x u16 starts, then code
    if (b2.size() >= 16) {
        unsigned ns = u16(&b2[10]);
        if (ns == 0) ns = 16;
        if (16 + 2 * ns <= b2.size()) {
            for (unsigned i = 0; i < ns; i++) w.starts.push_back(u16(&b2[16 + 2 * i]));
            w.code.assign(b2.begin() + 16 + 2 * ns, b2.end());
        }
    }
    return w;
}

WpcRun wpc_run(const Wpc& w, int ticks, uint32_t seed) {
    WpcRun out;
    PyRandom rnd(seed);
    const Bytes& code = w.code;
    const int ncode = (int)code.size();
    const int n = (int)w.starts.size();
    std::map<int, std::vector<int>> marks;
    std::map<int, Slot> S;
    for (int i = 0; i < n; i++) {
        if (w.starts[i] == 0xFFFF) continue;
        int nxt = i + 1 < n ? w.starts[i + 1] : 0xFFFF;
        int end = nxt == 0xFFFF ? ncode : nxt;
        std::vector<int> m;
        for (int p = w.starts[i]; p < std::min(end, ncode - 7); p += 8)
            if (code[p] == 7) m.push_back(p);
        if (m.empty()) m.push_back(w.starts[i]);
        marks[i] = std::move(m);
    }
    std::vector<ObjState> objs;
    for (auto& o : w.objs) objs.push_back({(int)o.anim0, (int)o.animN, (int)o.anim0, 1});

    auto animTicks = [&](int cur) { return cur >= 0 && cur < (int)w.anims.size() ? std::max(1, (w.anims[cur].ticks & 0xFF) >> 1) : 1; };
    auto pick = [&](int i) {
        Slot& s = S[i];
        int k = s.count;
        int idx = s.base + (k > 1 ? (int)rnd.randrange((uint32_t)k) : 0);
        s.last = idx;
        auto& m = marks[i];
        s.pc = m[std::min<int>(idx, (int)m.size() - 1)];
        s.vx = s.vy = 0;
        s.cnt = 0;
    };
    for (auto& kv : marks) {
        Slot s;
        s.start = w.starts[kv.first];
        s.count = std::max(1, (int)kv.second.size());
        S[kv.first] = s;
        pick(kv.first);
    }
    auto show = [&](int i, int o) {
        if (!S.count(i) || o >= (int)objs.size()) return;
        S[i].obj = o;
        ObjState& ob = objs[o];
        ob.cur = ob.a0;
        ob.cnt = animTicks(ob.cur);
    };

    for (int t = 0; t < ticks; t++) {
        for (auto& kv : S) {
            const int i = kv.first;
            Slot& s = kv.second;
            if (s.fl & 0x200) {
                s.fl &= ~0x200;
                if (!(s.fl & 0x400)) pick(i);
            }
            s.fl |= 4;
            int guard = 0;
            while ((s.fl & 4) && guard < 64) {
                guard++;
                if (s.pc < 0 || s.pc + 8 > ncode) { s.fl &= ~4; break; }
                const uint8_t* ins = &code[s.pc];
                int op = ins[0], A = u16(ins + 2), B = u16(ins + 4), C = ins[6];
                s.fl &= ~4;
                switch (op) {
                case 7: s.pc += 8; s.fl |= 4; break;
                case 1: s.x = (int64_t)(A - 128) << 12; s.y = (int64_t)(B - 128) << 12; s.pc += 8; s.fl |= 5; s.vx = s.vy = 0; break;
                case 2: {
                    s.tx = (int64_t)(A - 128) << 12; s.ty = (int64_t)(B - 128) << 12; show(i, C);
                    int64_t dx = std::llabs(s.tx - s.x) >> 12, dy = std::llabs(s.ty - s.y) >> 12;
                    int64_t mx = std::max<int64_t>({dx, dy, 1});
                    int64_t sx = s.tx > s.x ? 1 : -1, sy = s.ty > s.y ? 1 : -1;
                    s.vx = dx ? floordiv(sx * (dx << 11), mx) : 0;
                    s.vy = dy ? floordiv(sy * (dy << 11), mx) : 0;
                    s.pc += 8;
                    break;
                }
                case 3:
                    if (((((s.x + 0x800) ^ s.tx) & ~(int64_t)0xFFF) == 0) && ((((s.y + 0x800) ^ s.ty) & ~(int64_t)0xFFF) == 0)) {
                        s.pc += 8; s.fl &= ~8;
                    }
                    break;
                case 4:
                    if (s.fl & 1) {
                        s.fl &= ~1; s.cnt = A; show(i, B);
                        if (A == 0) { s.fl |= 0x800; break; }
                        s.fl |= 0x20;
                    }
                    if (s.fl & 0x800) break;
                    s.cnt -= 1;
                    if (s.cnt <= 0) { s.fl &= ~0x20; s.pc += 8; s.fl |= 1; }
                    break;
                case 5:
                    if (s.fl & 1) { s.fl = (s.fl & ~1) | 2; s.cnt = A; show(i, B); }
                    s.cnt -= 1;
                    if (s.cnt <= 0) { s.pc += 8; s.fl = (s.fl & ~2) | 1; }
                    break;
                case 8: s.pc = s.start; s.fl |= 0x204; break;
                case 9: s.pc = s.start; s.fl |= 4; break;
                case 0xA:
                    if (s.fl & 1) s.fl = (s.fl & 0xFF7E) | 0x40;
                    else if (s.fl & 0x80) { s.pc += 8; s.fl = (s.fl & 0xFF37) | 5; }
                    break;
                case 0x10:
                    if (s.fl & 1) s.fl = ((s.fl & ~1) & 0xFFFF) | 0x1040;
                    break;
                case 0xB: show(A, B); s.pc += 8; s.fl |= 4; break;
                case 0xC:
                    if (S.count(A)) S[A].param = B;
                    s.pc += 8; s.fl |= 4;
                    break;
                case 0xD:
                    if (S.count(A)) {
                        Slot& t_ = S[A];
                        auto& m = marks[A];
                        t_.pc = m[std::min<int>(ins[4], (int)m.size() - 1)];
                        t_.fl = (t_.fl & 0xE795) | 0x401;
                        t_.vx = t_.vy = 0;
                    }
                    s.pc += 8; s.fl |= 4;
                    break;
                case 0xF: s.base = A & 0xFF; s.count = std::max(1, (ins[4] - (A & 0xFF)) + 1); s.pc += 8; s.fl |= 4; break;
                case 0x12:
                    if (S.count(A)) S[A].fl &= ~0x400;
                    s.pc += 8; s.fl |= 4;
                    break;
                case 0x13: {
                    auto& m = marks[i];
                    s.pc = m[std::min<int>(s.last, (int)m.size() - 1)];
                    s.fl |= 4;
                    break;
                }
                case 0xE:
                    if (S.count(A)) S[A].fl &= ~0x100;
                    s.pc += 8; s.fl |= 4;
                    break;
                case 0x14: s.pc += 8; s.fl |= 4; break;
                case 0x11: case 0x15: case 0x16: case 0x17: case 0x18: case 0x1B: case 0x1C: case 0x1D:
                    out.fx.push_back({t, i, op, A, B, (int)u16(ins + 6)});
                    s.pc += 8; s.fl |= 4;
                    break;
                default: s.pc += 8; s.fl |= 4; break;
                }
            }
        }
        // movement + animation
        for (auto& kv : S) {
            Slot& s = kv.second;
            if (s.vx || s.vy) {
                if ((((s.x + 0x800) ^ s.tx) & ~(int64_t)0xFFF) || (((s.y + 0x800) ^ s.ty) & ~(int64_t)0xFFF)) {
                    s.x += s.vx; s.y += s.vy;
                } else {
                    s.vx = s.vy = 0;
                }
            }
            if (s.obj >= 0) {
                ObjState& ob = objs[s.obj];
                ob.cnt -= 1;
                if (ob.cnt <= 0) {
                    ob.cur += 1;
                    if (ob.cur >= ob.a0 + ob.an) {
                        s.fl |= 0x80;
                        if (s.fl & 0x40) ob.cur -= 1;
                        else ob.cur = ob.a0;
                    }
                    ob.cnt = animTicks(ob.cur);
                }
            }
        }
        std::vector<WpcItem> fr;
        for (auto& kv : S) {
            const Slot& s = kv.second;
            if (s.obj >= 0) fr.push_back({s.param, kv.first, s.obj, objs[s.obj].cur, (int)(s.x >> 12), (int)(s.y >> 12)});
        }
        out.frames.push_back(std::move(fr));
    }
    return out;
}

namespace {
struct GroupInfo { bool ok; int bx, by, bw, bh; size_t first, n; };

GroupInfo group_info(const Wpc& w, int g) {
    GroupInfo gi{false, 0, 0, 0, 0, 0, 0};
    if (g < 0 || g >= (int)w.groups.size()) return gi;
    size_t first = w.groups[g].first, n = w.groups[g].n;
    if (first > w.rects.size()) return gi;
    n = std::min(n, w.rects.size() - first);
    if (!n) return gi;
    int x0 = 1 << 30, y0 = 1 << 30, x1 = -(1 << 30), y1 = -(1 << 30);
    for (size_t k = 0; k < n; k++) {
        const WpcRect& r = w.rects[first + k];
        x0 = std::min<int>(x0, r.dstx); y0 = std::min<int>(y0, r.dsty);
        x1 = std::max<int>(x1, r.dstx + r.w); y1 = std::max<int>(y1, r.dsty + r.h);
    }
    gi = {true, x0 * kWpcScale, y0 * kWpcScale, (x1 - x0) * kWpcScale, (y1 - y0) * kWpcScale, first, n};
    return gi;
}
}  // namespace

WpcBounds wpc_bounds(const Wpc& w, const std::vector<std::vector<WpcItem>>& frames, size_t first, size_t step) {
    WpcBounds b;
    int bx0 = 1 << 30, by0 = 1 << 30, bx1 = -(1 << 30), by1 = -(1 << 30);
    for (size_t f = first; f < frames.size(); f += step) {
        for (const WpcItem& it : frames[f]) {
            const WpcAnim& a = w.anims[it.cur];
            GroupInfo gi = group_info(w, (int)a.group + (int)w.objs[it.obj].group0);
            if (!gi.ok) continue;
            int x = gi.bx + (it.x + a.dx) * kWpcScale, y = gi.by + (it.y + a.dy) * kWpcScale;
            bx0 = std::min(bx0, x); by0 = std::min(by0, y);
            bx1 = std::max(bx1, x + gi.bw); by1 = std::max(by1, y + gi.bh);
        }
    }
    if (bx0 <= bx1) { b.x0 = bx0; b.y0 = by0; b.x1 = bx1; b.y1 = by1; b.valid = true; }
    return b;
}

void wpc_draw(const Wpc& w, const std::vector<WpcItem>& items, Image& canvas, int ox, int oy) {
    std::vector<const WpcItem*> order;
    for (auto& it : items) order.push_back(&it);
    std::stable_sort(order.begin(), order.end(), [](const WpcItem* a, const WpcItem* b) {
        return a->param != b->param ? a->param < b->param : a->slot < b->slot;
    });
    const int S = kWpcScale;
    for (const WpcItem* it : order) {
        const WpcAnim& a = w.anims[it->cur];
        GroupInfo gi = group_info(w, (int)a.group + (int)w.objs[it->obj].group0);
        if (!gi.ok) continue;
        for (size_t k = 0; k < gi.n; k++) {
            const WpcRect& r = w.rects[gi.first + k];
            const Image* sh = (r.img < 4 && w.sheets[r.img].w) ? &w.sheets[r.img] : (w.sheets[0].w ? &w.sheets[0] : nullptr);
            if (!sh) continue;
            int dx0 = (r.dstx + it->x + a.dx) * S - ox, dy0 = (r.dsty + it->y + a.dy) * S - oy;
            int sx0 = r.srcx * S, sy0 = r.srcy * S, cw = r.w * S, ch = r.h * S;
            for (int yy = 0; yy < ch; yy++) {
                int cy = dy0 + yy, sy = sy0 + yy;
                if (cy < 0 || cy >= canvas.h || sy < 0 || sy >= sh->h) continue;
                for (int xx = 0; xx < cw; xx++) {
                    int cx = dx0 + xx, sx = sx0 + xx;
                    if (cx < 0 || cx >= canvas.w || sx < 0 || sx >= sh->w) continue;
                    const uint8_t* sp = &sh->rgba[((size_t)sy * sh->w + sx) * 4];
                    if (!sp[3]) continue;
                    uint8_t* dp = &canvas.rgba[((size_t)cy * canvas.w + cx) * 4];
                    dp[0] = sp[0]; dp[1] = sp[1]; dp[2] = sp[2]; dp[3] = 255;
                }
            }
        }
    }
}

}  // namespace pm3
