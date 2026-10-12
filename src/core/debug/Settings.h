#pragma once

#include <string>
#include <string_view>

namespace CB::core::debug {

    /// @brief Whether the kFlags row `id` (DebugFlags.h) is on: its IniBool key in settings.ini, or the presence of
    ///        its MarkerFile under Data/community_behaviors/.
    /// @note An absent, empty or unrecognized IniBool value yields the row's default for THIS build (defOnDebug
    ///       unless NDEBUG). An unknown `id` logs an error and yields false.
    /// @note settings.ini is parsed once per process, on first use; markers are re-checked on every call.
    bool FlagOn(std::string_view id);

    /// @brief settings.ini's `[section] key` (a non-flag value), or `def` when the file or key is absent.
    std::string Value(std::string_view section, std::string_view key, std::string_view def = {});

}  // namespace CB::core::debug
