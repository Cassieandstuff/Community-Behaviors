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
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>

namespace CB::core::resolve {

    /// @brief FNV-1a 64 — CB's internal content fingerprint.
    /// @note  Internal only: it never crosses to the engine, so it is not a Havok CRC.
    std::uint64_t Fnv64(std::string_view bytes, std::uint64_t h = 1469598103934665603ull);

    /// @brief Read a file from OUTSIDE the bundles that a cached output is built from (an OAR submod's config.json, a
    ///        loose fallback base), recording its content — or its absence — in this launch's cache key.
    /// @note  Thread-safe. The record lands in the sentinel and is re-read on the next launch's check, so editing the file
    ///        (or creating it) misses the cache.
    std::optional<std::string> ReadCacheInput(const std::filesystem::path& p);

    /// @brief This launch's cache key, as the sentinel's text, followed by `outputLines` — the per-output keys
    ///        ("graph\t<serve key>\t<hex>" lines) the sentinel also carries.
    /// @note  The per-input lines are computed ONCE, at the first call of this, CacheKeyMismatch or SharedGraphInputs,
    ///        so the key written after a compile describes the inputs as that launch found them. ReadCacheInput lines
    ///        are appended as of now.
    std::string CacheKeyText(const std::filesystem::path& dataDir, std::string_view outputLines = {});

    /// @return empty when `stored` (a sentinel's text) still matches the inputs; otherwise what changed, for the log.
    /// @note  Per-output lines are not inputs and are ignored here.
    std::string CacheKeyMismatch(std::string_view stored, const std::filesystem::path& dataDir);

    /// @brief The inputs EVERY compiled graph depends on — compiler, schema, output-changing settings, the BDI
    ///        declarations — folded into one value: the seed of each graph's own key.
    std::uint64_t SharedGraphInputs(const std::filesystem::path& dataDir);

    /// @brief The inputs the adsf / asdsf read straight from the bundles: every file of every bundle (packed: path, size,
    ///        CRC; unpacked: path, size, mtime) EXCEPT those inside a unit `skipUnit` names, plus loadorder.txt and the
    ///        shared inputs — folded into one value.
    /// @param skipUnit Called with a unit's path key ("meshes/…/x.hkx"); true leaves that unit's files out (the
    ///        Resolver skips behavior graphs, which the adsf sees only through their clip generators).
    std::uint64_t BundleFilesInput(const std::filesystem::path& dataDir,
                                   const std::function<bool(const std::string& unitKey)>& skipUnit);

    /// @brief Re-record every outside-file read a stored sentinel lists, with the file's value now — for an output kept
    ///        without re-running the build that read them, so the next key still watches those files.
    void CarryReads(std::string_view stored);

    /// @brief The per-output keys of `kind` a sentinel's text carries: name -> key (hex).
    std::unordered_map<std::string, std::string> StoredOutputKeys(std::string_view stored, std::string_view kind);

    /// @brief A 64-bit key as the sentinel writes it.
    std::string KeyHex(std::uint64_t h);

}  // namespace CB::core::resolve
