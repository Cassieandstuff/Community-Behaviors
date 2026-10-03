#pragma once

#include <interface/AnimationData.h>
#include <codec/format/AnimDataYaml.h>   // ProjectHeader, ParseAnimdataIndexYaml
#include <compile/AnimDataDeriver.h>     // DeriveClipInput

#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace CB::core::serve {

    namespace animdata = ::CB::core::animdata;

    struct ServeResult {
        bool                  attempted = false;
        bool                  ok = false;          ///< projected + wrote the file
        std::size_t           bundles = 0;         ///< bundles read (master + mods)
        std::size_t           baseProjects = 0;
        std::size_t           mergedProjects = 0;
        animdata::MergeStats  stats;
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

    /// @brief Project Data\community_behaviors_cachenimationdatasinglefile.txt from the load order (ProjectAnimData:
    ///        the master's project index, every project's merged behavior graphs, its served roster, and the bundles'
    ///        animations — the highest bundle carrying an animation wins) and write it through the schema (MetaText).
    ///        Never throws; a failure (including a frame the engine would misread) leaves vanilla in place.
    ServeResult ServeAnimData(const std::filesystem::path& dataDir, const std::filesystem::path& loadOrderIni,
                              const AdsfSource& source);

    // Install the DETERMINISTIC redirect: a write_call<5> detour on the animationdata loader's
    // file-open call site (FUN_140536ec0 does `MOV RCX,[0x14315c918]; CALL 0x140d0a100` at
    // RVA 0x536F8E — the FIRST of its three opens, the single-file one). Once armed, the detour
    // swaps the path to BR's cache. Call once at plugin load, AFTER SKSE::AllocTrampoline.
    // Returns false (installs nothing) if the site isn't the expected 5-byte CALL. AE-only.
    bool InstallAnimDataHook();

    // Arm the redirect with BR's merged cache (call after ServeAnimData). Activates the detour
    // if installed; else falls back to RedirectAnimDataGlobal. No-op when result.ok is false.
    void ArmAnimDataRedirect(const ServeResult& result);

    // Fallback delivery: overwrite the engine's single-file path global (a BSFixedString at
    // RVA 0x315C918) with BR's cache path. Used when the deterministic hook can't install.
    // Logs the global's CURRENT value first (RE confirmation) and only rewrites on result.ok.
    void RedirectAnimDataGlobal(const ServeResult& result);

}  // namespace CB::core::serve
