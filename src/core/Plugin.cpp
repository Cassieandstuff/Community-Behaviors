#include "PCH.h"

#include "core/serve/AnimationDataServer.h"
#include "core/serve/AnimationSetDataServer.h"
#include "core/serve/ByteServe.h"
#include "core/serve/conditions/Conditions.h"
#include "core/bootstrap/CompileGate.h"
#include "core/debug/DebugOverlay.h"
#include "core/debug/RuntimeTrace.h"
#include "core/debug/Settings.h"
#include "core/bootstrap/ProgressHud.h"
#include "core/bootstrap/ProgressOverlay.h"
#include "core/bootstrap/sequencer/CompileSequencer.h"   // CB::core::bootstrap::ThreadPool — parallel native-anim compile
#include "core/resolve/LoadOrderReader.h"
#include "core/resolve/Resolver.h"

#include <platform/classes/ClassRegistry.h>
#include <platform/hooks/Chain.h>
#include <platform/hooks/registry/Registry.h>
#include <platform/hooks/takeover/SchemaShim.h>
#include <platform/hooks/takeover/Takeover.h>
#include <platform/profile/Profile.h>

#include <interface/reflection/HavokSchema.h>             // schema::SetSharedSchemaDir / SharedRegistry / SharedRegistryError

#include <atomic>
#include <chrono>
#include <condition_variable>   // g_armCv — adsf-armed phase signal
#include <cstdlib>       // std::getenv — schema-dir env fallback
#include <filesystem>
#include <mutex>         // EnsureCompiledAndArmed one-shot latch
#include <optional>      // AnimParallel — pool held unless the sequencer.disable marker is present
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

    // Schema registry dir (settings.ini, [Compiler] sSchemaDir). The class schema ships with CB
    // (Data/community_behaviors/schema, staged by CMake); sSchemaDir overrides, then env
    // SCT_HAVOK_SCHEMA_DIR, then that shipped default.
    /// @note A schema that fails to LOAD (missing/foreign/version-mismatched tree) leaves the registry null:
    ///       every compile then fails and every graph serves VANILLA, loudly — never a half-armed compiler.
    static void ApplySchemaCompilerSetting()
    {
        std::string dir = CB::core::debug::Value("Compiler", "sSchemaDir");
        if (dir.empty() && std::getenv("SCT_HAVOK_SCHEMA_DIR") == nullptr)
            dir = "Data/community_behaviors/schema";   // CB-shipped schema tree (MO2 VFS-merged path)

        CB::engine::schema::SetSharedSchemaDir(dir);
        const bool ready = CB::engine::schema::SharedRegistry() != nullptr;
        LOG_INFO("Community Behaviors: data-driven compiler ENABLED (schema registry {}).",
                 ready ? "loaded" : "FAILED TO LOAD → all graphs serve VANILLA");
        if (!ready) {
            const std::string& why = CB::engine::schema::SharedRegistryError();
            if (!why.empty()) LOG_ERROR("Community Behaviors: schema compiler off, ALL GRAPHS SERVE VANILLA — {}.", why);
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
    // per-mod deltas), which fed the compile a corrupt merged 0_master. That is a
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
        return !CB::core::debug::FlagOn("sequencer_disable");
    }

    // Holds the worker pool ALIVE for the duration of a parallel phase (the native-anim compile, the adsf
    // projection's animation parse) and hands out an executor bound to it. Empty (serial) only when the opt-out marker forces it off. Workers get a
    // 64MB reserved stack — the same guard WarmUpThread uses — so a native-anim compile can never overflow a
    // default worker stack. Non-copyable (the optional<ThreadPool> makes it so); use it as a local only.
    struct AnimParallel
    {
        std::optional<CB::core::bootstrap::ThreadPool> pool;
        Resolver::AnimExecutor         exec;

        /// @param what The work the pool runs, for the log line.
        explicit AnimParallel(const char* what = "native animations compile")
        {
            if (!ParallelAnimCompileEnabled()) return;
            // 0 => hardware_concurrency; 64MB reserved stack/worker; BELOW_NORMAL so the anim compile
            // never out-competes the game's render/main threads while the window is coming up (minimize).
            pool.emplace(0u, 64u * 1024u * 1024u, THREAD_PRIORITY_BELOW_NORMAL);
            exec = [this](std::vector<std::function<void()>> tasks) { pool->parallel_for(std::move(tasks)); };
            LOG_INFO("Community Behaviors: {} in PARALLEL on {} pool thread(s) "
                     "(drop Data\\community_behaviors\\sequencer.disable to force serial).", what, pool->size());
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
        /// @note The graphs PlanIncremental kept (ArmThread, before the adsf) are skipped here and in the materialize.
        {
            CB_ZONE(zone, "warm-up: compile graphs");
            g_resolver.CompileAll([grandTotal](std::size_t done, std::size_t /*graphTotal*/) {
                CB::core::bootstrap::SetProgress(done, grandTotal, true);   // denominator = grand total
            });
        }
        // (The character animationNames roster is a union roster: the merge unions every layer's names at
        // compile time, so there is no runtime roster-completion pass between compile and disk write.)
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
            CB::core::bootstrap::SetProgress(graphCount + animDone.fetch_add(1, std::memory_order_relaxed) + 1,
                                         grandTotal, true);
        };
        AnimParallel animPar;   // parallel native-anim compile unless sequencer.disable; serial otherwise
        std::size_t  wrote = 0;
        {
            CB_ZONE(zone, "warm-up: materialize + animations");
            wrote = g_resolver.MaterializeCacheToDisk(std::filesystem::current_path() / "Data", nullptr, animPar.ptr(), &onAnimUnit);
        }
        const double secs = std::chrono::duration<double>(
                                std::chrono::steady_clock::now() - t0).count();
        CB::core::bootstrap::SetProgress(grandTotal, grandTotal, false);
        LOG_INFO("Community Behaviors: precompiled {} graph(s) + {} animation(s), materialized {} to disk "
                 "in {:.1f}s (runtime loads served from disk cache).",
                 graphCount, g_resolver.NativeAnimCount(), wrote, secs);
        return 0;
    }

    // Background arm/compile thread handle. Set once under the gate mutex when the gate kicks the
    // ArmThread; WaitForCompile joins it (so byteserve's owned-graph opens block until materialize).
    // Process-lifetime, one arm — the handle is intentionally never closed.
    static HANDLE g_compileThread = nullptr;

    // adsf-armed phase signal (gate-level, not a Resolver concept — arming lives here). The gate no
    // longer blocks the MAIN thread on WaitReady(): the whole arm runs on the ArmThread, which arms the
    // adsf/setdata redirects FIRST (instant + Init-independent on a warm cache) and signals this. The
    // engine's animationdata / animationsetdata open then waits ONLY on this — milliseconds on warm —
    // instead of parking the main thread ~50s on the full Init (the freeze + trapped cursor + no bar).
    // Mirrors the m_redirectReady/m_readyMutex/m_readyCv trio in the Resolver.
    static std::atomic<bool>       g_adsfArmed{ false };
    static std::mutex              g_armMutex;
    static std::condition_variable g_armCv;

    static void SignalAdsfArmed()
    {
        { std::lock_guard<std::mutex> lk(g_armMutex); g_adsfArmed.store(true, std::memory_order_release); }
        g_armCv.notify_all();
    }

    // Block until the adsf/setdata redirects are armed (or a generous timeout — never hang the engine's
    // load forever if arming somehow fails; on timeout the open falls through to vanilla, same as a
    // pre-Ready passthrough). Called by the animationdata/animationsetdata detours after they kick the
    // gate. Fast-path acquire, then a bounded wait, same shape as ProjectRedirect's redirect wait.
    void WaitAdsfArmed()
    {
        if (g_adsfArmed.load(std::memory_order_acquire)) return;
        std::unique_lock<std::mutex> lk(g_armMutex);
        if (!g_armCv.wait_for(lk, std::chrono::seconds(120),
                              [] { return g_adsfArmed.load(std::memory_order_acquire); }))
            LOG_WARN("Community Behaviors: adsf/setdata arm timed out (120s) — serving vanilla for this open.");
    }

    // adsf + setdata serve/arm.
    /// @note Independent of the graph compile: the adsf projection re-merges each unit's rosters / clip inputs
    ///       itself (Resolver::ServedRoster / MergedClipInputs), so the cold path can arm it BEFORE compiling.
    /// @param exec The BELOW_NORMAL pool the adsf projection parses animations on; nullptr = serial.
    static void ArmAdsfSetDataServe(const Resolver::AnimExecutor* exec)
    {
        /// @note A bundle that can't be read back intact throws (HkyArchive::file); that file then stays vanilla.
        CB::core::serve::SetDataServeResult sd;
        try {
            sd = CB::core::serve::ServeSetData("Data", "Data/community_behaviors/loadorder.txt");
        } catch (const std::exception& e) {
            sd.attempted = true; sd.error = e.what();
        }
        if (sd.attempted && !sd.ok)
            LOG_WARN("Community Behaviors: animationsetdata merge did not complete: {}", sd.error);
        CB::core::serve::ArmSetDataRedirect(sd);

        CB::core::serve::AdsfSource adsf;
        adsf.roster        = [](const std::string& key) { return g_resolver.ServedRoster(key); };
        /// @note Every clip read is traced: the next recompile keeps the adsf while these clips are unchanged.
        adsf.clips         = [](const std::string& key) {
            auto clips = g_resolver.MergedClipInputs(key);
            g_resolver.TraceClips(key, clips);
            return clips;
        };
        adsf.behaviorUnits = [] { return g_resolver.BehaviorUnitKeys(); };
        CB::core::serve::ServeResult ad;
        try {
            ad = CB::core::serve::ServeAnimData("Data", "Data/community_behaviors/loadorder.txt", adsf, exec);
        } catch (const std::exception& e) {
            ad.attempted = true; ad.error = e.what();
        }
        if (!ad.ok)
            LOG_WARN("Community Behaviors: animationdata projection did not complete: {}", ad.error);
        CB::core::serve::ArmAnimDataRedirect(ad);

        /// @note A file this pass did not produce must not survive in the cache: a later launch arms whatever it finds
        ///       there (ArmAdsfSetDataFromCache), so a stale one would be served against graphs it doesn't match.
        const std::filesystem::path cacheDir = std::filesystem::current_path() / "Data" / "community_behaviors_cache";
        std::error_code             ec;
        if (!sd.ok) std::filesystem::remove(cacheDir / "animationsetdatasinglefile.txt", ec);
        if (!ad.ok) std::filesystem::remove(cacheDir / "animationdatasinglefile.txt", ec);
        g_resolver.SetAnimDataComplete(ad.ok && (sd.ok || !sd.attempted));
    }

    // REUSE (warm start, or a recompile whose adsf/asdsf inputs are unchanged — Resolver::AnimDataReusable):
    // serve the CACHED adsf/asdsf verbatim instead of re-deriving them.
    //
    // The adsf/asdsf are COMPILE OUTPUTS, not independent merges: their clip -> animIndex assignment
    // (and the roster-dependent set-data CRC guard) is coherent ONLY with the character/behavior cache
    // produced in the SAME pass — char-setup binds animations through that index space. Re-running
    // ServeAnimData/ServeSetData on a warm start re-derives that mapping INDEPENDENTLY of the reused
    // graphs, which can desync it from the cached characters — an attack clip then binds a foreign
    // animation (an idle, even a furniture clip). Both
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
            CB::core::serve::SetDataServeResult sd; sd.attempted = true; sd.ok = true; sd.cachePath = asdsf.string();
            CB::core::serve::ArmSetDataRedirect(sd);
            LOG_INFO("Community Behaviors: warm reuse — serving cached set-data (no re-derive).");
        }

        const fs::path adsf = cacheDir / "animationdatasinglefile.txt";
        if (!fs::exists(adsf, ec)) {
            LOG_WARN("Community Behaviors: warm cache missing '{}' — re-deriving adsf/set-data this launch.",
                     adsf.string());
            return false;
        }
        CB::core::serve::ServeResult ad; ad.attempted = true; ad.ok = true; ad.cachePath = adsf.string();
        CB::core::serve::ArmAnimDataRedirect(ad);
        LOG_INFO("Community Behaviors: warm reuse — serving cached animationdata (no re-derive).");
        return true;
    }

    // The background arm/compile worker — ALL the heavy gate work runs here, off the main thread, so the
    // gate itself never stalls the window. On a WARM cache it arms the adsf/setdata redirects FIRST (an
    // instant, Init-independent file-existence check → redirect), signals adsf-armed so the early
    // animationdata open unblocks in milliseconds, THEN waits Init and arms the graphs from cache. On a
    // COLD cache adsf must be derived from the scan (needs Init), so it waits Init, arms adsf, signals,
    // then compiles the graphs in the background with the bar riding the live menu.
    /// @brief The arm's exit after a failed Init: release the adsf/setdata wait (unarmed → vanilla), retire the bar.
    /// @note  Nothing is compiled or written, so the on-disk cache a transient failure met is left as it was.
    static unsigned StandDown()
    {
        SignalAdsfArmed();
        CB::core::bootstrap::SetProgress(0, 0, false);
        t_compileGateInProgress = false;
        return 0;
    }

    /// @brief Hand runtime features the resolved load order (core::resolve::LoadOrderReader), once Init is ready.
    static void ArmLoadOrder()
    {
        CB::core::resolve::ArmLoadOrderReader({
            [](const std::string& key, CB::engine::decompile::ParsedHky& out) { return g_resolver.MergedGraph(key, out); },
            [](std::string_view path) { return g_resolver.NativeAnimationText(path); } });
    }

    static unsigned __stdcall ArmThread(void*)
    {
        t_compileGateInProgress = true;   // this worker is immune to a re-entrant compile-gate call
        SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);   // never starve the game's threads

        const std::filesystem::path dataAbs = std::filesystem::current_path() / "Data";
        const bool                  warm    = !CB::core::debug::FlagOn("force_regenerate") && g_resolver.CachePresent(dataAbs);

        if (warm) {
            // adsf/setdata FIRST: serve the cached adsf/asdsf verbatim (coherent with the reused graphs).
            // This only touches on-disk files + the redirect atomics — no resolver state — so it runs
            // BEFORE WaitReady() and lets the adsf open through in ms. Only a partial cache falls back to
            // the derive path (which needs the scan → after WaitReady, below).
            bool adsfFromCache = false;
            {
                CB_ZONE(zone, "warm-up: adsf/setdata from cache");
                adsfFromCache = ArmAdsfSetDataFromCache();
            }
            if (adsfFromCache) SignalAdsfArmed();

            {
                CB_ZONE(zone, "warm-up: wait for init");
                g_resolver.WaitReady();   // graphs need Init; the bar is already up over the live menu
            }
            if (g_resolver.Failed()) return StandDown();
            ArmLoadOrder();

            // Native anims are loose (not in the reused graph cache) so ArmCacheFromDisk recompiles them;
            // give it the same optional parallel executor. Sets m_redirectReady when done (materialize).
            AnimParallel animPar;
            {
                CB_ZONE(zone, "warm-up: arm cache from disk");
                g_resolver.ArmCacheFromDisk(dataAbs, animPar.ptr());
            }
            if (!adsfFromCache) {
                CB_ZONE(zone, "warm-up: adsf/setdata projection");
                ArmAdsfSetDataServe(animPar.ptr());
                SignalAdsfArmed();
            }
            CB::core::bootstrap::SetProgress(0, 0, false);   // work done — retire the bar
        } else {
            {
                CB_ZONE(zone, "warm-up: wait for init");
                g_resolver.WaitReady();   // cold: adsf derive + graph compile both need the scan
            }
            if (g_resolver.Failed()) return StandDown();
            ArmLoadOrder();

            const std::size_t graphCount = g_resolver.SourceCount();
            const std::size_t animCount  = g_resolver.NativeAnimCount();
            const std::size_t total      = graphCount + animCount;
            CB::core::bootstrap::SetProgress(0, total, true); CB::core::bootstrap::Install();

            /// @note Keep every graph whose own inputs are unchanged since the last complete cache, and the cached
            ///       adsf/asdsf when none of THEIR inputs changed; a forced regen keeps nothing.
            const bool keep = !CB::core::debug::FlagOn("force_regenerate");
            if (keep) {
                CB_ZONE(zone, "warm-up: plan incremental");
                g_resolver.PlanIncremental(dataAbs);
            }

            // adsf is independent of the graph compile → arm + signal NOW so the adsf open unblocks, then
            // compile the graphs in the background (bar rides the live menu).
            if (keep && g_resolver.AnimDataReusable(dataAbs) && ArmAdsfSetDataFromCache()) {
                g_resolver.SetAnimDataComplete(true);
            } else {
                CB_ZONE(zone, "warm-up: adsf/setdata projection");
                AnimParallel adsfPar("the adsf projection parses animations");
                ArmAdsfSetDataServe(adsfPar.ptr());
            }
            SignalAdsfArmed();
            LOG_INFO("Community Behaviors: COLD compile — adsf/setdata armed, compiling {} graph(s) + "
                     "{} animation(s) in the background (bar riding the compile).", graphCount, animCount);
            WarmUpThread(reinterpret_cast<void*>(total));   // compile + materialize (sets m_redirectReady)
        }

        t_compileGateInProgress = false;
        return 0;
    }

    // The compile gate — see CompileGate.h. One-shot, thread-safe, NON-blocking kick.
    void EnsureCompiledAndArmed()
    {
        static std::atomic<bool> s_kicked{ false };
        static std::mutex        s_mtx;

        if (s_kicked.load(std::memory_order_acquire)) return;
        if (t_compileGateInProgress) return;   // re-entrant call on the doing (Arm) thread — work underway
        std::lock_guard<std::mutex> lk(s_mtx);
        if (s_kicked.load(std::memory_order_relaxed)) return;

        LOG_INFO("Community Behaviors: compile gate fired on thread {} (main thread = {}){} — kicking "
                 "background arm.", ::GetCurrentThreadId(), g_mainThreadId,
                 (::GetCurrentThreadId() == g_mainThreadId) ? " — ON MAIN THREAD" : "");

        // Progress bar is DEFAULT — armed NOW, before the ArmThread does any work, so it's visible for the
        // whole arm. Indeterminate until a path sets the real total (DrawBar's total==0 path). No marker
        // gate; it only draws while a compile is running.
        CB::core::bootstrap::SetProgress(0, 0, true);   // 0 total => indeterminate "compiling…" bar
        CB::core::bootstrap::Install();                      // installs the present hook + imgui now (idempotent)

        // Kick the background arm and RETURN — the main thread stays free to pump the window + present, so
        // the bar paints and the cursor isn't trapped. Per-open waits (WaitAdsfArmed / WaitForCompile)
        // provide correctness. On spawn failure, run the arm inline (correctness over responsiveness).
        if (const std::uintptr_t h = _beginthreadex(nullptr, 64u * 1024u * 1024u,
                    &ArmThread, nullptr, STACK_SIZE_PARAM_IS_A_RESERVATION, nullptr)) {
            g_compileThread = reinterpret_cast<HANDLE>(h);   // byteserve joins via WaitForCompile()
        } else {
            LOG_ERROR("Community Behaviors: failed to launch background arm thread — arming inline (load may stall).");
            ArmThread(nullptr);
        }

        s_kicked.store(true, std::memory_order_release);
    }

    void WaitForCompile()
    {
        // Join the background arm/compile thread if one is running. No-op if it never spawned
        // (g_compileThread stays null — inline-fallback arm) or once it has finished. The thread's own
        // release (materialize + RedirectReady) is visible after the wait returns. Multiple threads may
        // wait concurrently. Self-guard: never join our OWN handle — if the ArmThread itself ever reaches
        // this (an owned open during its compile), that would deadlock; it just proceeds (RedirectReady
        // will be set by the time it needs the file).
        HANDLE h = g_compileThread;
        if (h && ::GetThreadId(h) != ::GetCurrentThreadId()) WaitForSingleObject(h, INFINITE);
    }

    // Resolver bring-up, run OFF the main thread. Init opens the .hky bundles, scans the load order, reads
    // every actor skeleton, and compiles the served skeletons — work that used to run synchronously in
    // SKSEPluginLoad ON THE MAIN THREAD, blocking the window's message pump during bring-up (the load-minimize + trapped cursor).
    // All resolver config lives here so it's published together via Init's ready-store (Resolver::Ready);
    // the serve hooks pass through to vanilla until then, and the compile gate blocks on WaitReady().
    // 64MB stack — the skeleton compile. Finishes during the intro, before the first owned open.
    static unsigned __stdcall InitThread(void*)
    {
        CB_ZONE(zone, "warm-up: init (bundles, scan, skeletons)");
        ApplySchemaCompilerSetting();   // configures SharedRegistry's dir — MUST precede Init's compile
        /// @note CachePresent runs even when a regen is forced: its first call fixes this launch's cache key (the inputs
        ///       as found now, before anything compiles).
        const bool cachePresent = g_resolver.CachePresent(std::filesystem::current_path() / "Data");
        const bool warmReuse    = !CB::core::debug::FlagOn("force_regenerate") && cachePresent;
        g_resolver.SetERGateEnabled(CB::core::debug::FlagOn("er_gate"));

        // Parallel skeleton compile (stage 2) — same opt-out marker as the anim compile. A pool of
        // 64MB-reserved-stack workers (skeleton compile recurses through Havok graph assembly, like the
        // anim path) at BELOW_NORMAL so it never starves the window's render/main threads during bring-up.
        // Fans the per-actor skeleton SERVE compile + bone-name table build inside Init; nullptr => serial.
        std::optional<CB::core::bootstrap::ThreadPool> skelPool;
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
            // gate: CB::core::bootstrap::Install eagerly builds imgui + its font/device objects, so doing it at
            // kDataLoaded keeps that heavy work off the live-present path (the Community Shaders pattern).
            // The bar is DEFAULT behavior — no marker gate. It only ever draws while a compile is running
            // (DrawBar's ReadProgress check), so installing it always is harmless; the gate keeps a
            // fallback Install() for the rare case the swapchain isn't ready yet at kDataLoaded.
            if (!CB::core::bootstrap::Install())
                LOG_INFO("Community Behaviors: progress bar early-install deferred (swapchain not ready "
                         "at kDataLoaded) — will retry at the compile gate.");
            // DebugOverlay needs kDataLoaded timing (SMF isn't up at plugin load).
            CB::core::debug::InstallTrace();   // arm [Debug] bRuntimeTrace before the present hook is live
            CB::core::debug::Install();         // DebugOverlay SMF HUD element + menu toggle
            CB::platform::hooks::RecheckTakeover(); // every plugin has hooked by now: hand back what they patched
            /// @note CB's own Havok classes, before any behavior loads (the first actor graph loads after the menu).
            if (const auto* schema = CB::engine::schema::SharedRegistry())
                CB::platform::classes::RegisterAll(*schema);
            else
                LOG_ERROR("classes: no schema registry ({}) - CB's own classes are not registered",
                          CB::engine::schema::SharedRegistryError());
        }
    }

}  // namespace CB

SKSEPluginLoad(const SKSE::LoadInterface* a_skse)
{
    SKSE::Init(a_skse);
    PluginLogger::Init("CommunityBehaviors", "Community Behaviors");
    CB::g_mainThreadId = ::GetCurrentThreadId();   // main thread — for the compile-gate thread diagnostic
    LOG_INFO("Community Behaviors v{} loading — runtime behavior compiler.",
             SKSE::PluginDeclaration::GetSingleton()->GetVersion().string("."));
    /// @note Before any hook or thread of CB's: every zone after this sees the profiler's real state.
    CB::platform::profile::Start();
    CB::platform::hooks::engine::LogAvailability();
    CB::platform::hooks::InstallSchemaShim();

    // The Havok runtime takeover goes FIRST, before any of CB's own hooks: those (and every later plugin's) then layer
    // on top of its entry jumps rather than being bypassed by them.
    CB::platform::hooks::InstallTakeover();

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
    // ample for the handful of write_call nodes (CallSite); Override entry detours use MinHook.
    SKSE::AllocTrampoline(1u << 11);
    CB::core::serve::InstallSetDataHook();
    CB::core::serve::InstallAnimDataHook();
    CB::core::serve::Install();  // typed-hkx byte-substitution: serves project/character/
                                          // behavior from the consolidated cache under vanilla paths
                                          // (subsumes the retired ProjectLoadProbe descriptor redirect)

    // OAR-parity conditional setdata (Inc 1): register the built-in condition roster + install the
    // producer/matcher hooks. DEFAULT OFF ([Conditions] bEnable) for the minimal release: the
    // conditional-replacement actuator (Inc 2b) is unbuilt and the hooks are unproven in a shipped build.
    // When off, no engine hooks are installed and only the vanilla setdata serve runs; when on, still
    // inert until config compose registers ConditionInstances (Inc 2). Flip the kFlags default once the
    // framework is proven end-to-end in-engine.
    if (CB::core::debug::FlagOn("conditions")) {
        CB::core::conditions::RegisterBuiltins();
        CB::core::conditions::InstallConditionHooks();
    }

    // Every hook subscriber is registered by now (InstallSetDataHook / InstallAnimDataHook →
    // AsdsfOpen / AdsfOpen; serve::Install → ResolveTypedHkx; the condition layer → the setdata producer +
    // matcher, only when enabled; features and ProgressHud at static init): attach every subscribed hook once
    // (a takeover cell where the image owns the function, else an entry or call detour) and close subscription.
    CB::platform::hooks::InstallAll();

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
    // CB::core::serve::Install), so the detours are already live to catch that first open. Here we only do
    // what must be ready before it: configure the resolver and hand byteserve the resolver pointer.
    // The behavior compile sources everything from the loose
    // .hky bundles (no BSA dependency), so the gate is safe to fire this early in load.
    {
        // Hand byteserve the resolver pointer NOW (it gates every use on g_resolver.Ready(), so a hook
        // that fires before Init finishes passes through to vanilla instead of reading half-built state).
        CB::core::serve::SetResolver(&CB::g_resolver);

        // Run the whole resolver bring-up (bundle open + scan + skeleton compile) on a BACKGROUND
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
