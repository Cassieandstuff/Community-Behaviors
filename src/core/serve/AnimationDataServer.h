#pragma once

#include <havok/anim/AnimationData.h>

#include <filesystem>
#include <string>

// AnimationDataServer — the runtime clip/motion-metadata merge (animationdatasinglefile.txt).
//
// The 4th leg, parallel to AnimationSetDataServer. At startup BR discovers each bundle's
// Nemesis animationdata deltas (Data\community_behaviors\plugins\<Mod>.hky\animationdata\
// <Project>~<n>\), merges them (load order) onto the current winning base
// animationdatasinglefile.txt, and writes the merged single file to BR's own cache. The
// game is then pointed at the cache by a read redirect on the loader's open call site.
//
// Same MO2-routing rationale as the set-data server: BR writes only a NEW path
// (community_behaviors_cache\, the consolidated compiled store at the Data root) so the canonical loose
// file stays a clean upstream base and the write lands in overwrite, never clobbering the
// providing mod.
namespace CB { class GraphClipSink; }   // fwd — the adsf-derive feature's clip accumulator

namespace CB::adserve {

    // The animationdata model (parse/emit/merge) now lives in havok-core so the clip-list
    // deriver can share it; keep the short `animdata::` spelling everything here already uses.
    namespace animdata = ::havok::animdata;

    struct ServeResult {
        bool                  attempted = false;   // found >=1 bundle contributing animationdata
        bool                  ok = false;          // produced + wrote the merged file
        std::size_t           bundles = 0;         // bundles that contributed animationdata
        std::size_t           baseProjects = 0;
        std::size_t           mergedProjects = 0;
        animdata::MergeStats  stats;               // totals across all merged patches
        std::string           cachePath;
        std::string           error;               // set iff ok == false and attempted == true
    };

    // Discover Data\community_behaviors\plugins\*.hky\animationdata\<Project>~<n>\ Nemesis patch
    // dirs (ordered by loadOrderIni), merge onto the base resolved from
    // Data\meshes\animationdatasinglefile.txt, and write the merged single file to
    // Data\community_behaviors_cache\animationdatasinglefile.txt. Safe + idempotent per launch;
    // never throws — failures land in ServeResult::error.
    // `sink` (optional): when non-null the caller opted into the adsf-derive feature, so this holds
    // every graph's feature-derived clip inputs. THIS FIRST CUT ONLY VALIDATES the sink — it logs a
    // summary of what the unified derive produced so it can be compared against the proven collated
    // output in-engine; it does NOT yet drive the emitted file (that is the follow-up once the sink is
    // confirmed correct). Passing null (the default) is the unchanged, proven path.
    // `rosterFromScan` (opt-in): when true, the emitted adsf's per-project `assets:` roster (the
    // list func3 enumerates) is UNIONED with the havok assets CB actually serves — every mod
    // bundle's behavior + character units, grouped by actor root — so a mod's added behaviors reach
    // the roster WITHOUT a hand-authored index.yaml. Additive + case-insensitive dedup onto the
    // canonically-cased base, so the ESM-cased char->adsf bind is never rewritten. Off by default
    // (the proven authored-index.yaml path is unchanged when false).
    ServeResult ServeAnimData(const std::filesystem::path& dataDir,
                              const std::filesystem::path& loadOrderIni,
                              const GraphClipSink*         sink = nullptr,
                              bool                         rosterFromScan = false);

    // Install the DETERMINISTIC redirect: a write_call<5> detour on the animationdata loader's
    // file-open call site (FUN_140536ec0 does `MOV RCX,[0x14315c918]; CALL 0x140d0a100` at
    // RVA 0x536F8E — the FIRST of its three opens, the single-file one). Once armed, the detour
    // swaps the path to BR's cache. Call once at plugin load, AFTER SKSE::AllocTrampoline.
    // Returns false (installs nothing) if the site isn't the expected 5-byte CALL. AE-only.
    bool InstallAnimDataHook();

    // Opt-in: patch ShouldLoadCollatedAnimTextData so the engine loads the per-project (dev) form
    // (Meshes\AnimationData\DirList.txt + <Project>.txt + BoundAnims\Anims_<Project>.txt) that BR
    // emits, instead of the collated blob. Call at plugin load, before the engine's animdata load.
    // Retail-untested engine path — gate behind an explicit opt-in. AE-only; returns false otherwise.
    bool EnablePerProjectAnimData();

    // Install the per-project loader gate: hooks the engine's animdata singleton ctor so that its
    // read BLOCKS on BR's merge + per-project materialize (run synchronously in the hook, once),
    // then proceeds. Removes the plugin-load timing race — correctness is by ordering. Call at
    // plugin load in per-project mode INSTEAD of running ServeAnimData there. Returns false on hook
    // failure (fall back to the collated path).
    bool InstallPerProjectGate();

    // Revert EnablePerProjectAnimData: flag byte -> 0x01 (collated) + clears per-project mode.
    // Call if the gate can't be installed, so a flipped flag never leaves the engine on the
    // per-project branch with nothing materialized. Safe to call unconditionally.
    void DisablePerProjectAnimData();

    // Arm the redirect with BR's merged cache (call after ServeAnimData). Activates the detour
    // if installed; else falls back to RedirectAnimDataGlobal. No-op when result.ok is false.
    void ArmAnimDataRedirect(const ServeResult& result);

    // Fallback delivery: overwrite the engine's single-file path global (a BSFixedString at
    // RVA 0x315C918) with BR's cache path. Used when the deterministic hook can't install.
    // Logs the global's CURRENT value first (RE confirmation) and only rewrites on result.ok.
    void RedirectAnimDataGlobal(const ServeResult& result);

}  // namespace CB::adserve
