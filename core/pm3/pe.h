// Minimal PE32 loader: maps PM3WIN.EXE into an X86 guest and binds imports to host-function slots.
#pragma once
#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <vector>

#include "pm3/x86.h"

namespace pm3 {

struct PeImport { std::string dll, name; uint32_t iat_addr; };
struct PeImage {
    uint32_t base = 0, entry = 0, size = 0;
    std::vector<PeImport> imports;
    static bool load(const std::vector<uint8_t>& file, X86& cpu, PeImage& out, std::string* err);
};

}  // namespace pm3
