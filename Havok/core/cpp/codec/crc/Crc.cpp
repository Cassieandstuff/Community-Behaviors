#include "codec/crc/Crc.h"

namespace CB::core::crc {

    std::uint32_t Crc32(std::string_view s)
    {
        std::uint32_t crc = 0;  // init=0, no final xor (NOT zlib)
        for (unsigned char ch : s) {
            if (ch >= 'A' && ch <= 'Z') ch = static_cast<unsigned char>(ch + 32);  // lowercase
            crc ^= ch;
            for (int k = 0; k < 8; ++k)
                crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
        }
        return crc;
    }

    CrcTriple TripleForAnimation(std::string_view dataRelativePath)
    {
        // Normalize to backslashes; split off the last component as the file.
        std::string p(dataRelativePath);
        for (char& c : p)
            if (c == '/') c = '\\';

        const std::size_t slash = p.find_last_of('\\');
        const std::string folder = (slash == std::string::npos) ? std::string() : p.substr(0, slash);
        const std::string file = (slash == std::string::npos) ? p : p.substr(slash + 1);

        const std::size_t dot = file.find_last_of('.');
        const std::string stem = (dot == std::string::npos) ? file : file.substr(0, dot);
        const std::string ext = (dot == std::string::npos) ? std::string() : file.substr(dot + 1);

        CrcTriple t;
        t.folder = Crc32(folder);  // Crc32 lowercases internally
        t.file = Crc32(stem);
        t.ext = 0;  // little-endian pack of the lowercased extension bytes (<=4)
        for (std::size_t i = 0; i < ext.size() && i < 4; ++i) {
            unsigned char c = static_cast<unsigned char>(ext[i]);
            if (c >= 'A' && c <= 'Z') c = static_cast<unsigned char>(c + 32);
            t.ext |= static_cast<std::uint32_t>(c) << (8 * i);
        }
        return t;
    }

    std::optional<std::string> solve(const CrcTriple& t, const std::vector<std::string>& candidates)
    {
        for (const std::string& path : candidates)
            if (TripleForAnimation(path) == t) return path;
        return std::nullopt;
    }

}  // namespace CB::core::crc
