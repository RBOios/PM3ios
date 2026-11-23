// pm3_dump list   <game_dir>            -> "<archive> <name> <ext> <begin> <end>" per entry
// pm3_dump images <game_dir> <out_dir>  -> PPM export mirroring py/export_images.py
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <iostream>

#include "pm3/fpa.h"
#include "pm3/image.h"

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
    return 2;
}
