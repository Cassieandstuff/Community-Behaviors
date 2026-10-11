#pragma once
/// @file
/// @brief CB::engine::disk — whole-file read/write, checked, optionally atomic.
/// @note Paths are used exactly as given (CLAUDE.md case directive): a file CB wrote is opened by the string it was
///       written under. Long Windows paths (> MAX_PATH) are handled here.

#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace CB::engine::disk {

struct ReadOptions {
    bool stripBom = false;   ///< drop a leading UTF-8 byte-order mark (text formats a person may have saved)
};

/// @brief The whole file as bytes-in-a-string (binary mode), or nullopt when it can't be opened or read.
std::optional<std::string> ReadFile(const std::filesystem::path& p, ReadOptions opt = {});

/// @brief The whole file as bytes, or nullopt when it can't be opened or read.
std::optional<std::vector<std::uint8_t>> ReadBytes(const std::filesystem::path& p);

struct WriteOptions {
    /// @brief Write `<p>.tmp` and rename it over `p` only once complete — for files a later stage or the game depends
    ///        on, so a failure never leaves a truncated file (or loses the previous one).
    bool atomic     = false;
    bool createDirs = true;   ///< create the parent directories first
};

/// @brief Write `data` to `p` (binary), then flush, close and check.
/// @return false (with `err` set when given) on any failure — open, write, flush, close, or the atomic rename.
bool WriteFile(const std::filesystem::path& p, std::string_view data, WriteOptions opt = {}, std::string* err = nullptr);
bool WriteFile(const std::filesystem::path& p, std::span<const std::uint8_t> data, WriteOptions opt = {},
               std::string* err = nullptr);

/// @brief `p` as Windows needs it to reach past MAX_PATH (the `\\?\` prefix on a long absolute path); unchanged
///        elsewhere.
std::filesystem::path LongPath(const std::filesystem::path& p);

}  // namespace CB::engine::disk
