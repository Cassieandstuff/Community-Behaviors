#pragma once
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

// HavokFile (SCT shell) — trivial bytes↔disk helpers for .hkx packfiles. Inline /
// header-only: no Havok knowledge, just I/O. The compiler/validator layer above
// produces or consumes the byte vectors.

namespace havok::sct {

inline bool WriteHavokFile(const std::filesystem::path& path,
                           const std::vector<std::uint8_t>& bytes,
                           std::string* outError = nullptr) {
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (!f) { if (outError) *outError = "cannot open for writing: " + path.string(); return false; }
    if (!bytes.empty())
        f.write(reinterpret_cast<const char*>(bytes.data()),
                static_cast<std::streamsize>(bytes.size()));
    if (!f.good()) { if (outError) *outError = "write failed: " + path.string(); return false; }
    return true;
}

inline bool ReadHavokFile(const std::filesystem::path& path,
                          std::vector<std::uint8_t>& outBytes,
                          std::string* outError = nullptr) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f) { if (outError) *outError = "cannot open for reading: " + path.string(); return false; }
    const std::streamoff size = f.tellg();
    if (size < 0) { if (outError) *outError = "cannot determine size: " + path.string(); return false; }
    outBytes.resize(static_cast<std::size_t>(size));
    f.seekg(0);
    if (size > 0) f.read(reinterpret_cast<char*>(outBytes.data()), size);
    if (f.bad()) { if (outError) *outError = "read failed: " + path.string(); return false; }
    return true;
}

} // namespace havok::sct
