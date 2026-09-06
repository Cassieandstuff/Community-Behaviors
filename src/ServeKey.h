#pragma once

// Pure serve-key helpers shared by the Resolver (runtime) and the br-servekey-test gate.
// Header-only, std-only — no SKSE/PCH — so the same functions the runtime uses are the ones
// the offline test exercises (no drift between "what we test" and "what serves").

#include <algorithm>
#include <cctype>
#include <string>
#include <string_view>

namespace CB::servekey {

    inline std::string ToLower(std::string s)
    {
        std::transform(s.begin(), s.end(), s.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return s;
    }

    // Canonical serve-path key: lowercase, forward slashes, no leading "./" or "data/".
    // Both the scan (relative to Data) and the game's open path fold to the same form,
    // e.g. "meshes/actors/character/behaviors/0_master.hkx".
    inline std::string NormalizeKey(std::string_view in)
    {
        std::string s = ToLower(std::string(in));
        for (char& c : s) if (c == '\\') c = '/';
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

    // The byte-substitution SWAP path for a raw Meshes-relative open path (what the Func3 resolver
    // receives — e.g. "Actors\Character\Behaviors\0_Master.hkx", backslashed, original case). The
    // engine PREPENDS the "meshes\" resource root to whatever we return, but community_behaviors_cache\ sits at
    // the DATA ROOT (one level above meshes\) — so we climb out with "..\": the engine builds
    // "meshes\..\community_behaviors_cache\<raw>", which the OS normalizes to "community_behaviors_cache\<raw>"
    // (Data-relative) → Data\community_behaviors_cache\<raw>.
    //
    // CASE (load-bearing): the swap keeps the raw's ORIGINAL case, because MO2's USVFS matches virtual
    // paths CASE-SENSITIVELY — a swap to "...\DefaultMale.hkx" will NOT find an on-disk
    // "...\defaultmale.hkx". So the file this swap resolves to must be WRITTEN at the same original case
    // (see CacheDiskRelRaw) — NOT at CacheDiskRel's lowercased key. (Confirmed in-game: only the engine-
    // opened PROJECT file mismatched — its case comes from Func3; character/behavior opens use BR's own
    // refs and are self-consistent.)
    inline std::string CacheSwapPath(std::string_view rawMeshesRel)
    {
        return std::string("..\\") + kConsolidatedCacheDir + "\\" + std::string(rawMeshesRel);
    }

    // Original-CASE Data-relative disk path the byte-substitution swap resolves to, for a raw
    // Meshes-relative open path (what Func3 gave us). This is the CASE-PRESERVING counterpart to
    // CacheDiskRel: use it whenever the file will be opened by the engine under a case we do NOT
    // control (the project file), so the written file and CacheSwapPath agree byte-for-byte on a
    // case-sensitive VFS. "Actors\Character\DefaultMale.hkx" → "community_behaviors_cache/Actors/Character/DefaultMale.hkx".
    inline std::string CacheDiskRelRaw(std::string_view rawMeshesRel)
    {
        std::string s(rawMeshesRel);
        for (char& c : s) if (c == '\\') c = '/';
        if (s.rfind("./", 0) == 0) s.erase(0, 2);
        return std::string(kConsolidatedCacheDir) + "/" + s;
    }

    // The folderRoot-relative form of a serve key ("behaviors/mymod/scene1.hkx"), or "" if the
    // key has no root. This is the shape a hkbBehaviorReferenceGenerator.behaviorName takes.
    inline std::string RootRelative(const std::string& key)
    {
        const std::string root = FolderRootOf(key);
        if (root.empty()) return {};
        return key.substr(root.size() + 1);
    }

}  // namespace CB::servekey
