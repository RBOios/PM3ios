#include "pm3/tfn.h"

#include <cstring>

namespace pm3 {

static uint32_t rd32(const uint8_t* p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }
static const size_t kFull = 18368, kHalf = 128;

bool Tfn::load(const uint8_t* d, size_t n, std::string* err) {
    auto fail = [&](const char* m) { if (err) *err = m; return false; };
    if (n < 19 || std::memcmp(d, "TGLFontFile", 11) != 0) return fail("bad TFN magic");
    size_ = (int)rd32(d + 11);
    int mult = (int)rd32(d + 15);
    if (mult <= 0 || mult > 64) return fail("bad TFN mult");
    rows_ = mult;
    size_t bhalf = 2 * (size_t)mult, bfull = 3 * (size_t)mult;
    size_t off = 19, need = 19 + kFull + kHalf * bhalf + kFull * bfull;
    if (n < need) return fail("TFN truncated");
    widths_.assign(d + off, d + off + kFull); off += kFull;
    half_.assign(d + off, d + off + kHalf * bhalf); off += kHalf * bhalf;
    full_.assign(d + off, d + off + kFull * bfull);
    return true;
}

bool Tfn::glyph(uint16_t code, bool wide, const uint8_t** rows, int* bpr) const {
    if (wide) {
        if (code < 0x8140 || (size_t)(code - 0x8140) >= kFull) return false;
        *bpr = 3; *rows = &full_[(size_t)(code - 0x8140) * 3 * rows_];
    } else {
        if (code >= kHalf) return false;
        *bpr = 2; *rows = &half_[(size_t)code * 2 * rows_];
    }
    return true;
}

int Tfn::draw_char(Image& im, int x, int y, uint16_t code, bool wide, uint32_t rgb) const {
    const uint8_t* g; int bpr;
    int adv = wide ? 24 : 12;
    if (!glyph(code, wide, &g, &bpr)) return adv;
    for (int r = 0; r < rows_; r++) {
        int py = y + r; if (py < 0 || py >= im.h) continue;
        for (int c = 0; c < bpr * 8; c++) {
            if (!(g[r * bpr + c / 8] & (0x80 >> (c % 8)))) continue;
            int px = x + c; if (px < 0 || px >= im.w) continue;
            uint8_t* o = &im.rgba[((size_t)py * im.w + px) * 4];
            o[0] = rgb >> 16; o[1] = rgb >> 8; o[2] = rgb; o[3] = 255;
        }
    }
    return adv;
}

int Tfn::draw_text(Image& im, int x, int y, const std::string& s, uint32_t rgb) const {
    for (size_t i = 0; i < s.size();) {
        uint8_t c = (uint8_t)s[i];
        if (c >= 0x81 && i + 1 < s.size()) { x += draw_char(im, x, y, (uint16_t)(c << 8 | (uint8_t)s[i + 1]), true, rgb); i += 2; }
        else { x += draw_char(im, x, y, c, false, rgb); i++; }
    }
    return x;
}

int Tfn::text_width(const std::string& s) const {
    int w = 0;
    for (size_t i = 0; i < s.size();) { if ((uint8_t)s[i] >= 0x81 && i + 1 < s.size()) { w += 24; i += 2; } else { w += 12; i++; } }
    return w;
}

void draw_dialogue_box(Image& im, const Tfn& font, const std::string& t) {
    const int pad = 12, lh = 28, maxw = im.w - 2 * pad;
    std::vector<std::string> lines; std::string cl; int cw = 0;
    for (size_t i = 0; i < t.size();) {
        uint8_t c = (uint8_t)t[i]; bool wide = c >= 0x81 && i + 1 < t.size();
        size_t n = wide ? 2 : 1;
        if (c == '\n') { lines.push_back(cl); cl.clear(); cw = 0; i++; continue; }
        int w = wide ? 24 : 12;
        if (cw + w > maxw) { lines.push_back(cl); cl.clear(); cw = 0; }
        cl += t.substr(i, n); cw += w; i += n;
    }
    if (!cl.empty()) lines.push_back(cl);
    int by = im.h - ((int)lines.size() * lh + pad);
    if (by < 0) by = 0;
    for (int yy = by; yy < im.h; yy++)
        for (int xx = 0; xx < im.w; xx++) {
            uint8_t* q = &im.rgba[((size_t)yy * im.w + xx) * 4];
            q[0] = (uint8_t)(q[0] / 4); q[1] = (uint8_t)(q[1] / 4); q[2] = (uint8_t)(q[2] / 3);
        }
    for (size_t li = 0; li < lines.size(); li++) {
        int ty = by + pad / 2 + (int)li * lh;
        font.draw_text(im, pad + 1, ty + 1, lines[li], 0x000000);
        font.draw_text(im, pad, ty, lines[li], 0xffffff);
    }
}

}  // namespace pm3
