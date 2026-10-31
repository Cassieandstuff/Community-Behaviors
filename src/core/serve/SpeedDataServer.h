#pragma once

#include <filesystem>
#include <string>

namespace CB::core::serve {

    struct SpeedDataServeResult {
        bool        attempted = false;
        bool        ok = false;          ///< merged + wrote the file
        std::string cachePath;
        std::string error;               ///< set iff ok == false and attempted == true
        std::size_t projects = 0;
        std::size_t fromMods = 0;        ///< projects a mod bundle replaced or added
    };

    /// @brief Merge Data\community_behaviors_cache\speeddatasinglefile.txt from the load order: the master's project
    ///        list (Skyrim.hky's speeddatasinglefile.txt/index.yaml, its order kept), each project's table taken WHOLE
    ///        from the highest bundle carrying speeddata/<Stem>.yaml; a project no lower bundle has is appended.
    ///        Written through the schema (MetaText). Never throws; a failure leaves vanilla in place.
    /// @note A project's table is derived from its locomotion blends × their clips' root motion. A bundle that changes
    ///       a project's behaviors or animations without shipping its speeddata leaves the table stale (wrong
    ///       SpeedSampled — skating): that is LOGGED per project, not corrected (no derivation yet).
    SpeedDataServeResult ServeSpeedData(const std::filesystem::path& dataDir, const std::filesystem::path& loadOrderIni);

    /// @brief Subscribe to the SpeedDataOpen call site (<platform/game/Resource.h>): the speed-sampler DB's one
    ///        single-file open merges (ServeSpeedData) and is handed the cache instead; a failed merge opens vanilla.
    /// @note The merge needs only the bundles — not the compiled graphs — so it runs in the open itself, cold or warm.
    /// @pre Called during SKSEPluginLoad, before CB::platform::hooks::InstallAll() and before the loader runs.
    void InstallSpeedDataHook();

}  // namespace CB::core::serve
