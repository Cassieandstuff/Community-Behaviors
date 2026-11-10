#pragma once

#include <compat/Registry.h>

#include <span>
#include <string>
#include <vector>

/// @file
/// @brief The plugin side of the compatibility registry (<compat/Registry.h>): finds the mods CB does not coexist with
///        and acts on them before anything of CB's reads a flag.

namespace CB::core::bootstrap::compat {

    /// @brief Scans Data/SKSE/Plugins for the registry's DLLs, switches off the features each found mod clashes with and
    ///        logs what it found.
    /// @pre   First thing in SKSEPluginLoad after the logger: it must run before the first FlagOn.
    /// @note  A file scan, not a loaded-module check: it needs no other plugin to have loaded yet, so the answer does not
    ///        depend on load order. Postflight checks it against what really loaded.
    void Preflight();

    /// @brief Compares the preflight scan with the modules that loaded, and logs every disagreement (a DLL the scan
    ///        found that did not load; a DLL that loaded but the scan missed).
    /// @pre   After every SKSE plugin has loaded (kPostLoad).
    /// @note  Changes nothing: a disagreement means the registry or the scan is wrong, and says so.
    void Postflight();

    /// @return What Preflight found, in table order. Empty before it runs; stable after.
    const std::vector<engine::compat::Finding>& Findings();

    /// @return One line per disagreement Postflight found; empty when it agreed (or has not run).
    std::vector<std::string> Drift();

}  // namespace CB::core::bootstrap::compat
