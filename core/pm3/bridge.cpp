#include "pm3/bridge.h"

#include <atomic>
#include <chrono>
#include <cstring>
#include <mutex>
#include <thread>
#include <vector>

#include "pm3/display.h"
#include "pm3/touch.h"
#include "pm3/vfs.h"
#include "pm3/x86host.h"

struct Pm3Bridge {
    pm3::Vfs vfs; pm3::TouchMouse touch; pm3::Viewport vp; std::vector<uint32_t> fb;
    std::mutex mu;                                   // guards touch queue + fb (UI thread <-> game thread)
    std::thread worker; std::atomic<bool> quit{false}, running{false}, paused{false}; std::atomic<uint64_t> paused_ms{0};
    Pm3Bridge() : fb((size_t)pm3::kGameW * pm3::kGameH, 0xFF000000u) { vp = pm3::fit_viewport(pm3::kGameW, pm3::kGameH); touch.set_viewport(vp); }
};

Pm3Bridge* pm3_bridge_create(void) { return new Pm3Bridge(); }
void pm3_bridge_destroy(Pm3Bridge* b) { if (b) pm3_bridge_stop_game(b); delete b; }
void pm3_bridge_set_paths(Pm3Bridge* b, const char* game, const char* save) { b->vfs.set_root(game ? game : ""); b->vfs.set_save_dir(save ? save : ""); }
int pm3_bridge_width(void) { return pm3::kGameW; }
int pm3_bridge_height(void) { return pm3::kGameH; }
uint32_t* pm3_bridge_framebuffer(Pm3Bridge* b) { return b->fb.data(); }

void pm3_bridge_present_surface(Pm3Bridge* b, const uint8_t* src, int w, int h, int pitch, int fmt, const uint32_t* pal) {
    if (!src || w <= 0 || h <= 0) return;
    std::lock_guard<std::mutex> lk(b->mu);
    if (w == pm3::kGameW && h == pm3::kGameH) { pm3::convert_to_argb(src, w, h, pitch, (pm3::PixFmt)fmt, pal, b->fb.data()); return; }
    std::vector<uint32_t> tmp((size_t)w * h); pm3::convert_to_argb(src, w, h, pitch, (pm3::PixFmt)fmt, pal, tmp.data());
    for (int y = 0; y < pm3::kGameH; y++) for (int x = 0; x < pm3::kGameW; x++) b->fb[(size_t)y * pm3::kGameW + x] = tmp[(size_t)(y * h / pm3::kGameH) * w + (x * w / pm3::kGameW)];
}

void pm3_bridge_set_view_size(Pm3Bridge* b, float vw, float vh, int integer_scale) { std::lock_guard<std::mutex> lk(b->mu); b->vp = pm3::fit_viewport(vw, vh, pm3::kGameW, pm3::kGameH, integer_scale != 0); b->touch.set_viewport(b->vp); }
Pm3Viewport pm3_bridge_viewport(const Pm3Bridge* b) { return Pm3Viewport{b->vp.x, b->vp.y, b->vp.w, b->vp.h, b->vp.scale}; }

void pm3_bridge_touch_begin(Pm3Bridge* b, int id, float x, float y) { std::lock_guard<std::mutex> lk(b->mu); b->touch.touch_begin(id, x, y); }
void pm3_bridge_touch_move(Pm3Bridge* b, int id, float x, float y) { std::lock_guard<std::mutex> lk(b->mu); b->touch.touch_move(id, x, y); }
void pm3_bridge_touch_end(Pm3Bridge* b, int id, float x, float y) { std::lock_guard<std::mutex> lk(b->mu); b->touch.touch_end(id, x, y); }
void pm3_bridge_touch_cancel(Pm3Bridge* b, int id) { std::lock_guard<std::mutex> lk(b->mu); b->touch.touch_cancel(id); }
int pm3_bridge_poll_mouse(Pm3Bridge* b, Pm3MouseEvent* out) { std::lock_guard<std::mutex> lk(b->mu); pm3::MouseEvent e; if (!b->touch.poll(&e)) return 0; out->type = (int)e.type; out->x = e.x; out->y = e.y; out->button = e.button; return 1; }

void* pm3_bridge_fopen(Pm3Bridge* b, const char* path, const char* mode) { return b->vfs.open(path, mode); }
int pm3_bridge_file_exists(Pm3Bridge* b, const char* path) { return b->vfs.exists(path) ? 1 : 0; }

// ---------------------------------------------------------------- game worker thread
void pm3_bridge_copy_frame(Pm3Bridge* b, uint32_t* dst) { std::lock_guard<std::mutex> lk(b->mu); std::memcpy(dst, b->fb.data(), b->fb.size() * sizeof(uint32_t)); }
int pm3_bridge_game_running(const Pm3Bridge* b) { return b->running.load() ? 1 : 0; }

int pm3_bridge_start_game(Pm3Bridge* b) {
    if (b->running.load() || b->worker.joinable()) return 0;
    if (!b->vfs.exists("PM3WIN.EXE")) return 0;
    b->quit = false; b->running = true;
    b->worker = std::thread([b] {
        std::string game = b->vfs.root(); pm3::X86Hooks h; h.save_dir = b->vfs.save_dir();
        auto t0 = std::chrono::steady_clock::now();
        h.now_ms = [t0, b] { return (uint32_t)(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count() - b->paused_ms.load()); };   // guest clock skips paused time
        h.poll_mouse = [b](int* t, int* x, int* y) { std::lock_guard<std::mutex> lk(b->mu); pm3::MouseEvent e; if (!b->touch.poll(&e)) return false; *t = (int)e.type; *x = e.x; *y = e.y; return true; };
        h.on_frame = [b](const pm3::X86Frame& f) {
            if (b->quit.load()) return false;
            if (b->paused.load()) { auto p0 = std::chrono::steady_clock::now();   // frozen: keep the last frame, wait
                while (b->paused.load() && !b->quit.load()) std::this_thread::sleep_for(std::chrono::milliseconds(20));
                b->paused_ms += (uint64_t)std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - p0).count(); if (b->quit.load()) return false; }
            pm3_bridge_present_surface(b, f.pixels, f.width, f.height, f.pitch, f.bpp == 16 ? PM3_FMT_RGB565 : PM3_FMT_BGRX32, nullptr);
            if (f.idle) std::this_thread::sleep_for(std::chrono::milliseconds(10));   // guest is only polling messages
            return !b->quit.load();
        };
        std::string a0 = "x86host", a2 = "0"; char* argv[3] = {&a0[0], &game[0], &a2[0]};
        pm3::x86host_main(3, argv, &h);
        b->running = false;
    });
    return 1;
}

void pm3_bridge_set_paused(Pm3Bridge* b, int paused) { b->paused = paused != 0; }
void pm3_bridge_stop_game(Pm3Bridge* b) { b->quit = true; if (b->worker.joinable()) b->worker.join(); b->running = false; }
