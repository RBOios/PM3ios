// x86_test <game_dir>  : run functions of the ORIGINAL PM3WIN.EXE inside the interpreter and diff against the verified C++ decoders.
#include <chrono>
#include <cstdio>
#include <fstream>
#include <iterator>

#include "pm3/abp.h"
#include "pm3/fpa.h"
#include "pm3/pe.h"
#include "pm3/x86.h"

using namespace pm3;

int main(int argc, char** argv) {
    if (argc < 2) { fprintf(stderr, "usage: x86_test <game_dir>\n"); return 2; }
    std::string gd = argv[1];
    std::ifstream ef(gd + "/PM3WIN.EXE", std::ios::binary); std::vector<uint8_t> exe((std::istreambuf_iterator<char>(ef)), {});
    X86 cpu; PeImage img; std::string err;
    if (!PeImage::load(exe, cpu, img, &err)) { fprintf(stderr, "PE: %s\n", err.c_str()); return 1; }
    printf("PE loaded: base %08x size %x entry %08x imports %zu\n", img.base, img.size, img.entry, img.imports.size());
    cpu.r[4] = 0x1F00000;   // stack
    // Test 1: EXE's ABP wrapper (0x404F3F: cdecl abp(src,size,dest) -> usz) on every EVX script
    Fpa f; if (!f.open(gd + "/PM3SYS0.FPA", &err)) { fprintf(stderr, "%s\n", err.c_str()); return 1; }
    int ok = 0, bad = 0; uint64_t insns = 0, bytes = 0; auto t0 = std::chrono::steady_clock::now();
    for (const FpaEntry& e : f.entries()) {
        if (e.ext != "EVX") continue;
        FpaEntry e2 = e; e2.begin -= 12; Bytes raw, ref;
        if (!f.read(e2, raw) || !abp_decompress(raw.data(), raw.size(), ref)) { bad++; continue; }
        const uint32_t SRC = 0x900000, DST = 0xA00000;
        cpu.write_bytes(SRC, raw.data(), raw.size());
        std::fill(cpu.mem.begin() + DST, cpu.mem.begin() + DST + ref.size() + 16, 0xEE);
        uint64_t s0 = cpu.steps;
        try {
            uint32_t n = cpu.call_cdecl(0x404F3F, {SRC, (uint32_t)raw.size(), DST});
            insns += cpu.steps - s0; bytes += ref.size();
            if (n == ref.size() && std::equal(ref.begin(), ref.end(), cpu.mem.begin() + DST)) ok++;
            else { bad++; printf("MISMATCH %s: ret %u vs %zu\n", e.name.c_str(), n, ref.size()); }
        } catch (std::exception& ex) { bad++; printf("FAIL %s: %s\n", e.name.c_str(), ex.what()); if (bad > 3) break; }
    }
    double sec = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    printf("EXE abp(): %d identical, %d bad; %llu guest instructions for %llu bytes (%.1f insn/byte), %.0f Minsn/s\n",
           ok, bad, (unsigned long long)insns, (unsigned long long)bytes, bytes ? (double)insns / bytes : 0.0, sec > 0 ? insns / sec / 1e6 : 0.0);
    return bad ? 1 : 0;
}
