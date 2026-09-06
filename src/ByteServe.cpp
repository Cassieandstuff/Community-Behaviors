#include "PCH.h"

#include "ByteServe.h"
#include "Resolver.h"
#include "ServeKey.h"

#include <MinHook.h>

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>

// ByteServe — the unified byte-substitution serve layer (replaces the `.br` project rename).
//
// Every typed-hkx asset (behavior PROJECT, hkbCharacterData, hkbBehaviorGraph, skeleton, ...) loads
// through BSResourceAssetLoader::Func3 (0x140bc4d30), which identifies the asset by its CANONICAL
// PATH and resolves+reads it in one inner call: FUN_140ba88b0 (0x140ba88b0), reached ONLY from Func3
// (verified — so this seam is typed-hkx-only; textures/meshes use other loaders). We detour that
// resolver and, for the assets BR owns, SWAP the path to BR's compiled bytes in the consolidated
// cache "Data\Meshes\community_behaviors_cache\<vanilla path>" — WITHOUT changing the caller's descriptor. The
// loaded asset keeps its VANILLA identity, so the per-project BSSpeedSamplerDBManager key resolves
// (no ice-skating, BR-1) and the AnimationClipData table keys on the stock entry (no `.br` alias).
//
// Confirmed by an instrumentation pass (the log): project/character/behavior all funnel through here
// as "Actors\Character\...\X.hkx" (Meshes-relative, original case), each resolved twice — first
// ".hkt" then ".hkx"; we act on the ".hkx" attempt. Char/behaviors are served from the cache the
// warm-up materialized (gated on RedirectReady so the files exist); the PROJECT is synthesized
// on-demand by Resolver::ProjectRedirect (which also performs the readiness WAIT that keeps an early
// actor from getting un-merged behaviors) and returns its swap path.

namespace CB::byteserve {

    namespace {

        using ResolveFn = std::int64_t (*)(const char*, void*, std::uint64_t, void*);
        ResolveFn  s_orig     = nullptr;
        Resolver*  s_resolver = nullptr;

        std::atomic<int> s_logBudget{ 80 };   // cap the "serving" spam in NPC-dense cells

        // SEH-guarded bounded C-string copy (POD-only; a bad pointer degrades to "" instead of AV).
        bool SehCopy(const char* a_p, char* a_out, std::size_t a_cap) {
            __try {
                std::size_t n = 0;
                while (n + 1 < a_cap && a_p[n] != '\0') { a_out[n] = a_p[n]; ++n; }
                a_out[n] = '\0';
                return n > 0;
            } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
        }

        // ── one-run serve capture (BR-diagnostic) ────────────────────────────────────
        // Append one line per PROJECT-shaped .hkx open + BR's decision to a dedicated file, capped so it
        // never grows unbounded. This shows exactly whether NPCs ask BR for their project and what BR
        // hands back — the runtime binding datum no static artifact reveals. Writes to
        // Data\community_behaviors\serve_capture.txt (overwrite via USVFS). Remove after diagnosis.
        void Capture(const std::string& line) {
            static std::mutex mtx;
            static int        count = 0;
            std::lock_guard<std::mutex> lk(mtx);
            if (count >= 600) return;
            const bool first = (count == 0);
            ++count;
            std::error_code ec;
            const auto dir = std::filesystem::current_path(ec) / "Data" / "community_behaviors";
            std::filesystem::create_directories(dir, ec);
            // First write of the process truncates (fresh capture per run); subsequent writes append.
            std::ofstream f(dir / "serve_capture.txt",
                            std::ios::binary | (first ? std::ios::trunc : std::ios::app));
            f << line << "\r\n";
        }
        // DECISIVE PROBE: does the byte-serve swap actually RESOLVE to a real file through USVFS? The
        // engine reads "Data\meshes\" + swap = "Data\meshes\..\community_behaviors_cache\<raw>", relying on the VFS
        // to honor the "..\" escape back to the Data root. If USVFS does NOT normalize "..", that path is
        // dead, BR's compiled asset never loads, and the actor A-poses (the hook can't fall back). We test
        // it from the SAME VFS the plugin lives in: check both the literal escaped path and the normalized
        // one, plus the file size. esc=N while norm=Y => the escape is the bug (file exists, path can't reach it).
        std::string CacheProbe(const char* raw) {
            std::string rel(raw);
            for (char& c : rel) if (c == '\\') c = '/';
            std::error_code ec;
            const std::string escaped    = "Data/meshes/../community_behaviors_cache/" + rel;   // exactly what the engine reads
            const std::string normalized = "Data/community_behaviors_cache/" + rel;             // where BR actually wrote it
            const bool escOk  = std::filesystem::exists(escaped, ec);
            const bool normOk = std::filesystem::exists(normalized, ec);
            std::uintmax_t sz = 0;
            if (normOk) { sz = std::filesystem::file_size(normalized, ec); if (ec) sz = 0; }
            return "  [esc=" + std::string(escOk ? "Y" : "N") + " norm=" + std::string(normOk ? "Y" : "N")
                 + " size=" + std::to_string(sz) + "]";
        }

        // A project-shaped open: an actor .hkx directly under its root (NOT a behaviors\ / characters\
        // subfolder file) — e.g. "actors/character/defaultmale.hkx", "actors/dragon/dragonproject.hkx".
        bool ProjectShaped(const std::string& serveKey) {
            return serveKey.rfind("meshes/actors/", 0) == 0 &&
                   serveKey.find("/behaviors/") == std::string::npos &&
                   serveKey.find("/characters") == std::string::npos;
        }

        // Case-insensitive ".hkx" suffix test (the resolver also probes ".hkt" — we only swap ".hkx").
        bool EndsHkx(const char* s, std::size_t n) {
            if (n < 4) return false;
            auto lc = [](char c) { return (c >= 'A' && c <= 'Z') ? char(c - 'A' + 'a') : c; };
            return lc(s[n - 4]) == '.' && lc(s[n - 3]) == 'h' && lc(s[n - 2]) == 'k' && lc(s[n - 1]) == 'x';
        }

        std::int64_t Hook_Resolve(const char* a_path, void* a_entry, std::uint64_t a_flag, void* a_ctx) {
            if (s_resolver && a_path) {
                // Sized well above any real Meshes-relative typed-hkx path (deep authored/VFS trees can
                // exceed the old 300): a truncated copy would clip the ".hkx" suffix, fail EndsHkx, and
                // silently pass a legitimately-owned graph through to vanilla with no swap.
                char raw[1024];
                if (SehCopy(a_path, raw, sizeof raw)) {
                    const std::size_t rn = std::char_traits<char>::length(raw);
                    if (EndsHkx(raw, rn)) {
                        // Serve key is meshes-prefixed; the resolver path is Meshes-relative.
                        const std::string serveKey = "meshes/" + servekey::NormalizeKey(raw);

                        const bool projShaped = ProjectShaped(serveKey);
                        // (1) Owned character / behavior graph — swap to the cache the warm-up wrote.
                        //     Gate on RedirectReady so we never point at a not-yet-materialized file.
                        if (s_resolver->Owns(serveKey)) {
                            // Owns() is membership only — a graph that FAILED to compile is still owned
                            // but has no cache file. Redirecting to a missing file abandons the vanilla
                            // open with no fallback (→ A-pose/CTD), so require the file to exist before
                            // swapping; otherwise fall through to vanilla (safe degradation).
                            if (s_resolver->RedirectReady() && s_resolver->HasCacheFile(serveKey)) {
                                thread_local std::string swap;
                                swap = servekey::CacheSwapPath(raw);
                                if (s_logBudget.fetch_sub(1) > 0)
                                    LOG_INFO("[byteserve] serving '{}' -> '{}'.", raw, swap);
                                if (projShaped) Capture(std::string("OWNED-SERVE  ") + raw + "  ->  " + swap + CacheProbe(raw));
                                return s_orig(swap.c_str(), a_entry, a_flag, a_ctx);
                            }
                            if (projShaped) Capture(std::string("OWNED-NOTREADY/NOFILE(passthrough)  ") + raw);
                            // not ready yet (pre-warm-up load), or owned-but-uncompiled (no cache file)
                            // → fall through to vanilla, safe.
                        }
                        // (2) Owned PROJECT — ProjectRedirect waits for the cache, synthesizes BR's
                        //     project into community_behaviors_cache\, and returns the swap path ("" if not ours).
                        else {
                            thread_local std::string pswap;
                            pswap = s_resolver->ProjectRedirect(raw);
                            if (!pswap.empty()) {
                                if (s_logBudget.fetch_sub(1) > 0)
                                    LOG_INFO("[byteserve] serving project '{}' -> '{}'.", raw, pswap);
                                if (projShaped) Capture(std::string("PROJECT-SERVE  ") + raw + "  ->  " + pswap + CacheProbe(raw));
                                return s_orig(pswap.c_str(), a_entry, a_flag, a_ctx);
                            }
                            if (projShaped) Capture(std::string("PROJECT-PASSTHROUGH(vanilla)  ") + raw);
                        }
                    }
                }
            }
            return s_orig(a_path, a_entry, a_flag, a_ctx);
        }

    }  // namespace

    void SetResolver(Resolver* a_resolver) { s_resolver = a_resolver; }

    void Install() {
        const MH_STATUS init = MH_Initialize();
        if (init != MH_OK && init != MH_ERROR_ALREADY_INITIALIZED) {
            LOG_WARN("[byteserve] MH_Initialize failed ({}) — byte-serve hook NOT installed.", static_cast<int>(init));
            return;
        }
        // FUN_140ba88b0 = the BSResourceAssetLoader::Func3 typed-hkx resolver (RVA 0xBA88B0; reached
        // only from Func3, so this is a typed-hkx-only seam).
        constexpr std::uintptr_t kResolveRVA = 0xBA88B0;
        void* const target = reinterpret_cast<void*>(REL::Module::get().base() + kResolveRVA);
        if (MH_CreateHook(target, reinterpret_cast<void*>(&Hook_Resolve),
                          reinterpret_cast<void**>(&s_orig)) != MH_OK ||
            MH_EnableHook(target) != MH_OK) {
            LOG_WARN("[byteserve] failed to install FUN_140ba88b0 hook @ rva 0x{:X}.", kResolveRVA);
            return;
        }
        LOG_INFO("[byteserve] byte-substitution serve hook installed @ rva 0x{:X} "
                 "(project/character/behavior served from the consolidated community_behaviors_cache).", kResolveRVA);
    }

}  // namespace CB::byteserve
