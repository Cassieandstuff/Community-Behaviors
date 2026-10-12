#include "PCH.h"

#include "core/serve/ByteServe.h"
#include "core/bootstrap/CompileGate.h"   // CB::EnsureCompiledAndArmed — backstop trigger for the lazy compile
#include "core/resolve/Resolver.h"
#include "core/discover/ServeKey.h"

#include <core/hooks/points/Resource.h>
#include <codec/resource/PathKey.h>

#include <atomic>
#include <cstdint>
#include <string>
#include <string_view>

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

namespace CB::core::serve {

    namespace {

        using ResolveNext = CB::core::hooks::ResolveTypedHkx::Next;
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

        std::int64_t OnResolve(ResolveNext next, const char* a_path, void* a_entry, std::uint64_t a_flag, void* a_ctx) {
            // Ready() gate: Init runs on a background thread, so until it completes the resolver's state
            // (m_sources/m_skeletonServe/…) is still being written — read nothing, pass the open through
            // to vanilla. Safe: nothing BR owns loads this early in startup, and Init finishes before it
            // would. Lock-free acquire; once ready, the resolver is immutable for the process lifetime.
            if (s_resolver && s_resolver->Ready() && a_path) {
                // Sized well above any real Meshes-relative typed-hkx path (deep authored/VFS trees can
                // exceed the old 300): a truncated copy would clip the ".hkx" suffix, fail the ".hkx" test, and
                // silently pass a legitimately-owned graph through to vanilla with no swap.
                char raw[1024];
                if (SehCopy(a_path, raw, sizeof raw)) {
                    const std::size_t rn = std::char_traits<char>::length(raw);
                    if (CB::engine::codec::resource::HasExt(std::string_view(raw, rn), ".hkx")) {
                        // Serve key is meshes-prefixed; the resolver path is Meshes-relative.
                        const std::string serveKey = "meshes/" + CB::core::discover::NormalizeKey(raw);

                        // (1) Owned character / behavior graph — swap to the cache the warm-up wrote.
                        //     Gate on RedirectReady so we never point at a not-yet-materialized file.
                        if (s_resolver->Owns(serveKey)) {
                            // Backstop for the compile gate: an owned graph must never be served
                            // half-compiled. Drive the gate (a no-op once armed), then WAIT for the
                            // compile — this is what makes the SPLIT path correct: the adsf/setdata
                            // detours armed the light merges and launched the graph compile in the
                            // background so the menu could present the progress bar; here, the first
                            // time a real BR graph is actually needed, we block until that background
                            // compile has finished and RedirectReady is true. On the synchronous path
                            // EnsureCompiledAndArmed already compiled+armed and WaitForCompile no-ops.
                            if (!s_resolver->RedirectReady()) {
                                CB::EnsureCompiledAndArmed();
                                CB::WaitForCompile();
                            }
                            // Owns() is membership only — a graph that FAILED to compile is still owned
                            // but has no cache file. Redirecting to a missing file abandons the vanilla
                            // open with no fallback (→ A-pose/CTD), so require the file to exist before
                            // swapping; otherwise fall through to vanilla (safe degradation).
                            if (s_resolver->RedirectReady() && s_resolver->HasCacheFile(serveKey)) {
                                thread_local std::string swap;
                                swap = CB::core::discover::CacheSwapPath(serveKey);
                                if (s_logBudget.fetch_sub(1) > 0)
                                    LOG_INFO("[byteserve] serving '{}' -> '{}'.", raw, swap);
                                return next(swap.c_str(), a_entry, a_flag, a_ctx);
                            }
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
                                return next(pswap.c_str(), a_entry, a_flag, a_ctx);
                            }
                        }
                    }
                }
            }
            return next(a_path, a_entry, a_flag, a_ctx);
        }

    }  // namespace

    void SetResolver(Resolver* a_resolver) { s_resolver = a_resolver; }

    void Install() {
        CB::core::hooks::ResolveTypedHkx::Subscribe(&OnResolve, CB::core::hooks::Priority::Normal, "ByteServe");
    }

}  // namespace CB::core::serve
