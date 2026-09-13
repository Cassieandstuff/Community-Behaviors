#pragma once

#include "core/discover/BundleManifest.h"
#include "core/resolve/GraphClipSink.h"   // GraphClipSink — the adsf-derive feature's clip accumulator
#include "core/resolve/SymbolInjector.h"

#include <havok/sct/BoneNames.h>   // BoneNameTable — per-actor skeleton bone list (bone-index -> name)

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace havok::model {
    struct IUnitSource;   // abstract per-unit backing store (disk dir OR in-memory .hky)
    class  HkyArchive;    // a packed single-file .hky decompressed in memory
    struct CharacterData; // compiled-character model (defs/CharacterDefs.h) — held by shared_ptr below
}

namespace CB {

    // Resolver — the load-order -> compiled-behavior engine.
    //
    // Discovery model: every mod ships ONE bundle, Data\community_behaviors\plugins\<Mod>.hky\,
    // that mirrors the game data tree inside itself. A graph unit's path WITHIN its bundle
    // is its serve path (the path the game opens):
    //
    //   Data\community_behaviors\plugins\<Mod>.hky\<data-path>\<graph>.hkx\
    //     -> serves at <data-path>\<graph>.hkx
    //   e.g. plugins\Skyrim.hky\meshes\actors\canine\behaviors wolf\wolfbehavior.hkx\
    //     -> meshes\actors\canine\behaviors wolf\wolfbehavior.hkx
    //
    // The <Mod>.hky NAME is the plugin identity (one load-order slot per mod, set by
    // loadorder.txt); a mod's whole behavior-domain contribution — behavior graphs, and
    // later animation-set-data / character deltas — lives in that one bundle. Same-named
    // graphs across different data paths stay distinct because the full path is the key.
    // Keeping bundles OUT of Meshes\ keeps them loose + readable (a mod that BSA-packs its
    // meshes can't accidentally swallow its .hky). No config locator: the tree IS it.
    //
    // Merge: all bundles providing a given serve path contribute layers, merged base-first
    // by load order (LoadMerged overlays them into one graph); the record-level additive
    // merge lives in Resolve()'s gather+merge step.
    class Resolver {
    public:
        // Optional parallel executor for the per-unit native-animation compile: given a batch of
        // independent tasks, run them ALL and BLOCK until every one has finished (exactly the
        // CB::seq::ThreadPool::parallel_for contract). nullptr => serial (the proven default). Kept as
        // a plain std::function so the Resolver carries NO dependency on the sequencer's ThreadPool
        // type; Plugin.cpp wires a real pool in behind the sequencer.enable marker. Each native-anim
        // compile is a pure function of its own def + the immutable served skeleton and writes its own
        // distinct output file, so parallel output is byte-identical to serial by construction.
        using AnimExecutor = std::function<void(std::vector<std::function<void()>>)>;

        // Scan dataDir/community_behaviors/plugins for .hky graph sets (their path under
        // plugins/ IS the serve path), ordered by loadOrderIni (optional). Safe with an
        // empty result (installing BR with no .hky is a no-op). A <Mod>.hky entry may be
        // EITHER an unpacked directory (author dev tree) OR a single packed .hky file (the
        // shipped form — e.g. the full-corpus Skyrim.hky master); both back their units
        // through the same IUnitSource, so the rest of the pipeline is identical.
        // warmReuse == the gate's warm signal (!bForceRegenerate && CachePresent), computed at plugin
        // load and passed in. When true, the served skeletons are already compiled in the on-disk cache
        // from a prior run, so Init registers their serve keys instead of recompiling them — removing
        // the redundant every-launch main-thread skeleton compile (the plugin-load / main-menu stall).
        // When false (cold / forced regen), skeletons compile fresh as before.
        void Init(const std::filesystem::path& dataDir, const std::filesystem::path& loadOrderIni,
                  bool warmReuse = false);

        // Compile (lazy + cached) and return bytes for a served path — the game's
        // open path (e.g. "meshes\\actors\\character\\behaviors\\0_master.hkx"); any
        // case/separator, a leading "data/" tolerated. nullptr = not ours or failed,
        // so the caller falls through to whatever the game would have loaded.
        std::shared_ptr<const std::vector<std::uint8_t>> Resolve(std::string_view servePath);

        // Compile every mapped graph up front (warms the lazy cache) so runtime behavior
        // loads don't stutter. progress(done, total) fires after each graph. Thread-safe —
        // Resolve() locks internally — so this is meant to run on a background thread at
        // startup while (rare, early) live loads fall through to the same cache.
        void CompileAll(const std::function<void(std::size_t done, std::size_t total)>& progress);

        // Compile (if needed) and WRITE every mapped graph to cacheRoot/<serve-path>, so a
        // compile-free file hook can just redirect opens to the on-disk bytes. Clears
        // cacheRoot first — stale bytes from a prior session (different load order) must
        // never be served. MUST run on the warm-up thread (big stack): compiling a master
        // graph on an engine resource/Havok-worker thread overflows its stack. Returns the
        // number of files written.
        std::size_t MaterializeCacheToDisk(const std::filesystem::path& cacheRoot,
                                           const std::function<void(std::size_t done, std::size_t total)>& progress,
                                           const AnimExecutor* animExec = nullptr);

        // Write the opt-in compiled skeletons (m_skeletonServe) into the consolidated cache. Called
        // from BOTH MaterializeCacheToDisk (regen — after the clear) and ArmCacheFromDisk (reuse), so
        // the served skeleton.hkx is always present on disk when serving is enabled. Returns the count.
        std::size_t WriteSkeletonServe(const std::filesystem::path& cacheRoot) const;

        // Compile the bundle-authored native animations (m_nativeAnims — every bare "<name>.hkx" FILE
        // found under an actor's animations\ dir in a .hky, a single-file YAML compile target) into
        // LOOSE .hkx binaries under Data\meshes\ so the engine's
        // startup loose scan indexes them (actor animations resolve by crc32(path) against that scan,
        // NOT via the on-demand Func3 community_behaviors_cache serve — hence loose, not community_behaviors_cache). The
        // clean name is rostered (folded into m_characterAnimNames at Init), so a clip binds it. Called
        // from the warm-up; returns the count written. dataRoot is the Data folder (parent of meshes\).
        std::size_t WriteNativeAnimations(const std::filesystem::path& dataRoot,
                                          const AnimExecutor* exec = nullptr) const;

        // ── Cache reuse (skip the recompile when a prior run's cache is still wanted) ──────
        // The warm-up recompile is a pure optimization; its OUTPUT (compiled graph files under
        // <folderRoot>\community_behaviors_cache\ + synthesized <char>.br.hkx projects) persists on
        // disk across runs. When bForceRegenerate is off and a completed cache is present, BR
        // can serve straight from it — no recompile.
        //   CachePresent()   — true iff MaterializeCacheToDisk's completion sentinel exists
        //                      (written only after a fully successful materialize).
        //   ArmCacheFromDisk() — rebuild ONLY the above-OAR redirect map (from m_sources; no
        //                      compile, no disk write) and mark the redirect ready, so
        //                      ProjectRedirect() serves the existing on-disk cache.
        // NAIVE today: presence, not validity. A stale cache from a DIFFERENT load order would
        // be reused (that's why debug builds default bForceRegenerate on). A content fingerprint
        // that also detects a changed bundle set is the planned next step (real invalidation).
        bool CachePresent(const std::filesystem::path& cacheRoot) const;
        void ArmCacheFromDisk(const std::filesystem::path& cacheRoot, const AnimExecutor* animExec = nullptr);

        // Cheap, lock-free ownership test: true iff a compiled unit is mapped for this
        // serve path (same key normalization as Resolve). m_sources is built entirely in
        // Init() and never mutated afterward, so this needs no lock — it is the hot-path
        // gate the asset hooks call FIRST, so the overwhelming majority of (non-owned)
        // loads never touch Resolve()'s mutex/compile machinery. Func3 fires for every
        // typed-hkx asset on background load threads; sending all of them through the
        // locked path is what destabilized async setup, so this gate is load-bearing.
        bool Owns(std::string_view servePath) const;

        // True iff the compiled cache FILE for an owned serve path is actually on disk under the
        // consolidated cache. Owns() only reports MEMBERSHIP (a unit is mapped) — a graph that failed
        // to compile is still owned but has no materialized file. The serve hook must check THIS before
        // redirecting an open: redirecting an owned-but-unwritten path to a nonexistent cache file
        // abandons the vanilla open with no fallback (→ A-pose/CTD). Lock-free: m_dataRoot is set in
        // BuildRedirectMap (both the materialize and reuse paths) before RedirectReady() flips, and the
        // on-disk cache is immutable while serving.
        bool HasCacheFile(std::string_view servePath) const;

        // ── Above-OAR project redirect ────────────────────────────────────────────
        // Given the vanilla behavior-PROJECT path the engine wrote into a load descriptor
        // (PopulateGraphProjectsToLoad, desc+0x108 — meshes-relative, backslashed, original
        // case, e.g. "Actors\Character\DefaultMale.hkx"), return the path of BR's synthesized
        // project to redirect it to — the SAME path with ".hkx" -> ".br.hkx" (same directory,
        // so the loader's base dir = dirname(desc+0x108) is UNCHANGED and every child resolves
        // as before; same PROJECT STEM so the animationdata association — keyed by the loaded
        // project's basename-minus-extension — still matches, via the ".br"-aliased blocks the
        // AnimData server emits). Returns "" when BR doesn't own that actor's tree (→ pass
        // through untouched).
        //
        // The redirected project's characterFilenames[0] points at BR's compiled character
        // under <folderRoot>\community_behaviors_cache\, whose own behaviorFilename (and every
        // recursive behavior reference) is cache-qualified for the children BR owns and left
        // vanilla for those it does not — so the WHOLE coherent tree loads through the engine's
        // own machinery, ABOVE Open Animation Replacer's Unk3 wrap. Replaces the deep
        // LoadBehaviorGraph / character-loader hooks (which sat below OAR → collision).
        //
        // The <projStem>.br.hkx file is synthesized on first request (the project STEM is only
        // known here, at runtime, from the descriptor) and written under the actor's vanilla
        // root; guarded + memoized, so each project file is built once. Yields "" until the
        // warm-up cache + redirect map are ready (m_redirectReady), so the redirect is inert —
        // safe vanilla fallback — until the compiled character/behaviors are on disk.
        std::string ProjectRedirect(std::string_view vanillaProjectPath);

        // True once MaterializeCacheToDisk has written the cache + synthesized projects and
        // published the redirect map. The redirect hook gates on this.
        bool RedirectReady() const { return m_redirectReady.load(std::memory_order_acquire); }

        std::size_t SourceCount() const { return m_sources.size(); }

        // ER wildcard gate toggle (default OFF). When on, BR injects BR_ERWildcardLock into every
        // compiled graph and gates every GLOBAL wildcard on it (for Engine Relay to flip per-actor).
        // It is a NEW transform applied to EVERY graph and not yet verified in-engine, so it is
        // OPT-IN (Data\SKSE\Plugins\Community Behaviors\settings.ini, [ERGate] bEnable=true): a broken
        // gate would block every wildcard and T-pose every actor, so it must never be mandatory-on
        // while unproven. Set from Plugin.cpp before the warm-up compile runs.
        void SetERGateEnabled(bool enabled) { m_erGateEnabled = enabled; }

        // adsf-derive feature toggle (default OFF; [Compiler] bAdsfFromFeature). When on, the
        // animation-relay.adsf-derive contributor feature runs during CompileAll and fills m_clipSink
        // with each graph's clip inputs; the animdata finalizer reads ClipSink() afterwards. It is a
        // NEW, unvalidated derive path parallel to the proven collated merge, so it is OPT-IN and does
        // NOT drive the emitted adsf until proven in-engine. Set from Plugin.cpp before the warm-up.
        void                 SetAdsfFromFeature(bool enabled) { m_adsfFromFeature = enabled; }
        bool                 AdsfFromFeature() const { return m_adsfFromFeature; }
        const GraphClipSink& ClipSink() const { return m_clipSink; }

        // Finalize the animationdata cache from the COMPILED results (opt-in; requires the adsf-derive
        // feature). Marries the per-graph clips the feature pushed into m_clipSink with the per-character
        // MERGED rosters captured during CompileAll, and writes the collated animationdatasinglefile.txt
        // to <dataDir>/community_behaviors_cache/. Clips come straight from the merged graphs — LoadMerged already
        // resolved the load order, so there are NO deltas to merge; the Skyrim.hky master supplies only
        // the un-derivable half (root motion + project headers). Call AFTER CompileAll. Returns true iff
        // it wrote the file; a no-op returning false when the feature is off or the master is unreadable.
        bool DeriveAnimData(const std::filesystem::path& dataDir);

        // Registered animations for an actor (key like "actors/character"; any case or
        // separators). Each value is a path relative to the actor dir, in animationNames
        // form ("animations\community_behaviors\...\x.hkx"). Empty if none. Lock-free —
        // m_actorAnimations is immutable after Init(). Feeds the in-memory character
        // animation append (authors drop .hkx under <actor>\animations\community_behaviors\;
        // no FNIS/Nemesis registration needed).
        const std::vector<std::string>& AnimationsForActor(std::string_view actorRoot) const;

        // Registered animations for the character whose actor-RELATIVE main behavior file
        // is behaviorFilename (e.g. "Behaviors\\0_Master.hkx" — a character carries no
        // actor prefix). Resolves the actor by finding the animation-owning actor whose
        // served behavior set contains that file. Empty if none. Lock-free.
        const std::vector<std::string>& AnimationsForCharacter(std::string_view behaviorFilename) const;

        std::size_t AnimationActorCount() const { return m_actorAnimations.size(); }

        // Character-roster additions ingested from bundles (Data\community_behaviors\plugins\
        // <Mod>.hky\animationnames\<character>.txt — one animation name per line, produced by
        // br-nemesis-to-hky from a mod's hkbCharacterStringData patches). Keyed by character
        // name (case-insensitive; matches hkbCharacterStringData::name, e.g. "DefaultMale").
        // The character injector dedup-appends these to the character's animationNames so a
        // mod's set-data crcs resolve against a roster that actually lists those anims — the
        // consistency the standalone set-data serve was missing. Lock-free (immutable after
        // Init). Empty if none.
        const std::vector<std::string>& AnimationNamesForCharacter(std::string_view characterName) const;

        std::size_t CharacterAnimNameCount() const { return m_characterAnimNames.size(); }

        // Parsed manifest for a bundle by its load-order stem (case-insensitive, e.g.
        // "bfco"), or nullptr if that bundle was never scanned. Immutable after Init();
        // a bundle with no manifest.json still has a defaulted record here (present=false).
        const BundleManifest* ManifestFor(std::string_view bundleStem) const;

        std::size_t ManifestCount() const { return m_manifests.size(); }

        // ── Node-granular load-order conflicts ─────────────────────────────────────
        // Built in Init(): every node that more than one bundle contributes to the SAME
        // served graph, classified as an expected OVERRIDE (a bundle edits a node one of its
        // declared masters introduced — load order arbitrates the winner) or a namespace
        // CLASH (two bundles with no master relationship both touch the node — a bug, the
        // behavior-domain analog of two ESPs editing one record with neither mastering the
        // other). Immutable after Init(); metadata only — never gates the serve path. The MO2
        // manager reads this to surface conflicts at behavior-node resolution.
        struct NodeConflict {
            std::string              servePath;      // the graph (normalized serve key)
            std::string              cls;            // node class (folder-agnostic identity, with `key`)
            std::string              key;            // node id-else-name
            std::vector<std::string> bundles;        // contributing bundle stems, base-first
            bool                     clash = false;  // true = namespace clash (bug); false = expected override
        };
        const std::vector<NodeConflict>& Conflicts() const { return m_conflicts; }
        std::size_t ConflictCount() const { return m_conflicts.size(); }

    private:
        // If BR serves the child a graph references (childRef is folderRoot-relative, e.g.
        // "Behaviors\1HM_Behavior.hkx"; graphKey is the referencing graph's serve key),
        // return childRef with the "community_behaviors_cache\" prefix; otherwise return childRef
        // unchanged. Lock-free read of m_sources (immutable after Init). Used by Resolve() to
        // steer above-OAR cascade loads into BR's cache for owned children only.
        std::string QualifyChildRef(const std::string& graphKey, const std::string& childRef) const;

        // Build the above-OAR redirect map (folderRoot -> owned characters) from m_sources and
        // publish it (m_redirectReady). Shared by MaterializeCacheToDisk (after it writes the
        // cache) and ArmCacheFromDisk (reuse path — map only, no compile). Depends only on
        // m_sources + the passed data root, never on the compiled bytes.
        void BuildRedirectMap(const std::filesystem::path& cacheRoot);

        // Path of the cache-completion sentinel under the data root (Community Behaviors/cache.ready).
        static std::filesystem::path CacheSentinelPath(const std::filesystem::path& cacheRoot);

        // The merge layers for ONE served graph: its unit sources ordered base-first
        // then deltas by load order, plus whether the base is a character (carries
        // character.yaml → the CharacterYamlLoader/CompileCharacter path) vs a behavior
        // graph. Each source is backed by a disk dir (DiskUnitSource) or a packed .hky
        // subtree (ZipUnitSource) transparently. LoadMerged overlays the layers (later
        // overrides earlier by node name) into one graph — the record-level merge.
        struct GraphSources {
            std::vector<std::shared_ptr<const havok::model::IUnitSource>> layers;
            bool isCharacter = false;
        };
        // normalized serve path -> its merge layers.
        std::unordered_map<std::string, GraphSources> m_sources;
        // Packed .hky archives held for the process lifetime — each vends ZipUnitSources
        // (into m_sources) that reference back into it, so it must outlive them. Empty
        // when every bundle is an unpacked directory. Immutable after Init().
        std::vector<std::shared_ptr<havok::model::HkyArchive>> m_archives;
        // actor root ("actors/character") -> animation paths relative to the actor dir
        // ("animations\community_behaviors\...\x.hkx"), registered via the opt-in
        // animations\community_behaviors\ subfolder. Immutable after Init().
        // NOTE: DEAD since the runtime roster injector was retired (bc4e489a) — the served
        // compiled character file now carries the merged roster. Kept until a resolver
        // dead-code pass prunes these feeders + their scan in Init().
        std::unordered_map<std::string, std::vector<std::string>> m_actorAnimations;
        // character-file stem (lowercase, e.g. "defaultmale") -> animationNames additions,
        // ingested from bundles' animationnames\<stem>.txt. The AUTHOR-FACING roster drop:
        // folded into the character's compiled animationNames at Resolve() (deduped). The
        // converter emits the same files, so authored + converted bundles share one path.
        // Immutable after Init().
        std::unordered_map<std::string, std::vector<std::string>> m_characterAnimNames;
        // Per-actor skeleton bone lists, read at Init from the ACTUAL loaded skeleton.hkx (the load
        // order's result — XPMSSE included) via SkeletonImport. Skeletons already merge at the file
        // level, so BR reads the game's one skeleton rather than compiling its own. Keyed by ACTOR
        // ("character"). Feeds data.boneNames at behavior compile so bone-index fields resolve their
        // NAMES. Immutable after Init().
        std::unordered_map<std::string, havok::sct::BoneNameTable> m_skeletons;
        // Skeleton SERVE (Stage D): the compiled skeleton.hkx bytes BR serves per actor, keyed by the
        // skeleton's normalized SERVE PATH ("meshes/actors/<actor>/character assets/skeleton.hkx").
        // Built in Init by compile-over-base (rebuild the anim skeleton from the merged bone-add layers,
        // carry the game skeleton's physics) — but ONLY when a bundle contributes bone-add layers for the
        // actor, so setups that add no bones stay empty (and inert). Owns() serves these like a compiled
        // graph; WriteSkeletonServe() writes them to the cache. Immutable after Init().
        std::unordered_map<std::string, std::vector<std::uint8_t>> m_skeletonServe;
        // Native BR animations authored as YAML inside a .hky. Each entry: { meshes-relative OUTPUT key
        // ("meshes/actors/<actor>/animations/community_behaviors/<mod>/<name>.hkx"), yaml text }. The key IS
        // the ".hkx" source path (compile-target model: the file is YAML, the ".hkx" name is the binary
        // to emit — source path == output path). Collected in Init from every bare "<name>.hkx" FILE
        // under an actor's animations\ dir; WriteNativeAnimations() compiles each to a loose .hkx there.
        // Their clean char-relative names are auto-folded into m_characterAnimNames at Init so a clip
        // binds. Immutable after Init().
        std::vector<std::pair<std::string, std::string>> m_nativeAnims;
        // bundle stem (lowercase, e.g. "bfco") -> its parsed manifest.json (identity,
        // version, declared masters). Every scanned .hky has an entry (defaulted when
        // no manifest ships). Immutable after Init(); metadata only — never gates serve.
        std::unordered_map<std::string, BundleManifest> m_manifests;
        // Node-granular load-order conflicts (see Conflicts()). Built in Init from each
        // multi-bundle graph's layers + the master DAG; immutable after Init().
        std::vector<NodeConflict> m_conflicts;
        // normalized serve path -> compiled bytes (nullptr = resolved-but-not-ours/failed)
        std::unordered_map<std::string, std::shared_ptr<const std::vector<std::uint8_t>>> m_cache;
        // Above-OAR redirect map, built in MaterializeCacheToDisk. Keyed by the actor's
        // behavior-project FOLDER ROOT (meshes-prefixed, normalized — e.g.
        // "meshes/actors/character"); each entry is a character served under that root, by its
        // file stem ("defaultmale") + the cache ref its synthesized project must point at
        // ("community_behaviors_cache\characters\defaultmale.hkx"). ProjectRedirect() resolves a
        // vanilla project path to its BR project via this map (sole-character or stem-match
        // disambiguation), then synthesizes the project on demand. Populated on the warm-up
        // thread before m_redirectReady flips; immutable after.
        struct ProjectRedirectEntry { std::string charStem; std::string cacheCharRef; };
        std::unordered_map<std::string, std::vector<ProjectRedirectEntry>> m_projectRedirects;
        // projKey (meshes-prefixed, e.g. "meshes/actors/character/defaultmale.hkx") -> the project's
        // ORIGINAL-CASE characterFilenames ref (e.g. "Characters\DefaultMale.hkx"), captured from the base
        // project unit's project.yaml at Init. ProjectRedirect re-emits the vanilla identity from this,
        // so it never needs the (BSA-packed, unreadable-as-loose) vanilla project — the case-sensitive
        // char->setdata/animdata bind stays intact for vanilla-only actors (the A-pose fix).
        std::unordered_map<std::string, std::string> m_projectOrigCharRef;
        std::atomic<bool>                                                  m_redirectReady{ false };
        // Lets an owned actor that loads BEFORE warm-up finishes WAIT for the cache instead of
        // falling back to vanilla (the redirect has no on-demand path; a master-graph compile
        // can't run on the load thread's small stack). Signaled once when the cache is armed.
        std::mutex                                                        m_readyMutex;
        std::condition_variable                                           m_readyCv;
        // Data root ("<game>/Data") captured at materialize time, so the on-demand project
        // synth writes <dataRoot>/Meshes/<vanilla project dir>/<stem>.br.hkx.
        std::filesystem::path                                              m_dataRoot;
        // On-demand project-synth memo + guard (ProjectRedirect runs on actor-load threads).
        std::unordered_map<std::string, bool>                             m_synthesizedProjects;
        std::mutex                                                        m_synthMutex;
        // ER wildcard gate on/off (default OFF; see SetERGateEnabled). Read from settings.ini
        // ([ERGate] bEnable) in Plugin.cpp and set before the warm-up compile.
        bool m_erGateEnabled = false;
        // adsf-derive feature on/off (default OFF; see SetAdsfFromFeature). When on, CompileAll
        // populates m_clipSink via the contributor feature and the finalizer consumes it.
        bool          m_adsfFromFeature = false;
        GraphClipSink m_clipSink;
        // Per-character MERGED roster (animationNames) + actor path, captured in the isCharacter compile
        // when m_adsfFromFeature is on. Both keyed by character stem ("defaultmale"). DeriveAnimData
        // resolves each sink clip's animIndex against the roster (the space the clip binds into) and uses
        // the actor path (from the character's own serve key — always well-formed) to gather that project's
        // clips out of the sink, instead of trusting the base header's `character` string shape.
        std::unordered_map<std::string, std::vector<std::string>> m_characterRosters;
        std::unordered_map<std::string, std::string>              m_characterActor;

        // Mod-declared events/variables (BDI-format), unioned into each graph's data
        // at compile time. Loaded once in Init().
        SymbolInjector m_symbols;
        std::mutex m_mutex;
    };

}  // namespace CB
