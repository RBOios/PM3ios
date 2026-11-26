#include "pm3/sideframe.h"

#include <algorithm>
#include <cmath>

namespace pm3 {

namespace {
// bilinear sample of the panel at (sx, sy) in source pixels (pixel centres at +0.5), clamped to the edges
uint32_t sample(const uint32_t* p, int pw, int ph, double sx, double sy) {
    double fx = sx - 0.5, fy = sy - 0.5; int x0 = (int)std::floor(fx), y0 = (int)std::floor(fy); double ax = fx - x0, ay = fy - y0;
    auto at = [&](int x, int y) { x = std::max(0, std::min(pw - 1, x)); y = std::max(0, std::min(ph - 1, y)); return p[(size_t)y * pw + x]; };
    uint32_t c[4] = {at(x0, y0), at(x0 + 1, y0), at(x0, y0 + 1), at(x0 + 1, y0 + 1)}; double w[4] = {(1 - ax) * (1 - ay), ax * (1 - ay), (1 - ax) * ay, ax * ay}; uint32_t out = 0xFF000000u;
    for (int ch = 0; ch < 3; ch++) { double v = 0; for (int i = 0; i < 4; i++) v += w[i] * ((c[i] >> (8 * ch)) & 255); out |= (uint32_t)std::lround(v) << (8 * ch); }
    return out;
}
}  // namespace

void compose_side_bar(const uint32_t* panel, int pw, int ph, int out_w, int out_h, bool left_bar, std::vector<uint32_t>& out) {
    out.assign((size_t)std::max(0, out_w) * std::max(0, out_h), 0xFF000000u);
    if (!panel || pw < 2 || ph < 2 || out_w < 1 || out_h < 1) return;
    const double s = (double)out_h / ph;               // the panel's own scale: fill the full height
    const double natural = pw * s;                      // width of one copy at that scale
    const double r = out_w / natural;
    if (r < 0.5) {   // a very narrow bar: show only the part of the panel next to the game, unsqueezed
        const double vis = out_w / s;                   // source pixels visible
        for (int y = 0; y < out_h; y++) for (int x = 0; x < out_w; x++) {
            double sx = left_bar ? (pw - vis) + (x + 0.5) / s : (x + 0.5) / s;
            out[(size_t)y * out_w + x] = sample(panel, pw, ph, sx, (y + 0.5) / s);
        }
        return;
    }
    const int k = 1;                                     // one copy, stretched horizontally to the whole bar (no repeating / mirroring)
    const double cw = (double)out_w / k;
    for (int x = 0; x < out_w; x++) {
        // index of the copy counted from the game edge, and the position inside it (0..1, screen-left to screen-right)
        double d = left_bar ? (out_w - (x + 0.5)) : (x + 0.5);        // distance from the game edge
        int i = std::min(k - 1, (int)(d / cw));
        double u = left_bar ? ((x + 0.5) - (out_w - (i + 1) * cw)) / cw : ((x + 0.5) - i * cw) / cw;
        if (i & 1) u = 1.0 - u;                                       // alternate copies are mirrored so they meet seamlessly
        double sx = std::max(0.0, std::min((double)pw, u * pw));
        for (int y = 0; y < out_h; y++) out[(size_t)y * out_w + x] = sample(panel, pw, ph, sx, (y + 0.5) / s);
    }
}

}  // namespace pm3
