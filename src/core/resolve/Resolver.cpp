#include "PCH.h"

#include "core/resolve/Resolver.h"
#include "core/discover/ServeKey.h"
#include "core/resolve/CacheKey.h"
#include "core/debug/Settings.h"
#include <platform/profile/Profile.h>
#include <feature-sdk/FeatureRegistry.h>   // compile-time graph features (ER wildcard gate, …); features/core/hpp on the path


#include <interface/UnitSource.h>
#include <codec/serialization/HkyArchive.h>
#include <compile/AnimDataFromBehavior.h>   // DeriveClipInputsFromGraph (ClipInputsOf)
#include <codec/format/AnimationYamlLoader.h>  // native animation YAML (in a .hky) -> AnimationDef
#include <codec/format/LoadOrderText.h>
#include <merge/LoadPlan.h>                    // CB::engine::merge::PlanLoadOrder (the shared load-order plan)
#include <compile/AnimationCompiler.h>         // CB::engine::anim::CompileAnimation (native anim -> loose .hkx)
#include <codec/motion/RootMotion.h>           // ResolveRootMotion — a unit's actor motion (record or folded root)
#include <interface/MasterTable.h>          // CB::engine::codec::formid::BuildMasterTable (ordered master table)
#include <compile/AnimDataDeriver.h>      // CB::engine::animdata::DeriveClipInput
#include <codec/resource/PathKey.h>       // path identity (PathKey/HasExt) + ClipSlotKey (the LoadClips slot rule)
#include <interface/reflection/HavokSchema.h>        // schema::SharedRegistry — pre-warm before parallel anim compile
#include "core/discover/BundleReader.h"               // read the shipped vanilla skeleton base from Skyrim.hky
#include <common/Xml.h>
#include <disk/Disk.h>
#include <text/Text.h>

#include <atomic>
#include <map>
#include <optional>
#include <set>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <functional>   // std::function — skeleton-compile tasks fanned across the pool (stage 2)
#include <memory>
#include <mutex>        // guard the shard merges into m_skeletonServe / m_skeletons (stage 2)
#include <string_view>
#include <system_error>
#include <unordered_set>

namespace fs = std::filesystem;

namespace CB {

    namespace {

        using CB::engine::codec::resource::HasExt;
        using CB::engine::codec::resource::PathKey;

        /// @brief A reflective unit's kind (its graph.yaml's root variant; a patch names its base's). A unit without
        ///        graph.yaml is a patch that leaves the root untouched — Behavior.
        CB::engine::codec::HkyArchive::UnitKind KindOfUnit(const CB::engine::common::IUnitSource& src)
        {
            const auto g = src.read("graph.yaml");
            return g ? CB::engine::codec::HkyArchive::KindOfGraphYaml(*g) : CB::engine::codec::HkyArchive::UnitKind::Behavior;
        }

        /// @brief True when the unit is complete on its own (not a patch over a master's unit).
        bool IsCompleteUnit(const CB::engine::common::IUnitSource& src)
        {
            const auto g = src.read("graph.yaml");
            return g && !CB::engine::codec::HkyArchive::IsPatchGraphYaml(*g);
        }

        using CB::engine::text::SplitLines;
        using CB::engine::text::StripBom;
        using CB::engine::text::Trim;
        namespace disk = CB::engine::disk;

        /// @brief Append an author roster drop (`animationnames\<char>.txt`) to `names`: one animation path per line,
        ///        blank and `#`/`;` lines skipped, a leading BOM ignored.
        /// @note XML entities are decoded. A roster can arrive HTML-escaped (a mod's character decompiled through an
        ///       XML pipeline), and "..\SharedKillMoves\Human&amp;Boar\..." must fold back to its literal '&' form
        ///       BEFORE it is deduped against the base roster: the escaped copy would fail the dedup and pad
        ///       animationNames with phantom paths, and OAR sets its synchronized-clip index offset to
        ///       animationNames.size() — the padding underflows its uint16 `animationBindingIndex -= offset`
        ///       (out of bounds → the char-setup CTD).
        void AppendRosterLines(std::string_view text, std::vector<std::string>& names)
        {
            for (const std::string_view line : SplitLines(StripBom(text))) {
                const std::string_view t = Trim(line);
                if (!t.empty() && t[0] != '#' && t[0] != ';') names.push_back(CB::engine::common::xml::Unescape(t));
            }
        }

        using CB::core::discover::NormalizeKey;
        using CB::core::discover::FolderRootOf;
        using CB::core::discover::CacheDiskRel;
        // (serve-key helpers live in core/discover/ServeKey.h)

        // ── Compile-time graph features now live in features/ ─────────────────────────
        // The ER wildcard gate (and future graph transforms) moved to self-registered
        // IGraphFeatures under features/<Owner>/ (see CLAUDE.md "Compiler features —
        // CB::feature (SOP)"). The transform runs from the compile loop via the
        // FeatureRegistry (below). This host-side adapter bridges a feature's logging to the
        // plugin logger — features never touch LOG_* directly (the pure-Apply rule keeps them
        // offline-reproducible).
        struct ResolverFeatureLog final : CB::feature::IFeatureLog {
            void Info(std::string_view m) override { LOG_INFO("{}", m); }
            void Warn(std::string_view m) override { LOG_WARN("{}", m); }
        };

        /// @brief The engine's load-order plan (merge::PlanLoadOrder, shared with hky-tool graph-html) over every
        ///        scanned manifest's masters, its warnings logged.
        /// @note  Masters drive order, not the flat loadorder.txt priority: a declared master always compiles base-first,
        ///        and every bundle implicitly masters Skyrim, so the vanilla corpus is the root however mods are ordered.
        ///        loadorder.txt only breaks ties among peers (MO2's domain). The relay tree falls out for free: it IS a
        ///        master DAG (engine_relay masters Community Behaviors; an ER mod masters engine_relay).
        CB::engine::merge::LoadPlan PlanLoadOrder(const std::unordered_map<std::string, BundleManifest>& manifests,
                                                  const CB::engine::codec::LoadOrder&                    order)
        {
            std::unordered_map<std::string, std::vector<std::string>> masters;
            for (const auto& [stem, mf] : manifests) masters[stem] = mf.masters;
            auto plan = CB::engine::merge::PlanLoadOrder(masters, order);
            for (const auto& w : plan.warnings) LOG_WARN("Resolver: load order — {}.", w);
            return plan;
        }

    }  // namespace

    void Resolver::Init(const fs::path& dataDir, const fs::path& loadOrderIni, bool warmReuse,
                        const AnimExecutor* skelExec)
    {
        try {
            Scan(dataDir, loadOrderIni, warmReuse, skelExec);
        } catch (const std::exception& e) {
            /// @note A half-built resolver must never serve: drop what serving reads, so CB owns nothing this launch.
            /// @warning On a warm start the cached adsf/asdsf were armed before Init finished and stay served.
            LOG_ERROR("Community Behaviors: reading the load order FAILED — {} — CB serves nothing this launch (vanilla "
                      "behaviors).", e.what());
            m_failed = true;
            m_sources.clear();
            m_skeletonServe.clear();
            m_projectUnits.clear();
            m_nativeAnims.clear();
        }
        // Publish the ready signal LAST (release), so any serve hook that acquires it sees every field completely
        // written. Init runs on a background thread; hooks pass through to vanilla until this fires, and the compile
        // gate blocks on WaitReady().
        { std::lock_guard<std::mutex> lk(m_initMutex); m_ready.store(true, std::memory_order_release); }
        m_initCv.notify_all();
    }

    void Resolver::Scan(const fs::path& dataDir, const fs::path& loadOrderIni, bool warmReuse,
                        const AnimExecutor* skelExec)
    {
        // 1) Load order (optional): .hky plugin name -> priority (higher wins).
        const auto order = CB::engine::codec::ReadLoadOrder(loadOrderIni).value_or(CB::engine::codec::LoadOrder{});

        // 2) Scan the plugins tree. Each top-level *.hky is a MOD BUNDLE that mirrors the
        //    game data tree inside itself, so a unit's path WITHIN its bundle IS its serve
        //    path — no config locator needed:
        //      Data\community_behaviors\plugins\<Mod>.hky\<data-path>\<graph>.hkx\
        //    serves at <data-path>\<graph>.hkx. The <Mod>.hky name is the plugin identity
        //    (one load-order slot); a mod's whole behavior-domain contribution — behavior
        //    graphs, and later animation-set-data / character deltas — lives in that one
        //    bundle, game-tree-mirrored. Keeping the source OUT of Meshes\ keeps it loose
        //    and readable — a mod that BSA-packs its meshes won't accidentally swallow it.
        const fs::path pluginsRoot = dataDir / "community_behaviors" / "plugins";

        // serveKey -> [(priority, unit source, isCharacter)]; ALL layers for a graph
        // (vanilla base + mod deltas), merged in priority order rather than a single
        // winner. The source is disk-backed (DiskUnitSource, an unpacked bundle) or
        // archive-backed (ZipUnitSource, a packed .hky) — the merge doesn't care which.
        struct LayerBuild {
            std::string                                      stem;    // owning bundle — its master-DAG rank orders this layer
            std::shared_ptr<const CB::engine::common::IUnitSource> source;
            bool                                             isChar;
        };
        std::unordered_map<std::string, std::vector<LayerBuild>> layers;
        /// @note Keys whose base is a project or skeleton: not merged graphs. A mod's patch unit that leaves the root
        ///       untouched carries no graph.yaml, so on its own it would read as a behavior layer.
        std::unordered_set<std::string> nonGraphKeys;
        int plugins = 0;

        // Collector for bundle-authored NATIVE ANIMATIONS. The .hky compile-target model: inside an
        // .hky, ".hkx" ALWAYS means "compile target", never a compiled binary. A "<name>.hkx/" FOLDER
        // is a multi-file YAML tree (behavior / skeleton / character); a bare "<name>.hkx" FILE is a
        // SINGLE-FILE YAML compile target — an animation (or anything else) whose ".hkx" name is the
        // binary the compiler will emit. So a native animation is a FILE key ending in ".hkx" (unit
        // internals end in .yaml/.txt, never .hkx, so the file-vs-tree split is unambiguous). Output
        // key == the ".hkx" path itself (== the author's clip animationName), keeping output file ==
        // roster entry == animationName byte-aligned (BR-16). Shared by the unpacked + packed branches.
        auto collectNativeAnim = [this](NativeAnim a) {
            if (a.size == 0) return;
            const std::string& relOrig = a.outKey;   // the ".hkx" path IS the output name
            const std::string  pathKey = PathKey(relOrig);
            if (!HasExt(pathKey, ".hkx")) return;

            // Must live UNDER an actor's animations dir (meshes/actors/<actor>/animations/...).
            // Belt-and-suspenders now: a ".hkx" FILE gate can no longer match the ADSF clip-generator
            // deltas (those are "<clip>.hkx.yaml", ending in .yaml — the old BR-18 false positive that
            // dumped ~1269 bogus .hkx into animationdatasinglefile.txt\clips\ and crashed char-setup);
            // the guard also rejects any stray binary .hkx that shouldn't be in an .hky at all.
            const auto ap = pathKey.find("meshes/actors/");
            if (ap == std::string::npos) return;
            const std::size_t as = ap + 14;
            const auto an = pathKey.find("/animations/", as);
            if (an == std::string::npos) return;

            // COLLECT FOR RECOMPILE ONLY — do NOT auto-roster. A .hkx file's presence under an
            // actor's animations\ dir is NOT membership in that actor's animationName roster. The
            // authoritative roster is the character's animationNames (a union roster across layers) plus
            // any explicit animationnames\<char>.txt drop and the rosterref clip membrane. Force-adding
            // every file here was over-inclusive: a full master carries ~5900 anims under
            // meshes\actors\character\animations\, but only ~1656 belong to defaultmale — the rest are
            // other character variants, DLC/creature-under-character projects, first-person, or
            // DAR/behavior-only clips. Folding all of them into defaultmale/defaultfemale inflated the
            // roster ~3.5x; char-setup binds against roster.size() (== the OAR synchronized offset), so
            // the surplus entries overran the binding/offset math → the AutoplayBehavior char-setup CTD.
            // (Reached only for a compile-opted-in bundle now; still don't auto-roster from raw file
            // discovery.) When the real in-memory serve lands, binding will be driven from the
            // authoritative roster, not from raw file discovery.
            m_nativeAnims.push_back(std::move(a));
        };


        std::error_code rootEc;
        if (!fs::is_directory(pluginsRoot, rootEc)) {
            LOG_WARN("Resolver: plugins root not found: '{}' — nothing to serve.", pluginsRoot.string());
        } else {
            std::error_code ec;
            for (fs::directory_iterator bi(pluginsRoot, ec), bEnd; !ec && bi != bEnd; bi.increment(ec)) {
                const fs::path bundle = bi->path();
                if (!HasExt(bundle.filename().string(), ".hky")) continue;  // a mod bundle
                std::error_code bec;
                const bool bundleIsDir = bi->is_directory(bec);   // dir = unpacked; file = packed .hky

                // Match against the load order by bundle STEM ("Skyrim", "bfco"), not the
                // filename ("vanilla.hky"): loadorder.txt lists bare bundle names. Using the
                // .hky filename here made every lookup miss -> every bundle priority 0 -> the
                // base-first sort became arbitrary (alphabetical), so the vanilla base was not
                // ordered first and the merge tried to use a patch bundle as
                // the base. (Masked until the stale full-bundle VFS collision was cleaned up.)
                // The bundle STEM is its load-order handle and its master-DAG node; the
                // actual ordering is resolved from the master graph after the scan
                // (PlanLoadOrder), not from a flat loadorder.txt priority here.
                const std::string plugName = PathKey(bundle.stem().string());
                ++plugins;

                // Parse this bundle's manifest.json (identity / version / declared masters).
                // Purely additive metadata: a bundle without one still scans + merges as
                // before, and this never touches m_sources / the serve path. A DIR bundle's
                // manifest reads off disk here; a PACKED bundle is a file (no disk manifest),
                // so its manifest is read from the archive in the packed branch below.
                auto logManifest = [](const BundleManifest& mf) {
                    if (!mf.present) return;
                    std::string masters;
                    for (const auto& mstr : mf.masters) { if (!masters.empty()) masters += ", "; masters += mstr; }
                    LOG_INFO("Resolver: bundle '{}' v{} by '{}'{}",
                             mf.name,
                             mf.version.empty() ? std::string("?") : mf.version,
                             mf.author.empty()  ? std::string("?") : mf.author,
                             masters.empty() ? std::string() : (" — masters: " + masters));
                };
                // OPT-IN animation compile: only bundles whose manifest sets compile_animations=true get
                // their native animations collected for compile (default false — Skyrim.hky + conversions
                // skip it; those animations exist only to feed adsf root motion, reunited at build time).
                // An opted-in bundle compiles its animations straight to the real meshes path (they serve);
                // a non-compile bundle skips the compile entirely (nothing to clobber). Retires the old
                // hardcoded gate + the CBanims/ staging harness.
                bool bundleCompileAnims = false;
                {
                    std::vector<std::string> mfWarn;
                    BundleManifest mf = BundleManifest::Load(bundle, bundle.stem().string(), mfWarn);
                    for (const auto& w : mfWarn) LOG_WARN("Resolver: manifest — {}", w);
                    logManifest(mf);
                    bundleCompileAnims = mf.compileAnimations;
                    if (bundleCompileAnims)
                        LOG_INFO("Resolver: bundle '{}' opts INTO animation compile (manifest compile_animations=true).", bundle.stem().string());
                    m_manifests.emplace(plugName, std::move(mf));
                }

              if (bundleIsDir) {
                // ── Unpacked bundle (author dev tree) — walk the game tree on disk. ──
                // Walk the game tree inside the bundle for graph units (<graph>.hkx/ dirs).
                std::error_code wec;
                for (fs::recursive_directory_iterator ui(bundle, wec), uEnd; !wec && ui != uEnd; ui.increment(wec)) {
                    std::error_code uec;
                    // Native animation source: a bare "<name>.hkx" FILE under an actor's animations\
                    // dir (a single-file YAML compile target — the .hky compile-target model). A
                    // "<name>.hkx" DIR is a unit (handled below); is_regular_file distinguishes them.
                    // The "/animations/" pre-check skips binary .hkx elsewhere in the tree (e.g. the
                    // skeleton physics source) so we don't read them; collectNativeAnim re-guards it.
                    if (std::error_code rfe; bundleCompileAnims && ui->is_regular_file(rfe)) {
                        const std::string relOrig = ui->path().lexically_relative(bundle).generic_string();
                        if (HasExt(relOrig, ".hkx") && PathKey(relOrig).find("/animations/") != std::string::npos) {
                            std::error_code sz, mt;
                            const auto size  = ui->file_size(sz);
                            const auto mtime = ui->last_write_time(mt);
                            if (!sz && !mt)
                                collectNativeAnim({ relOrig, nullptr, ui->path(), size,
                                                    static_cast<std::uint64_t>(mtime.time_since_epoch().count()) });
                        }
                        continue;
                    }
                    if (!ui->is_directory(uec)) continue;
                    const fs::path unit = ui->path();
                    if (!HasExt(unit.filename().string(), ".hkx")) continue;
                    ui.disable_recursion_pending();  // a unit is a leaf — don't descend into it

                    // Serve key = the unit's path RELATIVE TO THE BUNDLE (its game data
                    // path + graph), e.g. "meshes/actors/character/behaviors/0_master.hkx".
                    const std::string key = NormalizeKey(unit.lexically_relative(bundle).generic_string());
                    if (key.empty() || key[0] == '.') continue;

                    /// @note Skeletons compile through the skeleton path (path-local winners), and the project is
                    ///       synthesized by ProjectRedirect — neither is a merged graph layer. A project unit only
                    ///       contributes its original-case character ref.
                    auto src = std::make_shared<CB::engine::common::DiskUnitSource>(unit);
                    const auto kind = KindOfUnit(*src);
                    if (kind == CB::engine::codec::HkyArchive::UnitKind::Skeleton) { nonGraphKeys.insert(key); continue; }
                    if (kind == CB::engine::codec::HkyArchive::UnitKind::Project) {
                        nonGraphKeys.insert(key);
                        if (IsCompleteUnit(*src)) m_projectUnits[key] = std::move(src);
                        continue;
                    }
                    layers[key].push_back({ plugName, std::move(src), kind == CB::engine::codec::HkyArchive::UnitKind::Character });
                }

                // AUTHOR-FACING ROSTER DROP: a bundle adds animations to a character by
                // dropping a flat list at <Mod>.hky\animationnames\<character-file-stem>.txt
                // (e.g. animationnames\defaultmale.txt for characters\defaultmale.hkx). One
                // animation path per line; '#'/';' lines and blanks ignored. No need to mirror
                // the deep character-unit directory, and no Nemesis — these fold into the
                // character's compiled animationNames roster at Resolve() time (deduped
                // exactly). The converter emits the same files, so converter mods and
                // hand-authored bundles share one path. Unioned across bundles; the compile
                // dedups, so priority/order is immaterial here.
                std::error_code anec;
                const fs::path animNamesDir = bundle / "animationnames";
                if (fs::is_directory(animNamesDir, anec)) {
                    for (fs::directory_iterator ai(animNamesDir, anec), aEnd; !anec && ai != aEnd; ai.increment(anec)) {
                        std::error_code fec;
                        if (!ai->is_regular_file(fec)) continue;
                        if (!HasExt(ai->path().filename().string(), ".txt")) continue;
                        const std::string charKey = PathKey(ai->path().stem().string());  // "defaultmale"
                        auto& names = m_characterAnimNames[charKey];
                        if (const auto txt = disk::ReadFile(ai->path())) AppendRosterLines(*txt, names);
                    }
                }

              } else {
                // ── Packed bundle (shipped form) — opened through its zip index. ──
                // A single-file .hky is a zip of the same game-tree unit layout, read via
                // HkyArchive (immune to Windows MAX_PATH — the deep unit tree never touches
                // disk); each indexed unit vends a ZipUnitSource whose reads inflate the
                // file on demand. The archive is held in m_archives for the process lifetime
                // so those sources stay valid.
                std::string err;
                auto arc = CB::engine::codec::HkyArchive::LoadFromFile(bundle.string(), err);
                if (!arc) {
                    LOG_WARN("Resolver: cannot read packed .hky '{}': {} — skipped.", bundle.string(), err);
                } else {
                    // A packed bundle is a file, so the disk manifest Load above saw nothing —
                    // read the archive's own manifest.json so a packed bundle (e.g. the shipped
                    // Community Behaviors.hky) still declares its identity + masters to the load-order
                    // DAG. Overwrites the defaulted entry emplaced above.
                    if (auto mtext = arc->file("manifest.json")) {
                        std::vector<std::string> mfWarn;
                        BundleManifest mf = BundleManifest::Parse(*mtext, bundle.stem().string(), mfWarn);
                        for (const auto& w : mfWarn) LOG_WARN("Resolver: manifest — {}", w);
                        if (mf.present) {
                            logManifest(mf);
                            bundleCompileAnims = mf.compileAnimations;
                            if (bundleCompileAnims)
                                LOG_INFO("Resolver: bundle '{}' opts INTO animation compile (manifest compile_animations=true).", bundle.stem().string());
                            m_manifests[plugName] = std::move(mf);
                        }
                    }
                    for (const auto& u : arc->units()) {
                        // The project graph is not a merged layer — ProjectRedirect serves the complete
                        // project unit on demand (into the cache, vanilla identity, its ORIGINAL-CASE
                        // characterFilenames: a lowercased ref A-poses vanilla-only actors).
                        if (u.kind == CB::engine::codec::HkyArchive::UnitKind::Project) {
                            nonGraphKeys.insert(NormalizeKey(u.prefix));
                            if (auto src = arc->source(u.prefix))
                                if (IsCompleteUnit(*src)) m_projectUnits[NormalizeKey(u.prefix)] = std::move(src);
                            continue;
                        }
                        // Skeletons are NOT a behavior/character graph — they compile through the dedicated
                        // skeleton path (m_skeletonServe, path-local winners). Adding them here made Owns() true so
                        // ByteServe REDIRECTED every skeleton load into community_behaviors_cache, but no graph compile
                        // ever wrote that file — the redirect hit a missing file and the skeleton failed to
                        // load: universal A-pose + no root motion. Leave unlayered skeletons to vanilla.
                        if (u.kind == CB::engine::codec::HkyArchive::UnitKind::Skeleton) { nonGraphKeys.insert(NormalizeKey(u.prefix)); continue; }
                        const std::string key = NormalizeKey(u.prefix);   // already a path key
                        if (key.empty() || key[0] == '.') continue;
                        layers[key].push_back({ plugName, arc->source(u.prefix),
                                                u.kind == CB::engine::codec::HkyArchive::UnitKind::Character });
                    }

                    // Author roster drops inside a packed bundle live at "animationnames/
                    // <char-stem>.txt" (same convention as the on-disk dir above). The
                    // full-corpus master ships none, but a packed MOD bundle may.
                    for (const auto& full : arc->filesUnder("animationnames/")) {
                        if (!HasExt(full, ".txt")) continue;
                        const std::string charKey = PathKey(fs::path(full).stem().string());
                        if (auto txt = arc->file(full)) AppendRosterLines(*txt, m_characterAnimNames[charKey]);
                    }

                    // Native animations authored inside the packed bundle: a bare "<name>.hkx" FILE
                    // key under meshes\ (the .hky compile-target model — unit-tree keys are
                    // "<name>.hkx/<subfile>" ending in .yaml/.txt, so a key ENDING in ".hkx" is always
                    // a single-file compile target). Only the index entry is taken (original-case path,
                    // size, CRC); the YAML is read when it compiles. collectNativeAnim guards the
                    // meshes/actors/.../animations/ path.
                    if (bundleCompileAnims)
                        for (const auto& e : arc->entriesUnder("meshes/"))
                            if (HasExt(e.path, ".hkx")) collectNativeAnim({ e.path, arc, {}, e.size, e.crc });

                    m_archives.push_back(std::move(arc));
                }
              }
            }
        }

        // Resolve the master DAG into a base-first rank per bundle (masters drive order;
        // loadorder.txt breaks peer ties only) and the set of bundles skipped because a
        // declared master is absent. This REPLACES the old flat loadorder.txt priority —
        // the missing/out-of-order-master conditions the old warn-only pass merely reported
        // are now enforced: a master always sorts base-first, a dependent with no base drops.
        const auto plan = PlanLoadOrder(m_manifests, order);

        /// @note Skeletons are PATH-LOCAL reflective units in every bundle ("meshes/actors/<actor>/<variant>/<x>.hkx/":
        ///       graph.yaml + animation/ + ragdoll/, bones name-keyed and ordered by the boneorder compat shim), and the
        ///       load-order winner of a unit path IS that skeleton — a later bundle replaces it whole (XPMSSE ships its
        ///       full skeletons; an identical one is a no-op replacement). Bone tables bind through the characters'
        ///       `rig:` (see BonesFor), never "the first skeleton under the actor folder" — that picked
        ///       _1stperson/skeletonfirst for the 3rd-person character ('_' sorts before 'c').
        {
            const fs::path pluginsRoot = dataDir / "community_behaviors" / "plugins";
            struct RankedBundle { std::string stem; int rank = 0; BundleReader reader; };
            std::vector<RankedBundle> bundles;
            {
                std::error_code me;
                if (fs::is_directory(pluginsRoot, me))
                    for (fs::directory_iterator bi(pluginsRoot, me), bend; !me && bi != bend; bi.increment(me)) {
                        if (!HasExt(bi->path().filename().string(), ".hky")) continue;
                        const std::string stem = PathKey(bi->path().stem().string());
                        if (plan.dropped.count(stem)) continue;
                        auto br = BundleReader::Open(bi->path());
                        if (!br) continue;
                        const auto rit = plan.rank.find(stem);
                        bundles.push_back({ stem, rit != plan.rank.end() ? rit->second : 0, std::move(*br) });
                    }
            }
            std::sort(bundles.begin(), bundles.end(), [](const RankedBundle& a, const RankedBundle& b) {
                return a.rank != b.rank ? a.rank < b.rank : a.stem < b.stem;
            });

            /// @note Unit identity is the NORMALIZED path: the engine opens these files case-insensitively,
            ///       and XPMSSE ships "actors/Chaurus/…" / "skeever/…/Skeleton.hkx" over vanilla's lowercase
            ///       paths. Internal-only key; bone NAMES are read original-case (the unit source lists original case).
            /// @note The load-order winner of a unit path is the last bundle whose unit there is COMPLETE: a skeleton,
            ///       project or character graph.yaml owned by the unit itself — a mod's patch unit never replaces one.
            std::unordered_map<std::string, std::size_t> skelWinner, projWinner, charWinner;
            for (std::size_t bi = 0; bi < bundles.size(); ++bi)
                for (const std::string& f : bundles[bi].reader.filesUnder("meshes/", ".yaml")) {
                    if (f.size() <= 15 || f.compare(f.size() - 15, 15, ".hkx/graph.yaml") != 0) continue;
                    const std::string unit = f.substr(0, f.size() - 11);
                    const std::string text = bundles[bi].reader.read(f).value_or("");
                    if (CB::engine::codec::HkyArchive::IsPatchGraphYaml(text)) continue;
                    switch (CB::engine::codec::HkyArchive::KindOfGraphYaml(text)) {
                        case CB::engine::codec::HkyArchive::UnitKind::Skeleton:  skelWinner[unit] = bi; break;
                        case CB::engine::codec::HkyArchive::UnitKind::Project:   projWinner[unit] = bi; break;
                        case CB::engine::codec::HkyArchive::UnitKind::Character: charWinner[unit] = bi; break;
                        default: break;
                    }
                }

            /// @note Loaded HERE, single-threaded, before any fan-out: its one-time load is guarded by a plain,
            ///       unsynchronized `state` int, so two workers racing the first use could both attempt it.
            auto* schemaReg = CB::engine::schema::SharedRegistry();

            // SERVE: every unit a non-master bundle wins. Each task writes only its own m_skeletonServe key
            // (under serveMu), so fanning across units is byte-identical to serial.
            std::mutex serveMu;
            auto serveUnit = [&](const std::string& unit, std::size_t bi) {
                const std::string key = NormalizeKey(unit);
                // WARM REUSE: the compiled bytes from a prior run are already in the consolidated cache;
                // ByteServe serves that file via a path swap and Owns() needs only the key, so register it
                // and skip the recompile. Guarded on warmReuse (== the gate's !force && CachePresent) AND the
                // file existing, so a forced regen or a missing file compiles fresh. WriteSkeletonServe skips
                // empty-byte entries, so the key-only registration doesn't rewrite the present file.
                if (warmReuse) {
                    const std::string rel = CB::core::discover::CacheDiskRel(key);
                    if (!rel.empty() && std::filesystem::exists(dataDir / fs::path(rel))) {
                        { std::lock_guard<std::mutex> lk(serveMu); m_skeletonServe.emplace(key, std::vector<std::uint8_t>{}); }
                        LOG_INFO("Resolver: skeleton '{}' ({}) — warm cache hit, serve key registered (recompile skipped).",
                                 unit, bundles[bi].stem);
                        return;
                    }
                }
                /// @note The unit is the whole skeleton.hkx, so serving it is a parse and a serialize: nothing is derived.
                const auto src = bundles[bi].reader.unitSource(unit);
                CB::engine::decompile::ParsedHky graph;
                std::vector<std::uint8_t> bytes;
                std::string err;
                if (!src || !schemaReg || !CB::engine::decompile::ParseHkyReflective(*src, *schemaReg, graph, err, nullptr, unit) ||
                    !CB::engine::decompile::SerializeGraph(graph, bytes, err)) {
                    LOG_WARN("Resolver: skeleton '{}' ({}) did not compile: {} — not served.", unit, bundles[bi].stem,
                             src && schemaReg ? err : std::string("no unit source or schema"));
                    return;
                }
                const std::size_t size = bytes.size();
                { std::lock_guard<std::mutex> lk(serveMu); m_skeletonServe[key] = std::move(bytes); }
                LOG_INFO("Resolver: SERVING skeleton '{}' from '{}' ({} bytes).", unit, bundles[bi].stem, size);
            };
            {
                std::vector<std::function<void()>> serveTasks;
                for (const auto& [unit, bi] : skelWinner) {
                    if (bundles[bi].stem == "skyrim") continue;
                    const std::string* u = &unit;
                    const std::size_t  b = bi;
                    serveTasks.push_back([&serveUnit, u, b] { serveUnit(*u, b); });
                }
                if (skelExec && *skelExec && serveTasks.size() > 1) (*skelExec)(std::move(serveTasks));
                else for (auto& t : serveTasks) t();
            }

            // RIG BINDING (shared with the converter): project characterFilenames -> character rig/behavior.
            for (const auto& [projUnit, pbi] : projWinner) {
                const auto psrc = bundles[pbi].reader.unitSource(projUnit);
                if (!psrc || !schemaReg) continue;
                m_rigIndex.AddProject(projUnit, CB::engine::decompile::ReadProjectCharacters(*psrc, *schemaReg),
                    [&](const std::string& charUnit) -> std::optional<CB::engine::skeleton::CharacterRig> {
                        const auto cit = charWinner.find(charUnit);
                        if (cit == charWinner.end()) return std::nullopt;
                        const auto src = bundles[cit->second].reader.unitSource(charUnit);
                        const auto cs  = src ? CB::engine::decompile::ReadCharacterStrings(*src) : std::nullopt;
                        if (!cs) return std::nullopt;
                        return CB::engine::skeleton::CharacterRig{ cs->rigName, cs->behaviorFilename };
                    },
                    [&](const std::string& rig) { return skelWinner.count(rig) != 0; },
                    [](const std::string& m) { LOG_WARN("Resolver: {}.", m); });
            }
            m_rigIndex.Finalize();
            const std::set<std::string>& boundRigs = m_rigIndex.BoundRigs();

            // BONE TABLES, one per bound rig, from that rig unit's winner. Each task builds a LOCAL table
            // and moves it into its own distinct m_skeletons key under tblMu (serial-equivalent).
            std::mutex tblMu;
            std::vector<std::function<void()>> boneTasks;
            boneTasks.reserve(boundRigs.size());
            for (const std::string& rig : boundRigs) {
                const std::string* r  = &rig;
                const std::size_t  bi = skelWinner.at(rig);
                boneTasks.push_back([&, r, bi] {
                    const auto src = bundles[bi].reader.unitSource(*r);
                    std::string err;
                    auto names = src && schemaReg ? CB::engine::decompile::ReadSkeletonBones(*src, *schemaReg, *r, "animation", &err)
                                                  : std::nullopt;
                    if (!names) {
                        LOG_WARN("Resolver: rig '{}' ({}) has no animation skeleton bones{}", *r, bundles[bi].stem, err.empty() ? "" : ": " + err);
                        return;
                    }
                    CB::engine::common::BoneNameTable tbl;
                    tbl.names = std::move(*names);
                    const std::size_t nBones = tbl.names.size();
                    { std::lock_guard<std::mutex> lk(tblMu); m_skeletons[*r] = std::move(tbl); }
                    LOG_INFO("Resolver: rig '{}' = {} bone(s) from '{}'.", *r, nBones, bundles[bi].stem);
                });
            }
            if (skelExec && *skelExec && boneTasks.size() > 1) (*skelExec)(std::move(boneTasks));
            else for (auto& t : boneTasks) t();
            LOG_INFO("Resolver: {} skeleton unit(s) across {} bundle(s); {} served; {} rig(s) bound.",
                     skelWinner.size(), bundles.size(), m_skeletonServe.size(), m_skeletons.size());
        }

        // Record each graph's layers, base-first by the master-DAG rank, for the merge to
        // overlay. The base (front) layer's isChar flag decides the compile path (character
        // vs behavior graph). Layers from a skipped bundle are dropped.
        for (auto& [key, ls] : layers) {
            if (nonGraphKeys.count(key)) continue;
            ls.erase(std::remove_if(ls.begin(), ls.end(),
                         [&](const LayerBuild& l) { return plan.dropped.count(l.stem) != 0; }),
                     ls.end());
            if (ls.empty()) continue;
            std::stable_sort(ls.begin(), ls.end(),
                      [&](const LayerBuild& a, const LayerBuild& b) {
                          const auto ra = plan.rank.find(a.stem);
                          const auto rb = plan.rank.find(b.stem);
                          const int  va = ra != plan.rank.end() ? ra->second : 0;
                          const int  vb = rb != plan.rank.end() ? rb->second : 0;
                          return va < vb;
                      });
            GraphSources gs;
            gs.layers.reserve(ls.size());
            for (auto& l : ls) {
                // Carry each layer's load-order identity so the merge keys node identity by scope.
                CB::engine::common::LayerSource lsrc;
                lsrc.source = l.source;
                lsrc.stem   = l.stem;
                if (const auto mit = plan.masterTable.find(l.stem); mit != plan.masterTable.end())
                    lsrc.masterTable = mit->second;
                gs.layers.push_back(std::move(lsrc));
            }
            gs.isCharacter = !ls.empty() && ls.front().isChar;
            const std::size_t n = gs.layers.size();
            m_sources.emplace(key, std::move(gs));
            LOG_INFO("Resolver: watching '{}'  <-  {} layer(s)", key, n);
        }
        LOG_INFO("Resolver: {} .hky plugin(s), {} graph(s) mapped ({} packed archive(s)).",
                 plugins, m_sources.size(), m_archives.size());

        // Mod-declared symbols (events/variables) in BDI format — unioned into each
        // graph's data at compile time. One shared config dir; MO2 merges every
        // mod's *_BDI.json into it, so a single scan gathers the whole load order.
        m_symbols.Load({ dataDir / "SKSE" / "Plugins" / "BehaviorDataInjector" });
        LOG_INFO("Resolver: {} mod symbol declaration(s) loaded.", m_symbols.DeclCount());

        if (!m_characterAnimNames.empty()) {
            std::size_t total = 0;
            for (const auto& [k, v] : m_characterAnimNames) total += v.size();
            LOG_INFO("Resolver: {} character-roster addition list(s) ingested ({} name(s) total) via animationnames\\.",
                     m_characterAnimNames.size(), total);
        }
    }

    void Resolver::WaitReady() const
    {
        if (m_ready.load(std::memory_order_acquire)) return;
        std::unique_lock<std::mutex> lk(m_initMutex);
        m_initCv.wait(lk, [this] { return m_ready.load(std::memory_order_acquire); });
    }

    void Resolver::CompileAll(const std::function<void(std::size_t, std::size_t)>& progress)
    {
        // m_sources is immutable after Init(); snapshot its keys, then warm each through
        // Resolve() (which locks + caches). A graph already in the cache is near-free, so
        // this is safe to run even if a few live loads beat it to some graphs.
        std::vector<std::string> keys;
        keys.reserve(m_sources.size());
        for (const auto& [k, _] : m_sources) keys.push_back(k);

        const std::size_t total = keys.size();
        std::size_t       done  = 0;
        for (const auto& k : keys) {
            CB_ZONE(zone, "warm-up: graph");
            CB_ZONE_TEXT(zone, k);
            if (!m_reuse.count(k)) Resolve(k);   // a reused graph's cached file stays as it is
            if (progress) progress(++done, total);
        }
    }

    void Resolver::ComputeGraphKeys(const std::filesystem::path& dataRoot)
    {
        using CB::core::resolve::Fnv64;
        m_graphKeys.clear();
        /// @note Inputs shared by every graph: the compiler / schema / settings / BDI seed, and the OWNED-GRAPH SET —
        ///       QualifyChildRef rewrites a child reference only when CB serves that child.
        std::vector<std::string> owned;
        owned.reserve(m_sources.size());
        for (const auto& [k, gs] : m_sources) owned.push_back(k + (gs.isCharacter ? "\tC" : "\tB"));
        std::sort(owned.begin(), owned.end());
        std::uint64_t seed = CB::core::resolve::SharedGraphInputs(dataRoot);
        for (const auto& o : owned) seed = Fnv64(o + '\n', seed);

        for (const auto& [key, gs] : m_sources) {
            std::string in;
            for (const auto& layer : gs.layers) {
                in += "layer\t" + layer.stem + '\n';
                for (const auto& m : layer.masterTable) in += "master\t" + m + '\n';
                for (const auto& s : layer.source->stamps())
                    in += s.rel + '\t' + std::to_string(s.size) + '\t' + std::to_string(s.stamp) + '\n';
            }
            if (const RigBones rb = BonesFor(key); rb.table) {
                in += "rig\t" + rb.rig + (rb.ambiguous ? "\t1\n" : "\t0\n");
                for (const auto& n : rb.table->names) in += n + '\n';
            }
            if (gs.isCharacter)
                if (const auto it = m_characterAnimNames.find(PathKey(fs::path(key).stem().string())); it != m_characterAnimNames.end())
                    for (const auto& n : it->second) in += "roster\t" + n + '\n';
            m_graphKeys[key] = Fnv64(in, Fnv64(key, seed));
        }
    }

    std::size_t Resolver::PlanIncremental(const std::filesystem::path& dataRoot)
    {
        const auto t0 = std::chrono::steady_clock::now();
        ComputeGraphKeys(dataRoot);
        m_reuse.clear();
        /// @note The prior sentinel's per-graph keys. A sentinel exists only after a COMPLETE materialize, and is removed
        ///       before any cache file is rewritten, so a stored key always describes the file on disk.
        const auto text = disk::ReadFile(CacheSentinelPath(dataRoot));
        const auto prior = text ? CB::core::resolve::StoredOutputKeys(*text, "graph")
                                : std::unordered_map<std::string, std::string>{};
        for (const auto& [key, k] : m_graphKeys) {
            const auto it = prior.find(key);
            if (it == prior.end() || it->second != CB::core::resolve::KeyHex(k)) continue;
            std::error_code ec;
            if (const std::string rel = CacheDiskRel(key); !rel.empty() && fs::exists(dataRoot / fs::path(rel), ec))
                m_reuse.insert(key);
        }
        const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        LOG_INFO("Community Behaviors: incremental compile — {} of {} graph(s) unchanged and reused, {} to compile "
                 "(keys in {:.0f} ms).", m_reuse.size(), m_graphKeys.size(), m_graphKeys.size() - m_reuse.size(), ms);
        return m_reuse.size();
    }

    namespace {
        /// @brief Everything the adsf projection takes from one behavior's clip generators, folded into one value.
        std::uint64_t ClipHash(const std::vector<CB::engine::animdata::DeriveClipInput>& clips)
        {
            std::string s;
            const auto num = [&s](double v) { s.append(reinterpret_cast<const char*>(&v), sizeof v); };
            for (const auto& c : clips) {
                s += c.name + '\t' + c.animationName + '\t';
                num(c.playbackSpeed); num(c.cropStart); num(c.cropEnd);
                for (const auto& t : c.triggers) {
                    s += t.event + '\t';
                    num(t.localTime);
                    s += t.relativeToEndOfClip ? '1' : '0';
                    s += t.fromAnnotation ? '1' : '0';
                }
                s += '\n';
                if (c.graphEvents) {
                    std::vector<std::string> ev(c.graphEvents->begin(), c.graphEvents->end());
                    std::sort(ev.begin(), ev.end());
                    for (const auto& e : ev) s += e + '\t';
                }
                s += '\n';
            }
            return CB::core::resolve::Fnv64(s);
        }
    }  // namespace

    void Resolver::TraceClips(const std::string& key, const std::vector<CB::engine::animdata::DeriveClipInput>& clips)
    {
        const std::uint64_t h = ClipHash(clips);
        std::lock_guard<std::mutex> lk(m_clipTraceMutex);
        m_clipTrace[NormalizeKey(key)] = h;
    }

    std::uint64_t Resolver::AnimDataKey(const std::filesystem::path& dataRoot)
    {
        if (m_animDataKey) return *m_animDataKey;
        /// @note The adsf / asdsf read every bundle file directly (animations, setdata, rosters, character units, the
        ///       index) EXCEPT the behavior graphs, which reach them only as clip inputs (the clip trace).
        std::uint64_t h = CB::core::resolve::BundleFilesInput(dataRoot, [this](const std::string& unitKey) {
            const auto it = m_sources.find(unitKey);
            return it != m_sources.end() && !it->second.isCharacter;
        });
        auto units = BehaviorUnitKeys();
        std::sort(units.begin(), units.end());
        for (const auto& u : units) h = CB::core::resolve::Fnv64(u + '\n', h);
        m_animDataKey = h;
        return h;
    }

    bool Resolver::AnimDataReusable(const std::filesystem::path& dataRoot)
    {
        const auto t0  = std::chrono::steady_clock::now();
        const auto why = [&]() -> std::string {
            const auto text = disk::ReadFile(CacheSentinelPath(dataRoot));
            if (!text) return "no previous cache";
            const auto stored = CB::core::resolve::StoredOutputKeys(*text, "animdata");
            const auto it     = stored.find("adsf+asdsf");
            if (it == stored.end()) return "no previous animation-data key";
            if (it->second != CB::core::resolve::KeyHex(AnimDataKey(dataRoot))) return "a bundle file they read changed";

            /// @note Early cutoff: a traced behavior whose graph is unchanged keeps its clip hash; a changed one is
            ///       re-merged, and only clips that DIFFER redo the adsf.
            std::unordered_map<std::string, std::uint64_t> trace;
            for (const auto& [key, value] : CB::core::resolve::StoredOutputKeys(*text, "clips")) {
                const auto slash = value.find('/');
                const auto src   = m_sources.find(key);
                const auto gk    = m_graphKeys.find(key);
                if (slash == std::string::npos || src == m_sources.end() || src->second.isCharacter || gk == m_graphKeys.end())
                    return "behavior '" + key + "' is gone";
                const std::uint64_t clip = std::strtoull(value.c_str() + slash + 1, nullptr, 16);
                if (value.substr(0, slash) == CB::core::resolve::KeyHex(gk->second)) { trace[key] = clip; continue; }
                const std::uint64_t now = ClipHash(MergedClipInputs(key));
                if (now != clip) return "the clips of '" + key + "' changed";
                trace[key] = now;
            }
            std::error_code ec;
            if (!fs::exists(dataRoot / CB::core::discover::kConsolidatedCacheDir / "animationdatasinglefile.txt", ec))
                return "the cached animationdatasinglefile.txt is missing";
            CB::core::resolve::CarryReads(*text);
            std::lock_guard<std::mutex> lk(m_clipTraceMutex);
            m_clipTrace = std::move(trace);
            return {};
        }();
        const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        if (!why.empty()) {
            LOG_INFO("Community Behaviors: animation data rebuilt — {} ({:.0f} ms to check).", why, ms);
            return false;
        }
        LOG_INFO("Community Behaviors: animation data unchanged — cached adsf/asdsf kept ({:.0f} ms to check).", ms);
        return true;
    }

    std::size_t Resolver::WriteSkeletonServe(const std::filesystem::path& dataRoot) const
    {
        namespace fs = std::filesystem;
        std::size_t written = 0;
        for (const auto& [key, bytes] : m_skeletonServe) {
            if (bytes.empty()) continue;
            const std::string rel = CB::core::discover::CacheDiskRel(key);
            if (rel.empty()) { LOG_WARN("Community Behaviors: skeleton key '{}' has unexpected shape — skipped.", key); continue; }
            const fs::path out = dataRoot / fs::path(rel);
            if (std::string err; disk::WriteFile(out, bytes, { .atomic = true }, &err)) {
                ++written;
                LOG_INFO("Community Behaviors: wrote compiled skeleton -> '{}'.", out.string());
            } else
                LOG_ERROR("Community Behaviors: failed writing compiled skeleton: {}", err);
        }
        return written;
    }

    namespace {
        using CB::core::resolve::Fnv64;

        std::uint64_t Fnv64(const std::vector<std::uint8_t>& bytes)
        {
            return Fnv64(std::string_view(reinterpret_cast<const char*>(bytes.data()), bytes.size()));
        }

        /// @return the file's fingerprint, or nullopt when it can't be read (absent / locked).
        std::optional<std::uint64_t> FileFnv64(const std::filesystem::path& p)
        {
            const auto bytes = disk::ReadFile(p);
            if (!bytes) return std::nullopt;
            return Fnv64(*bytes);
        }

        /// @brief One animation CB compiled to Data\meshes: its original-case data path, the key of what it compiled
        ///        from (its source file's stamp + the rig bones its tracks bind), and the fingerprint of the bytes it wrote.
        struct AnimLedgerEntry {
            std::string   path;
            std::uint64_t src = 0;
            std::uint64_t out = 0;
        };

        /// @brief The ledger of compiled animations — what CB wrote into the real meshes\ tree, so a removed or
        ///        opted-out bundle's output can be found again and deleted.
        /// @note `current` is false when the header's compiler identity differs from this build's: the entries still
        ///       identify CB-written files (orphan cleanup), but none may be skipped as up to date.
        struct AnimLedger {
            std::unordered_map<std::string, AnimLedgerEntry> byKey;   ///< PathKey(path), internal-only key
            bool                                              current = false;
        };

        constexpr const char* kAnimLedgerMagic = "cb-anim-ledger";

        /// @brief The ledger's first line: its format and the compiler that wrote it (CacheKey's CompilerIdentity — the
        ///        DLL + schema), so any rebuild recompiles every ledgered animation once.
        std::string LedgerHeader(const std::string& compiler)
        {
            return std::string(kAnimLedgerMagic) + " 3 " + compiler;
        }

        AnimLedger LoadAnimLedger(const std::filesystem::path& file, const std::string& header)
        {
            AnimLedger led;
            const auto text = disk::ReadFile(file);
            if (!text) return led;
            const auto lines = SplitLines(*text);
            if (lines.empty()) return led;
            if (lines[0].rfind(kAnimLedgerMagic, 0) != 0) return led;   // not a ledger — track nothing
            led.current = (lines[0] == header);
            for (std::size_t i = 1; i < lines.size(); ++i) {
                const std::string_view line = lines[i];
                const auto t1 = line.find('\t');
                const auto t2 = t1 == std::string_view::npos ? std::string_view::npos : line.find('\t', t1 + 1);
                if (t2 == std::string_view::npos) continue;
                AnimLedgerEntry e;
                e.src  = std::strtoull(std::string(line.substr(0, t1)).c_str(), nullptr, 16);
                e.out  = std::strtoull(std::string(line.substr(t1 + 1, t2 - t1 - 1)).c_str(), nullptr, 16);
                e.path = line.substr(t2 + 1);
                if (!e.path.empty()) led.byKey[PathKey(e.path)] = std::move(e);
            }
            return led;
        }

        /// @brief Replace the ledger (write-then-rename, so a crash never leaves a half-written one). An empty
        ///        ledger removes the file.
        void SaveAnimLedger(const std::filesystem::path& file, const std::string& header,
                            const std::vector<AnimLedgerEntry>& entries)
        {
            namespace fs = std::filesystem;
            std::error_code ec;
            if (entries.empty()) { fs::remove(file, ec); return; }
            std::string out = header + "\n";
            char hx[2][17];
            for (const auto& e : entries) {
                std::snprintf(hx[0], sizeof hx[0], "%016llx", static_cast<unsigned long long>(e.src));
                std::snprintf(hx[1], sizeof hx[1], "%016llx", static_cast<unsigned long long>(e.out));
                out.append(hx[0]).append(1, '\t').append(hx[1]).append(1, '\t').append(e.path).append(1, '\n');
            }
            if (std::string err; !disk::WriteFile(file, out, { .atomic = true }, &err))
                LOG_WARN("Community Behaviors: could not write the animation ledger: {}", err);
        }
    }  // namespace

    std::size_t Resolver::WriteNativeAnimations(const std::filesystem::path& dataRoot,
                                                const AnimExecutor* exec,
                                                const std::function<void()>* onUnit) const
    {
        namespace fs = std::filesystem;
        std::atomic<std::size_t> written{ 0 }, failed{ 0 };

        // Pre-warm the shared schema registry ON THIS (single) THREAD before any fan-out. SharedRegistry()'s
        // one-time load is guarded by a plain `state` int, NOT synchronized — two worker threads racing the
        // first call could both attempt the load. After this call state is settled and every worker only
        // READS it (a pure pointer return). On the serial path this is a harmless no-op — the cold path's
        // CompileAll already warmed it; the WARM path (ArmCacheFromDisk) does NOT run CompileAll, so this is
        // the call that warms it there. Do it regardless of exec so both paths are covered.
        (void)CB::engine::schema::SharedRegistry();

        /// @note The ledger (community_behaviors_cache\animations.ledger) records every animation CB wrote into
        ///       the real meshes\ tree. An entry whose source (its file's stamp in the bundle index, and the rig bones
        ///       its tracks bind), output file and compiler are all unchanged is skipped — no recompile, and its YAML is
        ///       never read; an entry no longer produced — its bundle removed or opted out — is deleted, but only while
        ///       the file still holds the bytes CB wrote.
        const fs::path    ledgerPath = dataRoot / CB::core::discover::kConsolidatedCacheDir / "animations.ledger";
        const std::string header     = LedgerHeader(CB::core::resolve::CompilerIdentity(dataRoot));
        const AnimLedger  prior      = LoadAnimLedger(ledgerPath, header);
        const std::size_t n          = m_nativeAnims.size();
        std::vector<std::uint64_t> srcHash(n, 0), outHash(n, 0);
        std::vector<char>          produced(n, 0);   ///< slot i written by exactly one task — no lock needed
        std::size_t                unchanged = 0;
        for (std::size_t i = 0; i < n; ++i) {
            const NativeAnim& a      = m_nativeAnims[i];
            const std::string& outKey = a.outKey;
            std::string        in     = std::to_string(a.size) + '\t' + std::to_string(a.stamp) + '\n';
            if (const RigBones rb = BonesFor(outKey); rb.table) {
                in += "rig\t" + rb.rig + '\n';
                for (const auto& b : rb.table->names) in += b + '\n';
            }
            srcHash[i] = Fnv64(in);
            if (!prior.current) continue;
            const auto it = prior.byKey.find(PathKey(outKey));
            if (it == prior.byKey.end() || it->second.src != srcHash[i]) continue;
            if (const auto onDisk = FileFnv64(dataRoot / fs::path(outKey)); onDisk && *onDisk == it->second.out) {
                outHash[i]  = *onDisk;
                produced[i] = 2;   // up to date — skipped
                ++unchanged;
            }
        }

        // Compile+write ONE native animation. A pure function of its own source file and the
        // immutable-after-Init m_skeletons; writes its own distinct file and its own result slot. No shared
        // mutable state (archive reads are thread-safe), so this is safe to run concurrently and the parallel
        // result is byte-identical to the serial one.
        auto compileOne = [&](std::size_t i) {
            CB_ZONE(zone, "warm-up: animation");
            CB_ZONE_TEXT(zone, m_nativeAnims[i].outKey);
            if (produced[i]) return;
            const NativeAnim&  a      = m_nativeAnims[i];
            const std::string& outKey = a.outKey;
            // outKey: "meshes/actors/<actor>/animations/.../<name>.hkx" (original case). We only reach here
            // for a bundle that OPTED INTO animation compile (manifest compile_animations=true), so writing
            // to the REAL meshes path is the point — the compiled animation serves. The old meshes/CBanims/
            // redirect was a harness so the (then-always-on) recompile couldn't clobber vanilla loose/BSA
            // animations during validation; the per-bundle compile gate makes it unnecessary — a bundle that
            // doesn't opt in isn't compiled at all, so there is nothing to clobber.
            const fs::path out = dataRoot / fs::path(outKey);
            try {
                const auto yamlText = a.arc ? a.arc->file(PathKey(outKey)) : disk::ReadFile(a.file);
                if (!yamlText) {
                    failed.fetch_add(1, std::memory_order_relaxed);
                    LOG_ERROR("Community Behaviors: native animation '{}' could not be read.", outKey);
                    return;
                }
                auto def = CB::engine::anim::AnimationYamlLoader::LoadFromString(*yamlText, outKey);
                /// @note Called for its side effect: a folded root has its actor motion extracted and STRIPPED, so the
                ///       binary compiled below serves without the travel the adsf moves the actor by. The motion
                ///       itself reaches the adsf through the projection (AnimationDataServer), not from here.
                (void)CB::engine::anim::ResolveRootMotion(def);
                // INVERSE MEMBRANE: resolve this clip's per-track bone references through the rig of the
                // characters that play it (BonesFor — the same lookup the graph compile uses), so its
                // hkaAnimationBinding.transformTrackToBoneIndices follows the actor's bones by name.
                const std::vector<std::string>* boneNames = nullptr;
                if (const RigBones rb = BonesFor(outKey); rb.table) {
                    boneNames = &rb.table->names;
                    if (rb.ambiguous)
                        LOG_WARN("Resolver: animation '{}' binds rigs that disagree on bone order — tracks resolve "
                                 "against '{}'.", outKey, rb.rig);
                }
                const auto r   = CB::engine::anim::CompileAnimation(def, 30, CB::engine::codec::HKXHeader::SkyrimSE(), boneNames);
                if (!r.ok) {
                    failed.fetch_add(1, std::memory_order_relaxed);
                    LOG_ERROR("Community Behaviors: native animation compile FAILED '{}': {}", outKey, r.error);
                    return;
                }
                if (std::string werr; disk::WriteFile(out, r.bytes, {}, &werr)) {
                    outHash[i]  = Fnv64(r.bytes);
                    produced[i] = 1;
                    written.fetch_add(1, std::memory_order_relaxed);
                    LOG_INFO("Community Behaviors: compiled native animation -> '{}'.", out.string());
                } else {
                    failed.fetch_add(1, std::memory_order_relaxed);
                    LOG_ERROR("Community Behaviors: failed writing native animation: {}", werr);
                }
            } catch (const std::exception& e) {
                failed.fetch_add(1, std::memory_order_relaxed);
                LOG_ERROR("Community Behaviors: native animation '{}' — {}", outKey, e.what());
            }
        };

        // Progress tick — fire once per unit as it FINISHES, whatever the outcome (ok/skip/fail), so
        // the bar advances by unit processed, not unit succeeded. Ticked in the dispatch (not inside
        // compileOne, which has early returns) so both paths cover every outcome exactly once.
        auto tick = [&] { if (onUnit && *onUnit) (*onUnit)(); };

        if (exec && *exec && m_nativeAnims.size() > 1) {
            // Parallel: one task per entry, fanned across the pool; (*exec) blocks until all complete.
            // Each task captures its INDEX (m_nativeAnims is immutable after Init; slot i is written only by
            // task i). The tick runs on the worker thread, so onUnit must be thread-safe (it is — atomic
            // increment + store).
            std::vector<std::function<void()>> tasks;
            tasks.reserve(n);
            for (std::size_t i = 0; i < n; ++i)
                tasks.push_back([&compileOne, &tick, i] { compileOne(i); tick(); });
            (*exec)(std::move(tasks));
        } else {
            for (std::size_t i = 0; i < n; ++i) { compileOne(i); tick(); }
        }

        // Next ledger: everything produced this pass, plus any prior entry whose recompile FAILED this pass
        // (its old file is still CB's and still on disk — keep tracking it so a later opt-out removes it).
        std::vector<AnimLedgerEntry> next;
        std::unordered_set<std::string> nowKeys;
        for (std::size_t i = 0; i < n; ++i) {
            const std::string key = PathKey(m_nativeAnims[i].outKey);
            nowKeys.insert(key);
            if (produced[i]) next.push_back({ m_nativeAnims[i].outKey, srcHash[i], outHash[i] });
            else if (const auto it = prior.byKey.find(key); it != prior.byKey.end()) next.push_back(it->second);
        }

        // Orphans: animations CB wrote on an earlier run that nothing produces now. Delete only while the file
        // still holds exactly the bytes CB wrote — anything replaced since (by the user or another mod) is not
        // CB's to remove.
        std::size_t removed = 0;
        for (const auto& [key, e] : prior.byKey) {
            if (nowKeys.count(key)) continue;
            const fs::path p      = dataRoot / fs::path(e.path);
            const auto     onDisk = FileFnv64(p);
            if (!onDisk) continue;   // already gone
            if (*onDisk != e.out) {
                LOG_WARN("Community Behaviors: '{}' was compiled by CB but has changed since — left in place.", p.string());
                continue;
            }
            std::error_code rec;
            if (fs::remove(p, rec)) { ++removed; LOG_INFO("Community Behaviors: removed orphaned compiled animation '{}'.", p.string()); }
            else LOG_WARN("Community Behaviors: could not remove orphaned compiled animation '{}': {}", p.string(), rec.message());
        }
        SaveAnimLedger(ledgerPath, header, next);

        const std::size_t w = written.load(std::memory_order_relaxed);
        const std::size_t fl = failed.load(std::memory_order_relaxed);
        if (w || fl || unchanged || removed)
            LOG_INFO("Community Behaviors: native animations — {} compiled, {} unchanged, {} failed, {} orphan(s) removed "
                     "(compiled to the real meshes path — opt-in bundles only, manifest compile_animations=true).",
                     w, unchanged, fl, removed);
        return w;
    }

    std::size_t Resolver::MaterializeCacheToDisk(
        const std::filesystem::path&                                        dataRoot,
        const std::function<void(std::size_t, std::size_t)>&                progress,
        const AnimExecutor*                                                 animExec,
        const std::function<void()>*                                        animOnUnit)
    {
        namespace fs = std::filesystem;
        std::error_code ec;

        // The above-OAR serving layout writes BR output into the consolidated cache under
        // Data\Meshes\community_behaviors_cache\<serve path> — compiled graphs here, and the
        // synthesized project on demand in ProjectRedirect (same cache root, vanilla path/case).
        // All are NEW paths under community_behaviors_cache\, so no vanilla file is overwritten; the
        // ByteServe hook swaps each owned OPEN to its cache twin under the VANILLA identity (no
        // rename, no ".br" alias). The project's cache-qualified child refs resolve into
        // community_behaviors_cache\ and the untouched refs resolve to vanilla.

        // 0) Drop the completion sentinel FIRST: it is re-written only at the very end, on
        //    success, so a regenerate that crashes mid-way leaves NO sentinel and the next run
        //    rebuilds rather than reusing a half-cleared/half-written cache.
        fs::remove(CacheSentinelPath(dataRoot), ec);

        // 1) Clear the prior session's COMPILED-GRAPH files from the consolidated cache
        //    (Data\community_behaviors_cache\). The byte hook trusts anything on disk, so stale graph bytes must
        //    never survive a load-order change. SURGICAL: the animdata/setdata .txt caches were written to this
        //    same root EARLIER (before this warm-up) and MUST survive, as do the animation ledger and the
        //    sentinel — and every graph PlanIncremental REUSES keeps its file. Everything else goes: graphs
        //    being recompiled, graphs no longer owned, skeletons (rewritten below) and synthesized projects
        //    (rewritten on demand).
        if (m_graphKeys.empty()) ComputeGraphKeys(dataRoot);
        {
            const fs::path cacheRoot = dataRoot / CB::core::discover::kConsolidatedCacheDir;
            /// @note Compared by PATH IDENTITY (PathKey): the cache files are opened through the engine's
            ///       case-insensitive resource lookup, and a file an older build wrote in another case is the same file.
            std::unordered_set<std::string> keep;
            for (const auto& k : m_reuse)
                keep.insert(PathKey(fs::path(CacheDiskRel(k)).lexically_normal().generic_string()));
            std::vector<fs::path> doomed;
            if (fs::is_directory(cacheRoot, ec))
                for (fs::recursive_directory_iterator di(cacheRoot, ec), de; !ec && di != de; di.increment(ec)) {
                    std::error_code fec;
                    if (!di->is_regular_file(fec)) continue;
                    if (di.depth() == 0) {
                        const std::string fn = PathKey(di->path().filename().string());
                        if (fn == "animationdatasinglefile.txt" || fn == "animationsetdatasinglefile.txt" ||
                            fn == "animations.ledger" || fn == "cache.ready")
                            continue;
                    }
                    if (!keep.count(PathKey(di->path().lexically_relative(dataRoot).lexically_normal().generic_string())))
                        doomed.push_back(di->path());
                }
            std::size_t removed = 0;
            for (const auto& p : doomed) {
                std::error_code rec;
                if (fs::remove(p, rec)) ++removed;
            }
            if (removed || !m_reuse.empty())
                LOG_INFO("Community Behaviors: cache — kept {} unchanged graph file(s), removed {} file(s).",
                         m_reuse.size(), removed);
        }

        // 1a) One-time MIGRATION sweep of the OLD SPLIT cache layout, so a machine upgrading from a
        //     prior BR build isn't left with stale, now-unused trees: the compiled HKX under
        //     Meshes\community_behaviors_cache\, the animdata/setdata caches under community_behaviors\cache\, and
        //     the legacy sentinel community_behaviors\community_behaviors_cache.ready. (community_behaviors\plugins\ — the
        //     INPUT bundles — is NOT touched.)
        fs::remove_all(dataRoot / "Meshes" / CB::core::discover::kConsolidatedCacheDir, ec);
        fs::remove_all(dataRoot / "community_behaviors" / "cache", ec);
        fs::remove(dataRoot / "community_behaviors" / "community_behaviors_cache.ready", ec);

        // 1b) One-time MIGRATION sweep: remove leftovers from the OLD serving layout (per-actor
        //     community_behaviors_cache\ subtrees + synthesized *.br.hkx projects under each folderRoot)
        //     so a machine upgrading from a prior BR build isn't left with stale, now-unused files.
        std::unordered_set<std::string> folderRoots;
        for (const auto& [k, _] : m_sources)
            if (const std::string root = FolderRootOf(k); !root.empty()) folderRoots.insert(root);
        for (const auto& root : folderRoots) {
            fs::remove_all(dataRoot / fs::path(root) / "community_behaviors_cache", ec);
            const fs::path rootAbs = dataRoot / fs::path(root);
            if (fs::is_directory(rootAbs, ec))
                for (fs::directory_iterator di(rootAbs, ec), de; !ec && di != de; di.increment(ec)) {
                    std::error_code fec;
                    if (!di->is_regular_file(fec)) continue;
                    if (HasExt(di->path().filename().string(), ".br.hkx"))
                        fs::remove(di->path(), fec);
                }
        }

        std::vector<std::string> keys;
        keys.reserve(m_sources.size());
        for (const auto& [k, _] : m_sources) keys.push_back(k);

        // 2) Materialize each compiled (ref-qualified) graph into the split cache layout.
        const std::size_t total = keys.size();
        std::size_t       done = 0, written = 0, failedGraphs = 0;
        // Track whether EVERY graph compiled AND wrote. The completion sentinel (step 4) is gated on
        // this: a partial cache must NOT be stamped complete, or CachePresent() would trust it forever
        // and later runs would ArmCacheFromDisk (no recompile) with the missing graphs still broken —
        // and the serve hook would redirect those owned-but-missing opens to nonexistent files.
        for (const auto& k : keys) {
            if (m_reuse.count(k)) {   // unchanged since its file was written — kept as it is
                if (progress) progress(++done, total);
                continue;
            }
            CB_ZONE(zone, "warm-up: write graph");
            CB_ZONE_TEXT(zone, k);
            auto bytes = Resolve(k);   // Resolve = cached after CompileAll
            if (bytes && !bytes->empty()) {
                const std::string rel = CacheDiskRel(k);
                if (!rel.empty()) {
                    const fs::path out = dataRoot / fs::path(rel);
                    if (std::string err; disk::WriteFile(out, *bytes, { .atomic = true }, &err)) ++written;
                    else { LOG_ERROR("Community Behaviors: failed writing cache file: {}", err); ++failedGraphs; }
                } else {
                    LOG_WARN("Community Behaviors: cannot place '{}' in the split cache (unexpected key shape) — skipped.", k);
                    ++failedGraphs;
                }
            } else {
                // No compiled bytes (compile failed / fell through). Leave the cache incomplete so the
                // sentinel is withheld (next launch recompiles) and the serve hook's HasCacheFile() gate
                // falls this key through to vanilla rather than to a file that was never written.
                LOG_ERROR("Community Behaviors: graph '{}' produced no compiled bytes — cache left incomplete.", k);
                ++failedGraphs;
            }
            if (progress) progress(++done, total);
        }

        // 2b) Write the opt-in compiled skeletons (Stage D) to the same consolidated cache.
        written += WriteSkeletonServe(dataRoot);

        // 3) Build the above-OAR redirect map (folderRoot -> owned characters) + mark ready.
        //    Shared with the reuse path (ArmCacheFromDisk) — it depends only on m_sources, not
        //    on the bytes just written, so a run that reuses an existing on-disk cache arms the
        //    exact same redirect without recompiling.
        BuildRedirectMap(dataRoot);

        /// @note 3b) The opt-in native animations compile AFTER the graphs are armed: nothing in graph serving
        ///       waits on them (they're loose .hkx under Data\meshes\, found by the engine's own loose scan, not
        ///       the cache redirect), so their compile no longer holds graph serving back. Unchanged ones are
        ///       skipped and orphans removed via the animation ledger.
        WriteNativeAnimations(dataRoot, animExec, animOnUnit);

        // 4) Completion sentinel — written LAST, and ONLY when every graph compiled + wrote
        //    (failedGraphs == 0). Its presence is what CachePresent() trusts to skip the recompile on a
        //    later run, so a PARTIAL cache must never be stamped complete: leaving it un-sentinelled
        //    forces the next launch to recompile the missing graphs instead of arming a broken cache.
        if (failedGraphs == 0) {
            const fs::path sentinel = CacheSentinelPath(dataRoot);
            /// @note The sentinel holds the CACHE KEY (CacheKey.h): every input this launch compiled from, plus the
            ///       outside files the adsf/setdata builds read — CachePresent() reuses the whole cache only while it
            ///       matches — and each graph's OWN key, which the next recompile's PlanIncremental compares to keep
            ///       the graphs whose inputs didn't change.
            std::vector<std::string> graphLines;
            graphLines.reserve(m_graphKeys.size());
            for (const auto& [k, key] : m_graphKeys) graphLines.push_back("graph\t" + k + '\t' + CB::core::resolve::KeyHex(key) + '\n');
            /// @note The adsf / asdsf key and the clip trace are recorded only when both files are complete this launch
            ///       (kept, or rebuilt without error) — otherwise the next recompile rebuilds them.
            if (m_animDataComplete) {
                std::vector<std::string> traceLines;
                bool                     whole = true;   ///< every traced clip read is tied to a graph key
                {
                    std::lock_guard<std::mutex> lk(m_clipTraceMutex);
                    for (const auto& [k, clip] : m_clipTrace) {
                        const auto gk = m_graphKeys.find(k);
                        if (gk == m_graphKeys.end()) { whole = false; break; }
                        traceLines.push_back("clips\t" + k + '\t' + CB::core::resolve::KeyHex(gk->second) + '/' +
                                             CB::core::resolve::KeyHex(clip) + '\n');
                    }
                }
                if (whole) {
                    graphLines.insert(graphLines.end(), traceLines.begin(), traceLines.end());
                    graphLines.push_back("animdata\tadsf+asdsf\t" + CB::core::resolve::KeyHex(AnimDataKey(dataRoot)) + '\n');
                }
            }
            std::sort(graphLines.begin(), graphLines.end());
            std::string outputs;
            for (const auto& l : graphLines) outputs += l;
            if (std::string err; !disk::WriteFile(sentinel, CB::core::resolve::CacheKeyText(dataRoot, outputs),
                                                  { .atomic = true }, &err))
                LOG_ERROR("Community Behaviors: could not stamp the cache complete ({}) — the next launch will "
                          "recompile.", err);
        } else {
            LOG_WARN("Community Behaviors: cache incomplete ({} graph(s) failed to compile/write) — "
                     "completion sentinel withheld; the next launch will recompile.", failedGraphs);
        }

        LOG_INFO("Community Behaviors: materialized {}/{} graph cache file(s) under '{}\\{}'.",
                 written, total, dataRoot.string(), CB::core::discover::kConsolidatedCacheDir);
        return written;
    }

    void Resolver::BuildRedirectMap(const std::filesystem::path& dataRoot)
    {
        namespace fs = std::filesystem;

        // folderRoot -> [{charStem}] for each owned CHARACTER. The synthesized
        // project files themselves are written ON DEMAND in ProjectRedirect() — their name uses
        // the vanilla PROJECT stem (needed so the animationdata association still matches), which
        // is only known at runtime from the load descriptor, not here (a character's stem differs
        // from its project's, e.g. horse.hkx <- HorseProject.hkx). Capture the data root for
        // those writes.
        m_dataRoot = dataRoot;
        m_projectRedirects.clear();
        std::size_t characters = 0, deferred = 0;
        for (const auto& [k, gs] : m_sources) {
            if (!gs.isCharacter) continue;
            const std::string root = FolderRootOf(k);          // "meshes/actors/character"
            if (root.empty()) continue;

            // ONLY redirect an actor whose behavior tree BR also owns. If BR serves the
            // character but NOT any behavior under this root, redirecting would pair BR's
            // character with a NON-BR (loose / Pandora / HorsePower) behavior that expects a
            // DIFFERENT character — a character↔behavior mismatch that desyncs mounting and
            // T-poses the actor (the horse: HorsePower ships a matched loose character+behavior
            // pair; BR owns neither horse behavior). Defer those to the loose pair untouched.
            bool ownsBehavior = false;
            for (const auto& [k2, _] : m_sources)
                if (k2.rfind(root + "/behaviors/", 0) == 0) { ownsBehavior = true; break; }
            if (!ownsBehavior) {
                LOG_INFO("Community Behaviors: NOT redirecting actor root '{}' — BR owns its character but no "
                         "behavior under it; deferring to the loose character+behavior pair (avoids desync).", root);
                ++deferred;
                continue;
            }

            const std::string charStem = fs::path(k).stem().string();  // "defaultmale" (k is a path key)

            m_projectRedirects[root].push_back({ charStem });
            ++characters;
        }

        {
            std::lock_guard<std::mutex> lk(m_readyMutex);
            m_redirectReady.store(true, std::memory_order_release);
        }
        m_readyCv.notify_all();   // wake any owned actor that loaded early and is waiting
        LOG_INFO("Community Behaviors: above-OAR redirect armed for {} character(s) across {} actor root(s) "
                 "({} actor(s) deferred to loose pair).",
                 characters, m_projectRedirects.size(), deferred);
    }

    std::filesystem::path Resolver::CacheSentinelPath(const std::filesystem::path& dataRoot)
    {
        // Lives INSIDE the consolidated cache dir (Data\community_behaviors_cache\cache.ready). Its NEW path
        // (moved from the legacy community_behaviors\community_behaviors_cache.ready) is itself the upgrade trigger:
        // an older build's sentinel isn't found here, so CachePresent() returns false and BR
        // regenerates into the consolidated Data-root store instead of reusing incompatible stale
        // bytes. Safe against the step-1/step-2 cache wipe because it is removed first and (re)written
        // LAST, only on success (a mid-regen crash leaves no sentinel → next run rebuilds).
        return dataRoot / CB::core::discover::kConsolidatedCacheDir / "cache.ready";
    }

    bool Resolver::CachePresent(const std::filesystem::path& dataRoot) const
    {
        std::error_code ec;
        const fs::path sentinel = CacheSentinelPath(dataRoot);
        if (!std::filesystem::is_regular_file(sentinel, ec)) return false;
        const auto text = disk::ReadFile(sentinel);
        if (!text) return false;
        /// @note Present AND its cache key still matches every input — a changed bundle, load order, setting, schema,
        ///       BDI declaration, recorded outside file or a rebuilt DLL each miss.
        const std::string why = CB::core::resolve::CacheKeyMismatch(*text, dataRoot);
        if (!why.empty()) {
            static std::atomic<bool> logged{ false };
            if (!logged.exchange(true)) LOG_INFO("Community Behaviors: behavior cache is stale — {} — recompiling.", why);
            return false;
        }
        return true;
    }

    void Resolver::ArmCacheFromDisk(const std::filesystem::path& dataRoot, const AnimExecutor* animExec)
    {
        // Reuse path: the compiled graphs + synthesized projects from a prior run are already on
        // disk (guarded by the completion sentinel CachePresent() checked). Arm ONLY the redirect
        // map so ProjectRedirect() serves them — no CompileAll, no clear, no graph re-write. The
        // opt-in compiled skeletons ARE (re)written here: they were compiled fresh in Init (cheap),
        // and the graph-cache clear/reuse doesn't cover them, so this guarantees they're on disk.
        WriteSkeletonServe(dataRoot);
        BuildRedirectMap(dataRoot);
        /// @note Native animations are LOOSE (not in community_behaviors_cache). The ledger skips every one whose
        ///       source and on-disk output are unchanged, so a warm start only compiles what is new, edited, or
        ///       missing (e.g. a user cleared meshes\), and removes what a dropped bundle left behind.
        WriteNativeAnimations(dataRoot, animExec);
        LOG_INFO("Community Behaviors: reusing existing on-disk behavior cache under '{}\\Meshes' "
                 "(recompile skipped — set [Cache] bForceRegenerate=true or delete the cache to rebuild).",
                 dataRoot.string());
    }

    std::vector<CB::engine::animdata::DeriveClipInput> Resolver::ClipInputsOf(
        const std::string& key, const std::vector<CB::engine::common::LayerSource>& layers) const
    {
        auto* reg = CB::engine::schema::SharedRegistry();
        if (!reg) return {};
        const RigBones rb = BonesFor(key);
        CB::engine::decompile::ParsedHky graph;
        std::string err;
        if (!CB::engine::decompile::MergeLayersReflective(layers, *reg, rb.table ? &rb.table->names : nullptr, graph, err)) {
            LOG_WARN("Resolver: clip read of '{}' failed: {}", key, err);
            return {};
        }
        return CB::engine::compile::DeriveClipInputsFromGraph(graph.root);
    }

    std::size_t Resolver::FoldAuthorRoster(const std::string& characterStem, std::vector<std::string>& names) const
    {
        const auto ci = m_characterAnimNames.find(characterStem);
        if (ci == m_characterAnimNames.end()) return 0;
        std::unordered_set<std::string> have(names.begin(), names.end());
        /// @note Names are unioned EXACTLY, as authored. The engine's LoadClips keys slots by lowercase filename
        ///       (codec::resource::ClipSlotKey), so an authored name sharing a slot key with an existing entry would
        ///       overwrite that slot and pad the roster — passed through as written, but warned about.
        std::unordered_set<std::string> slots;
        for (const auto& n : names) slots.insert(CB::engine::codec::resource::ClipSlotKey(n));
        std::size_t folded = 0;
        for (const auto& a : ci->second) {
            if (!have.insert(a).second) continue;
            if (!slots.insert(CB::engine::codec::resource::ClipSlotKey(a)).second)
                LOG_WARN("Resolver: author roster entry '{}' for '{}' shares an animation slot (same file name) with an "
                         "existing entry — the engine binds only the later one.", a, characterStem);
            names.push_back(a);
            ++folded;
        }
        return folded;
    }

    std::vector<CB::engine::animdata::DeriveClipInput> Resolver::MergedClipInputs(const std::string& key) const
    {
        const auto it = m_sources.find(NormalizeKey(key));
        return it == m_sources.end() ? std::vector<CB::engine::animdata::DeriveClipInput>{} : ClipInputsOf(it->first, it->second.layers);
    }

    bool Resolver::MergedGraph(const std::string& key, CB::engine::decompile::ParsedHky& out) const
    {
        auto*      reg = CB::engine::schema::SharedRegistry();
        const auto it  = m_sources.find(NormalizeKey(key));
        if (!reg || it == m_sources.end()) return false;
        const RigBones rb = BonesFor(it->first);
        std::string    err;
        if (!CB::engine::decompile::MergeLayersReflective(it->second.layers, *reg, rb.table ? &rb.table->names : nullptr, out, err)) {
            LOG_WARN("Resolver: merged read of '{}' failed: {}", key, err);
            return false;
        }
        return true;
    }

    std::optional<std::string> Resolver::NativeAnimationText(std::string_view path) const
    {
        const std::string want = PathKey(path);
        for (auto a = m_nativeAnims.rbegin(); a != m_nativeAnims.rend(); ++a) {
            if (PathKey(a->outKey) != want) continue;
            try {
                return a->arc ? a->arc->file(PathKey(a->outKey)) : disk::ReadFile(a->file);
            } catch (const std::exception& e) {
                LOG_WARN("Resolver: native animation '{}' could not be read: {}", a->outKey, e.what());
                return std::nullopt;
            }
        }
        return std::nullopt;
    }

    std::vector<std::string> Resolver::ServedRoster(const std::string& characterKey) const
    {
        auto* reg = CB::engine::schema::SharedRegistry();
        const auto it = m_sources.find(NormalizeKey(characterKey));
        if (!reg || it == m_sources.end()) return {};
        const RigBones rb = BonesFor(it->first);
        CB::engine::decompile::ParsedHky graph;
        std::string err;
        if (!CB::engine::decompile::MergeLayersReflective(it->second.layers, *reg, rb.table ? &rb.table->names : nullptr, graph, err)) {
            LOG_WARN("Resolver: roster read of '{}' failed: {}", it->first, err);
            return {};
        }
        CB::engine::decompile::GraphEdit edit(graph, *reg);
        std::vector<std::string> names;
        bool found = false;
        edit.ForEach("hkbCharacterStringData", [&](CB::engine::codec::io::SchemaObject& so) {
            if (!found) { names = so.FieldRef("animationNames").strs; found = true; }
        });
        FoldAuthorRoster(PathKey(fs::path(it->first).stem().string()), names);
        return names;
    }

    std::vector<std::string> Resolver::BehaviorUnitKeys() const
    {
        std::vector<std::string> out;
        for (const auto& [key, gs] : m_sources) {
            if (gs.isCharacter) continue;
            const fs::path p(key);
            if (p.parent_path().filename().string().rfind("behaviors", 0) == 0) out.push_back(key);
        }
        std::sort(out.begin(), out.end());
        return out;
    }

    Resolver::RigBones Resolver::BonesFor(const std::string& key) const
    {
        const std::vector<std::string>* rigs = m_rigIndex.RigsFor(NormalizeKey(key));
        RigBones out;
        if (!rigs) return out;
        for (const std::string& r : *rigs) {
            const auto t = m_skeletons.find(r);
            if (t == m_skeletons.end()) continue;
            if (!out.table) { out.table = &t->second; out.rig = r; }
            else if (t->second.names != out.table->names) out.ambiguous = true;
        }
        return out;
    }

    std::string Resolver::QualifyChildRef(const std::string& /*graphKey*/, const std::string& childRef) const
    {
        // Byte-substitution model: compiled graphs/characters keep their VANILLA child refs. The
        // ByteServe hook intercepts every typed-hkx open and redirects the ones BR owns to the
        // consolidated community_behaviors_cache\ store — so no compile-time cache-qualification is needed (and
        // the loaded assets keep the vanilla identity the speed-sampler/animdata keys depend on).
        // Kept as a pass-through so the two call sites (character.behavior, behaviorReferences) stay
        // put; both only override when the result differs, so this is now a no-op for them.
        return childRef;
    }

    std::string Resolver::ProjectRedirect(std::string_view vanillaProjectPath)
    {
        if (vanillaProjectPath.empty()) return {};

        // The engine's descriptor path is meshes-RELATIVE (no "meshes\" prefix), e.g.
        // "Actors\Character\DefaultMale.hkx". Fold to the map's key space (meshes-prefixed).
        const std::string norm = NormalizeKey(vanillaProjectPath);         // "actors/character/defaultmale.hkx"
        const std::string projKey = (norm.rfind("meshes/", 0) == 0) ? norm : "meshes/" + norm;
        const auto slash = projKey.find_last_of('/');
        if (slash == std::string::npos) return {};
        const std::string folderRoot = projKey.substr(0, slash);            // "meshes/actors/character"
        std::string projStem = projKey.substr(slash + 1);                   // "defaultmale.hkx"
        if (const auto dot = projStem.find_last_of('.'); dot != std::string::npos) projStem.erase(dot);  // "defaultmale"

        // If warm-up hasn't armed the cache yet, only actors WE OWN wait for it — never stall
        // unowned actors (wolves, chickens) or the main menu's background loads. Ownership is
        // known from m_sources (built in Init, before warm-up): a character unit under this
        // project's folder root. Waiting (vs the old silent vanilla fallback) is what keeps an
        // early save-load from getting un-merged behaviors (TDM lean gone). Bounded so a stuck
        // warm-up degrades to vanilla instead of hanging forever.
        if (!m_redirectReady.load(std::memory_order_acquire)) {
            // Only actors we will actually redirect wait — i.e. those whose behavior tree BR
            // owns (same condition as the redirect-map build). An actor whose behavior BR does
            // not own (e.g. the horse, deferred to its loose pair) passes straight through.
            bool owned = false;
            for (const auto& [k, _] : m_sources)
                if (k.rfind(folderRoot + "/behaviors/", 0) == 0) { owned = true; break; }
            if (!owned) return {};
            std::unique_lock<std::mutex> lk(m_readyMutex);
            m_readyCv.wait_for(lk, std::chrono::seconds(120),
                               [this] { return m_redirectReady.load(std::memory_order_acquire); });
            if (!m_redirectReady.load(std::memory_order_acquire)) return {};  // timed out -> passthrough
        }

        const auto it = m_projectRedirects.find(folderRoot);
        if (it == m_projectRedirects.end() || it->second.empty()) return {};

        // Disambiguate the project -> character link on a multi-character root. The character stem and
        // the PROJECT stem do NOT generally match: Skyrim's convention is "<Name>Project.hkx" for the
        // project vs "<Name>.hkx" for the character (DragonProject <- dragon, DraugrSkeletonProject <-
        // draugrskeleton), so a bare `charStem == projStem` matches ONLY the humanoid case by luck
        // (DefaultMale <- defaultmale) and silently drops every multi-character CREATURE root (draugr =
        // draugr+draugrskeleton, the DLC creatures, …) -> no redirect -> those actors fall to vanilla.
        // Match both forms: the exact stem AND the "<charStem>project" convention (all lowercased already).
        const ProjectRedirectEntry* chosen = nullptr;
        if (it->second.size() == 1) {
            chosen = &it->second.front();
        } else {
            for (const auto& e : it->second)
                if (e.charStem == projStem || projStem == e.charStem + "project") { chosen = &e; break; }
        }
        if (!chosen) return {};

        // Byte-substitution serve: the SWAP path the ByteServe hook hands the engine in place of the
        // vanilla project open — the cache file written below, under the same key. The engine loads BR's
        // synthesized project but interns it under the VANILLA identity (the caller's descriptor is
        // untouched), so the speed-sampler DB key and the animdata table both resolve to the stock stem.
        const std::string swap = CB::core::discover::CacheSwapPath(projKey);

        // Materialize the project once, on demand, into the consolidated cache at
        // Data\Meshes\community_behaviors_cache\<projKey>. It points at the VANILLA character path; the
        // ByteServe hook redirects that character open — and every behavior open below it — into the
        // same cache. Tiny packfile (~KB, no graph compile), safe on the actor-load thread. Memoized + guarded.
        {
            std::lock_guard<std::mutex> lock(m_synthMutex);
            if (!m_synthesizedProjects[swap]) {
                bool ok = false;
                try {
                    /// @note KEEP THE VANILLA IDENTITY, ORIGINAL CASE: the served project is the vanilla one — its
                    ///       characterFilenames string binds the character to its setdata / animdata case-sensitively.
                    ///       From the base project unit; else the loose vanilla file verbatim; else no redirect.
                    std::vector<std::uint8_t> bytes;
                    std::string err;
                    if (const auto pu = m_projectUnits.find(projKey); pu != m_projectUnits.end()) {
                        CB::engine::decompile::ParsedHky project;
                        auto* reg = CB::engine::schema::SharedRegistry();
                        if (!reg || !CB::engine::decompile::ParseHkyReflective(*pu->second, *reg, project, err) ||
                            !CB::engine::decompile::SerializeGraph(project, bytes, err))
                            bytes.clear();
                    }
                    if (bytes.empty()) {
                        std::error_code fec;
                        const fs::path vanProj = fs::current_path(fec) / "Data" / "Meshes" / fs::path(std::string(vanillaProjectPath));
                        if (auto van = disk::ReadBytes(vanProj)) bytes = std::move(*van);
                    }
                    if (!bytes.empty()) {
                        /// @note Written under the exact string the swap names (CacheDiskRel / CacheSwapPath of the
                        ///       same key), so the open is precise on a case-sensitive filesystem.
                        const fs::path out = m_dataRoot / fs::path(CB::core::discover::CacheDiskRel(projKey));
                        std::string werr;
                        ok = disk::WriteFile(out, bytes, { .atomic = true }, &werr);
                        if (ok)
                            LOG_INFO("Community Behaviors: byte-served project '{}' (cache '{}').",
                                     vanillaProjectPath, swap);
                        else
                            LOG_ERROR("Community Behaviors: failed writing byte-served project: {}", werr);
                    } else {
                        LOG_ERROR("Community Behaviors: no project to serve for '{}' (no base project unit{}, no loose "
                                  "vanilla file) — not redirected.", vanillaProjectPath, err.empty() ? "" : ": " + err);
                    }
                } catch (const std::exception& e) {
                    LOG_ERROR("Community Behaviors: project synth threw for '{}': {}", vanillaProjectPath, e.what());
                }
                if (!ok) return {};                 // couldn't materialize → don't swap (safe passthrough)
                m_synthesizedProjects[swap] = true;
            }
        }
        return swap;
    }

    bool Resolver::Owns(std::string_view servePath) const
    {
        // No lock: m_sources / m_skeletonServe are immutable after Init() (Resolve only ever writes
        // m_cache). Concurrent lock-free reads from the background load threads are safe.
        const std::string key = NormalizeKey(servePath);
        return m_sources.find(key) != m_sources.end() ||
               m_skeletonServe.find(key) != m_skeletonServe.end();
    }

    bool Resolver::HasCacheFile(std::string_view servePath) const
    {
        // The on-disk cache file for an owned key is dataRoot / CacheDiskRel(key) — the exact path
        // MaterializeCacheToDisk / WriteSkeletonServe wrote and CacheSwapPath(key) opens. m_dataRoot is set
        // in BuildRedirectMap on both the materialize and reuse paths before RedirectReady() flips, so a set
        // root here means the cache pass has run.
        if (m_dataRoot.empty()) return false;   // not armed yet → treat as absent (hook falls through)
        const std::string key = NormalizeKey(servePath);
        std::error_code   ec;
        return std::filesystem::exists(m_dataRoot / std::filesystem::path(CacheDiskRel(key)), ec);
    }

    void Resolver::ResolveCharacter(const std::string& key, CB::engine::decompile::GraphEdit& edit)
    {
        CB::engine::codec::io::SchemaObject* sd = nullptr;
        edit.ForEach("hkbCharacterStringData", [&](CB::engine::codec::io::SchemaObject& so) { if (!sd) sd = &so; });
        if (!sd) { LOG_WARN("Resolver: character '{}' has no string data — served as merged.", key); return; }
        const std::string cstem = PathKey(fs::path(key).stem().string());

        /// @note The author-facing roster drop (animationnames\<stem>.txt) folds in as an EXACT-case union: Havok binds
        ///       case-sensitively, so a lowercased dedup would drop the exact name a clip binds.
        std::vector<std::string>& names = sd->FieldRef("animationNames").strs;
        if (const std::size_t folded = FoldAuthorRoster(cstem, names))
            LOG_INFO("Resolver: folded {} author roster addition(s) into character '{}'.", folded, key);

        /// @note Cache-qualify the character's main behavior reference when CB serves that behavior, so the
        ///       redirected load reaches CB's compiled graph instead of the vanilla file.
        std::string& behavior = sd->Str("behaviorFilename");
        if (const std::string q = QualifyChildRef(key, behavior); q != behavior) {
            LOG_INFO("Resolver: cache-qualified character '{}' behavior ref '{}' -> '{}'.", key, behavior, q);
            behavior = q;
        }
    }

    std::shared_ptr<const std::vector<std::uint8_t>> Resolver::Resolve(std::string_view servePath)
    {
        const std::string key = NormalizeKey(servePath);

        std::lock_guard<std::mutex> lock(m_mutex);

        if (auto it = m_cache.find(key); it != m_cache.end()) return it->second;

        const auto srcIt = m_sources.find(key);
        if (srcIt == m_sources.end()) {
            m_cache.emplace(key, nullptr);  // not ours — remember
            return nullptr;
        }

        /// @note Base + patches, base first: each layer's FormID patch applies over the merge so far (a later
        ///       layer's write to the same record wins). A layer is a disk dir or a packed-.hky subtree behind the
        ///       same IUnitSource, so the merge is identical either way.
        const GraphSources& gs = srcIt->second;

        std::shared_ptr<const std::vector<std::uint8_t>> result;
        try {
            const auto t0 = std::chrono::steady_clock::now();
            auto* reg = CB::engine::schema::SharedRegistry();
            if (!reg) throw std::runtime_error("schema registry unavailable");

            /// @note Bone names (weight maps, bone refs, bone-keyed rosters) resolve against the rig of the
            ///       characters that run this graph.
            const RigBones rb = BonesFor(key);
            const std::vector<std::string>* bones = rb.table ? &rb.table->names : nullptr;
            if (rb.table && rb.ambiguous)
                LOG_WARN("Resolver: '{}' is shared by characters whose rigs disagree on bone order — "
                         "bone names resolve against '{}'.", key, rb.rig);

            CB::engine::decompile::ParsedHky graph;
            std::string err;
            if (!CB::engine::decompile::MergeLayersReflective(gs.layers, *reg, bones, graph, err)) {
                LOG_ERROR("Community Behaviors: merge FAILED for '{}' — SERVING VANILLA (its mod edits are dropped): {}",
                          key, err);
                m_cache.emplace(key, nullptr);
                return nullptr;
            }
            CB::engine::decompile::GraphEdit edit(graph, *reg);

            if (gs.isCharacter) {
                ResolveCharacter(key, edit);
            } else {
                ResolverFeatureLog fLog;
                CB::feature::FeatureContext fctx{ .graphKey = key, .log = fLog, .symbols = &m_symbols };
                /// @note The ENABLED set is data (WHICH run); ResolveRunOrder derives the ORDER from each feature's
                ///       RunsAfter/RunsBefore. The ER gate stays opt-in ([ERGate] bEnable): a fault there rewrites
                ///       every wildcard and would T-pose every actor.
                std::vector<std::string> enabledIds{ "cb.watermark", "cb.bdi-symbols" };
                if (m_erGateEnabled) enabledIds.emplace_back("engine-relay.wildcard-gate");
                auto& features = CB::feature::FeatureRegistry::Instance();
                for (const auto& [id, r] : features.Run(features.ResolveRunOrder(enabledIds, fLog), edit, fctx))
                    if (r.applied && r.mutations)
                        LOG_INFO("Resolver: feature {} → {} mutation(s) in '{}'.", id, r.mutations, key);

                /// @note Cache-qualify every child behavior reference CB owns, so the recursive above-OAR load pulls
                ///       CB's compiled sub-behaviors while unowned children resolve to their vanilla files.
                std::size_t qualified = 0;
                edit.ForEach("hkbBehaviorReferenceGenerator", [&](CB::engine::codec::io::SchemaObject& so) {
                    std::string& name = so.Str("behaviorName");
                    if (const std::string q = QualifyChildRef(key, name); q != name) { name = q; ++qualified; }
                });
                if (qualified) LOG_INFO("Resolver: cache-qualified {} behavior reference(s) in '{}'.", qualified, key);

                /// @warning hkbBehaviorGraph::nextUniqueID (the runtime node-id minter) is a SIGNED 16-bit counter
                ///          (RE'd): a graph at/over 32767 hkbNodes mints negative ids into Havok's -1 "none" space,
                ///          corrupting the graph and (via the baked set-data count) SAVES. Refuse, serve vanilla.
                constexpr std::size_t kMaxGraphNodes = 0x7FFF;
                if (const std::size_t n = edit.CountDerived("hkbNode"); n >= kMaxGraphNodes) {
                    LOG_ERROR("Community Behaviors: '{}' merges to {} nodes, at/over the engine's {}-node ceiling — "
                              "SERVING VANILLA (a larger graph mints negative node ids and corrupts saves). Trim the "
                              "mods contributing to this graph.", key, n, kMaxGraphNodes);
                    m_cache.emplace(key, nullptr);
                    return nullptr;
                }
            }

            /// @note A patch can point a member at the wrong kind of node (one written against another numbering of a
            ///       shared Nemesis code). The game reads every pointer as its declared class, so such a node crashes it;
            ///       drop each one and name it. The writer leaves the same pointers out regardless.
            for (const std::string& m : edit.DropMistypedPointers())
                LOG_WARN("Resolver: '{}': {} — dropped (the game would read it as the declared class).", key, m);

            std::vector<std::uint8_t> bytes;
            const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
            if (CB::engine::decompile::SerializeGraph(graph, bytes, err)) {
                result = std::make_shared<const std::vector<std::uint8_t>>(std::move(bytes));
                LOG_INFO("Resolver: compiled '{}' — {} layer(s) merged, {} bytes in {:.1f} ms.",
                         key, gs.layers.size(), result->size(), ms);
            } else {
                LOG_ERROR("Community Behaviors: serialize FAILED for '{}' — SERVING VANILLA (its mod edits are dropped): {}",
                          key, err);
            }
        } catch (const std::exception& e) {
            LOG_ERROR("Resolver: exception resolving '{}': {}", key, e.what());
        }

        m_cache.emplace(key, result);
        return result;
    }

}  // namespace CB
