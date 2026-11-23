// FPA archive: 0x10-byte header, ABP_-compressed index of 15-byte records
// (8 name, 3 ext, u32 offset), payload base = 0x10 + A - 4 where A = u32 @ +8.
#pragma once
#include <cstdint>
#include <fstream>
#include <string>
#include <vector>

#include "pm3/abp.h"

namespace pm3 {

struct FpaEntry {
    std::string name;  // trailing spaces/NULs stripped
    std::string ext;   // "GDT", "WPC", "WMP", "EVX", "WAV", ...
    uint64_t begin = 0, end = 0;
    uint64_t size() const { return end - begin; }
};

class Fpa {
public:
    bool open(const std::string& path, std::string* err = nullptr);
    const std::vector<FpaEntry>& entries() const { return entries_; }
    // Reads [begin+skip, begin+skip+max) of an entry (max = 0 -> whole remainder).
    bool read(const FpaEntry& e, Bytes& out, uint64_t skip = 0, uint64_t max = 0) const;
    const std::string& path() const { return path_; }

private:
    std::string path_;
    mutable std::ifstream f_;
    uint64_t fileSize_ = 0;
    std::vector<FpaEntry> entries_;
};

}  // namespace pm3
