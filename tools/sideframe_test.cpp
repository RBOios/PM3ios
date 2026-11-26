// sideframe_test <left.ppm> <right.ppm> <game.ppm> <screen_w> <screen_h> <out.ppm> : composes a screen like the iOS view (game aspect-fit + side bars) for a visual check
#include <cstdio>
#include <cstdlib>
#include <vector>
#include "pm3/sideframe.h"
static bool rd(const char* p, int& w, int& h, std::vector<uint32_t>& px) { FILE* f = fopen(p, "rb"); if (!f) return false; int mx; if (fscanf(f, "P6 %d %d %d", &w, &h, &mx) != 3) return false; fgetc(f); px.resize((size_t)w * h); for (auto& v : px) { int r = fgetc(f), g = fgetc(f), b = fgetc(f); v = 0xFF000000u | (r << 16) | (g << 8) | b; } fclose(f); return true; }
int main(int argc, char** argv) {
    if (argc < 7) return 1; int lw, lh, rw, rh, gw, gh; std::vector<uint32_t> L, R, G; if (!rd(argv[1], lw, lh, L) || !rd(argv[2], rw, rh, R) || !rd(argv[3], gw, gh, G)) { puts("read failed"); return 2; }
    int W = atoi(argv[4]), H = atoi(argv[5]); int vw = (int)(H * 4.0 / 3.0 + 0.5); if (vw > W) vw = W; int vx = (W - vw) / 2, bar = vx;
    std::vector<uint32_t> scr((size_t)W * H, 0xFF000000u);
    for (int y = 0; y < H; y++) for (int x = 0; x < vw; x++) scr[(size_t)y * W + vx + x] = G[(size_t)(y * gh / H) * gw + (x * gw / vw)];
    if (bar > 0) { std::vector<uint32_t> a, b; pm3::compose_side_bar(L.data(), lw, lh, bar, H, true, a); pm3::compose_side_bar(R.data(), rw, rh, W - vx - vw, H, false, b);
        for (int y = 0; y < H; y++) { for (int x = 0; x < bar; x++) scr[(size_t)y * W + x] = a[(size_t)y * bar + x]; for (int x = 0; x < W - vx - vw; x++) scr[(size_t)y * W + vx + vw + x] = b[(size_t)y * (W - vx - vw) + x]; } }
    FILE* f = fopen(argv[6], "wb"); fprintf(f, "P6\n%d %d\n255\n", W, H); for (auto v : scr) { fputc((v >> 16) & 255, f); fputc((v >> 8) & 255, f); fputc(v & 255, f); } fclose(f); printf("bar=%d px, viewport=%d\n", bar, vw); return 0; }
