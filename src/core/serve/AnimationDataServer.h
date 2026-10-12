#pragma once

#include <interface/AnimationData.h>
#include <codec/format/AnimDataYaml.h>   // ProjectHeader, ParseAnimdataIndexYaml
#include <compile/AnimDataDeriver.h>     // DeriveClipInput

#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace CB::core::serve {

    namespace animdata = ::CB::engine::animdata;

    struct ServeResult {
        bool                  attempted = false;
        bool                  ok = false;          ///< projected + wrote the file
        std::string           cachePath;
        std::string           error;               ///< set iff ok == false and attempted == true
    };

    /// @brief The load order's merged data the adsf is projected from (the Resolver supplies it). Keys are normalized
    ///        serve paths ("meshes/actors/character/behaviors/0_master.hkx").
    struct AdsfSource {
        std::function<std::vector<std::string>(const std::string& characterKey)>         roster;   ///< served roster
        std::function<std::vector<animdata::DeriveClipInput>(const std::string& unitKey)> clips;   ///< merged clips
        std::function<std::vector<std::string>()>                                         behaviorUnits;
    };

    /// @brief Runs a batch of independent tasks and returns once all have finished — the
    ///        bootstrap::ThreadPool::parallel_for contract (the same type as Resolver::AnimExecutor).
    using Executor = std::function<void(std::vector<std::function<void()>>)>;

    /// @brief Project Data\community_behaviors_cachenimationdatasinglefile.txt from the load order (ProjectAnimData:
    ///        the master's project index, every project's merged behavior graphs, its served roster, and the bundles'
    ///        animations — the highest bundle carrying an animation wins) and write it through the schema (MetaText).
    ///        Never throws; a failure (including a frame the engine would misread) leaves vanilla in place.
    /// @param exec Runs the animation parse in parallel — pass the plugin's BELOW_NORMAL pool so it never competes
    ///        with the game's threads; nullptr parses serially. The output is identical either way.
    ServeResult ServeAnimData(const std::filesystem::path& dataDir, const std::filesystem::path& loadOrderIni,
                              const AdsfSource& source, const Executor* exec = nullptr);

    /// @brief Subscribe the DETERMINISTIC redirect to the AdsfOpen hook point (<core/hooks/points/Resource.h>): once
    ///        armed, the loader's single-file open is handed BR's cache path, whenever the loader runs.
    /// @pre Called during SKSEPluginLoad, before CB::core::hooks::InstallAll().
    void InstallAnimDataHook();

    /// @brief Arm the redirect with BR's merged cache (call after ServeAnimData). Activates the subscriber if
    ///        AdsfOpen installed; otherwise falls back to RedirectAnimDataGlobal. No-op when result.ok is false.
    void ArmAnimDataRedirect(const ServeResult& result);

    /// @brief Fallback delivery: overwrite the engine's single-file path global (registry
    ///        Adsf_SingleFilePath) with BR's cache path. Logs its CURRENT value first and only rewrites on
    ///        result.ok.
    /// @warning Races the loader if it has already run — the AdsfOpen redirect has no such window.
    void RedirectAnimDataGlobal(const ServeResult& result);

}  // namespace CB::core::serve
