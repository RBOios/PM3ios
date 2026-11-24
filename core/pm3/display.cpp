#include "pm3/display.h"

#include <algorithm>
#include <cmath>

namespace pm3 {

Viewport fit_viewport(float vw, float vh, int gw, int gh, bool integer_only) {
    Viewport vp; vp.game_w = gw; vp.game_h = gh;
    if (vw <= 0 || vh <= 0 || gw <= 0 || gh <= 0) return vp;
    float s = std::min(vw / gw, vh / gh);
    if (integer_only && s >= 1.0f) s = std::floor(s);
    vp.scale = s; vp.w = gw * s; vp.h = gh * s; vp.x = (vw - vp.w) * 0.5f; vp.y = (vh - vp.h) * 0.5f;
    return vp;
}

bool view_to_game(const Viewport& vp, float vx, float vy, int* gx, int* gy) {
    if (vp.scale <= 0) { *gx = *gy = 0; return false; }
    float fx = (vx - vp.x) / vp.scale, fy = (vy - vp.y) / vp.scale;
    bool inside = fx >= 0 && fy >= 0 && fx < vp.game_w && fy < vp.game_h;
    *gx = std::min(std::max((int)std::floor(fx), 0), vp.game_w - 1);
    *gy = std::min(std::max((int)std::floor(fy), 0), vp.game_h - 1);
    return inside;
}

void game_to_view(const Viewport& vp, int gx, int gy, float* vx, float* vy) { *vx = vp.x + gx * vp.scale; *vy = vp.y + gy * vp.scale; }

static inline uint32_t ex(uint32_t v, int bits) { return bits == 5 ? (v << 3) | (v >> 2) : (v << 2) | (v >> 4); }

void convert_to_argb(const uint8_t* src, int w, int h, int pitch, PixFmt fmt, const uint32_t* pal, uint32_t* dst) {
    for (int y = 0; y < h; y++) {
        const uint8_t* s = src + (size_t)y * pitch; uint32_t* d = dst + (size_t)y * w;
        for (int x = 0; x < w; x++) {
            uint32_t r = 0, g = 0, b = 0;
            switch (fmt) {
                case PixFmt::Pal8: { uint32_t c = pal ? pal[s[x]] : (s[x] * 0x010101u); r = (c >> 16) & 255; g = (c >> 8) & 255; b = c & 255; break; }
                case PixFmt::Rgb565: { uint32_t v = s[2 * x] | (s[2 * x + 1] << 8); r = ex((v >> 11) & 31, 5); g = ex((v >> 5) & 63, 6); b = ex(v & 31, 5); break; }
                case PixFmt::Rgb555: { uint32_t v = s[2 * x] | (s[2 * x + 1] << 8); r = ex((v >> 10) & 31, 5); g = ex((v >> 5) & 31, 5); b = ex(v & 31, 5); break; }
                case PixFmt::Bgr24: b = s[3 * x]; g = s[3 * x + 1]; r = s[3 * x + 2]; break;
                case PixFmt::Bgrx32: b = s[4 * x]; g = s[4 * x + 1]; r = s[4 * x + 2]; break;
            }
            d[x] = 0xFF000000u | (r << 16) | (g << 8) | b;
        }
    }
}

void palette_from_bgrx(const uint8_t* p, int count, uint32_t* out) {
    for (int i = 0; i < count; i++) out[i] = 0xFF000000u | ((uint32_t)p[4 * i + 2] << 16) | ((uint32_t)p[4 * i + 1] << 8) | p[4 * i];
}

}  // namespace pm3
