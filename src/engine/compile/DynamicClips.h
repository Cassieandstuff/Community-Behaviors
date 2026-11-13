#pragma once

#include <decompile/BehaviorDecompile.h>

#include <cstddef>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

/// @file
/// @brief The compile step that fills every CBDynamicClipGenerator's table from its folder. CB runs it on each merged
///        behavior at startup; BK runs the same function when it compiles, so both pick from the same table.

namespace CB::engine::compile {

    /// @brief One clip of a folder, as the merged load order holds it.
    struct FolderClip {
        /// @brief The name the clip binds by, as hkbClipGenerator's animationName and the character's roster spell it
        ///        (project-relative, e.g. "Animations\Idles\Random\Idle1.hkx"), byte-exact.
        std::string animationName;
        /// @brief The clip's animation unit text when it is a CB animation unit; empty for a binary clip, which has no
        ///        selection data (priority 0, weight 1, no condition, no flags).
        std::string unitText;
    };

    /// @brief The clips under `folder` of the project `projectRoot` (ProjectRootOf), over the merged load order.
    /// @note  `folder` is the generator's own, relative to the project's animation folder. Whether a path is under it is
    ///        path identity, so the lister matches it as the engine's own file lookup would.
    using ClipLister = std::function<std::vector<FolderClip>(std::string_view projectRoot, std::string_view folder)>;

    /// @return the project folder a unit lives in: its path two levels up ("meshes/actors/character/" for
    ///         "meshes/actors/character/behaviors/0_master.hkx"), with a trailing '/'; "" when the path is shallower.
    std::string ProjectRootOf(std::string_view unitKey);

    struct DynamicClipReport {
        std::size_t              generators = 0;   ///< generators filled
        std::size_t              rows       = 0;   ///< rows written, over all of them
        std::vector<std::string> warnings;         ///< a clip whose selection data did not parse (it kept the defaults), ...
    };

    /// @brief Fill each CBDynamicClipGenerator in `graph`: `rows` from its folder (one row per clip, sorted by name, each a
    ///        new hkbClipGenerator carrying the generator's clip settings, and the clip's selection data), and
    ///        `fallbackClip` from `fallback` (null when empty). Whatever `rows` and `fallbackClip` held is replaced.
    /// @note  Each generator parses its clips' selection data again, so two generators on one folder never share a
    ///        condition object (a shared object would be a node of its own).
    DynamicClipReport FillDynamicClipTables(decompile::GraphEdit& graph, std::string_view graphKey, const ClipLister& list);

    /// @return the folders the graph's CBDynamicClipGenerators name, each once, byte-exact (for rostering their clips).
    std::vector<std::string> DynamicClipFolders(decompile::GraphEdit& graph);

}  // namespace CB::engine::compile
