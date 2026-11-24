// wmp_dump <game_dir> -> "name ngroups nrecs checksum" per WMP (diff against py/wmplib.py)
#include <cstdio>
#include <string>
#include "pm3/fpa.h"
#include "pm3/wmp.h"
using namespace pm3;
int main(int argc, char** argv) {
    if (argc < 2) return 2; std::string gd = argv[1];
    for (const char* an : {"PM3DAT1", "PM3DAT2"}) {
        Fpa f; std::string err; if (!f.open(gd + "/" + an + ".FPA", &err)) continue;
        for (const FpaEntry& e : f.entries()) {
            if (e.ext != "WMP") continue;
            Bytes raw; if (!f.read(e, raw)) return 1;
            try {
                WmpInfo w = wmp_parse(raw.data(), raw.size()); size_t nr = 0; uint64_t cs = 0;
                for (auto& g : w.groups) for (auto& r : g) { nr++; const uint16_t* p = &r.dstx; for (int k = 0; k < 10; k++) cs = cs * 131 + p[k]; cs &= 0xffffffffffffULL; }
                printf("%s %zu %zu %llu\n", e.name.c_str(), w.groups.size(), nr, (unsigned long long)cs);
            } catch (std::exception& ex) { printf("%s ERR %s\n", e.name.c_str(), ex.what()); }
        }
    }
}
