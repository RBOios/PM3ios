// evx_dump <game_dir> all|<SCRIPT> [limit=60000] [--exe]   -> VM trace (age=12, seed=1), for diffing against py/evx_vm.py
// --exe: EXE-faithful variable semantics (EvxVm::exe_vars); traces then intentionally differ from the Python prototype
#include <cstdio>
#include <cstdlib>
#include <string>

#include "pm3/abp.h"
#include "pm3/evx.h"
#include "pm3/fpa.h"

using namespace pm3;

int main(int argc, char** argv) {
    if (argc < 3) { fprintf(stderr, "usage: evx_dump <game_dir> all|<SCRIPT> [limit]\n"); return 2; }
    std::string gd = argv[1], which = argv[2]; int limit = 60000; bool exe = false;
    for (int i = 3; i < argc; i++) { if (std::string(argv[i]) == "--exe") exe = true; else limit = atoi(argv[i]); }
    Fpa f; std::string err;
    if (!f.open(gd + "/PM3SYS0.FPA", &err)) { fprintf(stderr, "%s\n", err.c_str()); return 1; }
    int n = 0;
    for (const FpaEntry& e : f.entries()) {
        if (e.ext != "EVX" || (which != "all" && which != e.name)) continue;
        FpaEntry e2 = e; e2.begin -= 12;                 // EVX: the ABP_ header starts 12 bytes before the index offset
        Bytes raw, out;
        if (!f.read(e2, raw) || !abp_decompress(raw.data(), raw.size(), out)) { fprintf(stderr, "%s: abp failed\n", e.name.c_str()); return 1; }
        try {
            EvxScript sc = EvxScript::load(e.name, out);
            EvxVm vm(sc, 1); vm.exe_vars = exe;
            if (exe) { for (int i = 0; i < kEvxVarCount; i++) vm.setv(kEvxVars[i].var, 0); vm.setv(4, 12); }   // neutral init: every special var reads 0
            else { vm.V[4] = 12; }
            vm.run(limit);
            printf("== %s\n", e.name.c_str());
            for (auto& l : vm.log) printf("%s\n", l.c_str());
            n++;
        } catch (std::exception& ex) { fprintf(stderr, "%s: %s\n", e.name.c_str(), ex.what()); return 1; }
    }
    fprintf(stderr, "scripts: %d\n", n); return n ? 0 : 1;
}
