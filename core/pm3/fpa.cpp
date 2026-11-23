#include "pm3/fpa.h"

namespace pm3 {

static uint32_t rd32(const uint8_t* p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static std::string rstrip(std::string s) {
    while (!s.empty() && (s.back() == ' ' || s.back() == '\0')) s.pop_back();
    return s;
}

bool Fpa::open(const std::string& path, std::string* err) {
    auto fail = [&](const char* m) {
        if (err) *err = m;
        return false;
    };
    path_ = path;
    entries_.clear();
    f_.close();
    f_.clear();
    f_.open(path, std::ios::binary);
    if (!f_) return fail("cannot open file");
    f_.seekg(0, std::ios::end);
    fileSize_ = (uint64_t)f_.tellg();
    if (fileSize_ < 0x20) return fail("file too small");

    uint8_t hdr[0x10];
    f_.seekg(0);
    f_.read((char*)hdr, sizeof hdr);
    if (!f_) return fail("short header");
    uint32_t A = rd32(hdr + 8);
    uint64_t base = 0x10 + (uint64_t)A - 4;
    if (A < 4 || base > fileSize_) return fail("bad index size");

    // Index block = [0x10, base). Read a few extra bytes so the word-wise bit reader sees the same
    // data as when decoding from the full file.
    uint64_t blockEnd = std::min<uint64_t>(fileSize_, base + 16);
    Bytes block(blockEnd - 0x10);
    f_.seekg(0x10);
    f_.read((char*)block.data(), (std::streamsize)block.size());
    if (!f_) return fail("short index read");

    Bytes idx;
    if (!abp_decompress(block.data(), block.size(), idx)) return fail("index is not ABP_");
    size_t n = idx.size() / 15;
    if (n < 2) return fail("empty index");

    std::vector<uint32_t> offs(n);
    for (size_t i = 0; i < n; i++) offs[i] = rd32(&idx[i * 15 + 11]);
    for (size_t i = 0; i + 1 < n; i++) {
        FpaEntry e;
        e.name = rstrip(std::string((const char*)&idx[i * 15], 8));
        e.ext = rstrip(std::string((const char*)&idx[i * 15 + 8], 3));
        e.begin = base + offs[i];
        e.end = base + offs[i + 1];
        entries_.push_back(std::move(e));
    }
    return true;
}

bool Fpa::read(const FpaEntry& e, Bytes& out, uint64_t skip, uint64_t max) const {
    out.clear();
    // The last entry's recorded end can run a few bytes past EOF (PM3DAT1/2); the reference
    // tooling silently truncates, so clamp instead of failing.
    if (e.end < e.begin || e.begin > fileSize_) return false;
    uint64_t end = e.end < fileSize_ ? e.end : fileSize_;
    if (skip > end - e.begin) return false;
    uint64_t n = end - e.begin - skip;
    if (max && max < n) n = max;
    out.resize(n);
    f_.clear();
    f_.seekg((std::streamoff)(e.begin + skip));
    f_.read((char*)out.data(), (std::streamsize)n);
    return (bool)f_;
}

}  // namespace pm3
