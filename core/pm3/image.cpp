#include "pm3/image.h"

#include <cstdio>
#include <cstring>
#include <stdexcept>

#include "pm3/bitreader.h"

namespace pm3 {

namespace {

[[noreturn]] void bad(const char* m) { throw std::runtime_error(m); }

struct Rd {
    const uint8_t* p;
    size_t n;
    void need(size_t off, size_t len) const {
        if (off + len > n || off + len < off) bad("truncated data");
    }
    uint8_t u8(size_t o) const { need(o, 1); return p[o]; }
    uint16_t u16(size_t o) const { need(o, 2); return (uint16_t)(p[o] | (p[o + 1] << 8)); }
    uint32_t u32(size_t o) const {
        need(o, 4);
        return (uint32_t)p[o] | ((uint32_t)p[o + 1] << 8) | ((uint32_t)p[o + 2] << 16) |
               ((uint32_t)p[o + 3] << 24);
    }
    int32_t s32(size_t o) const { return (int32_t)u32(o); }
};

struct Off { int dx, dy; };

// --- entropy primitives (EXE 0x405a76 / 0x405b27) ---------------------------------------------
inline unsigned dec_index(BitReader& br) {
    if (br.bit() == 0) return br.bits(3);
    if (br.bit() == 0) return br.bits(4) + 8;
    return br.bits(6) + 0x18;
}

inline unsigned dec_value(BitReader& br, bool& esc) {
    esc = false;
    if (br.bit() == 0) return br.bit();
    if (br.bit() == 0) return br.bits(3) + 2;
    if (br.bit() == 0) return br.bits(5) + 10;
    if (br.bit() == 0) return br.bits(7) + 0x2A;
    if (br.bit() == 0) return br.bits(8) + 0xAA;
    esc = true;
    return br.bit() == 0 ? br.bits(8) : br.bits(16);
}

// Move-to-front list, one per context. The reference keeps a Python list that can grow when a
// decoded value exceeds its length (c = 0 is inserted without popping); std::vector mirrors that.
struct Mtf {
    std::vector<uint8_t> v;
};

// Shared MTF + neighbour-copy stream decoder. `img` must have slack (4096 bytes) after pitch*h.
void decode_stream(BitReader& br, std::vector<uint8_t>& img, int w, int h, int pitch,
                   const std::vector<Off>& tab, int minlen, int rows, int syms) {
    std::vector<Mtf> mtf(rows);
    for (int c = 0; c < rows; c++) {
        mtf[c].v.resize(syms);
        mtf[c].v.reserve(syms + 8);
        for (int s = 0; s < syms; s++) mtf[c].v[s] = (uint8_t)((s + c) % rows);
    }
    const long total = (long)img.size();
    for (int y = 0; y < h; y++) {
        int x = 0;
        unsigned prev = 0;
        long row = (long)y * pitch;
        while (x < w) {
            if (br.bit()) {
                unsigned idx = dec_index(br);
                bool esc;
                unsigned ln = dec_value(br, esc) + (unsigned)minlen;
                if (idx >= tab.size()) bad("neighbour index out of range");
                long src = row + x + tab[idx].dx - (long)tab[idx].dy * pitch;
                long dst = row + x;
                if (dst + (long)ln > total) bad("copy overruns image");
                for (unsigned i = 0; i < ln; i++) {
                    long s = src + i;
                    img[dst + i] = s >= 0 ? img[s] : 0;
                }
                x += (int)ln;
            } else {
                bool esc;
                unsigned v = dec_value(br, esc);
                if (prev >= mtf.size()) bad("context out of range");
                auto& m = mtf[prev].v;
                uint8_t c;
                if (esc) {
                    c = (uint8_t)(v & 0xFF);
                    if (!m.empty()) m.pop_back();
                    m.insert(m.begin(), c);
                } else if (v < m.size()) {
                    c = m[v];
                    m.erase(m.begin() + v);
                    m.insert(m.begin(), c);
                } else {
                    c = 0;
                    m.insert(m.begin(), c);
                }
                prev = c;
                img[row + x] = c;
                x++;
            }
        }
    }
}

std::vector<Off> read_tab(const Rd& r, size_t& pos) {
    unsigned ntab = r.u16(pos);
    pos += 2;
    std::vector<Off> tab(ntab);
    for (unsigned i = 0; i < ntab; i++) {
        tab[i].dx = (int8_t)r.u8(pos);
        tab[i].dy = r.u8(pos + 1);
        pos += 2;
    }
    return tab;
}

}  // namespace

Image Indexed8::toRgba() const {
    Image im;
    im.w = w;
    im.h = h;
    im.rgba.resize((size_t)w * h * 4);
    size_t cnt = pal.size() / 3;
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            uint8_t c = pix[(size_t)y * pitch + x];
            uint8_t* o = &im.rgba[((size_t)y * w + x) * 4];
            if (c < cnt) { o[0] = pal[c * 3]; o[1] = pal[c * 3 + 1]; o[2] = pal[c * 3 + 2]; }
            else { o[0] = o[1] = o[2] = 0; }
            o[3] = 255;
        }
    }
    return im;
}

Indexed8 decode_gdt8(const uint8_t* data, size_t n) {
    Rd r{data, n};
    int w = r.u16(0), h = r.u16(2);
    if (r.u8(5) != 0) bad("not an 8bpp-codec GDT");
    unsigned cnt = r.u16(8), rows = r.u16(10), syms = r.u16(12), minlen = r.u16(14);
    if (rows != 256 || syms != 256) bad("unsupported context/symbol count");
    if (w <= 0 || h <= 0) bad("empty image");
    size_t pos = 20;
    Indexed8 o;
    o.w = w; o.h = h; o.pitch = (w + 3) & ~3;
    r.need(pos, (size_t)cnt * 3);
    o.pal.assign(data + pos, data + pos + (size_t)cnt * 3);
    pos += (size_t)cnt * 3;
    auto tab = read_tab(r, pos);
    o.pix.assign((size_t)o.pitch * h + 4096, 0);
    BitReader br(data, n, pos);
    decode_stream(br, o.pix, w, h, o.pitch, tab, (int)minlen, 256, 256);
    return o;
}

Image decode_gdt24(const uint8_t* data, size_t n) {
    Rd r{data, n};
    int w = r.u16(0), h = r.u16(2);
    if (r.u8(5) != 2) bad("not a mode-2 GDT");
    if (w <= 0 || h <= 0) bad("empty image");
    unsigned flag = r.u16(10);
    size_t pos = 20;
    unsigned nx = r.u16(pos), ny = r.u16(pos + 2);
    pos += 4;
    if (nx < (unsigned)((w + 15) / 16) || ny < (unsigned)((h + 15) / 16)) bad("block map too small");
    std::vector<uint16_t> bmap((size_t)nx * ny);
    for (size_t i = 0; i < bmap.size(); i++) bmap[i] = r.u16(pos + i * 2);
    pos += bmap.size() * 2;
    unsigned C = r.u16(pos);
    pos += 2;
    pos += 4;  // D (unused)
    std::vector<std::vector<uint8_t>> groups(C);  // RGB triples per group
    for (unsigned g = 0; g < C; g++) {
        unsigned k = r.u16(pos);
        pos += 2;
        r.need(pos, (size_t)k * 2);
        auto& cols = groups[g];
        cols.resize((size_t)k * 3);
        for (unsigned i = 0; i < k; i++) {
            unsigned v = r.u16(pos + i * 2);
            cols[i * 3 + 0] = (uint8_t)((v << 3) & 0xF8);                              // R
            cols[i * 3 + 1] = (uint8_t)(flag ? ((v >> 8) & 0xFC) : ((v >> 7) & 0xF8));  // G
            cols[i * 3 + 2] = (uint8_t)((v >> 2) & 0xF8);                              // B
        }
        pos += (size_t)k * 2;
    }
    std::vector<uint8_t> idx((size_t)w * h + 4096, 0);
    unsigned comp = r.u16(14);
    if (!comp) {
        r.need(pos, (size_t)w * h);
        std::memcpy(idx.data(), data + pos, (size_t)w * h);
    } else {
        auto tab = read_tab(r, pos);
        BitReader br(data, n, pos);
        decode_stream(br, idx, w, h, w, tab, 3, 256, 256);
    }
    Image im;
    im.w = w; im.h = h;
    im.rgba.resize((size_t)w * h * 4);
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            unsigned g = bmap[(size_t)(y >> 4) * nx + (x >> 4)];
            if (g >= C) bad("block group out of range");
            const auto& pal = groups[g];
            unsigned c = idx[(size_t)y * w + x];
            uint8_t* o = &im.rgba[((size_t)y * w + x) * 4];
            if (c * 3 < pal.size()) { o[0] = pal[c * 3]; o[1] = pal[c * 3 + 1]; o[2] = pal[c * 3 + 2]; }
            else { o[0] = o[1] = o[2] = 0; }
            o[3] = 255;
        }
    }
    return im;
}

Image decode_gdt_dib(const uint8_t* data, size_t n) {
    if (n < 10 + 40) bad("DIB too small");
    Rd r{data + 10, n - 10};
    uint32_t hs = r.u32(0);
    int w = r.s32(4), h = r.s32(8);
    unsigned bpp = r.u16(14), comp = r.u32(16);
    uint32_t clr = r.u32(32);
    if (comp != 0) bad("compressed DIB unsupported");
    if (bpp != 1 && bpp != 4 && bpp != 8 && bpp != 24 && bpp != 32) bad("DIB bpp unsupported");
    if (clr == 0 && bpp <= 8) clr = 1u << bpp;
    bool bottomUp = h > 0;
    int ah = h < 0 ? -h : h;
    if (w <= 0 || ah <= 0) bad("bad DIB size");
    size_t palOff = hs, pixOff = hs + (size_t)clr * 4;
    size_t stride = (((size_t)w * bpp + 31) / 32) * 4;
    r.need(pixOff, stride * ah);
    Image im;
    im.w = w; im.h = ah;
    im.rgba.resize((size_t)w * ah * 4);
    for (int y = 0; y < ah; y++) {
        const uint8_t* src = r.p + pixOff + stride * (size_t)(bottomUp ? ah - 1 - y : y);
        for (int x = 0; x < w; x++) {
            uint8_t* o = &im.rgba[((size_t)y * w + x) * 4];
            uint8_t b, g, rr;
            if (bpp <= 8) {
                unsigned c;
                if (bpp == 8) c = src[x];
                else if (bpp == 4) c = (src[x >> 1] >> ((x & 1) ? 0 : 4)) & 0xF;
                else c = (src[x >> 3] >> (7 - (x & 7))) & 1;
                if (c >= clr) { b = g = rr = 0; }
                else { const uint8_t* q = r.p + palOff + c * 4; b = q[0]; g = q[1]; rr = q[2]; }
            } else {
                const uint8_t* q = src + x * (bpp / 8);
                b = q[0]; g = q[1]; rr = q[2];
            }
            o[0] = rr; o[1] = g; o[2] = b; o[3] = 255;
        }
    }
    return im;
}

Image decode_gdt_auto(const uint8_t* r, size_t n) {
    if (n < 16) bad("GDT too small");
    if (r[5] == 2) return decode_gdt24(r, n);
    if (r[5] == 0 && r[4] == 8 && (r[8] | (r[9] << 8)) <= 256) {
        try {
            return decode_gdt8(r, n).toRgba();
        } catch (const std::exception&) {
            // fall through to DIB, like the reference exporter
        }
    }
    if (n >= 14 && r[10] == 0x28 && r[11] == 0 && r[12] == 0 && r[13] == 0) return decode_gdt_dib(r, n);
    bad("unrecognised GDT variant");
}

size_t wpc_sheet_count(const uint8_t* hdr, size_t n) {
    if (n < 20) return 0;
    size_t c = 0;
    for (int i = 0; i < 4; i++) {
        uint32_t o = (uint32_t)hdr[i * 4] | ((uint32_t)hdr[i * 4 + 1] << 8) |
                     ((uint32_t)hdr[i * 4 + 2] << 16) | ((uint32_t)hdr[i * 4 + 3] << 24);
        if (o) c++;
    }
    return c;
}

std::vector<Image> decode_wpc_sheets(const uint8_t* data, size_t n) {
    Rd r{data, n};
    std::vector<uint32_t> offs;
    for (int i = 0; i < 4; i++)
        if (uint32_t o = r.u32(i * 4)) offs.push_back(o);
    std::vector<Image> out;
    for (size_t i = 0; i < offs.size(); i++) {
        size_t b = offs[i], e = (i + 1 < offs.size()) ? offs[i + 1] : n;
        if (b >= e || e > n) bad("bad WPC sheet range");
        out.push_back(decode_gdt8(data + b, e - b).toRgba());
    }
    return out;
}

Image decode_wmp(const uint8_t* data, size_t n) {
    Rd r{data, n};
    uint32_t o = r.u32(0);
    if (o >= n) bad("bad WMP offset");
    return decode_gdt24(data + o, n - o);
}

bool write_ppm(const std::string& path, const Image& im) {
    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return false;
    std::fprintf(f, "P6\n%d %d\n255\n", im.w, im.h);
    std::vector<uint8_t> row((size_t)im.w * 3);
    for (int y = 0; y < im.h; y++) {
        for (int x = 0; x < im.w; x++)
            std::memcpy(&row[(size_t)x * 3], &im.rgba[((size_t)y * im.w + x) * 4], 3);
        std::fwrite(row.data(), 1, row.size(), f);
    }
    std::fclose(f);
    return true;
}

}  // namespace pm3
