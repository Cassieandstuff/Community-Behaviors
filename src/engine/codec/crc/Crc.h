#pragma once
// CB::engine::codec::crc — the animationsetdata path CRC.
//
// A hash is lossy (many paths → one 32-bit value), so it has no `decode`: the forward `encode` is a
// genuine computable transform, and the API deliberately offers nothing in the reverse direction.
//
// The hash: CRC-32 with the reflected polynomial 0xEDB88320 but init=0 and NO final xor (NOT zlib's
// variant), over the lowercased byte string. RE'd + verified against vanilla (chicken stems + folders).
// Lifted from havok-core (CB::engine::animsetdata) — org-pass firesale, the second codec.

#include <cstdint>
#include <string>
#include <string_view>

namespace CB::engine::codec::crc {

    // One animation registration: (folderCrc, fileCrc, extCrc). folder+ext are constant within a set
    // (folder = the animation dir, ext = 7891816 for ".hkx"); file varies.
    struct CrcTriple {
        std::uint32_t folder = 0;
        std::uint32_t file = 0;
        std::uint32_t ext = 0;
        bool operator==(const CrcTriple& o) const { return folder == o.folder && file == o.file && ext == o.ext; }
    };

    // The raw path hash (lowercased input; init=0, no final xor).
    std::uint32_t Crc32(std::string_view s);

    // ENCODE — build (folderCrc, fileCrc, extCrc) for an animation from its data-relative path
    // ("meshes\actors\character\animations\x.hkx"): folder = Crc32(lower(dir)); file = Crc32(lower(stem));
    // ext = little-endian pack of the lowercased extension bytes (raw, not hashed; <=4 chars). Separators
    // may be / or \. (Historically named TripleForAnimation.)
    CrcTriple TripleForAnimation(std::string_view dataRelativePath);

}  // namespace CB::engine::codec::crc
