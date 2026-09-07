#pragma once

#include <havok/anim/AnimationSetData.h>   // the setdata model now lives in havok-core

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
namespace CB::asdserve {

    // The setdata model moved to havok-core (havok::animsetdata); this alias keeps the
    // server's `asd::` code unchanged. Declared inside asdserve so it shadows any outer
    // CB::asd (the Nemesis-convert tool namespace) within this server.
    namespace asd = havok::animsetdata;

    struct ServeResult {
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
    // once per launch. Never throws — failures land in ServeResult::error.
    ServeResult ServeSetData(const std::filesystem::path& dataDir,
                             const std::filesystem::path& loadOrderIni);

    // Install the DETERMINISTIC set-data redirect: a write_call<5> detour on the loader's
    // file-open call site (AnimationClipDataSingleton / FUN_14053b000 does
    //   MOV RCX,[single-file path global]; CALL 0x140d0a100
    // at RVA 0x53B084). Once armed, the detour swaps the path argument to BR's cache, so
    // the game opens BR's merged file whenever the loader runs — no dependency on WHEN the
    // loader runs relative to kDataLoaded (which the global-write below could race). Call
    // once at plugin load, AFTER SKSE::AllocTrampoline. Returns false (and installs
    // nothing) if the site isn't the expected 5-byte CALL — e.g. a different game version —
    // so the caller falls back to the global-write. AE-only.
    bool InstallSetDataHook();

    // Arm the redirect with BR's merged cache (call after ServeSetData). If the hook is
    // installed, this activates the detour; otherwise it falls back to RedirectSetDataGlobal
    // (the data-only global overwrite). No-op when `result.ok` is false.
    void ArmSetDataRedirect(const ServeResult& result);

    // Fallback delivery: overwrite the engine's single-file path global (a BSFixedString at
    // RVA 0x315C930) with BR's cache path — data-only, no detour. Used when the deterministic
    // hook can't be installed (unexpected game version). Logs the global's CURRENT value
    // first (RE confirmation) and only rewrites when `result.ok`. AE-only.
    void RedirectSetDataGlobal(const ServeResult& result);

}  // namespace CB::asdserve
