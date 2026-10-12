#pragma once
/// @file
/// @brief The cache key: a record of every input the on-disk behavior cache was built from, so a launch reuses the
///        cache only while all of them are unchanged.
/// @note  One line per input — the compiler (the plugin DLL), the schema tree, output-changing settings, loadorder.txt,
///        the BehaviorDataInjector declarations, each bundle (a packed one by its zip index: path, size and CRC of every
///        file, nothing inflated), plus every file outside the bundles a build read through ReadCacheInput. A mismatch
///        names the line that changed, so the log says WHY a launch recompiles.

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace CB::core::resolve {

    /// @brief FNV-1a 64 — CB's internal content fingerprint.
    /// @note  Internal only: it never crosses to the engine, so it is not a Havok CRC.
    std::uint64_t Fnv64(std::string_view bytes, std::uint64_t h = 1469598103934665603ull);

    /// @brief Read a file from OUTSIDE the bundles that a cached output is built from (an OAR submod's config.json, a
    ///        loose fallback base), recording its content — or its absence — in this launch's cache key.
    /// @note  Thread-safe. The record lands in the sentinel and is re-read on the next launch's check, so editing the file
    ///        (or creating it) misses the cache.
    std::optional<std::string> ReadCacheInput(const std::filesystem::path& p);

    /// @brief This launch's cache key, as the sentinel's text.
    /// @note  The per-input lines are computed ONCE, at the first call of this or CacheKeyMismatch, so the key written
    ///        after a compile describes the inputs as that launch found them. ReadCacheInput lines are appended as of now.
    std::string CacheKeyText(const std::filesystem::path& dataDir);

    /// @return empty when `stored` (a sentinel's text) still matches the inputs; otherwise what changed, for the log.
    std::string CacheKeyMismatch(std::string_view stored, const std::filesystem::path& dataDir);

}  // namespace CB::core::resolve
