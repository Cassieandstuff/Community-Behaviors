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

    /// @brief Makes the kFlags row `id` read as off from now on, whatever settings.ini or its marker file says.
    /// @param reason what forced it (a mod found by the compatibility registry); shown by ForcedOffReason.
    /// @pre   Called before the first FlagOn(id): callers cache, so a flag already read keeps its value.
    /// @note  An unknown `id` logs an error and changes nothing.
    void ForceOff(std::string_view id, std::string_view reason);

    /// @return Why `id` was forced off by ForceOff, or empty when it was not.
    std::string ForcedOffReason(std::string_view id);

    /// @brief settings.ini's `[section] key` (a non-flag value), or `def` when the file or key is absent.
    std::string Value(std::string_view section, std::string_view key, std::string_view def = {});

    /// @brief Set settings.ini's `[section] key` to `value` and write the file back, creating it when absent.
    /// @note A flag already read keeps its value until the next launch: FlagOn callers cache.
    /// @return false when the file could not be written (logged).
    bool SetValue(std::string_view section, std::string_view key, std::string_view value);

    /// @brief Remove settings.ini's `[section] key` and write the file back; true when nothing is left to remove.
    bool RemoveValue(std::string_view section, std::string_view key);

}  // namespace CB::core::debug
