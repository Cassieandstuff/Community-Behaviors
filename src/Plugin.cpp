#include "PCH.h"

#include "AnimDataProbe.h"
#include "AnimationDataServer.h"
#include "AnimationSetDataServer.h"
#include "ByteServe.h"
#include "DebugOverlay.h"
#include "ProgressOverlay.h"
#include "Resolver.h"
#include "SyncClipProbe.h"

#include "SimpleIni.h"   // [Cache] bForceRegenerate toggle
#include "havok/sct/BehaviorCompiler.h"   // SetSchemaCompiler — data-driven compiler toggle

#include <chrono>
#include <cstdlib>       // std::getenv — schema-dir env fallback
#include <filesystem>
#include <process.h>   // _beginthreadex — large-stack warm-up thread

namespace CB {

    // The load-order -> compiled-behavior engine. Lives for the process lifetime;
    // the interceptor holds a pointer to it.
    static Resolver g_resolver;

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
        const std::size_t total = reinterpret_cast<std::size_t>(param);
        const auto        t0    = std::chrono::steady_clock::now();
        g_resolver.CompileAll([](std::size_t done, std::size_t tot) {
            ProgressOverlay::SetProgress(done, tot, true);
        });
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

    static void OnMessage(SKSE::MessagingInterface::Message* a_msg)
    {
        if (!a_msg) return;

        if (a_msg->type == SKSE::MessagingInterface::kDataLoaded) {
            // The behavior serve (Init + compile/arm + SetResolver) now runs at PLUGIN LOAD
            // (see SKSEPluginLoad), armed atomically with the adsf/setdata redirects so the engine
            // never sees BR's merged animdata/setdata against a still-vanilla graph — the rep-stosq
            // char-setup mismatch window (BR-21). Only these two need kDataLoaded timing:
            //   • DebugOverlay — SMF isn't up at plugin load.
            //   • animprobe — the animationdata clip singleton isn't built yet at plugin load.
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

    // ── Behavior serve: Init + compile/arm HERE, at plugin load, BEFORE the adsf/setdata
    // redirects below (BR-21). Everything BR serves is armed atomically within this call, before
    // the engine reads anything — so the engine never sees BR's merged animdata/setdata against a
    // still-vanilla graph (the rep-stosq char-setup variable-value-set overrun). The behavior
    // compile sources its vanilla base + deltas entirely from the loose .hky bundles under
    // Data\community_behaviors\plugins\ (Skyrim.hky is the master), so it does NOT depend on BSA
    // archives being mounted — safe this early. Warm cache: arm the serve from disk. Cold/forced:
    // compile + materialize NOW on the 64MB-stack thread and WAIT for it (the compile is fast, so
    // the plugin-load stall is negligible) — BR is then consistent AND active on the SAME run, no
    // restart. (DebugOverlay + animprobe still init at kDataLoaded — SMF and the clip singleton
    // aren't up yet here.)
    {
        CB::g_resolver.Init("Data", "Data/community_behaviors/loadorder.txt");
        CB::g_resolver.SetERGateEnabled(CB::ReadERGateEnabled());
        CB::g_resolver.SetAdsfFromFeature(CB::ReadAdsfFromFeature());  // opt-in, before warm-up
        CB::ApplySchemaCompilerSetting();   // data-driven compiler opt-in (before any compile)
        const std::filesystem::path dataAbs = std::filesystem::current_path() / "Data";
        if (!CB::ReadForceRegenerate() && CB::g_resolver.CachePresent(dataAbs)) {
            CB::g_resolver.ArmCacheFromDisk(dataAbs);   // warm: arm behavior serve from disk
        } else {
            const std::size_t total = CB::g_resolver.SourceCount();
            LOG_INFO("Community Behaviors: precompiling {} graph(s) at plugin load (synchronous — "
                     "atomic arm with adsf/setdata).", total);
            // Compile on the large-stack thread and JOIN it, so the serve is armed before we return.
            if (const std::uintptr_t h = _beginthreadex(nullptr, 64u * 1024u * 1024u,
                        &CB::WarmUpThread, reinterpret_cast<void*>(total),
                        STACK_SIZE_PARAM_IS_A_RESERVATION, nullptr)) {
                WaitForSingleObject(reinterpret_cast<HANDLE>(h), INFINITE);
                CloseHandle(reinterpret_cast<HANDLE>(h));
            } else {
                LOG_ERROR("Community Behaviors: failed to launch precompile thread — behavior serve NOT armed.");
            }
        }
        CB::byteserve::SetResolver(&CB::g_resolver);
    }

    // Merge + arm the set-data redirect NOW (not at kDataLoaded): the engine opens
    // animationsetdatasinglefile.txt early and races the kDataLoaded arming — arming here makes
    // the detour live before that open, so the merged cache is reliably served. Now that the
    // behavior serve is armed just above, adsf/setdata land CONSISTENT with the served graphs.
    //
    // Roster consistency (set-data CRCs must resolve against the character's animationNames) is
    // now a COMPILE-TIME property: the served compiled character file carries the full merged
    // roster, so FUN_140bb0800's animationsetdata bind matches every served CRC without any
    // runtime roster mutation. The set-data guard remains as a belt-and-suspenders drop of any
    // still-uncovered CRC.
    {
        const auto sd = CB::asdserve::ServeSetData("Data", "Data/community_behaviors/loadorder.txt");
        if (sd.attempted && !sd.ok)
            LOG_WARN("Community Behaviors: animationsetdata merge did not complete: {}", sd.error);
        CB::asdserve::ArmSetDataRedirect(sd);

        // animationdatasinglefile.txt (clip/motion metadata) — the 4th leg.
        if (perProject) {
            // Per-project mode: don't merge/materialize here. Install the loader gate — the engine's
            // animdata read blocks on BR's merge + per-project materialize (run inside the hook),
            // guaranteeing the DirList\<Project>\Anims_ files exist before the read. If the gate
            // can't install, REVERT the flipped flag (else the engine takes the per-project branch
            // with nothing materialized) and fall back to the collated redirect below.
            if (!CB::adserve::InstallPerProjectGate()) {
                CB::adserve::DisablePerProjectAnimData();
                perProject = false;
            }
        }
        if (!perProject) {
            // Collated path: merge the bundles' Nemesis animationdata deltas onto the vanilla base,
            // cache it, and redirect the loader's open (FUN_140536ec0 @ RVA 0x536F8E). This is what
            // lets BR fully replace Pandora, whose ONLY real merge in this load order is this file.
            // Pass the clip sink only when the derive feature is on (warm-cache launches skip CompileAll,
            // so the sink is empty then and ClipSink() reports nothing — expected).
            const CB::GraphClipSink* clipSink =
                CB::g_resolver.AdsfFromFeature() ? &CB::g_resolver.ClipSink() : nullptr;
            const auto ad = CB::adserve::ServeAnimData("Data", "Data/community_behaviors/loadorder.txt", clipSink);
            if (ad.attempted && !ad.ok)
                LOG_WARN("Community Behaviors: animationdata merge did not complete: {}", ad.error);
            CB::adserve::ArmAnimDataRedirect(ad);

            // adsf-derive (opt-in authoritative path): OVERWRITE the collated cache with the finalize
            // built straight from the compiled graphs (clips) + Skyrim.hky (motion). Runs AFTER
            // ServeAnimData so the proven collated file is the fallback if the derive can't complete,
            // and the redirect armed above already points at this shared cache path. Only overwrites on
            // success; a warm-cache launch (empty sink) leaves the ServeAnimData file untouched.
            if (CB::g_resolver.AdsfFromFeature() &&
                CB::g_resolver.ClipSink().ClipCount() > 0)
                CB::g_resolver.DeriveAnimData(std::filesystem::current_path() / "Data");
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
