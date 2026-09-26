#pragma once
// CB::core::formid — the header MASTER TABLE, the resolution half of the FormId codec.
//
// A FormId's masterIndex is RELATIVE to the bundle it is stored in. Each bundle has an ordered table:
//   index 0     = the base game (Skyrim), RESERVED for everyone including Skyrim itself
//   index 1..k  = the bundle's declared masters (manifest file order, deduped)
//   index k+1   = self (this bundle)
// Resolution is table[idx] -> the owning bundle stem, recursed through THAT bundle's own table by the
// load order (that recursion is what dodges Bethesda's 255-plugin flat-byte cap). The table is
// APPEND-ONLY STABLE: adding a master appends, never renumbers, or stored FormIds would repoint.
//
// This is resolution GLUE (a correspondence, no computable encode), not a codec — hence it sits in
// interface beside the membranes, std-only, so both the plugin Resolver (which BUILDS the table from the
// master DAG) and the codec merge (which RESOLVES an id's index against it) can share one rule.

#include <algorithm>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace CB::core::formid {

    inline constexpr std::string_view BASE_GAME_STEM = "skyrim";   // index 0, reserved

    // Build a bundle's ordered master table. `declaredMasters` = its manifest.masters (bundle stems,
    // file order, LOWERCASE; may or may not list skyrim). `haveSkyrim` = a base-game bundle is present.
    // Result: [skyrim?, ...declared (deduped, skyrim/self excluded)..., self]. For skyrim itself the
    // table collapses to ["skyrim"] (index 0 = base = self). With no base game, index 0 is the first
    // declared master or self (a mod that adds a whole graph the base game lacks).
    inline std::vector<std::string> BuildMasterTable(std::string_view stem,
                                                     const std::vector<std::string>& declaredMasters,
                                                     bool haveSkyrim) {
        std::vector<std::string> table;
        const auto has = [&](std::string_view s) {
            return std::find(table.begin(), table.end(), s) != table.end();
        };
        if (haveSkyrim) table.emplace_back(BASE_GAME_STEM);          // index 0 = base game (reserved)
        for (const auto& m : declaredMasters) {
            if (m == stem || m == BASE_GAME_STEM) continue;          // self + base handled separately
            if (!has(m)) table.push_back(m);                        // declared masters, file order
        }
        if (!has(stem)) table.emplace_back(stem);                    // self = last (skyrim collapses via has)
        return table;
    }

    // Resolve a FormId's masterIndex against a bundle's table -> the owning bundle stem, or nullopt if
    // the index is out of range (a malformed / cross-versioned id — surface it, never bind blindly).
    inline std::optional<std::string_view> ResolveOwner(const std::vector<std::string>& table,
                                                        unsigned masterIndex) {
        if (masterIndex >= table.size()) return std::nullopt;
        return std::string_view(table[masterIndex]);
    }

}  // namespace CB::core::formid
