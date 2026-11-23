// ABP_ LZSS decompressor (4096-byte ring, 12-bit pos / 4-bit len).
#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>

namespace pm3 {

using Bytes = std::vector<uint8_t>;

// `buf` points at the "ABP_" magic. Returns false only if the magic/header is missing.
// Like the reference decoder, the output is NOT truncated to the declared size
// (FPA index parsing depends on the exact decoded length). `declared` receives the header size.
bool abp_decompress(const uint8_t* buf, size_t len, Bytes& out, uint32_t* declared = nullptr);

}  // namespace pm3
