#include "pm3/pe.h"

#include <cstring>

namespace pm3 {
namespace {
uint32_t u32(const std::vector<uint8_t>& d, size_t o) { uint32_t v = 0; std::memcpy(&v, &d[o], 4); return v; }
uint16_t u16(const std::vector<uint8_t>& d, size_t o) { uint16_t v = 0; std::memcpy(&v, &d[o], 2); return v; }
}  // namespace

bool PeImage::load(const std::vector<uint8_t>& f, X86& cpu, PeImage& out, std::string* err) {
    auto bad = [&](const char* m) { if (err) *err = m; return false; };
    if (f.size() < 0x200 || f[0] != 'M' || f[1] != 'Z') return bad("not MZ");
    uint32_t pe = u32(f, 0x3c); if (pe + 0x100 > f.size() || std::memcmp(&f[pe], "PE\0\0", 4)) return bad("not PE");
    uint16_t nsec = u16(f, pe + 6), osz = u16(f, pe + 20); size_t opt = pe + 24;
    if (u16(f, opt) != 0x10b) return bad("not PE32");
    out.entry = u32(f, opt + 16); out.base = u32(f, opt + 28); out.size = u32(f, opt + 56); uint32_t hdr = u32(f, opt + 60);
    if ((uint64_t)out.base + out.size > cpu.mem.size()) return bad("guest memory too small");
    std::memcpy(&cpu.mem[out.base], f.data(), std::min<size_t>(hdr, f.size()));
    for (int i = 0; i < nsec; i++) {
        size_t o = opt + osz + 40 * i; uint32_t va = u32(f, o + 12), rs = u32(f, o + 16), ro = u32(f, o + 20);
        if (ro + rs > f.size() || (uint64_t)out.base + va + rs > cpu.mem.size()) return bad("bad section");
        std::memcpy(&cpu.mem[out.base + va], &f[ro], rs);
    }
    uint32_t imp = u32(f, opt + 96 + 8);   // import directory RVA (mapped image can be read directly)
    for (uint32_t o = out.base + imp;; o += 20) {
        uint32_t ol = cpu.rd32(o), nm = cpu.rd32(o + 12), ft = cpu.rd32(o + 16); if (!nm) break;
        std::string dll; for (uint32_t p = out.base + nm; cpu.mem[p]; p++) dll += (char)cpu.mem[p];
        uint32_t t = out.base + (ol ? ol : ft);
        for (int i = 0;; i++) {
            uint32_t v = cpu.rd32(t + 4 * i); if (!v) break;
            std::string n; if (v & 0x80000000u) n = "#" + std::to_string(v & 0xffff); else for (uint32_t p = out.base + v + 2; cpu.mem[p]; p++) n += (char)cpu.mem[p];
            out.imports.push_back({dll, n, out.base + ft + 4 * (uint32_t)i});
        }
    }
    return true;
}

}  // namespace pm3
