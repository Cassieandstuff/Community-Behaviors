#include "PCH.h"

#include "core/debug/AnimDataProbe.h"
#include "core/serve/AnimationDataServer.h"
#include "core/serve/AnimationSetDataServer.h"
#include "core/serve/ByteServe.h"
#include "core/bootstrap/CompileGate.h"
#include "core/debug/DebugOverlay.h"
#include "core/debug/RuntimeTrace.h"
#include "core/bootstrap/ProgressHud.h"
#include "core/bootstrap/ProgressOverlay.h"
#include "core/resolve/Resolver.h"
#include "core/debug/SyncClipProbe.h"

#include "SimpleIni.h"   // [Cache] bForceRegenerate toggle
#include "havok/sct/BehaviorCompiler.h"   // SetSchemaCompiler — data-driven compiler toggle

#include <atomic>
#include <chrono>
#include <cstdlib>       // std::getenv — schema-dir env fallback
#include <filesystem>
#include <mutex>         // EnsureCompiledAndArmed one-shot latch
#include <process.h>   // _beginthreadex — large-stack warm-up thread

namespace CB {

    // The load-order -> compiled-behavior engine. Lives for the process lifetime;
    // the interceptor holds a pointer to it.
    static Resolver g_resolver;

    // Per-project animdata mode (opt-in file marker), decided at plugin load. The compile gate
    // reads it to skip the collated animdata serve when the per-project loader gate owns that leg.
    static std::atomic<bool> g_perProject{ false };

    // Set on any thread currently executing the compile gate's work — the loader thread that claimed
    // it AND the 64MB compile worker it spawns. A re-entrant gate call from either (e.g. a detour
    // fired from inside the compile) returns immediately instead of blocking, so the join can never
    // deadlock against the mutex the claiming thread holds. The compile is pure file I/O today and
    // won't re-enter, but this keeps that a guarantee, not an assumption.
    static thread_local bool t_compileGateInProgress = false;

    // Cache force-regenerate toggle (Data\SKSE\Plugins\Community Behaviors\settings.ini,
    // [Cache] bForceRegenerate). The warm-up recompile is a pure optimization whose output
    // persists on disk between runs, so BR can skip it and reuse the existing cache. This flag
    // forces a fresh recompile instead. Its DEFAULT is build-dependent: DEBUG builds always
    // regenerate (the cache can't drift while iterating on the converter/bundles); release
    // builds reuse (fast start). The ini key, WHEN PRESENT, overrides the build default in
    // either direction — so a release user can force a rebuild, and the shipped settings.ini
    // leaves the key commented so the build default applies until someone opts in.
    static bool ReadForceRegenerate()
    {
#ifdef NDEBUG
        constexpr bool kDefault = false;   // release: reuse an existing cache
#else
        constexpr bool kDefault = true;    // debug: always regenerate
#endif
        CSimpleIniA ini;
        ini.SetUnicode();
        if (ini.LoadFile("Data/SKSE/Plugins/Community Behaviors/settings.ini") >= 0)
            return ini.GetBoolValue("Cache", "bForceRegenerate", kDefault);
        return kDefault;   // no settings.ini shipped/edited -> build default
    }

    // ER wildcard gate on/off (settings.ini, [ERGate] bEnable). DEFAULT OFF in EVERY build: the gate
    // rewrites every fire-on-event wildcard into a conditional one across every graph and is not yet
    // verified in-engine — a fault would block all wildcards and T-pose every actor — so it is strictly
    // opt-in until proven, at which point this default flips. Unrelated to the watermark (always on).
    static bool ReadERGateEnabled()
    {
        CSimpleIniA ini;
        ini.SetUnicode();
        if (ini.LoadFile("Data/SKSE/Plugins/Community Behaviors/settings.ini") >= 0)
            return ini.GetBoolValue("ERGate", "bEnable", false);
        return false;
    }

    // adsf-derive feature toggle (settings.ini, [Compiler] bAdsfFromFeature). DEFAULT OFF: the
    // unified derive (animationdata straight off the compiled graph via the contributor feature) is a
    // NEW path parallel to the proven collated merge. When on, CompileAll fills the resolver's clip
    // sink and ServeAnimData reports what it produced for comparison — it does not yet drive the emit.
    static bool ReadAdsfFromFeature()
    {
        CSimpleIniA ini;
        ini.SetUnicode();
        if (ini.LoadFile("Data/SKSE/Plugins/Community Behaviors/settings.ini") >= 0)
            return ini.GetBoolValue("Compiler", "bAdsfFromFeature", false);
        return false;
    }

    // adsf roster-from-scan toggle (settings.ini, [Compiler] bAdsfRosterFromScan). DEFAULT OFF:
    // sourcing the adsf per-project asset roster (the paths func3 enumerates) from the hky scan
    // instead of a hand-authored index.yaml is a NEW transform. It is additive/union only and
    // never rewrites the canonically-cased base, but it changes the emitted `assets:`, so it is
    // opt-in until proven in-engine (diff the emitted per-project `assets:` against an authored run).
    static bool ReadAdsfRosterFromScan()
    {
        CSimpleIniA ini;
        ini.SetUnicode();
        if (ini.LoadFile("Data/SKSE/Plugins/Community Behaviors/settings.ini") >= 0)
            return ini.GetBoolValue("Compiler", "bAdsfRosterFromScan", false);
        return false;
    }

    // Data-driven compiler toggle (settings.ini, [Compiler] bUseSchema / sSchemaDir). DEFAULT ON now
    // (havok-core retirement flip): the schema-driven AssembleGraph is byte-identical to the typed
    // builder offline across the whole vanilla corpus, and CompileBehavior falls back to the typed path
    // on ANY per-graph failure — so serving through it can't regress a graph, only route it. The Havok/
    // schema tree ships with BR (Data/Community Behaviors/Havok, staged by CMake); sSchemaDir overrides,
    // then env SCT_HAVOK_SCHEMA_DIR, then that shipped default. Set bUseSchema=false to force typed.
    static void ApplySchemaCompilerSetting()
    {
        bool        enabled = true;
        std::string dir;
        CSimpleIniA ini;
        ini.SetUnicode();
        if (ini.LoadFile("Data/SKSE/Plugins/Community Behaviors/settings.ini") >= 0) {
            enabled = ini.GetBoolValue("Compiler", "bUseSchema", true);
            dir     = ini.GetValue("Compiler", "sSchemaDir", "");
        }
        if (dir.empty() && std::getenv("SCT_HAVOK_SCHEMA_DIR") == nullptr)
            dir = "Data/Community Behaviors/Havok";   // BR-shipped schema tree (MO2 VFS-merged path)
        havok::sct::SetSchemaCompiler(enabled, dir);
        LOG_INFO("Community Behaviors: data-driven compiler {} (schema registry {}).",
                 enabled ? "ENABLED" : "off",
                 havok::sct::SchemaCompilerReady() ? "loaded" : (enabled ? "FAILED TO LOAD → typed fallback" : "not loaded"));
        // Surface WHY the schema path is off when it was asked for — a schema-version mismatch
        // (outdated/ahead Havok/ tree vs the version this build speaks) reads as a loud WARN, not a
        // silent typed fallback.
        if (enabled && !havok::sct::SchemaCompilerReady()) {
            const std::string& why = havok::sct::SchemaCompilerError();
            if (!why.empty()) LOG_ERROR("Community Behaviors: schema compiler off — {}.", why);
        }
    }

    // Cache warm-up entry. Runs on a dedicated thread with a generous (64MB) stack
    // RESERVE as defense-in-depth for compiling the deepest master graphs — the reserve
    // is virtual-only until touched, so it costs nothing. A bare std::thread's ~1MB
    // default is almost certainly sufficient (a 1MB-stack repro of the 6-layer 0_master
    // compile succeeded); this just removes any ceiling.
    //
    // NOTE: the main-menu freeze-then-crash originally chased here was NOT a stack
    // overflow. It was a VFS collision between two enabled mods both shipping
    // Data\community_behaviors\plugins\ (a stale offline pre-merged bundle overlaying the
    // per-mod deltas), which fed CompileBehavior a corrupt merged 0_master. That is a
    // load-order/data fix (drop the stale bundle), not a code fix.
    static unsigned __stdcall WarmUpThread(void* param)
    {
        t_compileGateInProgress = true;   // this worker is immune to a re-entrant compile-gate call
        const std::size_t total = reinterpret_cast<std::size_t>(param);
        const auto        t0    = std::chrono::steady_clock::now();
        g_resolver.CompileAll([](std::size_t done, std::size_t tot) {
            ProgressOverlay::SetProgress(done, tot, true);
        });
        // (The character animationNames roster is built offline now — the converter scans clip
        // generators into each unit's data/animations.yaml and LoadMerged unions every layer at
        // compile time — so there is no runtime roster-completion pass between compile and disk write.)
        // Write every compiled graph + a synthesized project per character to disk, in the
        // above-OAR split layout (<Data>\Meshes\<folderRoot>\community_behaviors_cache\... plus
        // <folderRoot>\<char>.br.hkx), and publish the redirect map. The project-load hook
        // (fragile: fires on the actor-load path) then only reads that map + rewrites a
        // descriptor string — never compiles. This 64MB-stack thread is the ONLY place a
        // compile is allowed. Clears stale cross-session BR output first.
        const std::size_t wrote = g_resolver.MaterializeCacheToDisk(
            std::filesystem::current_path() / "Data", nullptr);
        const double secs = std::chrono::duration<double>(
                                std::chrono::steady_clock::now() - t0).count();
        ProgressOverlay::SetProgress(total, total, false);
        LOG_INFO("Community Behaviors: precompiled {} graph(s), materialized {} to disk in {:.1f}s "
                 "(runtime loads served from disk cache).", total, wrote, secs);
        { std::size_t s = 0, t = 0; havok::sct::SchemaCompilerStats(s, t);
          if (s || t) LOG_INFO("Community Behaviors: compiler paths this warm-up — schema-driven {}, typed {}.", s, t); }
        return 0;
    }

    // Background compile thread handle (split path only). Set once under the gate mutex; WaitForCompile
    // joins it. Process-lifetime, one compile — the handle is intentionally never closed.
    static HANDLE g_compileThread = nullptr;

    // adsf + setdata serve/arm. Independent of the graph compile when the adsf-derive feature is OFF
    // (the default): the merge reads the vanilla base + bundle deltas, no clip sink. Set-data always;
    // the collated animdata leg only when the per-project loader gate isn't serving that leg itself.
    static void ArmAdsfSetDataServe(bool perProject)
    {
        const std::filesystem::path dataAbs = std::filesystem::current_path() / "Data";

        const auto sd = asdserve::ServeSetData("Data", "Data/community_behaviors/loadorder.txt");
        if (sd.attempted && !sd.ok)
            LOG_WARN("Community Behaviors: animationsetdata merge did not complete: {}", sd.error);
        asdserve::ArmSetDataRedirect(sd);

        if (!perProject) {
            const GraphClipSink* clipSink =
                g_resolver.AdsfFromFeature() ? &g_resolver.ClipSink() : nullptr;
            const auto ad = adserve::ServeAnimData("Data", "Data/community_behaviors/loadorder.txt",
                                                   clipSink, ReadAdsfRosterFromScan());
            if (ad.attempted && !ad.ok)
                LOG_WARN("Community Behaviors: animationdata merge did not complete: {}", ad.error);
            adserve::ArmAnimDataRedirect(ad);

            if (g_resolver.AdsfFromFeature() && g_resolver.ClipSink().ClipCount() > 0)
                g_resolver.DeriveAnimData(dataAbs);
        }
    }

    // REUSE (warm start): serve the CACHED adsf/asdsf verbatim instead of re-deriving them.
    //
    // The adsf/asdsf are COMPILE OUTPUTS, not independent merges: their clip -> high-band-animIndex
    // assignment (and the roster-dependent set-data CRC guard) is coherent ONLY with the character/
    // behavior cache produced in the SAME pass — char-setup binds animations through that index space.
    // Re-running ServeAnimData/ServeSetData on a warm start re-derives that mapping INDEPENDENTLY of the
    // reused graphs (ArmCacheFromDisk skips CompileAll, so m_characterRosters/the clip sink are empty and
    // the high-band allocation floor can shift), which desyncs it from the cached characters — on the
    // second run an attack clip then binds a foreign animation (an idle, even a furniture clip). Both
    // files live in community_behaviors_cache alongside the graphs and are explicitly PRESERVED by
    // MaterializeCacheToDisk's cache wipe, so whenever the graph cache is reusable these are present and
    // coherent. Arm the redirects straight at them; the hooks intern a fixed cache path and only gate on
    // ServeResult::ok, so a synthetic ok result serves the on-disk file with no re-derive.
    //
    // Returns true once every needed leg is armed from cache. Returns false ONLY when the collated
    // animationdata (which always has content — the vanilla base) is missing, i.e. the cache is partial;
    // the caller then falls back to the derive path. A missing set-data file is NOT a failure: a load
    // order with no set-data bundles legitimately produced none (run-1 left the redirect inactive and the
    // engine read vanilla), so "absent" here reproduces that exactly.
    static bool ArmAdsfSetDataFromCache(bool perProject)
    {
        namespace fs = std::filesystem;
        const fs::path cacheDir = fs::current_path() / "Data" / "community_behaviors_cache";
        std::error_code ec;

        const fs::path asdsf = cacheDir / "animationsetdatasinglefile.txt";
        if (fs::exists(asdsf, ec)) {
            asdserve::ServeResult sd; sd.attempted = true; sd.ok = true; sd.cachePath = asdsf.string();
            asdserve::ArmSetDataRedirect(sd);
            LOG_INFO("Community Behaviors: warm reuse — serving cached set-data (no re-derive).");
        }

        if (!perProject) {
            const fs::path adsf = cacheDir / "animationdatasinglefile.txt";
            if (!fs::exists(adsf, ec)) {
                LOG_WARN("Community Behaviors: warm cache missing '{}' — re-deriving adsf/set-data this launch.",
                         adsf.string());
                return false;
            }
            adserve::ServeResult ad; ad.attempted = true; ad.ok = true; ad.cachePath = adsf.string();
            adserve::ArmAnimDataRedirect(ad);
            LOG_INFO("Community Behaviors: warm reuse — serving cached animationdata (no re-derive).");
        }
        return true;
    }

    // The compile gate — see CompileGate.h. One-shot, thread-safe.
    void EnsureCompiledAndArmed()
    {
        static std::atomic<bool> s_done{ false };
        static std::mutex        s_mtx;

        if (s_done.load(std::memory_order_acquire)) return;
        if (t_compileGateInProgress) return;   // re-entrant call on a doing thread — work is underway
        std::lock_guard<std::mutex> lk(s_mtx);
        if (s_done.load(std::memory_order_relaxed)) return;
        t_compileGateInProgress = true;

        const bool                  perProject = g_perProject.load(std::memory_order_acquire);
        const std::filesystem::path dataAbs    = std::filesystem::current_path() / "Data";
        const bool                  warm       = !ReadForceRegenerate() && g_resolver.CachePresent(dataAbs);

        // Split path (progress bar) is the DEFAULT for a cold compile: the bar rides the game's own
        // present via ProgressHud, the same coexisting pattern CS/OAR use, so it's safe to ship on.
        // Two guards remain: the adsf-derive feature (adsf then needs the compile's clip sink, so the
        // merge can't run ahead of the compile — must stay synchronous), and an explicit opt-out marker
        // (Data\community_behaviors\progressbar.disable) that forces the old fully-synchronous compile.
        const bool split = !warm &&
                            !g_resolver.AdsfFromFeature() &&
                            !std::filesystem::exists("Data/community_behaviors/progressbar.disable");

        if (warm) {
            g_resolver.ArmCacheFromDisk(dataAbs);   // instant; no compile, no bar
            // Serve the cached adsf/asdsf verbatim (coherent with the reused graphs); only re-derive if
            // the collated cache is partial. See ArmAdsfSetDataFromCache.
            if (!ArmAdsfSetDataFromCache(perProject))
                ArmAdsfSetDataServe(perProject);
        } else if (split) {
            // Arm adsf/setdata NOW (fast, independent of the graph compile) so the engine's early reads
            // are served, then run the heavy compile in the BACKGROUND and RETURN. The game reaches its
            // menu and presents while the compile runs; ProgressHud's passive present hook draws the bar.
            // byteserve calls WaitForCompile() before serving an owned graph, so nothing is served
            // half-compiled — correctness holds regardless of the bar.
            ArmAdsfSetDataServe(perProject);
            const std::size_t total = g_resolver.SourceCount();
            ProgressOverlay::SetProgress(0, total, true);
            ProgressHud::Install();   // passive Present hook; safe (rides the game present, never forces one)
            LOG_INFO("Community Behaviors: SPLIT compile — arming adsf/setdata now, compiling {} graph(s) "
                     "in the background (progress bar enabled).", total);
            if (const std::uintptr_t h = _beginthreadex(nullptr, 64u * 1024u * 1024u,
                        &WarmUpThread, reinterpret_cast<void*>(total),
                        STACK_SIZE_PARAM_IS_A_RESERVATION, nullptr)) {
                g_compileThread = reinterpret_cast<HANDLE>(h);   // byteserve joins via WaitForCompile()
            } else {
                LOG_ERROR("Community Behaviors: failed to launch background precompile thread — falling back.");
                ProgressOverlay::SetProgress(0, 0, false);
            }
        } else {
            // Default proven path: compile + materialize on the 64MB-stack thread and JOIN it (parked in
            // the detour, arm ordered ahead of graph load), THEN arm adsf/setdata. No progress bar.
            const std::size_t total = g_resolver.SourceCount();
            LOG_INFO("Community Behaviors: precompiling {} graph(s) at first engine open on thread {} "
                     "(loader thread parked in our hook — arm ordered ahead of graph load).",
                     total, ::GetCurrentThreadId());
            if (const std::uintptr_t h = _beginthreadex(nullptr, 64u * 1024u * 1024u,
                        &WarmUpThread, reinterpret_cast<void*>(total),
                        STACK_SIZE_PARAM_IS_A_RESERVATION, nullptr)) {
                WaitForSingleObject(reinterpret_cast<HANDLE>(h), INFINITE);
                CloseHandle(reinterpret_cast<HANDLE>(h));
            } else {
                LOG_ERROR("Community Behaviors: failed to launch precompile thread — behavior serve NOT armed.");
            }
            ArmAdsfSetDataServe(perProject);
        }

        t_compileGateInProgress = false;
        s_done.store(true, std::memory_order_release);
    }

    void WaitForCompile()
    {
        // Join the split-path background compile if one is running. No-op on the synchronous path
        // (g_compileThread stays null) or once the thread has already finished. Publish under the same
        // mutex-established happens-before via the handle read; the thread's own release (materialize +
        // RedirectReady) is visible after the wait returns. Multiple threads may wait concurrently.
        HANDLE h = g_compileThread;
        if (h) WaitForSingleObject(h, INFINITE);
    }

    static void OnMessage(SKSE::MessagingInterface::Message* a_msg)
    {
        if (!a_msg) return;

        if (a_msg->type == SKSE::MessagingInterface::kDataLoaded) {
            // The compile progress bar is now drawn by ProgressHud (own ImGui+D3D11 via a passive
            // present hook installed from the compile gate) — no SMF registration, nothing to install
            // for it here. These two still need kDataLoaded timing:
            //   • DebugOverlay — SMF isn't up at plugin load.
            //   • animprobe — the animationdata clip singleton isn't built yet at plugin load.
            CB::RuntimeTrace::Install();   // arm [Debug] bRuntimeTrace before the present hook is live
            CB::DebugOverlay::Install();
            CB::animprobe::ProbeAnimClipData();
        }
        else if (a_msg->type == SKSE::MessagingInterface::kPostLoadGame ||
                 a_msg->type == SKSE::MessagingInterface::kNewGame) {
            // By now the animationdata singleton is definitely built and animations are active.
            CB::animprobe::ProbeAnimClipData();
        }
    }

}  // namespace CB

SKSEPluginLoad(const SKSE::LoadInterface* a_skse)
{
    SKSE::Init(a_skse);
    PluginLogger::Init("CommunityBehaviors", "Community Behaviors");
    LOG_INFO("Community Behaviors v0.3.1.0 loading — runtime behavior compiler.");

    // Behavior + character + project DELIVERY is BYTE-SUBSTITUTION (ByteServe): MinHook the
    // BSResourceAssetLoader::Func3 typed-hkx resolver (FUN_140ba88b0) and swap each owned vanilla
    // open -> BR's compiled bytes in the consolidated Data\community_behaviors_cache\, WITHOUT touching
    // the caller's descriptor. The loaded asset keeps its vanilla identity, so the whole tree
    // (project -> character -> behaviors) loads through the engine's OWN machinery ABOVE Open
    // Animation Replacer's Unk3 wrap, with no rep-stosq collision. This retired both the old
    // above-OAR project-descriptor rename (ProjectLoadProbe) and the deep LoadBehaviorGraph /
    // character-loader function hooks (BehaviorFileInterceptor) — both now deleted.
    //
    // The set-data / anim-data file-open detours are a different, OAR-independent layer (they hook
    // the low-level open call site, below OAR) and stay. One shared trampoline covers them; 2KB is
    // ample for the handful of write_call nodes. (ByteServe uses MinHook, not the trampoline.)
    SKSE::AllocTrampoline(1u << 11);
    CB::asdserve::InstallSetDataHook();
    CB::adserve::InstallAnimDataHook();
    CB::byteserve::Install();  // typed-hkx byte-substitution: serves project/character/
                                          // behavior from the consolidated cache under vanilla paths
                                          // (subsumes the retired ProjectLoadProbe descriptor redirect)

    // Debug probe (opt-in): log every paired/synchronized clip as it activates, to pin the
    // paired-animation breakage (dialogue idle T-pose, mount jitter). Off unless the marker exists.
    if (std::filesystem::exists("Data/community_behaviors/syncprobe.enable"))
        CB::syncprobe::Install();

    // Animdata cache form. COLLATED is the default and the only supported path: it serves
    // community_behaviors_cache/animationdatasinglefile.txt and the engine reads clips AND motion
    // from it. The per-project loader (the engine's Meshes\AnimationData\ form) is RETAINED but
    // OFF by default — it was a workaround for dialogue T-posing whose real cause was the missing
    // set-data ".br" alias (fixed separately), and in a real load order it silently BREAKS root
    // motion AND paired/synchronized animations (horse mount, killmoves) — the engine's per-project
    // read doesn't deliver motion the way the collated read does. Opt-in is a FILE marker
    // (Data\community_behaviors\perproject.enable), deliberately NOT an .ini key, so it can't be flipped
    // on by accident through config. The loader GATE (below) still defers merge+materialize so
    // plugin load stays trivial when it IS enabled.
    bool perProject = false;
    if (std::filesystem::exists("Data/community_behaviors/perproject.enable"))
        perProject = CB::adserve::EnablePerProjectAnimData();

    // ── Behavior serve wiring (cheap, must-be-early only). The HEAVY work — compile + materialize
    // + adsf/setdata serve/arm — is DEFERRED to the compile gate (CompileGate.h / EnsureCompiledAndArmed):
    // the engine's first animationsetdata/animationdata open (or first BR-owned behavior resolve)
    // runs it from INSIDE our detour, parking the loader thread there until it returns. The engine's
    // own single-threaded load order then sequences everything downstream (graph arming, the later
    // adsf/setdata reads, byteserve) AFTER the compile — no still-vanilla-graph window — while the
    // progress overlay renders on the render thread. That retires the old synchronous-at-plugin-load
    // compile, whose only justification (BR-21) the engine's load order already gives us here for free.
    //
    // The serve HOOKS themselves are installed above (InstallSetDataHook/InstallAnimDataHook/
    // byteserve::Install), so the detours are already live to catch that first open. Here we only do
    // what must be ready before it: configure the resolver, install the per-project loader gate, and
    // hand byteserve the resolver pointer. The behavior compile sources everything from the loose
    // .hky bundles (no BSA dependency), so the gate is safe to fire this early in load.
    {
        // Schema dir FIRST, before Init(): Init() compiles the served skeletons (CompileSkeletonFull),
        // which is the first consumer of havok::schema::SharedRegistry(). That registry loads at most
        // ONCE and caches the outcome, so if Init() runs before the dir is configured, every schema
        // consumer (skeletons, the schema-native behavior/anim compile) is poisoned with a "no schema
        // directory configured" failure for the rest of the process — a silent fall-through to the
        // typed path plus dead skeleton serves. ApplySchemaCompilerSetting has no dependency on Init
        // (it only reads settings.ini + sets the shared dir), so it must precede it.
        CB::ApplySchemaCompilerSetting();   // configures SharedRegistry's dir — MUST be before any compile
        CB::g_resolver.Init("Data", "Data/community_behaviors/loadorder.txt");
        CB::g_resolver.SetERGateEnabled(CB::ReadERGateEnabled());
        CB::g_resolver.SetAdsfFromFeature(CB::ReadAdsfFromFeature());  // opt-in, before any compile

        // Per-project animdata (opt-in): install its loader gate NOW so the ClipDataCtor hook is live
        // before the clip singleton is built. On install failure, revert the flipped flag (else the
        // engine takes the per-project branch with nothing materialized) and fall back to collated.
        if (perProject && !CB::adserve::InstallPerProjectGate()) {
            CB::adserve::DisablePerProjectAnimData();
            perProject = false;
        }
        CB::g_perProject.store(perProject, std::memory_order_release);  // read by the compile gate

        CB::byteserve::SetResolver(&CB::g_resolver);
    }

    auto* messaging = SKSE::GetMessagingInterface();
    if (!messaging) {
        LOG_ERROR("Community Behaviors: failed to get SKSE messaging interface.");
        return false;
    }
    messaging->RegisterListener(CB::OnMessage);

    LOG_INFO("Community Behaviors loaded.");
    return true;
}
