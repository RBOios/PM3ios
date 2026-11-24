// PM3 asset viewer: first runnable SDL2 shell (Phase 2 scaffold).
// Browses every GDT / WPC sheet / WMP image in PM3SYS0, PM3DAT1, PM3DAT2, and plays each WPC
// scene animation ("NAME.WPC PLAY", slot scripts run at 30 ticks/s, looped).
// Pages named "<SCRIPT>.EVX SCENE" show what that EVX script displays: WMP background (+ WPC sprites).
//   keys : Left/Right = prev/next, Up/Down = +/-20, B = next background (PLAY/SCENE pages), Esc = quit
//   touch: tap left/right third = prev/next, tap top band = +/-20 (left/right),
//          tap centre = next background on PLAY/SCENE pages, else jump to start
#include <SDL.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include "font5x7.h"
#include "pm3/tfn.h"
#include "platform.h"
#include "pm3/fpa.h"
#include "pm3/image.h"
#include "pm3/scene.h"
#include "pm3/wav.h"
#include "pm3/wbd.h"
#include "pm3/wpc.h"

namespace fs = std::filesystem;

struct Asset {
    Asset(int a, size_t e, int sh, std::string l) : arch(a), entry(e), sheet(sh), label(std::move(l)) {}
    int arch;        // index into archives
    size_t entry;    // entry index
    int sheet;       // WPC sheet number; -1 = plain image; -2 = WPC animation player; -3 = EVX scene; -4 = WBD portrait (blink loop); -5 = WAV sound
    std::string label;
    int bg = -1;     // SCENE: background id (M####) the script asked for
    int sprite = -1; // SCENE: sprite id (P###) the script asked for
    std::vector<pm3::ScenePortrait> portraits;   // SCENE: portrait slots at the end of the script run
    std::vector<pm3::SceneStep> steps;           // SCENE: dialogue lines (op 71/84) with the state at each line
};

struct WmpRef { int arch; size_t entry; int id; };

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

static int name_id(const std::string& n) { return n.size() > 1 ? std::atoi(n.c_str() + 1) : -1; }

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
    std::vector<WmpRef> wmps;                 // every background scene, sorted by id
    std::vector<std::pair<int, int>> pairs;   // (sprite P id, background M id) taken from the EVX scripts
    pm3::Tfn tfn; bool haveFont = false;      // FONT24.TFN (dialogue text), optional
    if (!dir.empty()) {
        std::string fp = find_ci(dir, "FONT24.TFN");
        if (FILE* ff = fp.empty() ? nullptr : std::fopen(fp.c_str(), "rb")) {
            std::vector<uint8_t> fd; uint8_t buf[65536]; size_t k;
            while ((k = std::fread(buf, 1, sizeof buf, ff)) > 0) fd.insert(fd.end(), buf, buf + k);
            std::fclose(ff);
            haveFont = tfn.load(fd.data(), fd.size());
        }
    }
    if (!dir.empty()) {
        for (const char* an : {"PM3SYS0", "PM3DAT1", "PM3DAT2", "PM3TAK1", "PM3TAK2"}) {   // TAK* = voices, optional
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
                    assets.emplace_back(ai, i, -1, std::string(an) + "/" + e.name + "." + e.ext);
                    if (e.ext == "WMP") wmps.push_back({ai, i, name_id(e.name)});
                } else if (e.ext == "WPC") {
                    pm3::Bytes hdr;
                    a->fpa.read(e, hdr, 0, 20);
                    size_t n = pm3::wpc_sheet_count(hdr.data(), hdr.size());
                    for (size_t s = 0; s < n; s++)
                        assets.emplace_back(ai, i, (int)s, std::string(an) + "/" + e.name + ".WPC#" + std::to_string(s));
                    assets.emplace_back(ai, i, -2, std::string(an) + "/" + e.name + ".WPC PLAY");
                } else if (e.ext == "WBD") {
                    assets.emplace_back(ai, i, -4, std::string(an) + "/" + e.name + ".WBD");
                } else if (e.ext == "WAV") {
                    assets.emplace_back(ai, i, -5, std::string(an) + "/" + e.name + ".WAV");
                } else if (e.ext == "EVX") {
                    pm3::EvxSceneInfo info;
                    if (pm3::evx_scene_info(a->fpa, e, info) && info.bg() >= 0) {
                        Asset as(ai, i, -3, std::string(an) + "/" + e.name + ".EVX SCENE");
                        as.bg = info.bg();
                        as.sprite = info.sprite();
                        as.portraits = info.portraits;
                        as.steps = info.steps;
                        assets.push_back(as);
                        if (as.sprite >= 0) pairs.push_back({as.sprite, as.bg});
                    }
                }
            }
            archives.push_back(std::move(a));
        }
        std::sort(wmps.begin(), wmps.end(), [](const WmpRef& x, const WmpRef& y) { return x.id < y.id; });
    }

    // ---- state -----------------------------------------------------------------------------
    int cur = 0;
    SDL_Texture* tex = nullptr;
    int texW = 0, texH = 0;
    std::string status, info2;
    bool dirty = true;

    // Player pages (WPC PLAY / EVX SCENE): optional WMP background + optional WPC sprites.
    bool playing = false;
    bool haveWpc = false, haveBg = false;
    pm3::Wpc wpc;
    pm3::WpcRun run;
    pm3::WpcBounds pb;
    pm3::WmpScene bgScene;
    int bgSel = -1;                 // index into wmps, -1 = none
    bool autoPlay = false; Uint32 lineStart = 0;   // SCENE auto-advance (script WAIT time + reading time)
    int dlg = 0;                    // SCENE: current dialogue step
    std::string dlgText;            // SCENE: text of the current step (cp949)
    pm3::Image canvas;
    Uint32 playStart = 0;
    int lastTick = -1, lastBf = -1;
    struct ScenePortraitView { pm3::Wbd w; std::vector<pm3::Image> frames; std::vector<int> dur; int total = 1; int x = 0, y = 0; };
    std::vector<ScenePortraitView> sceneP;   // portraits drawn over a SCENE page
    const int kPlayTicks = 1800;
    // WBD portrait loop (timeline 2, else 1): pre-rendered frames over a flat background colour
    bool portraitPlay = false;
    std::vector<pm3::Image> pframes;
    std::vector<int> pdur;      // ms per frame
    int pTotal = 1, lastPf = -1, lastPfKey = -1;
    // WAV page: decoded sound, played on a single 22.05 kHz mono S16 device; waveform + playhead drawn into `tex`.
    const int kAudioRate = 22050;
    SDL_AudioDeviceID adev = 0;
    bool audioTried = false, wavPage = false;
    std::vector<int16_t> wavMono;        // samples at kAudioRate
    pm3::Image wavBase;                  // waveform picture without playhead
    int lastHead = -1;

    auto find_wmp = [&](int id) {
        for (size_t i = 0; i < wmps.size(); i++) if (wmps[i].id == id) return (int)i;
        return -1;
    };
    auto find_wpc = [&](int id) -> const pm3::FpaEntry* {   // sprite P### lives in PM3SYS0 (archive 0)
        if (archives.empty() || archives[0]->name != "PM3SYS0") return nullptr;
        for (const auto& e : archives[0]->fpa.entries())
            if (e.ext == "WPC" && name_id(e.name) == id) return &e;
        return nullptr;
    };
    auto free_tex = [&]() { if (tex) { SDL_DestroyTexture(tex); tex = nullptr; } };
    // (Re)creates canvas + texture for the current wpc/background combination.
    auto setup_canvas = [&]() {
        free_tex();
        if (haveBg) { canvas.w = 640; canvas.h = 480; }
        else if (haveWpc && pb.valid) { canvas.w = pb.x1 - pb.x0; canvas.h = pb.y1 - pb.y0; }
        else throw std::runtime_error("NOTHING TO DRAW");
        canvas.rgba.assign((size_t)canvas.w * canvas.h * 4, 255);
        tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, canvas.w, canvas.h);
        if (!tex) throw std::runtime_error(SDL_GetError());
        texW = canvas.w; texH = canvas.h;
        lastTick = lastBf = -1;
        dirty = true;
    };
    auto set_bg = [&](int sel) {   // sel = index into wmps, -1 = none
        haveBg = false;
        bgScene.frames.clear();
        bgSel = -1;
        if (sel >= 0 && sel < (int)wmps.size()) {
            Archive& ar = *archives[wmps[sel].arch];
            pm3::Bytes raw;
            if (!ar.fpa.read(ar.fpa.entries()[wmps[sel].entry], raw)) throw std::runtime_error("bg read failed");
            bgScene = pm3::wmp_scene_load(raw.data(), raw.size());
            haveBg = !bgScene.frames.empty();
            bgSel = sel;
        }
        char b[64];
        if (haveBg) std::snprintf(b, sizeof b, "BG M%04d", wmps[bgSel].id); else std::snprintf(b, sizeof b, "NO BG");
        info2 = b;
    };

    auto stop_audio = [&]() { if (adev) SDL_ClearQueuedAudio(adev); };
    auto ensure_audio = [&]() -> bool {
        if (adev) return true;
        if (audioTried) return false;
        audioTried = true;
        SDL_SetHint("SDL_AUDIO_CATEGORY", "playback");   // iOS: audible even with the silent switch on
        if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) return false;
        SDL_AudioSpec want;
        SDL_zero(want);
        want.freq = kAudioRate; want.format = AUDIO_S16SYS; want.channels = 1; want.samples = 1024;
        adev = SDL_OpenAudioDevice(nullptr, 0, &want, nullptr, 0);   // allowed_changes = 0: SDL converts for the hardware
        if (adev) SDL_PauseAudioDevice(adev, 0);
        return adev != 0;
    };
    auto play_wav = [&]() {
        if (wavMono.empty()) return;
        if (!ensure_audio()) { status = std::string("AUDIO UNAVAILABLE: ") + SDL_GetError(); return; }
        SDL_ClearQueuedAudio(adev);
        if (SDL_QueueAudio(adev, wavMono.data(), (Uint32)(wavMono.size() * 2)) != 0) status = std::string("AUDIO: ") + SDL_GetError();
        lastHead = -1;
    };
    // 640x120 min/max waveform; each column covers an equal slice of the sound.
    auto make_wave = [&](const std::vector<int16_t>& v) {
        pm3::Image im; im.w = 640; im.h = 120; im.rgba.assign((size_t)im.w * im.h * 4, 0);
        for (size_t q = 0; q < im.rgba.size(); q += 4) { im.rgba[q] = 20; im.rgba[q + 1] = 28; im.rgba[q + 2] = 40; im.rgba[q + 3] = 255; }
        for (int x = 0; x < im.w && !v.empty(); x++) {
            size_t a = v.size() * (size_t)x / (size_t)im.w, b = std::max(a + 1, v.size() * (size_t)(x + 1) / (size_t)im.w);
            int lo = 0, hi = 0;
            for (size_t k = a; k < b && k < v.size(); k++) { lo = std::min<int>(lo, v[k]); hi = std::max<int>(hi, v[k]); }
            int y0 = 60 - hi * 58 / 32768, y1 = 60 - lo * 58 / 32768;
            for (int y = y0; y <= y1; y++) { uint8_t* o = &im.rgba[((size_t)y * im.w + x) * 4]; o[0] = 120; o[1] = 200; o[2] = 255; }
        }
        return im;
    };

    auto load = [&]() {
        stop_audio();
        wavPage = false; wavMono.clear();
        free_tex();
        status.clear();
        info2.clear();
        playing = haveWpc = haveBg = portraitPlay = false;
        sceneP.clear();
        dlgText.clear();
        if (assets.empty()) return;
        const Asset& a = assets[cur];
        Archive& ar = *archives[a.arch];
        const pm3::FpaEntry& e = ar.fpa.entries()[a.entry];
        try {
            pm3::Bytes raw;
            pm3::Image im;
            if (a.sheet == -2 || a.sheet == -3) {
                const pm3::FpaEntry* we = nullptr;
                int wantBg = -1;
                if (a.sheet == -2) {
                    we = &e;
                    int pid = name_id(e.name);
                    for (auto& pr : pairs) if (pr.first == pid) wantBg = pr.second;   // known pairing from EVX, else none
                } else {
                    const pm3::SceneStep* st = nullptr;
                    if (!a.steps.empty()) { dlg = std::max(0, std::min(dlg, (int)a.steps.size() - 1)); st = &a.steps[dlg]; }
                    wantBg = st ? st->bg : a.bg;
                    int sp = st ? st->sprite : a.sprite;
                    dlgText = st ? st->text : std::string();
                    if (sp >= 0) we = find_wpc(sp);
                }
                if (we) {
                    pm3::Bytes wr;
                    if (!archives[0]->fpa.read(*we, wr)) throw std::runtime_error("read failed");
                    wpc = pm3::wpc_load(wr.data(), wr.size());
                    run = pm3::wpc_run(wpc, kPlayTicks, SDL_GetTicks());
                    pb = pm3::wpc_bounds(wpc, run.frames, 0, 1);
                    haveWpc = true;
                }
                set_bg(wantBg >= 0 ? find_wmp(wantBg) : -1);
                if (a.sheet == -3) {
                    for (const auto& pt : (a.steps.empty() ? a.portraits : a.steps[dlg].portraits)) {   // WBD "B<hi:3><lo:3>" lives in PM3DAT1
                        char nm[16];
                        std::snprintf(nm, sizeof nm, "B%03d%03d", pt.hi, pt.lo);
                        for (auto& arp : archives) {
                            const pm3::FpaEntry* wbe = nullptr;
                            for (const auto& ee : arp->fpa.entries()) if (ee.ext == "WBD" && ee.name == nm) { wbe = &ee; break; }
                            if (!wbe) continue;
                            pm3::Bytes br;
                            if (!arp->fpa.read(*wbe, br)) break;
                            ScenePortraitView v;
                            v.w = pm3::wbd_load(br.data(), br.size());
                            auto sq = v.w.seq(2);
                            if (sq.empty()) sq = v.w.seq(1);
                            if (sq.empty()) sq = {{0, 30}};
                            v.total = 0;
                            for (auto& pr : sq) { v.frames.push_back(v.w.render(pr.first)); v.dur.push_back(std::max(1, pr.second) * 33); v.total += v.dur.back(); }
                            v.x = pt.x; v.y = pt.y;
                            sceneP.push_back(std::move(v));
                            break;
                        }
                    }
                    char b[96];
                    std::snprintf(b, sizeof b, "%s  SPRITE %s  LINE %d/%d%s", info2.c_str(), we ? "ON" : "NONE", a.steps.empty() ? 0 : dlg + 1, (int)a.steps.size(), autoPlay ? " AUTO" : "");
                    info2 = b;
                }
                setup_canvas();
                playing = true;
                playStart = SDL_GetTicks();
                return;
            }
            if (a.sheet == -5) {
                if (!ar.fpa.read(e, raw)) throw std::runtime_error("read failed");
                pm3::Pcm pcm;
                if (!pm3::wav_decode(raw.data(), raw.size(), pcm)) throw std::runtime_error("UNSUPPORTED WAV");
                wavMono = pm3::pcm_to_mono_rate(pcm, kAudioRate);
                wavBase = make_wave(wavMono);
                tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, wavBase.w, wavBase.h);
                if (!tex) throw std::runtime_error(SDL_GetError());
                SDL_UpdateTexture(tex, nullptr, wavBase.rgba.data(), wavBase.w * 4);
                texW = wavBase.w; texH = wavBase.h;
                char b[96];
                std::snprintf(b, sizeof b, "%d HZ %s  %.2f S   SPACE OR TAP CENTRE = REPLAY", pcm.rate, pcm.channels == 1 ? "MONO" : "STEREO",
                              (double)wavMono.size() / kAudioRate);
                info2 = b;
                wavPage = true;
                play_wav();
                return;
            }
            if (a.sheet == -4) {
                if (!ar.fpa.read(e, raw)) throw std::runtime_error("read failed");
                pm3::Wbd w = pm3::wbd_load(raw.data(), raw.size());
                auto sq = w.seq(2);
                if (sq.empty()) sq = w.seq(1);
                if (sq.empty()) sq = {{0, 30}};
                pframes.clear(); pdur.clear(); pTotal = 0;
                for (auto& pr : sq) {
                    pm3::Image fr = w.render(pr.first);
                    for (size_t q = 0; q < fr.rgba.size(); q += 4) {   // flatten over the background colour
                        if (fr.rgba[q + 3] == 0) { fr.rgba[q] = 70; fr.rgba[q + 1] = 90; fr.rgba[q + 2] = 110; fr.rgba[q + 3] = 255; }
                    }
                    pframes.push_back(std::move(fr));
                    pdur.push_back(std::max(1, pr.second) * 33);
                    pTotal += pdur.back();
                }
                if (pframes.empty() || pframes[0].w <= 0 || pframes[0].h <= 0) throw std::runtime_error("EMPTY PORTRAIT");
                tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, pframes[0].w, pframes[0].h);
                if (!tex) throw std::runtime_error(SDL_GetError());
                texW = pframes[0].w; texH = pframes[0].h;
                portraitPlay = true; playStart = SDL_GetTicks(); lastPf = -1;
                return;
            }
            if (!ar.fpa.read(e, raw)) throw std::runtime_error("read failed");
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
        dlg = 0; lineStart = SDL_GetTicks();
        load();
        dirty = true;
    };
    auto next_bg = [&]() {
        if (!playing || wmps.empty()) return;
        try {
            int n = (int)wmps.size();
            int sel = bgSel + 1 >= n ? -1 : bgSel + 1;
            set_bg(sel);
            setup_canvas();
        } catch (const std::exception& ex) {
            status = std::string("DECODE FAILED: ") + ex.what();
        }
    };
    auto auto_ms = [&]() -> Uint32 {
        const auto& stp = assets[cur].steps[dlg];
        return std::max<Uint32>((Uint32)stp.ticks * 1000u / 30u, 1500u) + (Uint32)stp.text.size() * 60u;
    };
    auto next_line = [&]() {   // advance the dialogue of a SCENE page (wraps around)
        if (assets.empty() || assets[cur].sheet != -3 || assets[cur].steps.empty()) return false;
        dlg = (dlg + 1) % (int)assets[cur].steps.size();
        lineStart = SDL_GetTicks();
        load();
        dirty = true;
        return true;
    };
    load();

    // ---- main loop -------------------------------------------------------------------------
    bool quit = false;
    while (!quit) {
        SDL_Event ev;
        while (SDL_WaitEventTimeout(&ev, (playing || portraitPlay) ? 8 : (wavPage ? 30 : 250))) {
            if (ev.type == SDL_QUIT) quit = true;
            else if (ev.type == SDL_KEYDOWN) {
                switch (ev.key.keysym.sym) {
                    case SDLK_RIGHT: go(1); break;
                    case SDLK_LEFT: go(-1); break;
                    case SDLK_UP: go(20); break;
                    case SDLK_DOWN: go(-20); break;
                    case SDLK_b: next_bg(); break;
                    case SDLK_a: autoPlay = !autoPlay; lineStart = SDL_GetTicks(); dirty = true; break;
                    case SDLK_SPACE:
                        if (wavPage) { play_wav(); dirty = true; break; }   // WAV page: Space = replay
                        next_line(); break;
                    case SDLK_RETURN: next_line(); break;
                    case SDLK_ESCAPE: quit = true; break;
                }
            } else if (ev.type == SDL_MOUSEBUTTONDOWN && ev.button.button == SDL_BUTTON_LEFT) {
                int ww, wh;
                SDL_GetWindowSize(win, &ww, &wh);
                float fx = (float)ev.button.x / (float)std::max(ww, 1), fy = (float)ev.button.y / (float)std::max(wh, 1);
                int step = fy < 0.2f ? 20 : 1;
                if (fx < 1.f / 3) go(-step);
                else if (fx > 2.f / 3) go(step);
                else if (playing && fy < 0.2f) next_bg();
                else if (playing && fy > 0.8f && !assets.empty() && assets[cur].sheet == -3 && !assets[cur].steps.empty()) { autoPlay = !autoPlay; lineStart = SDL_GetTicks(); dirty = true; }
                else if (playing && next_line()) {}
                else if (playing) next_bg();
                else if (wavPage) { play_wav(); dirty = true; }
                else go(-cur);
            } else if (ev.type == SDL_WINDOWEVENT || ev.type == SDL_RENDER_TARGETS_RESET || ev.type == SDL_RENDER_DEVICE_RESET) {
                dirty = true;
            }
            if (quit) break;
        }
        if (quit) break;
        if (autoPlay && playing && !assets.empty() && assets[cur].sheet == -3 && !assets[cur].steps.empty() && SDL_GetTicks() - lineStart >= auto_ms()) next_line();
        if (playing && tex) {
            Uint32 el = SDL_GetTicks() - playStart;
            int tk = haveWpc ? (int)((el * 30ull / 1000ull) % (Uint32)run.frames.size()) : 0;
            int bf = haveBg ? (int)((el / (Uint32)pm3::kWmpFrameMs) % (Uint32)bgScene.frames.size()) : 0;
            int pfKey = 0;   // changes whenever any portrait blinks
            for (auto& v : sceneP) {
                Uint32 pe = el % (Uint32)v.total; int pf = 0;
                while (pf + 1 < (int)v.frames.size() && pe >= (Uint32)v.dur[pf]) { pe -= (Uint32)v.dur[pf]; pf++; }
                pfKey = pfKey * 8 + pf;
            }
            if (tk != lastTick || bf != lastBf || pfKey != lastPfKey) {
                lastTick = tk; lastBf = bf; lastPfKey = pfKey;
                if (haveBg) {
                    canvas.rgba = bgScene.frames[bf].rgba;
                } else {
                    for (size_t q = 0; q < canvas.rgba.size(); q += 4) {
                        canvas.rgba[q] = 70; canvas.rgba[q + 1] = 90; canvas.rgba[q + 2] = 110; canvas.rgba[q + 3] = 255;
                    }
                }
                if (haveWpc) pm3::wpc_draw(wpc, run.frames[tk], canvas, haveBg ? 0 : pb.x0, haveBg ? 0 : pb.y0);
                for (auto& v : sceneP) {
                    Uint32 pe = el % (Uint32)v.total; int pf = 0;
                    while (pf + 1 < (int)v.frames.size() && pe >= (Uint32)v.dur[pf]) { pe -= (Uint32)v.dur[pf]; pf++; }
                    pm3::draw_portrait(v.w, v.frames[pf], v.x, v.y, canvas);
                }
                if (haveFont && !dlgText.empty()) pm3::draw_dialogue_box(canvas, tfn, dlgText);
                SDL_UpdateTexture(tex, nullptr, canvas.rgba.data(), canvas.w * 4);
                dirty = true;
            }
        }
        if (portraitPlay && tex) {
            Uint32 el = (SDL_GetTicks() - playStart) % (Uint32)pTotal;
            int pf = 0;
            while (pf + 1 < (int)pframes.size() && el >= (Uint32)pdur[pf]) { el -= (Uint32)pdur[pf]; pf++; }
            if (pf != lastPf) {
                lastPf = pf;
                SDL_UpdateTexture(tex, nullptr, pframes[pf].rgba.data(), pframes[pf].w * 4);
                dirty = true;
            }
        }
        if (wavPage && tex && adev && !wavMono.empty()) {   // playhead = fraction of the queued sound already consumed
            Uint32 left = SDL_GetQueuedAudioSize(adev);
            double frac = 1.0 - (double)left / (double)(wavMono.size() * 2);
            int head = left == 0 ? -1 : std::max(0, std::min(wavBase.w - 1, (int)(frac * wavBase.w)));
            if (head != lastHead) {
                lastHead = head;
                pm3::Image im = wavBase;
                if (head >= 0)
                    for (int y = 0; y < im.h; y++) { uint8_t* o = &im.rgba[((size_t)y * im.w + head) * 4]; o[0] = 255; o[1] = 200; o[2] = 60; }
                SDL_UpdateTexture(tex, nullptr, im.rgba.data(), im.w * 4);
                dirty = true;
            }
        }
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
            // iOS draws its status bar (clock/battery) over the top-left; push the HUD below it.
            const int top = (std::strcmp(SDL_GetPlatform(), "iOS") == 0 ? 16 : 0) * ts;
            draw_text(ren, hud, 4 * ts, top + 4 * ts, ts, white);
            int line = 14;
            if (!info2.empty()) { draw_text(ren, info2, 4 * ts, top + line * ts, ts, white); line += 10; }
            if (!status.empty()) draw_text(ren, status, 4 * ts, top + line * ts, ts, warn);
        }
        SDL_RenderPresent(ren);
    }

    free_tex();
    if (adev) SDL_CloseAudioDevice(adev);
    SDL_DestroyRenderer(ren);
    SDL_DestroyWindow(win);
    SDL_Quit();
    return 0;
}
