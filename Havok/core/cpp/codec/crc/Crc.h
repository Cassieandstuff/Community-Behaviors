#pragma once
// CB::core::crc — the animationsetdata path CRC, as a SEARCHABLE codec.
//
// A hash is lossy (many paths → one 32-bit value), so it has no `decode`. It qualifies as a codec
// only under the SEARCHABLE kind: the forward `encode` is a genuine computable transform, and the
// reverse is `solve(triple, candidates)` — a lookup over an enumerated candidate set, NOT an inversion.
// Paths outside the candidate set are unrecoverable (the vanilla asdsf's ~5% that "don't reverse to a
// roster name"). The API names that honestly — there is deliberately no `decode`.
//
// The hash: CRC-32 with the reflected polynomial 0xEDB88320 but init=0 and NO final xor (NOT zlib's
// variant), over the lowercased byte string. RE'd + verified against vanilla (chicken stems + folders).
// Lifted from havok-core (havok::animsetdata) — org-pass firesale, the second codec.

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace CB::core::crc {

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

    // SOLVE (the searchable reverse) — find which candidate path produces `t`, by hashing each candidate
    // and matching. Returns the first match, or nullopt if `t` isn't produced by any candidate (the
    // irreducible residue). This is a search over an enumerated set, NOT decoding a hash.
    std::optional<std::string> solve(const CrcTriple& t, const std::vector<std::string>& candidates);

}  // namespace CB::core::crc
