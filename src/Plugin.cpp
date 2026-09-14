#include "PCH.h"

#include "core/serve/AnimationDataServer.h"
#include "core/serve/AnimationSetDataServer.h"
#include "core/serve/ByteServe.h"
#include "core/bootstrap/CompileGate.h"
#include "core/debug/DebugOverlay.h"
#include "core/debug/RuntimeTrace.h"
#include "core/bootstrap/ProgressHud.h"
#include "core/bootstrap/ProgressOverlay.h"
#include "core/bootstrap/sequencer/CompileSequencer.h"   // seq::ThreadPool — parallel native-anim compile
#include "core/resolve/Resolver.h"

#include "SimpleIni.h"   // [Cache] bForceRegenerate toggle
#include "havok/sct/BehaviorCompiler.h"   // SetSchemaCompiler — data-driven compiler toggle

#include <atomic>
#include <chrono>
#include <cstdlib>       // std::getenv — schema-dir env fallback
#include <filesystem>
#include <mutex>         // EnsureCompiledAndArmed one-shot latch
#include <optional>      // AnimParallel — pool held only when sequencer.enable
#include <process.h>   // _beginthreadex — large-stack warm-up thread

namespace CB {

    // The load-order -> compiled-behavior engine. Lives for the process lifetime;
    // the interceptor holds a pointer to it.
    static Resolver g_resolver;

    // The thread SKSEPluginLoad runs on (the game's main thread). Captured at plugin load so the
    // compile gate can log whether it fires on the main thread — diagnostic for the load-minimize
    // (a heavy compile BLOCKING the main thread stops the window message pump).
    static unsigned long g_mainThreadId = 0;

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

    // adsf-derive toggle (settings.ini, [Compiler] bAdsfDerive). DEFAULT OFF: the derive
    // (animationdata straight off the compiled graph via the first-class adsf-derive compile stage) is a
    // NEW path parallel to the proven collated merge. When on, CompileAll fills the resolver's clip
    // sink and ServeAnimData reports what it produced for comparison — it does not yet drive the emit.
    static bool ReadAdsfDerive()
    {
        CSimpleIniA ini;
        ini.SetUnicode();
        if (ini.LoadFile("Data/SKSE/Plugins/Community Behaviors/settings.ini") >= 0)
            return ini.GetBoolValue("Compiler", "bAdsfDerive", false);
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
    // Multithreaded native-animation compile toggle. DEFAULT ON in every build — the per-unit native-anim
    // compile fans across a worker pool (proven massively faster in-game, and each unit is a pure function
    // of its own def + the immutable served skeleton, so the parallel output is byte-identical to serial).
    // The escape hatch is an opt-OUT FILE marker (Data\community_behaviors\sequencer.disable), not an .ini
    // key — so it can be dropped in to force the old serial path without editing config (and can't be
    // toggled by accident through .ini merges), for a machine that ever shows trouble. (Increment A of the
    // compile sequencer: only the embarrassingly-parallel animation phase is parallelized; behaviors stay
    // serial for now.)
    static bool ParallelAnimCompileEnabled()
    {
        return !std::filesystem::exists("Data/community_behaviors/sequencer.disable");
    }

    // Holds the worker pool ALIVE for the duration of a parallel native-anim compile and hands the Resolver
    // an executor bound to it. Empty (serial) only when the opt-out marker forces it off. Workers get a
    // 64MB reserved stack — the same guard WarmUpThread uses — so a native-anim compile can never overflow a
    // default worker stack. Non-copyable (the optional<ThreadPool> makes it so); use it as a local only.
    struct AnimParallel
    {
        std::optional<seq::ThreadPool> pool;
        Resolver::AnimExecutor         exec;

        AnimParallel()
        {
            if (!ParallelAnimCompileEnabled()) return;
            // 0 => hardware_concurrency; 64MB reserved stack/worker; BELOW_NORMAL so the anim compile
            // never out-competes the game's render/main threads while the window is coming up (minimize).
            pool.emplace(0u, 64u * 1024u * 1024u, THREAD_PRIORITY_BELOW_NORMAL);
            exec = [this](std::vector<std::function<void()>> tasks) { pool->parallel_for(std::move(tasks)); };
            LOG_INFO("Community Behaviors: native animations compile in PARALLEL on {} pool thread(s) "
                     "(drop Data\\community_behaviors\\sequencer.disable to force serial).", pool->size());
        }

        // nullptr when disabled (empty std::function) => Resolver takes the serial path.
        const Resolver::AnimExecutor* ptr() const { return exec ? &exec : nullptr; }
    };

    static unsigned __stdcall WarmUpThread(void* param)
    {
        t_compileGateInProgress = true;   // this worker is immune to a re-entrant compile-gate call
        // Run the whole background compile (graphs on this thread + native animations on the pool)
        // BELOW the game's threads, so it can't starve the render/main threads while the window is
        // coming up — the load-minimize. The compile takes marginally longer; the window stays live.
        SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
        // grandTotal = graphs + native animations (computed by the gate and passed in), so the bar
        // reflects ALL compiled assets, not just graphs. Phase 1 (CompileAll) fills 0..graphCount of
        // grandTotal; phase 2 (native animations) fills graphCount..grandTotal.
        const std::size_t grandTotal = reinterpret_cast<std::size_t>(param);
        const std::size_t graphCount = g_resolver.SourceCount();
        const auto        t0         = std::chrono::steady_clock::now();
        g_resolver.CompileAll([grandTotal](std::size_t done, std::size_t /*graphTotal*/) {
            ProgressOverlay::SetProgress(done, grandTotal, true);   // denominator = grand total
        });
        // (The character animationNames roster is built offline now — the converter scans clip
        // generators into each unit's data/animations.yaml and LoadMerged unions every layer at
        // compile time — so there is no runtime roster-completion pass between compile and disk write.)
        // Write every compiled graph to the consolidated cache (<Data>\Meshes\community_behaviors_cache\...)
        // and publish the redirect map; the synthesized project per character is written on demand by
        // ProjectRedirect into the same cache (vanilla path/identity, no ".br"). The project serve hook
        // (fragile: fires on the actor-load path) then only reads that map + byte-swaps the open — never
        // compiles. This 64MB-stack thread is the ONLY place a compile is allowed. Clears stale
        // cross-session BR output first.
        // Phase 2 — native animations. Each finished unit advances the bar from graphCount toward
        // grandTotal. Shared atomic (the compile may run parallel across the pool), stored into the
        // atomic ProgressOverlay; the tick runs on worker threads, hence the atomic. See the
        // synchronicity note on WriteNativeAnimations — the bar is a passive reader, never a gate.
        std::atomic<std::size_t> animDone{ 0 };
        const std::function<void()> onAnimUnit = [grandTotal, graphCount, &animDone] {
            ProgressOverlay::SetProgress(graphCount + animDone.fetch_add(1, std::memory_order_relaxed) + 1,
                                         grandTotal, true);
        };
        AnimParallel animPar;   // parallel native-anim compile when sequencer.enable; serial otherwise
        const std::size_t wrote = g_resolver.MaterializeCacheToDisk(
            std::filesystem::current_path() / "Data", nullptr, animPar.ptr(), &onAnimUnit);
        const double secs = std::chrono::duration<double>(
                                std::chrono::steady_clock::now() - t0).count();
        ProgressOverlay::SetProgress(grandTotal, grandTotal, false);
        LOG_INFO("Community Behaviors: precompiled {} graph(s) + {} animation(s), materialized {} to disk "
                 "in {:.1f}s (runtime loads served from disk cache).",
                 graphCount, g_resolver.NativeAnimCount(), wrote, secs);
        { std::size_t s = 0, t = 0; havok::sct::SchemaCompilerStats(s, t);
          if (s || t) LOG_INFO("Community Behaviors: compiler paths this warm-up — schema-driven {}, typed {}.", s, t); }
        return 0;
    }

    // Background compile thread handle (split path only). Set once under the gate mutex; WaitForCompile
    // joins it. Process-lifetime, one compile — the handle is intentionally never closed.
    static HANDLE g_compileThread = nullptr;

    // adsf + setdata serve/arm. Independent of the graph compile when the adsf-derive stage is OFF
    // (the default): the merge reads the vanilla base + bundle deltas, no clip sink.
    static void ArmAdsfSetDataServe()
    {
        const std::filesystem::path dataAbs = std::filesystem::current_path() / "Data";

        const auto sd = asdserve::ServeSetData("Data", "Data/community_behaviors/loadorder.txt");
        if (sd.attempted && !sd.ok)
            LOG_WARN("Community Behaviors: animationsetdata merge did not complete: {}", sd.error);
        asdserve::ArmSetDataRedirect(sd);

        const GraphClipSink* clipSink =
            g_resolver.AdsfDerive() ? &g_resolver.ClipSink() : nullptr;
        const auto ad = adserve::ServeAnimData("Data", "Data/community_behaviors/loadorder.txt",
                                               clipSink, ReadAdsfRosterFromScan());
        if (ad.attempted && !ad.ok)
            LOG_WARN("Community Behaviors: animationdata merge did not complete: {}", ad.error);
        adserve::ArmAnimDataRedirect(ad);

        if (g_resolver.AdsfDerive() && g_resolver.ClipSink().ClipCount() > 0)
            g_resolver.DeriveAnimData(dataAbs);
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
    static bool ArmAdsfSetDataFromCache()
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

        const fs::path adsf = cacheDir / "animationdatasinglefile.txt";
        if (!fs::exists(adsf, ec)) {
            LOG_WARN("Community Behaviors: warm cache missing '{}' — re-deriving adsf/set-data this launch.",
                     adsf.string());
            return false;
        }
        adserve::ServeResult ad; ad.attempted = true; ad.ok = true; ad.cachePath = adsf.string();
        adserve::ArmAnimDataRedirect(ad);
        LOG_INFO("Community Behaviors: warm reuse — serving cached animationdata (no re-derive).");
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

        LOG_INFO("Community Behaviors: compile gate fired on thread {} (main thread = {}){}.",
                 ::GetCurrentThreadId(), g_mainThreadId,
                 (::GetCurrentThreadId() == g_mainThreadId) ? " — ON MAIN THREAD" : "");

        const std::filesystem::path dataAbs    = std::filesystem::current_path() / "Data";
        const bool                  warm       = !ReadForceRegenerate() && g_resolver.CachePresent(dataAbs);

        // Progress bar is DEFAULT on EVERY launch — armed NOW, before the WaitReady/work blocks below, so
        // it's visible for the WHOLE wait (the ~50s Init unpack+scan happens every launch; warm then
        // recompiles native anims, cold compiles everything). The render thread keeps presenting the
        // loading screen, so the bar draws over it the whole time. Indeterminate until a path sets the
        // real total (see DrawBar's total==0 path). NOT gated on warm/cold — only an explicit opt-out
        // marker suppresses it. Armed before WaitReady is safe (it touches only the atomic overlay).
        const bool barEnabled = !std::filesystem::exists("Data/community_behaviors/progressbar.disable");
        if (barEnabled) {
            ProgressOverlay::SetProgress(0, 0, true);   // 0 total => indeterminate "compiling…" bar
            ProgressHud::Install();                      // installs the present hook + imgui now (idempotent)
        }

        // Init now runs on a background thread; never compile against a half-built resolver. The bar above
        // is already up during this wait (Init is the bulk of it on both warm and cold).
        g_resolver.WaitReady();

        // Split path (progress bar) is the DEFAULT for a cold compile: the bar rides the game's own
        // present via ProgressHud, the same coexisting pattern CS/OAR use, so it's safe to ship on.
        // Two guards remain: the adsf-derive stage (adsf then needs the compile's clip sink, so the
        // merge can't run ahead of the compile — must stay synchronous), and an explicit opt-out marker
        // (Data\community_behaviors\progressbar.disable) that forces the old fully-synchronous compile.
        const bool split = !warm &&
                            !g_resolver.AdsfDerive() &&
                            !std::filesystem::exists("Data/community_behaviors/progressbar.disable");

        if (warm) {
            // The warm path still RECOMPILES native animations (ArmCacheFromDisk -> WriteNativeAnimations):
            // they're loose, not in the reused graph cache. That serial recompile is the warm-launch
            // "Continue" stall — so give it the same optional parallel executor.
            AnimParallel animPar;
            g_resolver.ArmCacheFromDisk(dataAbs, animPar.ptr());   // instant graphs; anims (re)compiled here
            // Serve the cached adsf/asdsf verbatim (coherent with the reused graphs); only re-derive if
            // the collated cache is partial. See ArmAdsfSetDataFromCache.
            if (!ArmAdsfSetDataFromCache())
                ArmAdsfSetDataServe();
            if (barEnabled) ProgressOverlay::SetProgress(0, 0, false);   // work done — retire the bar
        } else if (split) {
            // Arm adsf/setdata NOW (fast, independent of the graph compile) so the engine's early reads
            // are served, then run the heavy compile in the BACKGROUND and RETURN. The game reaches its
            // menu and presents while the compile runs; ProgressHud's passive present hook draws the bar.
            // byteserve calls WaitForCompile() before serving an owned graph, so nothing is served
            // half-compiled — correctness holds regardless of the bar.
            ArmAdsfSetDataServe();
            // Grand total = graphs + native animations, so the bar counts ALL compiled assets. Passed
            // to WarmUpThread as the denominator for both phases.
            const std::size_t graphCount = g_resolver.SourceCount();
            const std::size_t animCount  = g_resolver.NativeAnimCount();
            const std::size_t total      = graphCount + animCount;
            ProgressOverlay::SetProgress(0, total, true);
            ProgressHud::Install();   // FALLBACK: normally already installed early at kDataLoaded (idempotent);
                                      // installs here only if the swapchain wasn't ready then.
            LOG_INFO("Community Behaviors: SPLIT compile — arming adsf/setdata now, compiling {} graph(s) "
                     "+ {} animation(s) in the background (progress bar enabled).", graphCount, animCount);
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
            // the detour, arm ordered ahead of graph load), THEN arm adsf/setdata. No progress bar, but
            // pass the same grand total (graphs + animations) so WarmUpThread's phase math + log agree.
            const std::size_t total = g_resolver.SourceCount() + g_resolver.NativeAnimCount();
            LOG_INFO("Community Behaviors: precompiling {} graph(s) + {} animation(s) at first engine open "
                     "on thread {} (loader thread parked in our hook — arm ordered ahead of graph load).",
                     g_resolver.SourceCount(), g_resolver.NativeAnimCount(), ::GetCurrentThreadId());
            if (const std::uintptr_t h = _beginthreadex(nullptr, 64u * 1024u * 1024u,
                        &WarmUpThread, reinterpret_cast<void*>(total),
                        STACK_SIZE_PARAM_IS_A_RESERVATION, nullptr)) {
                WaitForSingleObject(reinterpret_cast<HANDLE>(h), INFINITE);
                CloseHandle(reinterpret_cast<HANDLE>(h));
            } else {
                LOG_ERROR("Community Behaviors: failed to launch precompile thread — behavior serve NOT armed.");
            }
            ArmAdsfSetDataServe();
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

    // Resolver bring-up, run OFF the main thread. Init fully UNPACKS the .hky bundles (decompresses each
    // whole archive into memory), scans the load order, reads every actor skeleton, and compiles the
    // served skeletons — ~50s of work that used to run synchronously in SKSEPluginLoad ON THE MAIN
    // THREAD, blocking the window's message pump during bring-up (the load-minimize + trapped cursor).
    // All resolver config lives here so it's published together via Init's ready-store (Resolver::Ready);
    // the serve hooks pass through to vanilla until then, and the compile gate blocks on WaitReady().
    // 64MB stack — the skeleton compile. Finishes during the intro, before the first owned open.
    static unsigned __stdcall InitThread(void*)
    {
        ApplySchemaCompilerSetting();   // configures SharedRegistry's dir — MUST precede Init's compile
        const bool warmReuse = !ReadForceRegenerate() &&
                               g_resolver.CachePresent(std::filesystem::current_path() / "Data");
        g_resolver.SetERGateEnabled(ReadERGateEnabled());
        g_resolver.SetAdsfDerive(ReadAdsfDerive());   // opt-in, before any compile

        // Parallel skeleton compile (stage 2) — same opt-out marker as the anim compile. A pool of
        // 64MB-reserved-stack workers (skeleton compile recurses through Havok graph assembly, like the
        // anim path) at BELOW_NORMAL so it never starves the window's render/main threads during bring-up.
        // Fans the per-actor skeleton SERVE compile + bone-name table build inside Init; nullptr => serial.
        std::optional<seq::ThreadPool> skelPool;
        Resolver::AnimExecutor         skelExec;
        if (ParallelAnimCompileEnabled()) {
            skelPool.emplace(0u, 64u * 1024u * 1024u, THREAD_PRIORITY_BELOW_NORMAL);
            skelExec = [&skelPool](std::vector<std::function<void()>> tasks) { skelPool->parallel_for(std::move(tasks)); };
        }
        g_resolver.Init("Data", "Data/community_behaviors/loadorder.txt", warmReuse,
                        skelExec ? &skelExec : nullptr);   // ready-store at end
        return 0;
    }

    static void OnMessage(SKSE::MessagingInterface::Message* a_msg)
    {
        if (!a_msg) return;

        if (a_msg->type == SKSE::MessagingInterface::kDataLoaded) {
            // Pre-install the compile progress bar EARLY here (before the menu), NOT at the later compile
            // gate: ProgressHud::Install now eagerly builds imgui + its font/device objects, so doing it at
            // kDataLoaded keeps that heavy work off the live-present path (the Community Shaders pattern).
            // The old lazy build inside the first hooked present hitched the render thread mid-menu and
            // ghosted/minimized the window. Only install when a cold split-path compile will actually run
            // (a bar to show) — same predicate the gate uses; the gate keeps a fallback Install() for the
            // rare case the swapchain isn't ready yet at kDataLoaded.
            {
                const std::filesystem::path dataAbs = std::filesystem::current_path() / "Data";
                const bool warm        = !ReadForceRegenerate() && g_resolver.CachePresent(dataAbs);
                const bool willShowBar = !warm && !g_resolver.AdsfDerive() &&
                                         !std::filesystem::exists("Data/community_behaviors/progressbar.disable");
                if (willShowBar && !ProgressHud::Install())
                    LOG_INFO("Community Behaviors: progress bar early-install deferred (swapchain not ready "
                             "at kDataLoaded) — will retry at the compile gate.");
            }
            // DebugOverlay needs kDataLoaded timing (SMF isn't up at plugin load).
            CB::RuntimeTrace::Install();   // arm [Debug] bRuntimeTrace before the present hook is live
            CB::DebugOverlay::Install();
        }
    }

}  // namespace CB

SKSEPluginLoad(const SKSE::LoadInterface* a_skse)
{
    SKSE::Init(a_skse);
    PluginLogger::Init("CommunityBehaviors", "Community Behaviors");
    CB::g_mainThreadId = ::GetCurrentThreadId();   // main thread — for the compile-gate thread diagnostic
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

    // Animdata cache form: COLLATED is the only supported path — it serves
    // community_behaviors_cache/animationdatasinglefile.txt and the engine reads clips AND motion from
    // it. (The old per-project loader form was a dead workaround that broke root motion + paired anims
    // in a real load order; retired.)

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
    // what must be ready before it: configure the resolver and hand byteserve the resolver pointer.
    // The behavior compile sources everything from the loose
    // .hky bundles (no BSA dependency), so the gate is safe to fire this early in load.
    {
        // Hand byteserve the resolver pointer NOW (it gates every use on g_resolver.Ready(), so a hook
        // that fires before Init finishes passes through to vanilla instead of reading half-built state).
        CB::byteserve::SetResolver(&CB::g_resolver);

        // Run the whole resolver bring-up (unpack + scan + skeleton compile — ~50s) on a BACKGROUND
        // thread, so SKSEPluginLoad returns immediately and the main thread is free to bring the window
        // up. It used to run synchronously here on the main thread, blocking the message pump the entire
        // time → the game launched minimized with the cursor trapped. Init finishes during the intro,
        // before the first owned asset opens; the compile gate blocks on WaitReady() if it somehow fires
        // first. 64MB stack for the skeleton compile; handle closed immediately (fire-and-forget — the
        // ready-store is the sync point, not a join).
        if (const std::uintptr_t h = _beginthreadex(nullptr, 64u * 1024u * 1024u,
                    &CB::InitThread, nullptr, STACK_SIZE_PARAM_IS_A_RESERVATION, nullptr)) {
            CloseHandle(reinterpret_cast<HANDLE>(h));
        } else {
            LOG_ERROR("Community Behaviors: failed to spawn Init thread — running Init synchronously (load may stall).");
            CB::InitThread(nullptr);   // fallback: correctness over responsiveness
        }
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
