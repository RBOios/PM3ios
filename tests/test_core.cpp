// Self-contained tests that need no game data.
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <cmath>

#include "pm3/abp.h"
#include "pm3/bitreader.h"
#include "pm3/evx.h"
#include "pm3/wav.h"
#include "pm3/bridge.h"
#include "pm3/display.h"
#include "pm3/touch.h"
#include "pm3/midi.h"
#include "pm3/vfs.h"
#include <cstdlib>
#include <sys/stat.h>
#include <unistd.h>

static int fails = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

struct BitWriter {  // MSB-first into LE32 words
    std::vector<uint8_t> out; uint32_t cur = 0; int n = 0;
    void put(uint32_t v, int bits) {
        for (int i = bits - 1; i >= 0; i--) {
            cur = (cur << 1) | ((v >> i) & 1);
            if (++n == 32) flush();
        }
    }
    void flush() { for (int i = 0; i < 4; i++) out.push_back((cur >> (8 * i)) & 0xFF); cur = 0; n = 0; }
    void finish() { if (n) { cur <<= (32 - n); flush(); } }
};

int main() {
    // BitReader: words are LE, bits MSB-first
    { uint8_t b[8] = {0x78, 0x56, 0x34, 0x12, 0xFF, 0, 0, 0};
      pm3::BitReader br(b, 8, 0);
      CHECK(br.bits(8) == 0x12); CHECK(br.bits(4) == 0x3); CHECK(br.bits(20) == 0x45678); CHECK(br.bits(8) == 0); }
    // BitReader past the end yields zeros
    { uint8_t b[2] = {0xFF, 0xFF}; pm3::BitReader br(b, 2, 0); CHECK(br.bits(16) == 0); }

    // EVX: header + one string + main: SETV(5, 7); R = 3*4; SETV(6, R) ; RET.
    //   push 7; push 5; SETV; drop 2 ... (arguments pushed in reverse: arg(0) = last pushed)
    { std::vector<uint8_t> d = {'E','V','X','0', 0,0,0,0, 0,0, 0x10,0, 0,0,0,0};
      d.push_back('H'); d.push_back('i'); d.push_back(0);                    // string "Hi" @0x10
      size_t entry = d.size();
      auto push = [&](uint16_t v) { d.push_back(0x01); d.push_back(v & 0xff); d.push_back(v >> 8); };
      push(7); push(5); d.push_back(0x56); d.push_back(0x0a); d.push_back(2);   // SETV(5,7)
      push(4); push(3); d.push_back(0x22);                                      // R = 3*4
      d.push_back(0x04); push(6); d.push_back(0x56); d.push_back(0x0a); d.push_back(2);  // SETV(6,R)
      d.push_back(0x0b);
      d[8] = entry & 0xff; d[9] = entry >> 8;
      pm3::EvxScript sc = pm3::EvxScript::load("T", d);
      CHECK(sc.code == entry); CHECK(sc.strs.size() == 1 && sc.strs.at(0x10) == "Hi");
      auto ins = sc.sweep(); CHECK(ins.size() == 12); CHECK(ins[0].op == 0x01 && ins[0].arg == 7 && ins[0].len == 3);
      pm3::EvxVm vm(sc); CHECK(vm.run());
      CHECK(vm.V[5] == 7); CHECK(vm.V[6] == 12);
      CHECK(vm.log.size() == 3 && vm.log[2] == "<return from main>"); }

    // EVX exe_vars mode: EXE-faithful GETV/SETV (clamping, special fields, old value returned in R)
    { std::vector<uint8_t> d = {'E','V','X','0', 0,0,0,0, 0,0, 0x10,0, 0,0,0,0};
      d.push_back(0);                                                          // empty string area
      size_t entry = d.size();
      auto push = [&](uint16_t v) { d.push_back(0x01); d.push_back(v & 0xff); d.push_back(v >> 8); };
      push(7); push(5); d.push_back(0x56); d.push_back(0x0a); d.push_back(2);   // SETV(5,7)  (special: BYTE field)
      push(4); push(3); d.push_back(0x22);                                      // R = 12
      d.push_back(0x04); push(6); d.push_back(0x56); d.push_back(0x0a); d.push_back(2);  // SETV(6,R) (special: WORD field)
      d.push_back(0x0b);
      d[8] = entry & 0xff; d[9] = entry >> 8;
      pm3::EvxScript sc = pm3::EvxScript::load("T2", d);
      pm3::EvxVm vm(sc); vm.exe_vars = true; CHECK(vm.run());
      CHECK(vm.getv(5) == 7); CHECK(vm.getv(6) == 12);
      CHECK(vm.V.empty());                                                      // specials never touch the fallback map
      pm3::EvxVm v2(sc); v2.exe_vars = true;
      v2.setv(4, 12); CHECK(v2.getv(4) == 12);                                  // age field stores value-10, reads +10
      v2.setv(4, 13); CHECK(v2.result() == 12);                                 // old value comes back in R
      v2.setv(6, 20000); CHECK(v2.getv(6) == 9999);                             // clamp 0..9999
      v2.setv(6, -5); CHECK(v2.getv(6) == 0);
      v2.setv(7, 3); CHECK(v2.getv(7) == 3); v2.setv(7, 10); CHECK(v2.result() == 3);   // field offset +1
      v2.setv(0x120, -5); CHECK(v2.getv(0x120) == -5);                          // direct array: raw s16, no clamp
      v2.setv(0x120, 7); CHECK(v2.result() == (uint16_t)-5);
      v2.setv(0x37f, 1); CHECK(v2.getv(0x37f) == 1);
      v2.setv(0x380, 20000); CHECK(v2.getv(0x380) == 9999);                     // first non-direct var: clamped fallback
      v2.setv(100, -3); CHECK(v2.getv(100) == 0); }

    // ---- VFS: case-insensitive lookup, drive/alias stripping, save dir shadows the read-only game dir ----
    { char tmpl[] = "/tmp/pm3vfsXXXXXX"; std::string base = mkdtemp(tmpl);
      std::string game = base + "/game", save = base + "/docs"; mkdir(game.c_str(), 0777); mkdir((game + "/Midi").c_str(), 0777);
      { FILE* f = fopen((game + "/Midi/M001.MID").c_str(), "wb"); fputs("MIDI", f); fclose(f); }
      { FILE* f = fopen((game + "/PM3SYS0.FPA").c_str(), "wb"); fputs("FPA", f); fclose(f); }
      pm3::Vfs v; v.set_root(game); v.set_save_dir(save);
      CHECK(v.exists("D:\\PM3W\\midi\\m001.mid")); CHECK(v.exists("c:\\pm3\\MIDI\\M001.MID")); CHECK(v.exists("pm3sys0.fpa"));
      CHECK(!v.exists("MIDI\\M999.MID")); CHECK(!v.exists("..\\etc\\passwd")); CHECK(v.file_size("PM3SYS0.FPA") == 3);
      std::string rel; CHECK(v.normalize("D:\\PM3W\\ID_PM3JP.DAT", &rel) && rel == "ID_PM3JP.DAT"); CHECK(v.normalize("C:\\PM3", &rel) && rel.empty());
      FILE* w = v.open("SAVE\\Game1.SAV", "wb"); CHECK(w != nullptr); if (w) { fputs("SAVED", w); fclose(w); }
      CHECK(v.find("save/game1.sav") == save + "/SAVE/Game1.SAV");                       // created under the save dir, found case-insensitively
      FILE* r = v.open("midi\\m001.mid", "r+b"); CHECK(r != nullptr); if (r) { fseek(r, 0, SEEK_SET); fputs("XXXX", r); fclose(r); }
      { FILE* f = fopen((game + "/Midi/M001.MID").c_str(), "rb"); char b4[5] = {0}; size_t got = fread(b4, 1, 4, f); fclose(f); CHECK(got == 4); CHECK(std::string(b4) == "MIDI"); }   // shipped file untouched
      CHECK(v.find("MIDI/M001.MID").find(save) == 0);                                    // save copy now shadows it
      pm3::Vfs ro; ro.set_root(game); CHECK(ro.open("new.txt", "wb") == nullptr); }       // no save dir -> no writes
    // ---- display: aspect-fit viewport, touch inverse mapping, pixel conversion ----
    { pm3::Viewport p = pm3::fit_viewport(390, 844);                                        // iPhone portrait (points)
      CHECK(p.x == 0 && p.w == 390 && p.h > 292.4f && p.h < 292.6f && p.y > 275.f && p.y < 276.f);
      int gx, gy; CHECK(pm3::view_to_game(p, 195, 422, &gx, &gy) && gx == 320 && gy == 240);
      CHECK(!pm3::view_to_game(p, 195, 10, &gx, &gy) && gy == 0);                           // letterbox bar, clamped
      pm3::Viewport q = pm3::fit_viewport(1024, 768); CHECK(q.scale == 1.6f && q.x == 0 && q.y == 0);   // iPad 4:3 fills
      pm3::Viewport z = pm3::fit_viewport(1280, 960, 640, 480, true); CHECK(z.scale == 2.0f);
      pm3::Viewport e = pm3::fit_viewport(1000, 500, 640, 480, true); CHECK(e.scale == 1.0f);                  // 1.04 snaps down to 1
      pm3::Viewport sm = pm3::fit_viewport(600, 450, 640, 480, true); CHECK(sm.scale == 0.9375f);              // below 1 is never snapped
      float vx, vy; pm3::game_to_view(q, 100, 50, &vx, &vy); CHECK(vx == 160.f && vy == 80.f);
      uint8_t s565[4] = {0x00, 0xF8, 0xE0, 0x07}; uint32_t o[2];                            // 0xF800 red, 0x07E0 green (little endian)
      pm3::convert_to_argb(s565, 2, 1, 4, pm3::PixFmt::Rgb565, nullptr, o); CHECK(o[0] == 0xFFFF0000u && o[1] == 0xFF00FF00u);
      uint8_t b24[3] = {1, 2, 3}; pm3::convert_to_argb(b24, 1, 1, 3, pm3::PixFmt::Bgr24, nullptr, o); CHECK(o[0] == 0xFF030201u);
      uint8_t bx[8] = {0x10, 0x20, 0x30, 0x00, 0xFF, 0xFF, 0xFF, 0x7F}; uint32_t o4[2]; pm3::convert_to_argb(bx, 2, 1, 8, pm3::PixFmt::Bgrx32, nullptr, o4); CHECK(o4[0] == 0xFF302010u && o4[1] == 0xFFFFFFFFu);
      uint8_t q4[8] = {0, 0, 0, 0, 0x11, 0x22, 0x33, 0}; uint32_t pal[2]; pm3::palette_from_bgrx(q4, 2, pal); CHECK(pal[0] == 0xFF000000u && pal[1] == 0xFF332211u);
      uint8_t idx[2] = {1, 0}; pm3::convert_to_argb(idx, 2, 1, 2, pm3::PixFmt::Pal8, pal, o); CHECK(o[0] == 0xFF332211u && o[1] == 0xFF000000u); }
    // ---- touch -> mouse: hover while moving, click on release, bars/second finger ignored ----
    { pm3::TouchMouse t; t.set_viewport(pm3::fit_viewport(390, 844)); pm3::MouseEvent e;
      t.touch_begin(7, 195, 5); CHECK(!t.poll(&e) && !t.tracking());                         // started in the top bar
      t.touch_begin(1, 195, 422); CHECK(t.poll(&e) && e.type == pm3::MouseEvent::Move && e.x == 320 && e.y == 240 && !t.poll(&e));
      t.touch_begin(2, 100, 400); CHECK(!t.poll(&e));                                       // second finger
      t.touch_move(1, 195 + 31.25f, 422); CHECK(t.poll(&e) && e.type == pm3::MouseEvent::Move && e.x == 371);   // (195+31.25)/0.609375 = 371.28 -> 371
      t.touch_end(1, 195, 422);
      CHECK(t.poll(&e) && e.type == pm3::MouseEvent::Move && e.x == 320); CHECK(t.poll(&e) && e.type == pm3::MouseEvent::ButtonDown); CHECK(t.poll(&e) && e.type == pm3::MouseEvent::ButtonUp && e.x == 320 && e.y == 240);
      CHECK(!t.poll(&e) && !t.tracking());
      t.touch_begin(3, 195, 422); t.poll(&e); t.touch_cancel(3); t.touch_end(3, 195, 422); CHECK(!t.poll(&e)); }   // cancel -> no click
    // ---- bridge C API: frame buffer upload, viewport, input, files ----
    { Pm3Bridge* b = pm3_bridge_create(); char tmpl[] = "/tmp/pm3brXXXXXX"; std::string base = mkdtemp(tmpl); mkdir((base + "/g").c_str(), 0777);
      { FILE* f = fopen((base + "/g/ID_PM3JP.DAT").c_str(), "wb"); fputs("x", f); fclose(f); }
      pm3_bridge_set_paths(b, (base + "/g").c_str(), (base + "/docs").c_str()); CHECK(pm3_bridge_file_exists(b, "d:\\pm3w\\id_pm3jp.dat") == 1);
      CHECK(pm3_bridge_width() == 640 && pm3_bridge_height() == 480);
      std::vector<uint8_t> half(320 * 240 * 2, 0); half[0] = 0x00; half[1] = 0xF8;           // top-left source pixel red (RGB565)
      pm3_bridge_present_surface(b, half.data(), 320, 240, 640, PM3_FMT_RGB565, nullptr);
      uint32_t* fb = pm3_bridge_framebuffer(b); CHECK(fb[0] == 0xFFFF0000u && fb[1] == 0xFFFF0000u && fb[640] == 0xFFFF0000u && fb[2] == 0xFF000000u);   // 2x nearest upscale
      pm3_bridge_set_view_size(b, 1170, 2532, 0); Pm3Viewport vp = pm3_bridge_viewport(b); CHECK(vp.w == 1170.f && vp.scale > 1.82f && vp.scale < 1.83f);
      pm3_bridge_touch_begin(b, 1, 585, 1266); pm3_bridge_touch_end(b, 1, 585, 1266); Pm3MouseEvent me; int n = 0; while (pm3_bridge_poll_mouse(b, &me)) n++; CHECK(n == 3);
      FILE* f = (FILE*)pm3_bridge_fopen(b, "A.SAV", "wb"); CHECK(f != nullptr); if (f) fclose(f); CHECK(pm3_bridge_file_exists(b, "a.sav") == 1);
      pm3_bridge_destroy(b); }

    // ABP: literals "ABC", then copy (pos=1,len=3 -> "ABC"), then terminator.
    { BitWriter w;
      for (char c : std::string("ABC")) { w.put(1, 1); w.put((uint8_t)c, 8); }
      w.put(0, 1); w.put(1, 12); w.put(1, 4);   // pos 1, len = 1+2 = 3
      w.put(0, 1); w.put(0, 12);                // end
      w.finish();
      std::vector<uint8_t> blob(0x18, 0);
      std::memcpy(blob.data(), "ABP_", 4); blob[4] = 6;
      blob.insert(blob.end(), w.out.begin(), w.out.end());
      pm3::Bytes out; uint32_t d = 0;
      CHECK(pm3::abp_decompress(blob.data(), blob.size(), out, &d));
      CHECK(d == 6); CHECK(std::string(out.begin(), out.end()) == "ABCABC"); }
    // ABP: bad magic
    { uint8_t z[0x20] = {0}; pm3::Bytes o; CHECK(!pm3::abp_decompress(z, sizeof z, o)); }

    // WAV: PCM16 passthrough (no RIFF wrapper) and IMA ADPCM block (predictor 1000, index 0, all-zero nibbles -> flat)
    { uint8_t w[] = {'f','m','t',' ',16,0,0,0, 1,0, 1,0, 0x40,0x1F,0,0, 0x80,0x3E,0,0, 2,0, 16,0, 'd','a','t','a',4,0,0,0, 0x34,0x12, 0xFE,0xFF};
      pm3::Pcm p; CHECK(pm3::wav_decode(w, sizeof w, p)); CHECK(p.rate == 8000 && p.channels == 1);
      CHECK(p.samples.size() == 2 && p.samples[0] == 0x1234 && p.samples[1] == -2);
      CHECK(pm3::pcm_to_riff(p).size() == 44 + 4); }
    { std::vector<uint8_t> w = {'f','m','t',' ',20,0,0,0, 0x11,0, 1,0, 0x22,0x56,0,0, 0x5C,0x2B,0,0, 0,2, 4,0, 2,0, 0xF9,3,
                                'f','a','c','t',4,0,0,0, 0xF9,3,0,0, 'd','a','t','a',0,2,0,0};
      std::vector<uint8_t> blk(512, 0); blk[0] = 0xE8; blk[1] = 0x03; w.insert(w.end(), blk.begin(), blk.end());
      pm3::Pcm p; CHECK(pm3::wav_decode(w.data(), w.size(), p)); CHECK(p.rate == 22050);
      CHECK(p.samples.size() == 1017); CHECK(p.samples[0] == 1000 && p.samples[1016] == 1000); }
    { uint8_t bad[] = {'x','m','t',' ',16,0,0,0}; pm3::Pcm p; CHECK(!pm3::wav_decode(bad, sizeof bad, p)); }

    // Resample: stereo -> mono mix-down, 8 kHz -> 22.05 kHz length and ramp interpolation
    { pm3::Pcm st; st.rate = 8000; st.channels = 2; st.samples = {100, 300, -200, 200};
      auto m = pm3::pcm_to_mono_rate(st, 8000); CHECK(m.size() == 2 && m[0] == 200 && m[1] == 0);
      pm3::Pcm r; r.rate = 8000; r.channels = 1; r.samples.assign(8000, 0); for (int i = 0; i < 8000; i++) r.samples[i] = (int16_t)(i * 4);
      auto o = pm3::pcm_to_mono_rate(r, 22050); CHECK(o.size() == 22050);
      CHECK(o[0] == 0); CHECK(o[11025] > 15900 && o[11025] < 16100);   // halfway through the ramp ~ 4000*4
      bool mono = true; for (size_t i = 1; i < o.size(); i++) if (o[i] < o[i - 1]) mono = false; CHECK(mono); }

    // MIDI: tiny SMF (div 48, 120 bpm = 24 ticks/0.25 s): C4 for 1 beat (0.5 s), then silence; non-loop ends, loop keeps playing
    { std::vector<uint8_t> f = {'M','T','h','d',0,0,0,6,0,0,0,1,0,48, 'M','T','r','k',0,0,0,0};
      std::vector<uint8_t> tr = {0,0xC0,0, 0,0x90,60,100, 48,0x80,60,0, 48,0xFF,0x2F,0};
      f[21] = (uint8_t)tr.size(); f.insert(f.end(), tr.begin(), tr.end());
      pm3::MidiPlayer m(44100); CHECK(m.load(f.data(), f.size())); CHECK(m.length_seconds() > 0.49 && m.length_seconds() < 0.51);
      std::vector<float> b(44100 * 2 * 3, 0.f); m.play(false); CHECK(m.playing());
      for (int i = 0; i < 44100 * 3; i += 441) m.render(&b[i * 2], 441);
      auto rms = [&](int a, int z) { double e = 0; for (int i = a * 2; i < z * 2; i++) e += b[i] * b[i]; return std::sqrt(e / ((z - a) * 2)); };
      CHECK(rms(1000, 20000) > 0.01); CHECK(rms(44100 * 2, 44100 * 3) < 1e-6); CHECK(!m.playing());
      std::fill(b.begin(), b.end(), 0.f); m.play(true); for (int i = 0; i < 44100 * 3; i += 441) m.render(&b[i * 2], 441);
      CHECK(m.playing()); CHECK(rms(44100 * 2 + 1000, 44100 * 2 + 20000) > 0.01);   // second loop pass still sounds
      m.stop(); CHECK(!m.playing()); uint8_t junk[] = {1, 2, 3}; CHECK(!m.load(junk, 3)); }

    std::printf(fails ? "FAILED\n" : "all tests passed\n");
    return fails ? 1 : 0;
}
