#pragma once
/// @file
/// @brief Path IDENTITY — whether two spellings name the same resource. The one place CB folds a path's case.
/// @note The engine's resource lookup is case-insensitive (BSA lookup hashes the lowercased path; vanilla BSAs
///       store every name lowercase while references spell paths in mixed case), so path identity folds here and
///       nowhere else (CLAUDE.md case directive). A key is for COMPARISON: never write one into data, and open a
///       file by it only when CB wrote that file under it.

#include <string>
#include <string_view>

namespace CB::engine::codec::resource {

/// @brief ASCII-lowercase + '\\' → '/'.
/// @invariant Length-preserving: HkyArchive slices original-case paths by offsets taken from their keys.
inline std::string PathKey(std::string_view p)
{
    std::string s(p);
    for (char& c : s) {
        if (c == '\\') c = '/';
        else if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    }
    return s;
}

/// @brief Whether `path` ends in the extension `extLower` (already lowercase, with the dot, e.g. ".hkx") — path
///        identity, so ".HKX" / ".Hkx" match too.
inline bool HasExt(std::string_view path, std::string_view extLower)
{
    return path.size() >= extLower.size() && PathKey(path.substr(path.size() - extLower.size())) == extLower;
}

/// @brief The engine's animation-SLOT key for a roster entry: the lowercased FILENAME, folder ignored.
/// @note LoadClips keys roster slots this way and a later slot overwrites an earlier one on collision (RE'd), so
///       `male\MT_X` and `female\MT_X` are the same slot. Use it only to model that rule (slot alignment, collision
///       warnings) — roster names themselves are copied and compared exactly.
inline std::string ClipSlotKey(std::string_view rosterEntry)
{
    const auto p = rosterEntry.find_last_of("\\/");
    return PathKey(p == std::string_view::npos ? rosterEntry : rosterEntry.substr(p + 1));
}

}  // namespace CB::engine::codec::resource
