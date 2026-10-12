#pragma once
/// @file
/// @brief The loadorder.txt codec — the one reader and writer of `community_behaviors/loadorder.txt`, shared by the
///        runtime (Resolver, adsf/asdsf servers) and the converter (its writer and the Load Order tab).
/// @note Format: one bundle stem per line (`Skyrim`, `BFCO` — no `.hky`), surrounding whitespace ignored; blank lines
///       and lines starting `#` or `;` are notes, never bundles. A LATER line wins: the converter writes the master
///       first and the modlist winner last. Entry identity is path identity (codec::resource::PathKey), so `bfco` and
///       `BFCO` are one bundle; spellings are kept as written.

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace CB::engine::codec {

/// @brief Whether a loadorder.txt line names a bundle (not blank, not a `#` / `;` note).
bool IsLoadOrderEntry(std::string_view line);

/// @brief A parsed loadorder.txt.
struct LoadOrder {
    /// @brief Every line verbatim (CR dropped), notes and blanks included — the base a writer rewrites from, so a
    ///        hand-written comment keeps its place.
    std::vector<std::string> lines;

    /// @brief The bundle stems in winning order (lowest priority first), one per PathKey, trimmed, each spelled as
    ///        its LAST (winning) line spells it.
    std::vector<std::string> entries;

    /// @brief `bundleStem`'s priority: higher wins, 0 = unlisted.
    /// @note The 1-based position of the stem's LAST line among the file's bundle lines (a duplicate takes its last
    ///       position). Only comparisons are meaningful; the gaps duplicates leave are not.
    int Rank(std::string_view bundleStem) const;

private:
    friend LoadOrder ParseLoadOrder(std::string_view fileText);
    std::unordered_map<std::string, int> m_rank;   ///< PathKey(stem) -> rank
};

/// @brief Parse loadorder.txt text (a leading UTF-8 BOM, CRLF or LF line ends all tolerated).
LoadOrder ParseLoadOrder(std::string_view fileText);

/// @brief Read and parse the file; nullopt when it can't be read (a missing load order is not an error — callers
///        treat it as empty).
std::optional<LoadOrder> ReadLoadOrder(const std::filesystem::path& p);

/// @brief The file text for `lines`: each line LF-terminated, written verbatim (no BOM).
std::string EmitLoadOrder(const std::vector<std::string>& lines);

/// @brief Emit `lines` and write them to `p` atomically — a failed write leaves the previous file intact.
/// @return false with `err` set (when given) on failure.
bool WriteLoadOrder(const std::filesystem::path& p, const std::vector<std::string>& lines, std::string* err = nullptr);

}  // namespace CB::engine::codec
