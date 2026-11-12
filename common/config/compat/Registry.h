#pragma once

#include <functional>
#include <span>
#include <string_view>
#include <vector>

/// @file
/// @brief The compatibility registry: which mods CB does not coexist with, how each is detected, how bad the clash is,
///        and what CB does about it. Data and matching only; the plugin applies the actions, the converter reports them.
/// @note  Names are copied byte-exact and handed to the caller's probe exactly as spelled here. Whether a file called
///        `TrueDirectionalMovement.dll` is "the same" as `truedirectionalmovement.dll` is the filesystem's answer (the
///        probe's `exists` / `GetModuleHandle`), never folded here.

namespace CB::config::compat {

    /// @brief How bad the clash is. Note: tell the user, change nothing. Warn: tell the user, CB still works. Incompatible:
    ///        the mod and a CB feature fight over the same thing; the actions switch that feature off.
    enum class Severity { Note, Warn, Incompatible };

    /// @brief One thing CB does when a mod is found.
    struct Action {
        enum class Kind {
            DisableFeature,   ///< `text` is a DebugFlags id; the flag reads as off whatever settings.ini says
            Notice,           ///< `text` is the sentence shown to the user (log, config page, the converter)
        };
        Kind             kind;
        std::string_view text;
    };

    /// @brief One mod (or family) CB reacts to.
    struct Entry {
        std::string_view                  id;        ///< stable identifier (log key)
        std::string_view                  name;      ///< what the user knows it as
        std::span<const std::string_view> dlls;      ///< file names in Data/SKSE/Plugins; any one present means the mod is
        Severity                          severity;
        std::span<const Action>           actions;
    };

    /// @brief What a probe found for one entry.
    struct Finding {
        const Entry*                  entry = nullptr;
        std::vector<std::string_view> dlls;   ///< the entry's DLL names the probe reported present
    };

    /// @return Every entry, in table order. Valid for the process lifetime.
    std::span<const Entry> Entries();

    /// @brief Whether the DLL called `dll` (the registry's own spelling) is present in the probe's world: a directory
    ///        listing for the plugin's preflight, a loaded-module check for its postflight, a mod folder for the converter.
    using Probe = std::function<bool(std::string_view dll)>;

    /// @return The entries with at least one DLL the probe reports present, in table order.
    std::vector<Finding> Match(const Probe& present);

    /// @return "note", "warn" or "incompatible".
    std::string_view SeverityName(Severity severity);

}  // namespace CB::config::compat
