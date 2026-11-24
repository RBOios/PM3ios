// pm3_dump list   <game_dir>            -> "<archive> <name> <ext> <begin> <end>" per entry
// pm3_dump images <game_dir> <out_dir>  -> PPM export mirroring py/export_images.py
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <iostream>

#include "pm3/fpa.h"
#include "pm3/wav.h"
#include "pm3/image.h"
#include "pm3/evx.h"
#include "pm3/scene.h"
#include "pm3/tfn.h"
#include "pm3/wbd.h"
#include "pm3/wpc.h"

namespace fs = std::filesystem;
using namespace pm3;

static std::string find_ci(const std::string& dir, const std::string& name) {
    for (auto& e : fs::directory_iterator(dir)) {
        std::string f = e.path().filename().string(), a = f, b = name;
        for (auto& c : a) c = (char)toupper(c);
        for (auto& c : b) c = (char)toupper(c);
        if (a == b) return e.path().string();
    }
    return "";
}

int main(int argc, char** argv) {
    if (argc < 3) { std::fprintf(stderr, "usage: pm3_dump list|images <game_dir> [out_dir]\n"); return 2; }
    std::string cmd = argv[1], dir = argv[2];
    if (cmd == "list") {
        for (const char* an : {"PM3SYS0", "PM3DAT1", "PM3DAT2", "PM3TAK1", "PM3TAK2"}) {
            std::string p = find_ci(dir, std::string(an) + ".FPA");
            if (p.empty()) continue;
            Fpa f; std::string err;
            if (!f.open(p, &err)) { std::fprintf(stderr, "%s: %s\n", an, err.c_str()); return 1; }
            for (auto& e : f.entries())
                std::printf("%s %s %s %llu %llu\n", an, e.name.c_str(), e.ext.c_str(),
                            (unsigned long long)e.begin, (unsigned long long)e.end);
        }
        return 0;
    }
    if (cmd == "wav" && argc >= 4) {   // export every WAV entry as a standard RIFF .wav (PCM16)
        std::string out = argv[3]; std::filesystem::create_directories(out); int ok = 0, bad = 0;
        for (const char* an : {"PM3SYS0", "PM3TAK1", "PM3TAK2"}) {
            std::string p = find_ci(dir, std::string(an) + ".FPA"); if (p.empty()) continue;
            Fpa f; std::string err; if (!f.open(p, &err)) { std::fprintf(stderr, "%s: %s\n", an, err.c_str()); return 1; }
            for (auto& e : f.entries()) {
                if (e.ext != "WAV") continue;
                Bytes raw; Pcm pcm;
                if (!f.read(e, raw) || !wav_decode(raw.data(), raw.size(), pcm)) { std::fprintf(stderr, "bad %s/%s\n", an, e.name.c_str()); bad++; continue; }
                Bytes w = pcm_to_riff(pcm);
                std::ofstream o(out + "/" + an + "_" + e.name + ".wav", std::ios::binary); o.write((const char*)w.data(), (std::streamsize)w.size()); ok++;
            }
        }
        std::printf("exported %d bad %d\n", ok, bad); return bad ? 1 : 0;
    }
    if (cmd == "images" && argc >= 4) {
        std::string out = argv[3];
        fs::create_directories(out + "/wmp");
        int ok = 0, skip = 0;
        auto save = [&](const std::string& path, const Image& im) { if (write_ppm(path, im)) ok++; else skip++; };
        if (std::string p = find_ci(dir, "PM3SYS0.FPA"); !p.empty()) {
            Fpa f; f.open(p);
            Bytes r;
            for (auto& e : f.entries()) {
                if (!f.read(e, r)) { skip++; continue; }
                try {
                    if (e.ext == "GDT") save(out + "/" + e.name + ".ppm", decode_gdt_auto(r.data(), r.size()));
                    else if (e.ext == "WPC") {
                        auto sheets = decode_wpc_sheets(r.data(), r.size());
                        for (size_t i = 0; i < sheets.size(); i++)
                            save(out + "/" + e.name + "_" + std::to_string(i) + ".ppm", sheets[i]);
                    }
                } catch (const std::exception& ex) { std::fprintf(stderr, "fail %s: %s\n", e.name.c_str(), ex.what()); skip++; }
            }
        }
        for (const char* an : {"PM3DAT1", "PM3DAT2"}) {
            std::string p = find_ci(dir, std::string(an) + ".FPA");
            if (p.empty()) continue;
            Fpa f; f.open(p);
            Bytes r;
            for (auto& e : f.entries()) {
                if (e.ext != "WMP" || !f.read(e, r)) continue;
                try { save(out + "/wmp/" + e.name + ".ppm", decode_wmp(r.data(), r.size())); }
                catch (const std::exception& ex) { std::fprintf(stderr, "fail %s: %s\n", e.name.c_str(), ex.what()); skip++; }
            }
        }
        std::printf("exported %d skipped %d\n", ok, skip);
        return 0;
    }
    // pm3_dump wmpscene <game_dir> <out_dir> -> <out_dir>/<M####>.ppm (frame 0) + <out_dir>/frames.txt
    if (cmd == "wmpscene" && argc >= 4) {
        std::string out = argv[3];
        fs::create_directories(out);
        FILE* t = std::fopen((out + "/frames.txt").c_str(), "w");
        int ok = 0, bad = 0;
        for (const char* an : {"PM3DAT1", "PM3DAT2"}) {
            std::string p = find_ci(dir, std::string(an) + ".FPA");
            if (p.empty()) continue;
            Fpa f; f.open(p);
            Bytes r;
            for (auto& e : f.entries()) {
                if (e.ext != "WMP" || !f.read(e, r)) continue;
                try {
                    WmpScene sc = wmp_scene_load(r.data(), r.size());
                    write_ppm(out + "/" + e.name + ".ppm", sc.frames[0]);
                    std::fprintf(t, "%s %zu\n", e.name.c_str(), sc.frames.size());
                    ok++;
                } catch (const std::exception& ex) { std::fprintf(stderr, "fail %s: %s\n", e.name.c_str(), ex.what()); bad++; }
            }
        }
        std::fclose(t);
        std::printf("wmpscene ok %d fail %d\n", ok, bad);
        return 0;
    }
    // pm3_dump font <FONT.TFN> <out.ppm> <hex-cp949-bytes>  -> text line rendering (verification of the TFN reader)
    if (cmd == "font" && argc >= 5) {
        FILE* f = std::fopen(dir.c_str(), "rb"); if (!f) return 1;
        Bytes d; { uint8_t b[65536]; size_t k; while ((k = std::fread(b, 1, sizeof b, f)) > 0) d.insert(d.end(), b, b + k); }
        std::fclose(f);
        Tfn t; std::string er; if (!t.load(d.data(), d.size(), &er)) { std::fprintf(stderr, "%s\n", er.c_str()); return 1; }
        std::string hx = argv[4], s;
        for (size_t i = 0; i + 1 < hx.size(); i += 2) s += (char)std::strtol(hx.substr(i, 2).c_str(), nullptr, 16);
        Image im; im.w = t.text_width(s) + 8; im.h = t.height() + 8; im.rgba.assign((size_t)im.w * im.h * 4, 0);
        for (size_t i = 3; i < im.rgba.size(); i += 4) im.rgba[i] = 255;
        t.draw_text(im, 4, 4, s, 0xffffff);
        write_ppm(argv[3], im); return 0;
    }
    // pm3_dump wbd <game_dir> <out_dir> -> <NAME>.pam (default frame) + all.txt ("NAME ntimelines seq(2) or seq(1)")
    if (cmd == "wbd" && argc >= 4) {
        std::string out = argv[3];
        fs::create_directories(out);
        FILE* t = std::fopen((out + "/all.txt").c_str(), "w");
        Fpa f; if (!f.open(find_ci(dir, "PM3DAT1.FPA"))) return 1;
        Bytes r; int ok = 0, bad = 0;
        for (auto& e : f.entries()) {
            if (e.ext != "WBD" || !f.read(e, r)) continue;
            try {
                Wbd w = wbd_load(r.data(), r.size());
                auto sq = w.seq(2); if (sq.empty()) sq = w.seq(1); if (sq.empty()) sq = {{0, 30}};
                write_pam((out + "/" + e.name + ".pam").c_str(), w.render(sq[0].first));
                std::fprintf(t, "%s %zu", e.name.c_str(), w.groups.size());
                for (auto& pr : sq) std::fprintf(t, " %d:%d", pr.first, pr.second);
                std::fprintf(t, "\n"); ok++;
            } catch (const std::exception& ex) { std::fprintf(stderr, "fail %s: %s\n", e.name.c_str(), ex.what()); bad++; }
        }
        std::fclose(t);
        std::printf("wbd ok %d fail %d\n", ok, bad);
        return 0;
    }
    // pm3_dump dialog <game_dir> <SCRIPT>  -> dialogue steps (raw cp949 bytes on stdout, one "[bg sprite who nportraits] text" per line)
    if (cmd == "dialog" && argc >= 4) {
        Fpa sys; if (!sys.open(find_ci(dir, "PM3SYS0.FPA"))) return 1;
        for (auto& e : sys.entries()) if (e.ext == "EVX" && e.name == argv[3]) {
            EvxSceneInfo si; if (!evx_scene_info(sys, e, si)) return 1;
            if (argc >= 6) {   // dialog <dir> <SCRIPT> <step> <out.ppm>: bg + portraits + text box (FONT24.TFN)
                size_t k = (size_t)std::atoi(argv[4]); if (k >= si.steps.size()) return 1;
                const SceneStep& st = si.steps[k];
                Image can; can.w = 640; can.h = 480; can.rgba.assign((size_t)640 * 480 * 4, 255);
                Bytes r;
                for (const char* an : {"PM3DAT1", "PM3DAT2"}) {
                    std::string p = find_ci(dir, std::string(an) + ".FPA"); if (p.empty()) continue;
                    Fpa f; f.open(p);
                    for (auto& we : f.entries()) {
                        if (we.ext == "WMP" && std::atoi(we.name.c_str() + 1) == st.bg && f.read(we, r)) can = wmp_scene_load(r.data(), r.size()).frames[0];
                    }
                    for (auto& pt : st.portraits) {
                        char nm[16]; std::snprintf(nm, sizeof nm, "B%03d%03d", pt.hi, pt.lo);
                        for (auto& we : f.entries()) if (we.ext == "WBD" && we.name == nm && f.read(we, r)) {
                            Wbd w = wbd_load(r.data(), r.size()); auto sq = w.seq(2); if (sq.empty()) sq = w.seq(1); if (sq.empty()) sq = {{0, 30}};
                            draw_portrait(w, w.render(sq[0].first), pt.x, pt.y, can);
                        }
                    }
                }
                FILE* ff = std::fopen((dir + "/FONT24.TFN").c_str(), "rb"); if (!ff) return 1;
                Bytes fd; { uint8_t b[65536]; size_t n; while ((n = std::fread(b, 1, sizeof b, ff)) > 0) fd.insert(fd.end(), b, b + n); }
                std::fclose(ff);
                Tfn t; if (!t.load(fd.data(), fd.size())) return 1;
                draw_dialogue_box(can, t, st.text);
                write_ppm(argv[5], can); return 0;
            }
            for (auto& st : si.steps) {
                std::string t = st.text; for (auto& ch : t) if (ch == '\n') ch = '|';
                std::printf("[M%d P%d who=%d n=%zu op=%02x ticks=%d] %s\n", st.bg, st.sprite, st.who, st.portraits.size(), st.op, st.ticks, t.c_str());
            }
        }
        return 0;
    }
    // pm3_dump scene <game_dir> <out.ppm> <SCRIPT | P###:M####> [tick]  -> one composed frame (same path as the viewer)
    if (cmd == "scene" && argc >= 5) {
        std::string out = argv[3], spec = argv[4];
        int tick = argc >= 6 ? std::atoi(argv[5]) : 60;
        Fpa sys; if (!sys.open(find_ci(dir, "PM3SYS0.FPA"))) return 1;
        int wantBg = -1, wantSp = -1;
        size_t colon = spec.find(':');
        if (colon != std::string::npos) { wantSp = std::atoi(spec.c_str() + 1); wantBg = std::atoi(spec.c_str() + colon + 2); }
        else {
            for (auto& e : sys.entries()) if (e.ext == "EVX" && e.name == spec) {
                EvxSceneInfo si; if (!evx_scene_info(sys, e, si)) return 1;
                wantBg = si.bg(); wantSp = si.sprite();
            }
        }
        std::printf("background M%04d sprite P%03d\n", wantBg, wantSp);
        Image can; can.w = 640; can.h = 480; can.rgba.assign((size_t)640 * 480 * 4, 255);
        Bytes r;
        for (const char* an : {"PM3DAT1", "PM3DAT2"}) {
            std::string p = find_ci(dir, std::string(an) + ".FPA"); if (p.empty()) continue;
            Fpa f; f.open(p);
            for (auto& e : f.entries()) {
                if (e.ext != "WMP" || std::atoi(e.name.c_str() + 1) != wantBg || !f.read(e, r)) continue;
                WmpScene sc = wmp_scene_load(r.data(), r.size());
                can = sc.frames[0];
            }
        }
        for (auto& e : sys.entries()) {
            if (e.ext != "WPC" || std::atoi(e.name.c_str() + 1) != wantSp || !sys.read(e, r)) continue;
            Wpc w = wpc_load(r.data(), r.size());
            WpcRun run = wpc_run(w, tick + 1, 1);
            wpc_draw(w, run.frames[tick], can, 0, 0);
        }
        if (colon == std::string::npos) {   // EVX scene: portraits too
            for (auto& e : sys.entries()) if (e.ext == "EVX" && e.name == spec) {
                EvxSceneInfo si; evx_scene_info(sys, e, si);
                Fpa dat; if (!dat.open(find_ci(dir, "PM3DAT1.FPA"))) break;
                for (auto& pt : si.portraits) {
                    char nm[16]; std::snprintf(nm, sizeof nm, "B%03d%03d", pt.hi, pt.lo);
                    std::printf("portrait slot %d %s at (%d,%d)\n", pt.slot, nm, pt.x, pt.y);
                    for (auto& we : dat.entries()) {
                        if (we.ext != "WBD" || we.name != nm || !dat.read(we, r)) continue;
                        Wbd w = wbd_load(r.data(), r.size());
                        auto sq = w.seq(2); if (sq.empty()) sq = w.seq(1); if (sq.empty()) sq = {{0, 30}};
                        draw_portrait(w, w.render(sq[0].first), pt.x, pt.y, can);
                    }
                }
            }
        }
        write_ppm(out, can);
        return 0;
    }
    // pm3_dump wpc <game_dir> <out_dir> <ticks> <seed> [NAME]
    //   writes <out_dir>/<NAME>.txt (per-tick sprite list, FX ops, bounds); with NAME also PPM frames
    //   (indices 0,50,150 of every 2nd tick) for pixel comparison with py/wpc_play.py.
    if (cmd == "wpc" && argc >= 6) {
        std::string out = argv[3];
        int ticks = std::atoi(argv[4]);
        uint32_t seed = (uint32_t)std::strtoul(argv[5], nullptr, 10);
        std::string only = argc >= 7 ? argv[6] : "";
        fs::create_directories(out);
        Fpa f;
        if (!f.open(find_ci(dir, "PM3SYS0.FPA"))) return 1;
        Bytes r;
        int done = 0, fail = 0;
        for (auto& e : f.entries()) {
            if (e.ext != "WPC" || (!only.empty() && e.name != only)) continue;
            if (!f.read(e, r)) { fail++; continue; }
            try {
                Wpc w = wpc_load(r.data(), r.size());
                WpcRun run = wpc_run(w, ticks, seed);
                FILE* t = std::fopen((out + "/" + e.name + ".txt").c_str(), "w");
                for (size_t k = 0; k < run.frames.size(); k++) {
                    std::fprintf(t, "%zu:", k);
                    for (auto& it : run.frames[k]) std::fprintf(t, " %d,%d,%d,%d,%d,%d", it.param, it.slot, it.obj, it.cur, it.x, it.y);
                    std::fprintf(t, "\n");
                }
                for (auto& x : run.fx) std::fprintf(t, "fx %d %d %d %d %d %d\n", x.tick, x.slot, x.op, x.a, x.b, x.c);
                WpcBounds b = wpc_bounds(w, run.frames, 0, 2);
                std::fprintf(t, "bounds %d %d %d %d %d\n", b.valid, b.x0, b.y0, b.x1, b.y1);
                std::fclose(t);
                if (!only.empty() && b.valid) {
                    for (size_t idx : {0, 50, 150}) {
                        size_t tick = idx * 2;
                        if (tick >= run.frames.size()) continue;
                        Image can; can.w = b.x1 - b.x0; can.h = b.y1 - b.y0;
                        can.rgba.resize((size_t)can.w * can.h * 4);
                        for (size_t q = 0; q < can.rgba.size(); q += 4) { can.rgba[q] = 70; can.rgba[q+1] = 90; can.rgba[q+2] = 110; can.rgba[q+3] = 255; }
                        wpc_draw(w, run.frames[tick], can, b.x0, b.y0);
                        write_ppm(out + "/" + e.name + "_f" + std::to_string(idx) + ".ppm", can);
                    }
                }
                done++;
            } catch (const std::exception& ex) { std::fprintf(stderr, "fail %s: %s\n", e.name.c_str(), ex.what()); fail++; }
        }
        std::printf("wpc ok %d fail %d\n", done, fail);
        return 0;
    }
    return 2;
}
