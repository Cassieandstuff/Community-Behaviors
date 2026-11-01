#pragma once

// Pure serve-key helpers (header-only, std + engine codec only — no SKSE/PCH).

#include <codec/resource/PathKey.h>

#include <algorithm>
#include <string>
#include <string_view>

namespace CB::core::discover {

    /// @brief The serve key: path identity (codec::resource::PathKey) plus serve-path cleanup — no repeated
    ///        slashes, no leading "./" or "data/". Both the scan (relative to Data) and the game's open path
    ///        reach the same form, e.g. "meshes/actors/character/behaviors/0_master.hkx".
    inline std::string NormalizeKey(std::string_view in)
    {
        std::string s = CB::engine::codec::resource::PathKey(in);
        // Collapse repeated slashes ("a//b" -> "a/b"). The game opens character files with a
        // trailing-'\' folderRoot ("Actors\Character\") plus a subfoldered file
        // ("Characters\DefaultMale.hkx"), which our "root/file" join turns into a '//'. Both
        // the scan-time key and the runtime key fold the same way.
        s.erase(std::unique(s.begin(), s.end(),
                            [](char a, char b) { return a == '/' && b == '/'; }),
                s.end());
        if (s.rfind("./", 0) == 0) s.erase(0, 2);
        if (s.rfind("data/", 0) == 0) s.erase(0, 5);
        return s;
    }

    // The CONSOLIDATED cache dir — ONE top-level tree at the DATA ROOT, Data\community_behaviors_cache\,
    // mirroring the full serve path so two creature mods' same-named projects never collide. It
    // holds BOTH the compiled graph/project HKX tree AND the animationdata/animationsetdata .txt
    // caches (+ the readiness sentinel) — a single consolidated compiled-output location.
    //
    // BR serves compiled HKX bytes by BYTE-SUBSTITUTION: the load hook swaps a vanilla open "<X>.hkx"
    // to the cache copy. The engine's typed-hkx resolver (Func3) prepends the "meshes\" resource
    // root to whatever it's handed, but community_behaviors_cache\ lives one level ABOVE meshes\, so the swap
    // path climbs out with "..\" (see CacheSwapPath). The loaded asset keeps its VANILLA identity
    // (basename + descriptor stem), so the per-project speed-sampler DB key resolves (no ice-skating)
    // and the animdata table keys on the stock entry (no ".br" alias). Compiled graphs carry VANILLA
    // child refs — the hook redirects every owned open, so no ref-qualification is needed. Replaces
    // the old per-actor "community_behaviors_cache\" subtree + ".br.hkx" rename AND the old split
    // Meshes\community_behaviors_cache\ (HKX) + community_behaviors\cache\ (txt) layout entirely.
    inline constexpr const char* kConsolidatedCacheDir = "community_behaviors_cache";
    /// @brief Motion matching's warm-up cache in kConsolidatedCacheDir (CBMotionMatchingCache), kept across warm-ups.
    inline constexpr const char* kMotionMatchingCacheFile = "motionmatching.hkx";

    // A behavior/character serve key is "<folderRoot>/<subdir>/…/<file>.hkx" where folderRoot
    // is the actor's PROJECT folder root (dirname of the .hkx project the engine loads — every
    // child ref in the graph tree resolves relative to it). Vanilla layout is exactly two
    // components deep (<root>\behaviors\*.hkx, <root>\characters\*.hkx, <root>\characters
    // female\*.hkx), but authored bundles may nest graphs deeper (cutscene units under
    // <root>\behaviors\<Mod>\<Cutscene>.hkx — see docs/true-cinematics/cutscene-format.md).
    //
    // Rule: the root is everything BEFORE the first non-leading "behaviors" / "characters*"
    // segment (the engine's fixed subfolder names). If neither is present, fall back to the
    // vanilla "all but the last two components" rule. Returns "" if the key isn't that deep.
    //
    // Every consumer of the root (CacheDiskRel, QualifyChildRef, the redirect map) MUST use this
    // one helper — the nested-unit bug was exactly these disagreeing on the root.
    inline std::string FolderRootOf(const std::string& key)
    {
        std::size_t pos = 0;
        while (pos < key.size()) {
            const auto next = key.find('/', pos);
            if (next == std::string::npos) break;
            const std::string_view seg(key.data() + pos, next - pos);
            if (pos != 0 && (seg == "behaviors" || seg.rfind("characters", 0) == 0))
                return key.substr(0, pos - 1);   // e.g. "meshes/actors/character"
            pos = next + 1;
        }
        const auto s1 = key.find_last_of('/');
        if (s1 == std::string::npos || s1 == 0) return {};
        const auto s2 = key.find_last_of('/', s1 - 1);
        if (s2 == std::string::npos) return {};
        return key.substr(0, s2);   // vanilla fallback
    }

    // Disk path (relative to Data) for a cached graph/project. community_behaviors_cache\ is at the DATA ROOT,
    // so strip the "meshes/" resource prefix and hang the rest under community_behaviors_cache/ —
    // "community_behaviors_cache/<rest of serve key>". Full structure preserved (collision-free). This is the
    // exact file the byte-substitution swap (CacheSwapPath) resolves to.
    inline std::string CacheDiskRel(const std::string& key)
    {
        if (key.rfind("meshes/", 0) == 0)
            return std::string(kConsolidatedCacheDir) + "/" + key.substr(7);
        // Keys without the meshes/ prefix (rare) still get a consolidated home.
        return std::string(kConsolidatedCacheDir) + "/" + key;
    }

    /// @brief The byte-substitution SWAP path for a serve key: the exact cache file CacheDiskRel(key) names, as the
    ///        Meshes-relative path the engine opens instead of the vanilla one.
    /// @note The engine prepends its "meshes\" resource root, and community_behaviors_cache\ sits one level above
    ///       it at the Data root — hence the leading "..\" ("meshes\..\community_behaviors_cache\<rest>").
    /// @note CASE: the swap names the cache file by the EXACT string CB wrote it under (CacheDiskRel of the same
    ///       key), so the open is precise on a case-sensitive filesystem no matter how the engine spelled its
    ///       request. Swapping the engine's raw spelling instead only worked where the lookup folds.
    inline std::string CacheSwapPath(const std::string& key)
    {
        std::string s = "..\\" + CacheDiskRel(key);
        for (char& c : s) if (c == '/') c = '\\';
        return s;
    }

}  // namespace CB::core::discover
