#pragma once
#include <disk/Disk.h>

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

// HavokFile (SCT shell) — trivial bytes↔disk helpers for .hkx packfiles. Inline /
// header-only: no Havok knowledge, just I/O. The compiler/validator layer above
// produces or consumes the byte vectors.

namespace CB::engine::codec {
using namespace CB::engine::common;   // this layer operates on the interface data model

inline bool ReadHavokFile(const std::filesystem::path& path,
                          std::vector<std::uint8_t>& outBytes,
                          std::string* outError = nullptr) {
    auto bytes = CB::engine::disk::ReadBytes(path);
    if (!bytes) { if (outError) *outError = "cannot open for reading: " + path.string(); return false; }
    outBytes = std::move(*bytes);
    return true;
}

} // namespace CB::engine::codec
