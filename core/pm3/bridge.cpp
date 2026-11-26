#include "pm3/bridge.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <cstring>
#include <mutex>
#include <thread>
#include <vector>

#include "pm3/display.h"
#include "pm3/midi.h"
#include "pm3/touch.h"
#include "pm3/vfs.h"
#include "pm3/x86host.h"

struct Pm3Bridge {
    pm3::Vfs vfs; pm3::TouchMouse touch; pm3::Viewport vp; std::vector<uint32_t> fb;
    pm3::MidiPlayer midi{44100};                      // BGM synth (PMMIDI.dll replacement); rendered inside pm3_bridge_pull_audio
    std::mutex amu; std::vector<int16_t> abuf, vbuf;   // audio mix queues (stereo, PM3_AUDIO_RATE): abuf = sound effects, vbuf = voices; guarded by amu
    std::atomic<float> vol[3] = {1.0f, 1.0f, 1.0f};  // PM3_VOL_VOICE / BGM / SFX
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
void pm3_bridge_right_click(Pm3Bridge* b, float x, float y) { std::lock_guard<std::mutex> lk(b->mu); b->touch.right_click(x, y); }
int pm3_bridge_poll_mouse(Pm3Bridge* b, Pm3MouseEvent* out) { std::lock_guard<std::mutex> lk(b->mu); pm3::MouseEvent e; if (!b->touch.poll(&e)) return 0; out->type = (int)e.type; out->x = e.x; out->y = e.y; out->button = e.button; return 1; }

void* pm3_bridge_fopen(Pm3Bridge* b, const char* path, const char* mode) { return b->vfs.open(path, mode); }
int pm3_bridge_file_exists(Pm3Bridge* b, const char* path) { return b->vfs.exists(path) ? 1 : 0; }

// ---------------------------------------------------------------- game worker thread
void pm3_bridge_copy_frame(Pm3Bridge* b, uint32_t* dst) { std::lock_guard<std::mutex> lk(b->mu); std::memcpy(dst, b->fb.data(), b->fb.size() * sizeof(uint32_t)); }
int pm3_bridge_game_running(const Pm3Bridge* b) { return b->running.load() ? 1 : 0; }

static void push_pcm(Pm3Bridge* b, const uint8_t* d, uint32_t bytes, int ch, int rate, int bits, bool voice);   // audio section below

int pm3_bridge_start_game(Pm3Bridge* b) {
    if (b->running.load() || b->worker.joinable()) return 0;
    if (!b->vfs.exists("PM3WIN.EXE")) return 0;
    b->quit = false; b->running = true; b->midi.stop();
    b->worker = std::thread([b] {
        std::string game = b->vfs.root(); pm3::X86Hooks h; h.midi = &b->midi; h.save_dir = b->vfs.save_dir();
        auto t0 = std::chrono::steady_clock::now();
        h.now_ms = [t0, b] { return (uint32_t)(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count() - b->paused_ms.load()); };   // guest clock skips paused time
        h.on_pcm_ex = [b](const uint8_t* d, uint32_t n, int ch, int rate, int bits, bool voice) { push_pcm(b, d, n, ch, rate, bits, voice); };
        h.poll_mouse = [b](int* t, int* x, int* y) { std::lock_guard<std::mutex> lk(b->mu); pm3::MouseEvent e; if (!b->touch.poll(&e)) return false; *t = (e.button == 1 && e.type != pm3::MouseEvent::Move) ? (e.type == pm3::MouseEvent::ButtonDown ? 3 : 4) : (int)e.type; *x = e.x; *y = e.y; return true; };
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

// ---------------------------------------------------------------- audio
static void push_pcm(Pm3Bridge* b, const uint8_t* d, uint32_t bytes, int ch, int rate, int bits, bool voice) {
    if (!d || ch < 1 || rate < 1000 || (bits != 8 && bits != 16)) return;
    uint32_t bps = (uint32_t)(bits / 8) * ch, n = bytes / bps; if (!n) return;
    if (getenv("PM3_PCMLOG")) { int pk = 0; for (uint32_t i = 0; i + (bits / 8) <= bytes; i += bits / 8) { int v = bits == 8 ? (int)d[i] - 128 : (int16_t)(d[i] | (d[i + 1] << 8)); pk = std::max(pk, v < 0 ? -v : v); } { if (getenv("PM3_PCMDUMP") && rate == 22050) { static int k = 0; char nm[64]; snprintf(nm, sizeof nm, "/tmp/pcm22_%d.raw", k++); if (FILE* f = fopen(nm, "wb")) { fwrite(d, 1, bytes, f); fclose(f); } }
        fprintf(stderr, "[pcm] %u bytes ch=%d rate=%d bits=%d peak=%d (%s)\n", bytes, ch, rate, bits, pk, bits == 8 ? "8-bit units" : "16-bit"); }}
    auto sample = [&](uint32_t i, int c) -> int32_t { const uint8_t* p = d + (size_t)i * bps + (size_t)c * (bits / 8); return bits == 8 ? ((int32_t)p[0] - 128) * 256 : (int16_t)(p[0] | (p[1] << 8)); };
    uint64_t out_n = (uint64_t)n * PM3_AUDIO_RATE / rate; if (out_n > (uint64_t)PM3_AUDIO_RATE * 20) out_n = (uint64_t)PM3_AUDIO_RATE * 20;
    std::lock_guard<std::mutex> lk(b->amu);
    std::vector<int16_t>& q = voice ? b->vbuf : b->abuf;   // voices and sound effects are kept apart so each group has its own volume
    if (q.size() < out_n * 2) q.resize(out_n * 2, 0);
    for (uint64_t o = 0; o < out_n; o++) {   // linear resample, mono -> both channels, 3+ channels: first two
        double pos = (double)o * rate / PM3_AUDIO_RATE; uint32_t i0 = (uint32_t)pos; if (i0 >= n) i0 = n - 1; uint32_t i1 = i0 + 1 < n ? i0 + 1 : i0; double fr = pos - i0;
        for (int c = 0; c < 2; c++) { int cc = ch > 1 ? c : 0; int32_t v = (int32_t)(sample(i0, cc) * (1.0 - fr) + sample(i1, cc) * fr);
            int32_t m = (int32_t)q[o * 2 + c] + v; q[o * 2 + c] = (int16_t)(m > 32767 ? 32767 : m < -32768 ? -32768 : m); } }
}

static const float kVoiceGain = 0.6f, kBgmGain = 1.54f;   // voice : BGM loudness about 2 : 1 (voice RMS ~8000 -> ~4800, BGM RMS ~1350 -> ~2400)
int pm3_bridge_pull_audio(Pm3Bridge* b, int16_t* dst, int frames) {
    if (frames <= 0) return 0;
    const float gv = kVoiceGain * b->vol[PM3_VOL_VOICE].load(), gs = kVoiceGain * b->vol[PM3_VOL_SFX].load(), gb = kBgmGain * b->vol[PM3_VOL_BGM].load();
    size_t n = 0;
    { std::lock_guard<std::mutex> lk(b->amu);
      size_t ns = std::min<size_t>(b->abuf.size() / 2, (size_t)frames), nv = std::min<size_t>(b->vbuf.size() / 2, (size_t)frames); n = std::max(ns, nv);
      for (size_t i = 0; i < (size_t)frames * 2; i++) {   // sound effects and voices: separate gains, then summed with saturation
          float v = 0.f; if (i < ns * 2) v += (float)b->abuf[i] * gs; if (i < nv * 2) v += (float)b->vbuf[i] * gv;
          dst[i] = (int16_t)(v > 32767.f ? 32767.f : v < -32768.f ? -32768.f : v); }
      if (ns) b->abuf.erase(b->abuf.begin(), b->abuf.begin() + (ptrdiff_t)(ns * 2));
      if (nv) b->vbuf.erase(b->vbuf.begin(), b->vbuf.begin() + (ptrdiff_t)(nv * 2)); }
    if (!b->paused.load() && gb > 0.f) {   // BGM (soft knee above ~80% so BGM + loud voice never hard-clips): synthesize and add on top of the PCM (SE / voice)
        std::vector<float> m((size_t)frames * 2, 0.f); b->midi.render(m.data(), frames);
        for (size_t i = 0; i < m.size(); i++) { if (m[i] == 0.f) continue; float v = (float)dst[i] + m[i] * 32767.f * gb, av = v < 0 ? -v : v; if (av > 26000.f) { av = 26000.f + 6767.f * std::tanh((av - 26000.f) / 6767.f); v = v < 0 ? -av : av; } dst[i] = (int16_t)(v > 32767.f ? 32767.f : v < -32768.f ? -32768.f : v); n = std::max<size_t>(n, i / 2 + 1); }   // BGM counts as sound: the return value drives AVAudioEngine's isSilence flag
    }
    return (int)n;
}

void pm3_bridge_set_volume(Pm3Bridge* b, int channel, float v) { if (channel < 0 || channel > 2) return; if (!(v >= 0.f)) v = 0.f; if (v > 2.f) v = 2.f; b->vol[channel] = v; }   // NaN -> 0
float pm3_bridge_get_volume(Pm3Bridge* b, int channel) { return (channel < 0 || channel > 2) ? 0.f : b->vol[channel].load(); }

void pm3_bridge_audio_push(Pm3Bridge* b, int channel, const uint8_t* d, uint32_t bytes, int ch, int rate, int bits) {
    if (channel != PM3_VOL_VOICE && channel != PM3_VOL_SFX) return;
    push_pcm(b, d, bytes, ch, rate, bits, channel == PM3_VOL_VOICE);
}

void pm3_bridge_audio_preview(Pm3Bridge* b, int channel) {
    if (channel == PM3_VOL_BGM) {   // C - E - G - C arpeggio (format 0, 96 ticks per quarter, 120 bpm); the game owns the synth while it runs
        if (b->running.load()) return;
        static const uint8_t smf[] = {'M','T','h','d',0,0,0,6,0,0,0,1,0,96, 'M','T','r','k',0,0,0,52,
            0,0xFF,0x51,3,0x07,0xA1,0x20, 0,0xC0,0,
            0,0x90,60,90, 48,0x80,60,0,  0,0x90,64,90, 48,0x80,64,0,  0,0x90,67,90, 48,0x80,67,0,  0,0x90,72,90, 96,0x80,72,0, 0,0xFF,0x2F,0};
        b->midi.stop(); if (b->midi.load(smf, sizeof smf)) b->midi.play(false);
        return;
    }
    if (channel != PM3_VOL_VOICE && channel != PM3_VOL_SFX) return;
    const int rate = 22050; const bool voice = channel == PM3_VOL_VOICE; const int n = (int)(rate * (voice ? 0.5 : 0.3)); std::vector<int16_t> pcm((size_t)n);
    for (int i = 0; i < n; i++) {   // decaying blip(s): SFX one high note, voice two lower notes; ~peak 9000 like the game's own sounds
        double t = (double)i / rate, env = std::exp(-6.0 * (voice ? std::fmod(t, 0.25) : t)), f = voice ? (t < 0.25 ? 330.0 : 440.0) : 880.0;
        pcm[(size_t)i] = (int16_t)(9000.0 * env * std::sin(6.2831853 * f * t)); }
    push_pcm(b, (const uint8_t*)pcm.data(), (uint32_t)(pcm.size() * 2), 1, rate, 16, voice);
}
