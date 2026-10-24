#pragma once

#include <codec/format/LoadOrderText.h>

#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

/// @file
/// @brief The load-order plan: masters drive order, loadorder.txt breaks peer ties. The one planner the runtime's
///        Resolver and offline tools (hky-tool graph-html) share, so a tool merges a unit's layers exactly as the game.

namespace CB::engine::merge {

    /// @brief Every bundle's rank, its master table, and the bundles left out.
    struct LoadPlan {
        std::unordered_map<std::string, int> rank;      ///< stem -> base-first position (lower = base)
        std::unordered_set<std::string>      dropped;   ///< stems excluded: a declared master is absent, transitively
        /// @brief stem -> its ordered master table (index 0 = self, 1 = the base game when present, then declared
        ///        masters): what a node's FormId masterIndex resolves against (codec::formid::BuildMasterTable).
        std::unordered_map<std::string, std::vector<std::string>> masterTable;
        std::vector<std::string> warnings;   ///< why a bundle was dropped, master cycles: the caller decides how to report
    };

    /// @brief A topological sort of the master DAG, base first.
    /// @param masters stem (lowercase, PathKey) -> its manifest's declared masters, in file order. A bundle without a
    ///        manifest is absent from the map, and so from the plan (rank 0 when asked, no master table).
    /// @param order   loadorder.txt: only the tie-break among bundles with no dependency between them.
    /// @note  Every bundle but "skyrim" implicitly masters it, so the vanilla corpus sorts first however the mods are
    ///        ordered; a missing Skyrim drops nothing. A missing declared master drops its dependent (and theirs): its
    ///        overrides would bind to nothing. A master cycle degrades to loadorder.txt order with a warning.
    LoadPlan PlanLoadOrder(const std::unordered_map<std::string, std::vector<std::string>>& masters,
                           const codec::LoadOrder&                                         order);

}  // namespace CB::engine::merge
