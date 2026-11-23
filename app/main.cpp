// PM3 asset viewer: first runnable SDL2 shell (Phase 2 scaffold).
// Browses every GDT / WPC sheet / WMP image in PM3SYS0, PM3DAT1, PM3DAT2.
//   keys : Left/Right = prev/next, Up/Down = +/-20, Esc = quit
//   touch: tap left/right third = prev/next, tap top band = +/-20 (left/right), tap centre = jump to start
#include <SDL.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include "font5x7.h"
#include "platform.h"
#include "pm3/fpa.h"
#include "pm3/image.h"

namespace fs = std::filesystem;

struct Asset {
    int arch;        // index into archives
    size_t entry;    // entry index
    int sheet;       // WPC sheet number, else -1
    std::string label;
};

struct Archive {
    std::string name;
    pm3::Fpa fpa;
};

static std::string find_ci(const fs::path& dir, const std::string& name) {
    std::error_code ec;
    for (auto& e : fs::directory_iterator(dir, ec)) {
        std::string a = e.path().filename().string(), b = name;
        for (auto& c : a) c = (char)toupper((unsigned char)c);
        for (auto& c : b) c = (char)toupper((unsigned char)c);
        if (a == b) return e.path().string();
    }
    return "";
}

// Accept either the game folder itself or a parent containing a PM3 folder.
static fs::path resolve_data_dir(const std::string& base) {
    for (fs::path d : {fs::path(base), fs::path(base) / "PM3", fs::path(base) / "pm3"})
        if (!find_ci(d, "PM3SYS0.FPA").empty()) return d;
    return {};
}

static void draw_text(SDL_Renderer* r, const std::string& s, int x, int y, int scale, SDL_Color col) {
    SDL_SetRenderDrawColor(r, col.r, col.g, col.b, 255);
    for (char ch : s) {
        char c = (char)toupper((unsigned char)ch);
        const Glyph* g = &kFont[0];
        for (const auto& k : kFont) if (k.c == c) { g = &k; break; }
        for (int row = 0; row < 7; row++)
            for (int col5 = 0; col5 < 5; col5++)
                if (g->rows[row] & (0x10 >> col5)) {
                    SDL_Rect q{x + col5 * scale, y + row * scale, scale, scale};
                    SDL_RenderFillRect(r, &q);
                }
        x += 6 * scale;
    }
}

int main(int argc, char** argv) {
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        std::fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return 1;
    }
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");
    SDL_Window* win = SDL_CreateWindow("PM3 Viewer", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 640, 480,
                                       SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
    SDL_Renderer* ren = win ? SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC) : nullptr;
    if (!ren) {
        std::fprintf(stderr, "window/renderer: %s\n", SDL_GetError());
        return 1;
    }

    // ---- locate and index data -------------------------------------------------------------
    std::string base = argc > 1 ? argv[1] : pm3_default_data_dir();
    fs::path dir = resolve_data_dir(base);
    std::vector<std::unique_ptr<Archive>> archives;
    std::vector<Asset> assets;
    if (!dir.empty()) {
        for (const char* an : {"PM3SYS0", "PM3DAT1", "PM3DAT2"}) {
            std::string p = find_ci(dir, std::string(an) + ".FPA");
            if (p.empty()) continue;
            auto a = std::make_unique<Archive>();
            a->name = an;
            if (!a->fpa.open(p)) continue;
            int ai = (int)archives.size();
            const auto& es = a->fpa.entries();
            for (size_t i = 0; i < es.size(); i++) {
                const auto& e = es[i];
                if (e.ext == "GDT" || e.ext == "WMP") {
                    assets.push_back({ai, i, -1, std::string(an) + "/" + e.name + "." + e.ext});
                } else if (e.ext == "WPC") {
                    pm3::Bytes hdr;
                    a->fpa.read(e, hdr, 0, 20);
                    size_t n = pm3::wpc_sheet_count(hdr.data(), hdr.size());
                    for (size_t s = 0; s < n; s++)
                        assets.push_back({ai, i, (int)s, std::string(an) + "/" + e.name + ".WPC#" + std::to_string(s)});
                }
            }
            archives.push_back(std::move(a));
        }
    }

    // ---- state -----------------------------------------------------------------------------
    int cur = 0;
    SDL_Texture* tex = nullptr;
    int texW = 0, texH = 0;
    std::string status;
    bool dirty = true;

    auto load = [&]() {
        if (tex) { SDL_DestroyTexture(tex); tex = nullptr; }
        status.clear();
        if (assets.empty()) return;
        const Asset& a = assets[cur];
        Archive& ar = *archives[a.arch];
        const pm3::FpaEntry& e = ar.fpa.entries()[a.entry];
        try {
            pm3::Bytes raw;
            if (!ar.fpa.read(e, raw)) throw std::runtime_error("read failed");
            pm3::Image im;
            if (e.ext == "GDT") im = pm3::decode_gdt_auto(raw.data(), raw.size());
            else if (e.ext == "WMP") im = pm3::decode_wmp(raw.data(), raw.size());
            else im = std::move(pm3::decode_wpc_sheets(raw.data(), raw.size()).at((size_t)a.sheet));
            tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, im.w, im.h);
            if (!tex) throw std::runtime_error(SDL_GetError());
            SDL_UpdateTexture(tex, nullptr, im.rgba.data(), im.w * 4);
            texW = im.w; texH = im.h;
        } catch (const std::exception& ex) {
            status = std::string("DECODE FAILED: ") + ex.what();
        }
    };
    auto go = [&](int d) {
        if (assets.empty()) return;
        int n = (int)assets.size();
        cur = ((cur + d) % n + n) % n;
        load();
        dirty = true;
    };
    load();

    // ---- main loop -------------------------------------------------------------------------
    bool quit = false;
    while (!quit) {
        SDL_Event ev;
        while (SDL_WaitEventTimeout(&ev, 250)) {
            if (ev.type == SDL_QUIT) quit = true;
            else if (ev.type == SDL_KEYDOWN) {
                switch (ev.key.keysym.sym) {
                    case SDLK_RIGHT: go(1); break;
                    case SDLK_LEFT: go(-1); break;
                    case SDLK_UP: go(20); break;
                    case SDLK_DOWN: go(-20); break;
                    case SDLK_ESCAPE: quit = true; break;
                }
            } else if (ev.type == SDL_MOUSEBUTTONDOWN && ev.button.button == SDL_BUTTON_LEFT) {
                int ww, wh;
                SDL_GetWindowSize(win, &ww, &wh);
                float fx = (float)ev.button.x / (float)std::max(ww, 1), fy = (float)ev.button.y / (float)std::max(wh, 1);
                int step = fy < 0.2f ? 20 : 1;
                if (fx < 1.f / 3) go(-step);
                else if (fx > 2.f / 3) go(step);
                else go(-cur);
            } else if (ev.type == SDL_WINDOWEVENT || ev.type == SDL_RENDER_TARGETS_RESET || ev.type == SDL_RENDER_DEVICE_RESET) {
                dirty = true;
            }
            if (quit) break;
        }
        if (quit) break;
        if (!dirty) continue;
        dirty = false;

        int ow, oh;
        SDL_GetRendererOutputSize(ren, &ow, &oh);
        SDL_SetRenderDrawColor(ren, 24, 24, 32, 255);
        SDL_RenderClear(ren);
        int ts = std::max(1, std::min(ow / 320, 4));  // HUD text scale
        if (tex) {
            float s = std::min((float)ow / texW, (float)oh / texH);
            if (s >= 1.f) s = std::floor(s);  // integer scale when it fits: crisp pixels
            SDL_Rect dst{(int)((ow - texW * s) / 2), (int)((oh - texH * s) / 2), (int)(texW * s), (int)(texH * s)};
            SDL_RenderCopy(ren, tex, nullptr, &dst);
        }
        SDL_Color white{255, 255, 255, 255}, warn{255, 160, 120, 255};
        if (assets.empty()) {
            draw_text(ren, "NO PM3 DATA FOUND", 8 * ts, 8 * ts, 2 * ts, warn);
            draw_text(ren, "COPY PM3SYS0.FPA PM3DAT1.FPA PM3DAT2.FPA", 8 * ts, 28 * ts, ts, white);
            draw_text(ren, "INTO THIS APP IN THE FILES APP", 8 * ts, 38 * ts, ts, white);
            draw_text(ren, "DATA DIR: " + (dir.empty() ? base : dir.string()), 8 * ts, 48 * ts, ts, white);
        } else {
            std::string hud = std::to_string(cur + 1) + "/" + std::to_string(assets.size()) + "  " + assets[cur].label +
                              (tex ? "  " + std::to_string(texW) + "X" + std::to_string(texH) : "");
            draw_text(ren, hud, 4 * ts, 4 * ts, ts, white);
            if (!status.empty()) draw_text(ren, status, 4 * ts, 14 * ts, ts, warn);
        }
        SDL_RenderPresent(ren);
    }

    if (tex) SDL_DestroyTexture(tex);
    SDL_DestroyRenderer(ren);
    SDL_DestroyWindow(win);
    SDL_Quit();
    return 0;
}
