#pragma once

#include <codec/format/AnimationSetData.h>   // the setdata model (CB::engine::animsetdata)
#include <exec/Parallel.h>                   // TaskRunner

#include <filesystem>
#include <string>

// AnimationSetDataServer — the runtime moveset-table merge.
//
// At startup BR discovers split-form animationsetdata contributions inside the .hky
// bundles, merges them (load order) onto the current winning base
// animationsetdatasinglefile.txt, and writes the merged single file to BR's own cache.
//
// Why the cache and not the canonical loose path: under MO2 a write to an EXISTING
// virtual Data path (meshes\animationsetdatasinglefile.txt, provided by Skyrim
// Unpacked / an Output_* mod) routes INTO that providing mod and clobbers it — the same
// trap that once dropped 21 character animations into Output_Pandora. Writing a NEW path
// (community_behaviors_cache\, the consolidated compiled store at the Data root) routes to overwrite
// instead, so BR stays contained and the
// canonical file remains a clean upstream base every launch (no self-read, no staleness).
// Pointing the game at the merged cache is a separate delivery step (a read redirect).
namespace CB::core::serve {

    // Short alias for the engine's setdata model, used throughout this server.
    namespace asd = CB::engine::animsetdata;

    struct SetDataServeResult {
        bool        attempted = false;   // found >=1 bundle contributing set-data
        bool        ok = false;          // produced + wrote the merged file
        std::size_t bundles = 0;         // bundles that contributed set-data
        std::size_t baseProjects = 0;
        std::size_t mergedProjects = 0;
        asd::MergeStats stats;           // totals across all merged deltas
        std::string cachePath;           // where the merged single file was written
        std::string error;               // set iff ok == false and attempted == true
    };

    // Discover Data\community_behaviors\plugins\*.hky\meshes\animationsetdata\ split-form
    // deltas (ordered by loadOrderIni), merge onto the base resolved from
    // Data\meshes\animationsetdatasinglefile.txt, and write the merged single file to
    // Data\community_behaviors_cache\animationsetdatasinglefile.txt. Safe + idempotent to call
    // once per launch. Never throws — failures land in SetDataServeResult::error.
    /// @param exec The plugin's BELOW_NORMAL pool, for inflating packed bundles (nullptr: default-priority threads).
    SetDataServeResult ServeSetData(const std::filesystem::path& dataDir,
                             const std::filesystem::path& loadOrderIni,
                             const CB::engine::exec::TaskRunner* exec = nullptr);

    /// @brief Subscribe the DETERMINISTIC set-data redirect to the AsdsfOpen hook point
    ///        (<core/hooks/points/Resource.h>): once armed, the loader's single-file open is handed BR's cache
    ///        path whenever the loader runs — no dependency on WHEN it runs relative to kDataLoaded.
    /// @pre Called during SKSEPluginLoad, before CB::core::hooks::InstallAll().
    void InstallSetDataHook();

    /// @brief Arm the redirect with BR's merged cache (call after ServeSetData). Activates the subscriber if
    ///        AsdsfOpen installed; otherwise falls back to RedirectSetDataGlobal. No-op when `result.ok` is false.
    void ArmSetDataRedirect(const SetDataServeResult& result);

    /// @brief Fallback delivery: overwrite the engine's single-file path global (registry
    ///        Asdsf_SingleFilePath) with BR's cache path — data-only, no detour. Logs its CURRENT value first
    ///        and only rewrites when `result.ok`.
    /// @warning Races the loader if it has already run (kDataLoaded timing) — the AsdsfOpen redirect has no
    ///          such window.
    void RedirectSetDataGlobal(const SetDataServeResult& result);

}  // namespace CB::core::serve
