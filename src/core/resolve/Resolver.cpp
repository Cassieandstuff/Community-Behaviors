#include "PCH.h"

#include "core/resolve/Resolver.h"
#include "core/discover/ServeKey.h"
#include "core/resolve/Watermark.h"
#include <feature-sdk/FeatureRegistry.h>   // compile-time graph features (ER wildcard gate, …); features/core/hpp on the path


#include <interface/UnitSource.h>
#include <codec/serialization/HkyArchive.h>
#include <compile/AnimDataFromBehavior.h>   // DeriveClipInputsFromGraph — first-class adsf-derive stage
#include <codec/format/SkeletonYaml.h>       // LoadSkeletonYamlFromTexts (path-local skeleton units)
#include <compile/SkeletonCompiler.h>        // CompileSkeletonFull (skeleton serve)
#include <codec/format/AnimationYamlLoader.h>  // native animation YAML (in a .hky) -> AnimationDef
#include <compile/AnimationCompiler.h>         // CB::core::anim::CompileAnimation (native anim -> loose .hkx)
#include <codec/motion/RootMotion.h>           // ResolveRootMotion — a unit's actor motion (record or folded root)
#include <interface/AnimationData.h>        // animdata::SingleFile / EmitSingleFile (DeriveAnimData)
#include <interface/MasterTable.h>          // CB::core::codec::formid::BuildMasterTable (ordered master table)
#include <codec/format/AnimDataYaml.h>         // AssembleAnimdata / ParseAnimdataIndexYaml / ParseMotionSidecar / StemForProjectName
#include <compile/AnimDataDeriver.h>      // DeriveClipList (sink clip inputs + roster -> ClipGenerators)
#include <interface/reflection/HavokSchema.h>        // schema::SharedRegistry — pre-warm before parallel anim compile
#include "core/discover/BundleReader.h"               // read the shipped vanilla skeleton base from Skyrim.hky

#include <atomic>
#include <map>
#include <optional>
#include <set>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <exception>
#include <fstream>
#include <functional>   // std::function — skeleton-compile tasks fanned across the pool (stage 2)
#include <iterator>
#include <memory>
#include <mutex>        // guard the shard merges into m_skeletonServe / m_skeletons (stage 2)
#include <sstream>
#include <system_error>
#include <unordered_set>

namespace fs = std::filesystem;

namespace CB {

    namespace {

        std::string ToLower(std::string s)
        {
            std::transform(s.begin(), s.end(), s.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            return s;
        }

        /// @brief A reflective unit's kind (its graph.yaml's root variant; a patch names its base's). A unit without
        ///        graph.yaml is a patch that leaves the root untouched — Behavior.
        CB::core::codec::HkyArchive::UnitKind KindOfUnit(const CB::core::common::IUnitSource& src)
        {
            const auto g = src.read("graph.yaml");
            return g ? CB::core::codec::HkyArchive::KindOfGraphYaml(*g) : CB::core::codec::HkyArchive::UnitKind::Behavior;
        }

        /// @brief True when the unit is complete on its own (not a patch over a master's unit).
        bool IsCompleteUnit(const CB::core::common::IUnitSource& src)
        {
            const auto g = src.read("graph.yaml");
            return g && !CB::core::codec::HkyArchive::IsPatchGraphYaml(*g);
        }

        // The actor identity for a graph serve key: everything between "actors/" and the
        // "/behaviors/" (or "/characters/") boundary. Top-level actors are one segment
        // ("bear"); NESTED creatures keep their namespace ("dlc02/netch", "ambient/chicken").
        // Both the bone-name-table PRODUCER (Init) and the behavior CONSUMER (compile) key the
        // per-actor bone table through this, so they agree for nested actors. The old flat
        // first-segment key ("dlc02") collapsed every dlc02/* creature onto one table AND
        // never found their skeleton (no "actors/dlc02/character assets/…"), so their
        // bone-index-by-name arrays failed to compile → broken graph → hkbRagdollDriver CTD
        // on load (BR-20). Returns lowercased '/'-normalized path; empty if not under actors/.
        std::string ActorPathOf(const std::string& serveKey)
        {
            const std::string k = ToLower(serveKey);
            const auto ap = k.find("actors/");
            if (ap == std::string::npos) return {};
            const std::string rest = k.substr(ap + 7);
            for (const char* seg : { "/behaviors/", "/characters/" })
                if (const auto b = rest.find(seg); b != std::string::npos) return rest.substr(0, b);
            return rest.substr(0, rest.find('/'));   // fallback: first segment
        }

        // Decode XML entity references (&amp; &lt; &gt; &quot; &apos;) to their literal
        // characters. Author-drop
        // animationnames\<char>.txt rosters can arrive HTML-escaped (a mod's loose character
        // decompiled through an XML pipeline), so a shared-killmove path like
        // "..\SharedKillMoves\Human&amp;Boar\..." must fold back to "...Human&Boar\..." BEFORE
        // it is deduped against the (literal-'&') base roster. Without this, the escaped copy
        // fails the dedup, is appended as a phantom "addition", and inflates animationNames.
        // Those phantom paths don't exist on disk so they bind to nothing — but OAR sets its
        // synchronized-clip index offset to animationNames.size(), so the padding makes OAR's
        // (uint16) `animationBindingIndex -= offset` UNDERFLOW → out-of-bounds → the rep-stosq
        // char-setup CTD. Keeping the roster literal keeps count == bindings == OAR's offset.
        std::string UnescapeXml(const std::string& s)
        {
            std::string o;
            o.reserve(s.size());
            for (std::size_t i = 0; i < s.size();) {
                if (s[i] == '&') {
                    if (s.compare(i, 5, "&amp;")  == 0) { o += '&';  i += 5; continue; }
                    if (s.compare(i, 4, "&lt;")   == 0) { o += '<';  i += 4; continue; }
                    if (s.compare(i, 4, "&gt;")   == 0) { o += '>';  i += 4; continue; }
                    if (s.compare(i, 6, "&quot;") == 0) { o += '"';  i += 6; continue; }
                    if (s.compare(i, 6, "&apos;") == 0) { o += '\''; i += 6; continue; }
                }
                o += s[i++];
            }
            return o;
        }

        using CB::core::discover::NormalizeKey;
        using CB::core::discover::FolderRootOf;
        using CB::core::discover::CacheDiskRel;
        // (serve-key helpers live in hpp/ServeKey.h — shared with br-servekey-test)

        std::string TrimLine(const std::string& line)
        {
            const auto b = line.find_first_not_of(" \t\r\n");
            if (b == std::string::npos) return {};
            const auto e = line.find_last_not_of(" \t\r\n");
            return line.substr(b, e - b + 1);
        }

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

        // ── Load-order plan: masters drive order, loadorder.txt breaks peer ties ───────
        // The overhaul. Order is no longer the flat loadorder.txt priority; it is a
        // TOPOLOGICAL SORT of the master DAG, so a declared master always compiles base-first
        // (lower rank) than the bundles that build on it — regardless of its line in
        // loadorder.txt. "Skyrim is the base" stops being "Skyrim is line 1" and becomes
        // "Skyrim is the root because everything masters it": every non-Skyrim bundle
        // IMPLICITLY masters "skyrim" (BundleManifest: "Skyrim implicit"), so the implicit
        // edge alone pins the vanilla corpus first no matter how a mod is ordered. loadorder.txt
        // survives only as the PEER tie-break — the order among bundles with no dependency
        // between them (MO2's domain). The relay tree falls out for free: it *is* a master DAG
        // (engine_relay masters Community Behaviors; an ER mod masters engine_relay), so the topo
        // sort layers the relay hubs correctly with no special-casing.
        struct LoadPlan {
            std::unordered_map<std::string, int> rank;     // stem -> base-first position (lower = base)
            std::unordered_set<std::string>      dropped;  // stems excluded (missing explicit master)
            // stem -> its transitive masters (all ancestors in the DAG, incl. implicit skyrim).
            // Lets the conflict report tell an expected OVERRIDE (a bundle editing a node its
            // master introduced) from a namespace CLASH (two unrelated bundles editing one node).
            std::unordered_map<std::string, std::unordered_set<std::string>> ancestors;
            // stem -> its ORDERED master table (index 0 = base game, 1..k = declared masters, last =
            // self). A node's FormId masterIndex resolves against this. Built from the DIRECT declared
            // masters (positional), so it's ordered where `ancestors` (a transitive set) is not.
            std::unordered_map<std::string, std::vector<std::string>> masterTable;
        };

        // `manifests`: every scanned bundle stem -> its manifest (masters declared here).
        // `order`: loadorder.txt line index per listed stem (later line = higher; unlisted = 0),
        //          used ONLY as the peer tie-break. Returns the rank each surviving stem sorts by,
        //          plus the set dropped because a DECLARED master is absent (transitively) — a
        //          dependent whose base isn't installed would bind its overrides to nothing, so
        //          per the "skip the dependent, load the rest" rule it contributes no layers.
        LoadPlan PlanLoadOrder(const std::unordered_map<std::string, BundleManifest>& manifests,
                               const std::unordered_map<std::string, int>&            order)
        {
            LoadPlan plan;
            const bool haveSkyrim = manifests.find("skyrim") != manifests.end();

            // Effective master edges per stem. Declared masters are EXPLICIT (a missing one
            // drops the dependent); "skyrim" is added IMPLICITLY to every other bundle (a
            // missing Skyrim never drops anything — it just means no vanilla base to precede).
            std::unordered_map<std::string, std::vector<std::string>> explicitMasters;
            for (const auto& [stem, mf] : manifests) {
                auto& ems = explicitMasters[stem];
                for (const auto& m : mf.masters) {
                    const std::string mk = ToLower(m);
                    if (mk == stem) continue;                                   // self-reference
                    if (std::find(ems.begin(), ems.end(), mk) == ems.end()) ems.push_back(mk);
                }
            }

            // Drop set: a stem with an ABSENT explicit master is dropped; propagate through
            // explicit edges (a bundle mastering a dropped bundle is itself dropped). Iterate
            // to fixpoint — the bundle count is tiny.
            bool changed = true;
            while (changed) {
                changed = false;
                for (const auto& [stem, ems] : explicitMasters) {
                    if (plan.dropped.count(stem)) continue;
                    for (const std::string& m : ems) {
                        const bool absent  = manifests.find(m) == manifests.end();
                        const bool dropped = plan.dropped.count(m) != 0;
                        if (absent || dropped) {
                            LOG_WARN("Resolver: load order — skipping bundle '{}': declared master '{}' "
                                     "is {} (its overrides would bind to nothing).",
                                     stem, m, absent ? "not installed" : "itself skipped");
                            plan.dropped.insert(stem);
                            changed = true;
                            break;
                        }
                    }
                }
            }

            // Kahn topological sort over the survivors. Edge master -> dependent; a node is
            // ready when all its masters have been emitted. Among ready nodes, emit the peer
            // with the lowest loadorder value first (unlisted = 0 sorts earliest), then by stem
            // for determinism. Skyrim carries no masters -> ready immediately; every other
            // bundle holds the implicit skyrim edge -> can't precede it.
            std::unordered_map<std::string, int>                      indeg;
            std::unordered_map<std::string, std::vector<std::string>> children;
            std::unordered_map<std::string, std::vector<std::string>> parents;   // s -> its direct effective masters
            std::vector<std::string>                                  survivors;
            for (const auto& [stem, mf] : manifests) {
                (void)mf;
                if (plan.dropped.count(stem)) continue;
                survivors.push_back(stem);
                indeg.emplace(stem, 0);
            }

            // Ordered master table per survivor (index 0 = base game, 1..k = declared masters in file
            // order, last = self) — what a node's FormId masterIndex resolves against. explicitMasters
            // is the DIRECT declared set in manifest order (deduped, self-excluded, lowercase).
            for (const std::string& s : survivors)
                plan.masterTable[s] = CB::core::codec::formid::BuildMasterTable(s, explicitMasters[s], haveSkyrim);
            for (const std::string& s : survivors) {
                std::vector<std::string> masters = explicitMasters[s];          // all present (survivors)
                if (haveSkyrim && s != "skyrim" &&
                    std::find(masters.begin(), masters.end(), "skyrim") == masters.end())
                    masters.push_back("skyrim");                                 // implicit base edge
                for (const std::string& m : masters) {
                    if (plan.dropped.count(m) || indeg.find(m) == indeg.end()) continue;  // absent/dropped
                    children[m].push_back(s);
                    parents[s].push_back(m);
                    ++indeg[s];
                }
            }

            auto loadVal = [&](const std::string& s) -> int {                   // peer tie-break key
                const auto it = order.find(s);
                return it != order.end() ? it->second : 0;                      // unlisted = earliest
            };
            auto readyLess = [&](const std::string& a, const std::string& b) {
                const int la = loadVal(a), lb = loadVal(b);
                return la != lb ? la < lb : a < b;
            };

            std::vector<std::string> ready;
            for (const std::string& s : survivors)
                if (indeg[s] == 0) ready.push_back(s);
            std::sort(ready.begin(), ready.end(), readyLess);

            int nextRank = 0;
            while (!ready.empty()) {
                const std::string s = ready.front();
                ready.erase(ready.begin());
                plan.rank[s] = nextRank++;
                auto& anc = plan.ancestors[s];                                   // masters emit first -> complete
                for (const std::string& m : parents[s]) {
                    anc.insert(m);
                    const auto mit = plan.ancestors.find(m);
                    if (mit != plan.ancestors.end()) anc.insert(mit->second.begin(), mit->second.end());
                }
                std::vector<std::string> freed;
                for (const std::string& c : children[s])
                    if (--indeg[c] == 0) freed.push_back(c);
                for (const std::string& c : freed) {                            // stable insert, keep sorted
                    auto pos = std::lower_bound(ready.begin(), ready.end(), c, readyLess);
                    ready.insert(pos, c);
                }
            }

            // Any survivor without a rank sits in a master CYCLE (A masters B masters A).
            // Don't drop — degrade to loadorder order and warn, so a self-referential mess
            // still loads deterministically instead of vanishing.
            if (plan.rank.size() != survivors.size()) {
                std::vector<std::string> cyc;
                for (const std::string& s : survivors)
                    if (!plan.rank.count(s)) cyc.push_back(s);
                std::sort(cyc.begin(), cyc.end(), readyLess);
                std::string names;
                for (const std::string& s : cyc) { if (!names.empty()) names += ", "; names += s; }
                LOG_WARN("Resolver: load order — master cycle among [{}]; falling back to loadorder.txt "
                         "order for them (masters can't resolve a cycle).", names);
                for (const std::string& s : cyc) plan.rank[s] = nextRank++;
            }

            return plan;
        }

    }  // namespace

    void Resolver::Init(const fs::path& dataDir, const fs::path& loadOrderIni, bool warmReuse,
                        const AnimExecutor* skelExec)
    {
        // 1) Load order (optional): .hky plugin name -> priority (higher wins).
        std::unordered_map<std::string, int> order;
        if (std::ifstream f{ loadOrderIni }) {
            std::string line;
            int idx = 0;
            while (std::getline(f, line)) {
                const std::string entry = TrimLine(line);
                if (entry.empty() || entry[0] == '#' || entry[0] == ';') continue;
                order[ToLower(entry)] = ++idx;  // later line wins; unlisted stay at 0
            }
        }

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
            std::shared_ptr<const CB::core::common::IUnitSource> source;
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
        auto collectNativeAnim = [this](const std::string& relOrig, std::string yamlText) {
            if (yamlText.empty()) return;
            const std::string lower = ToLower(relOrig);   // '/'-separated already
            if (lower.size() < 4 || lower.compare(lower.size() - 4, 4, ".hkx") != 0) return;

            // Must live UNDER an actor's animations dir (meshes/actors/<actor>/animations/...).
            // Belt-and-suspenders now: a ".hkx" FILE gate can no longer match the ADSF clip-generator
            // deltas (those are "<clip>.hkx.yaml", ending in .yaml — the old BR-18 false positive that
            // dumped ~1269 bogus .hkx into animationdatasinglefile.txt\clips\ and crashed char-setup);
            // the guard also rejects any stray binary .hkx that shouldn't be in an .hky at all.
            const auto ap = lower.find("meshes/actors/");
            if (ap == std::string::npos) return;
            const std::size_t as = ap + 14;
            const auto an = lower.find("/animations/", as);
            if (an == std::string::npos) return;

            const std::string outKey = relOrig;            // the ".hkx" path IS the output name

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
            m_nativeAnims.emplace_back(outKey, std::move(yamlText));
        };


        std::error_code rootEc;
        if (!fs::is_directory(pluginsRoot, rootEc)) {
            LOG_WARN("Resolver: plugins root not found: '{}' — nothing to serve.", pluginsRoot.string());
        } else {
            std::error_code ec;
            for (fs::directory_iterator bi(pluginsRoot, ec), bEnd; !ec && bi != bEnd; bi.increment(ec)) {
                const fs::path bundle = bi->path();
                if (ToLower(bundle.extension().string()) != ".hky") continue;  // a mod bundle
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
                const std::string plugName = ToLower(bundle.stem().string());
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
                        LOG_INFO("Resolver: bundle '{}' opts INTO animation compile (manifest compile_animations=true).", plugName);
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
                        const std::string relLow  = ToLower(relOrig);
                        if (relLow.size() >= 4 && relLow.compare(relLow.size() - 4, 4, ".hkx") == 0 &&
                            relLow.find("/animations/") != std::string::npos) {
                            std::ifstream  yf(ui->path(), std::ios::binary);
                            std::ostringstream ys; ys << yf.rdbuf();
                            collectNativeAnim(relOrig, ys.str());
                        }
                        continue;
                    }
                    if (!ui->is_directory(uec)) continue;
                    const fs::path unit = ui->path();
                    if (ToLower(unit.extension().string()) != ".hkx") continue;
                    ui.disable_recursion_pending();  // a unit is a leaf — don't descend into it

                    // Serve key = the unit's path RELATIVE TO THE BUNDLE (its game data
                    // path + graph), e.g. "meshes/actors/character/behaviors/0_master.hkx".
                    const std::string key = NormalizeKey(unit.lexically_relative(bundle).generic_string());
                    if (key.empty() || key[0] == '.') continue;

                    /// @note Skeletons compile through the skeleton path (path-local winners), and the project is
                    ///       synthesized by ProjectRedirect — neither is a merged graph layer. A project unit only
                    ///       contributes its original-case character ref.
                    auto src = std::make_shared<CB::core::common::DiskUnitSource>(unit);
                    const auto kind = KindOfUnit(*src);
                    if (kind == CB::core::codec::HkyArchive::UnitKind::Skeleton) { nonGraphKeys.insert(key); continue; }
                    if (kind == CB::core::codec::HkyArchive::UnitKind::Project) {
                        nonGraphKeys.insert(key);
                        if (IsCompleteUnit(*src)) m_projectUnits[key] = std::move(src);
                        continue;
                    }
                    layers[key].push_back({ plugName, std::move(src), kind == CB::core::codec::HkyArchive::UnitKind::Character });
                }

                // AUTHOR-FACING ROSTER DROP: a bundle adds animations to a character by
                // dropping a flat list at <Mod>.hky\animationnames\<character-file-stem>.txt
                // (e.g. animationnames\defaultmale.txt for characters\defaultmale.hkx). One
                // animation path per line; '#'/';' lines and blanks ignored. No need to mirror
                // the deep character-unit directory, and no Nemesis — these fold into the
                // character's compiled animationNames roster at Resolve() time (deduped,
                // case-insensitive). The converter emits the same files, so converter mods and
                // hand-authored bundles share one path. Unioned across bundles; the compile
                // dedups, so priority/order is immaterial here.
                std::error_code anec;
                const fs::path animNamesDir = bundle / "animationnames";
                if (fs::is_directory(animNamesDir, anec)) {
                    for (fs::directory_iterator ai(animNamesDir, anec), aEnd; !anec && ai != aEnd; ai.increment(anec)) {
                        std::error_code fec;
                        if (!ai->is_regular_file(fec)) continue;
                        if (ToLower(ai->path().extension().string()) != ".txt") continue;
                        const std::string charKey = ToLower(ai->path().stem().string());  // "defaultmale"
                        std::ifstream     nf(ai->path());
                        std::string       nline;
                        auto&             names = m_characterAnimNames[charKey];
                        while (std::getline(nf, nline)) {
                            const std::string t = TrimLine(nline);
                            // Un-escape XML entities so escaped killmove paths fold back to
                            // their literal form and dedup against the base roster (see
                            // UnescapeXml) — otherwise they pad animationNames and underflow
                            // OAR's synchronized-clip index offset.
                            if (!t.empty() && t[0] != '#' && t[0] != ';') names.push_back(UnescapeXml(t));
                        }
                    }
                }

              } else {
                // ── Packed bundle (shipped form) — decompress the whole .hky in memory. ──
                // A single-file .hky is a zip of the same game-tree unit layout. Read it
                // once via HkyArchive (immune to Windows MAX_PATH — the deep unit tree never
                // touches disk); each indexed unit vends a ZipUnitSource that reads straight
                // out of the in-memory blob. The archive is held in m_archives for the
                // process lifetime so those sources stay valid.
                std::string err;
                auto arc = CB::core::codec::HkyArchive::LoadFromFile(bundle.string(), err);
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
                        if (mf.present) { logManifest(mf); m_manifests[plugName] = std::move(mf); }
                    }
                    for (const auto& u : arc->units()) {
                        // The project graph is not a merged layer — ProjectRedirect serves the complete
                        // project unit on demand (into the cache, vanilla identity, its ORIGINAL-CASE
                        // characterFilenames: a lowercased ref A-poses vanilla-only actors).
                        if (u.kind == CB::core::codec::HkyArchive::UnitKind::Project) {
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
                        if (u.kind == CB::core::codec::HkyArchive::UnitKind::Skeleton) { nonGraphKeys.insert(NormalizeKey(u.prefix)); continue; }
                        const std::string key = NormalizeKey(u.prefix);   // already lower/'/'-normal
                        if (key.empty() || key[0] == '.') continue;
                        layers[key].push_back({ plugName, arc->source(u.prefix),
                                                u.kind == CB::core::codec::HkyArchive::UnitKind::Character });
                    }

                    // Author roster drops inside a packed bundle live at "animationnames/
                    // <char-stem>.txt" (same convention as the on-disk dir above). The
                    // full-corpus master ships none, but a packed MOD bundle may.
                    for (const auto& full : arc->filesUnder("animationnames/")) {
                        if (ToLower(fs::path(full).extension().string()) != ".txt") continue;
                        const std::string charKey = ToLower(fs::path(full).stem().string());
                        if (auto txt = arc->file(full)) {
                            auto& names = m_characterAnimNames[charKey];
                            std::istringstream ns(*txt);
                            std::string        nline;
                            while (std::getline(ns, nline)) {
                                const std::string t = TrimLine(nline);
                                if (!t.empty() && t[0] != '#' && t[0] != ';') names.push_back(UnescapeXml(t));
                            }
                        }
                    }

                    // Native animations authored inside the packed bundle: a bare "<name>.hkx" FILE
                    // key under meshes\ (the .hky compile-target model — unit-tree keys are
                    // "<name>.hkx/<subfile>" ending in .yaml/.txt, so a key ENDING in ".hkx" is always
                    // a single-file compile target). filesUnder (normalized) + filesUnderOrig (original
                    // case) walk the SAME ordered range, so index i aligns — read by the normalized key,
                    // name by the original; collectNativeAnim guards the meshes/actors/.../animations/ path.
                    if (bundleCompileAnims) {
                        const auto norm = arc->filesUnder("meshes/");
                        const auto orig = arc->filesUnderOrig("meshes/");
                        for (std::size_t i = 0; i < norm.size() && i < orig.size(); ++i) {
                            if (norm[i].size() < 4 || norm[i].compare(norm[i].size() - 4, 4, ".hkx") != 0) continue;
                            if (auto txt = arc->file(norm[i])) collectNativeAnim(orig[i], *txt);
                        }
                    }

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
        const LoadPlan plan = PlanLoadOrder(m_manifests, order);

        /// @note Skeletons are PATH-LOCAL units in every bundle ("meshes/actors/<actor>/<variant>/<x>.hkx/"
        ///       bonelist.yaml + bones/ — the skeleton compiler's own format, not a reflective graph: the schema
        ///       codec can't yet read their ragdoll physics), and the load-order winner of a unit path IS that
        ///       skeleton — a later
        ///       bundle replaces it whole (XPMSSE ships its full skeletons; an identical one is a no-op
        ///       replacement). Bone tables bind through the characters' `rig:` (see BonesFor), never
        ///       "the first skeleton under the actor folder" — that picked _1stperson/skeletonfirst for the
        ///       3rd-person character ('_' sorts before 'c').
        {
            const fs::path pluginsRoot = dataDir / "community_behaviors" / "plugins";
            struct RankedBundle { std::string stem; int rank = 0; BundleReader reader; };
            std::vector<RankedBundle> bundles;
            {
                std::error_code me;
                if (fs::is_directory(pluginsRoot, me))
                    for (fs::directory_iterator bi(pluginsRoot, me), bend; !me && bi != bend; bi.increment(me)) {
                        if (ToLower(bi->path().extension().string()) != ".hky") continue;
                        const std::string stem = ToLower(bi->path().stem().string());
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
            ///       paths. Internal-only key; bone NAMES are read original-case (readUnit).
            /// @note The load-order winner of a unit path is the last bundle whose unit there is COMPLETE: a skeleton
            ///       (bonelist.yaml), or a project / character graph.yaml owned by the unit itself — a mod's patch unit
            ///       never replaces one.
            std::unordered_map<std::string, std::size_t> skelWinner, projWinner, charWinner;
            for (std::size_t bi = 0; bi < bundles.size(); ++bi)
                for (const std::string& f : bundles[bi].reader.filesUnder("meshes/", ".yaml")) {
                    const auto endsWith = [&f](std::string_view t) {
                        return f.size() > t.size() && f.compare(f.size() - t.size(), t.size(), t) == 0;
                    };
                    if (endsWith("/bonelist.yaml")) { skelWinner[f.substr(0, f.size() - 14)] = bi; continue; }
                    if (!endsWith(".hkx/graph.yaml")) continue;
                    const std::string unit = f.substr(0, f.size() - 11);
                    const std::string text = bundles[bi].reader.read(f).value_or("");
                    if (CB::core::codec::HkyArchive::IsPatchGraphYaml(text)) continue;
                    switch (CB::core::codec::HkyArchive::KindOfGraphYaml(text)) {
                        case CB::core::codec::HkyArchive::UnitKind::Project:   projWinner[unit] = bi; break;
                        case CB::core::codec::HkyArchive::UnitKind::Character: charWinner[unit] = bi; break;
                        default: break;
                    }
                }

            /// @note filesUnder (normalized, for read) and filesUnderOrig (original case, for the bone NAME)
            ///       walk the same range, so index i aligns — a lowercased bone would orphan its skin binding.
            auto readUnit = [&](std::size_t bi, const std::string& unit, CB::core::skeleton::SkeletonData& out) -> bool {
                const BundleReader& br = bundles[bi].reader;
                const std::string bl   = br.read(unit + "/bonelist.yaml").value_or("");
                const auto norm = br.filesUnder(unit + "/bones/", ".yaml");
                const auto orig = br.filesUnderOrig(unit + "/bones/", ".yaml");
                std::vector<std::pair<std::string, std::string>> boneFiles;
                for (std::size_t i = 0; i < norm.size() && i < orig.size(); ++i)
                    if (auto t = br.read(norm[i]))
                        boneFiles.emplace_back(fs::path(orig[i]).stem().string(), *t);
                if (bl.empty() && boneFiles.empty()) return false;
                std::string err;
                if (!CB::core::skeleton::LoadSkeletonYamlFromTexts(bl, boneFiles, out, &err)) {
                    LOG_WARN("Resolver: skeleton '{}' ({}) YAML load failed: {}", unit, bundles[bi].stem, err);
                    return false;
                }
                return true;
            };
            /// @note Loaded HERE, single-threaded, before any fan-out: its one-time load is guarded by a plain,
            ///       unsynchronized `state` int, so two workers racing the first use could both attempt it.
            auto* schemaReg = CB::core::schema::SharedRegistry();

            // DIAGNOSTIC opt-out (marker file Data\community_behaviors\noskeletonserve.enable): skip the
            // skeleton SERVE entirely so skeleton opens fall through to the mod-provided loose skeleton.
            // The bone-name tables below are still built (behaviors need them); only the serve changes.
            const bool skipSkelServe = std::filesystem::exists("Data/community_behaviors/noskeletonserve.enable");
            if (skipSkelServe)
                LOG_WARN("Resolver: SKELETON SERVE DISABLED (noskeletonserve.enable) — skeleton opens fall "
                         "through to loose files. Diagnostic only.");

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
                CB::core::skeleton::SkeletonData sk;
                if (!readUnit(bi, unit, sk)) return;
                auto cr = CB::core::skeleton::CompileSkeletonFull(sk);
                if (cr.ok) {
                    const std::size_t nBones = sk.bones.size();
                    { std::lock_guard<std::mutex> lk(serveMu); m_skeletonServe[key] = std::move(cr.bytes); }
                    LOG_INFO("Resolver: SERVING skeleton '{}' from '{}' ({} bones).", unit, bundles[bi].stem, nBones);
                } else
                    LOG_WARN("Resolver: skeleton '{}' ({}) compile failed: {} — not served.", unit, bundles[bi].stem, cr.error);
            };
            if (!skipSkelServe) {
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
                m_rigIndex.AddProject(projUnit, CB::core::decompile::ReadProjectCharacters(*psrc, *schemaReg),
                    [&](const std::string& charUnit) -> std::optional<CB::core::skeleton::CharacterRig> {
                        const auto cit = charWinner.find(charUnit);
                        if (cit == charWinner.end()) return std::nullopt;
                        const auto src = bundles[cit->second].reader.unitSource(charUnit);
                        const auto cs  = src ? CB::core::decompile::ReadCharacterStrings(*src) : std::nullopt;
                        if (!cs) return std::nullopt;
                        return CB::core::skeleton::CharacterRig{ cs->rigName, cs->behaviorFilename };
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
                    CB::core::skeleton::SkeletonData sk;
                    if (!readUnit(bi, *r, sk)) return;
                    CB::core::common::BoneNameTable tbl;
                    for (const auto& b : sk.bones) tbl.names.push_back(b.name);
                    tbl.Reindex();
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
                CB::core::common::LayerSource lsrc;
                lsrc.source = l.source;
                lsrc.stem   = l.stem;
                const auto rit = plan.rank.find(l.stem);
                lsrc.rank   = rit != plan.rank.end() ? rit->second : 0;
                if (const auto ait = plan.ancestors.find(l.stem); ait != plan.ancestors.end())
                    lsrc.ancestors.assign(ait->second.begin(), ait->second.end());
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

        // Registered animations: check each actor's <actor>\animations\community_behaviors\
        // opt-in subfolder. Every .hkx under one is appended (in memory, at character
        // setup) to that actor's character animationNames — authors just drop the file in
        // the folder, no FNIS/Nemesis run. Actors sit at depth 1-2 under Meshes\actors\
        // (e.g. actors\character, actors\dlc02\riekling), so this probes specific paths
        // rather than walking the whole (huge) meshes tree. The stored value is the
        // animation path RELATIVE TO THE ACTOR DIR, backslashed — exactly what
        // animationNames holds. (.yaml compile-in-place is a later stage.)
        const fs::path meshesActors = dataDir / "meshes" / "actors";
        int animCount = 0;

        auto scanActorAnims = [&](const fs::path& actorDir) {
            std::error_code bec;
            const fs::path brAnim = actorDir / "animations" / "community_behaviors";
            if (!fs::is_directory(brAnim, bec)) return;
            const std::string actorKey =
                NormalizeKey(actorDir.lexically_relative(meshesActors.parent_path()).generic_string());
            if (actorKey.empty() || actorKey[0] == '.') return;  // e.g. "actors/character"
            std::error_code cec;
            for (fs::recursive_directory_iterator fi(brAnim, cec), fend; !cec && fi != fend; fi.increment(cec)) {
                std::error_code fec;
                if (!fi->is_regular_file(fec)) continue;
                if (ToLower(fi->path().extension().string()) != ".hkx") continue;  // .hkx now; .yaml later
                std::string rel = fi->path().lexically_relative(actorDir).generic_string();  // animations/community_behaviors/.../x.hkx
                for (char& c : rel) if (c == '/') c = '\\';                                   // animationNames form
                m_actorAnimations[actorKey].push_back(std::move(rel));
                ++animCount;
            }
        };

        std::error_code aec;
        if (fs::is_directory(meshesActors, aec)) {
            for (fs::directory_iterator a1(meshesActors, aec), e1; !aec && a1 != e1; a1.increment(aec)) {
                std::error_code d1;
                if (!a1->is_directory(d1)) continue;
                scanActorAnims(a1->path());                              // depth 1: actors\<actor>
                std::error_code d2;
                for (fs::directory_iterator a2(a1->path(), d2), e2; !d2 && a2 != e2; a2.increment(d2)) {
                    std::error_code dd;
                    if (a2->is_directory(dd)) scanActorAnims(a2->path());  // depth 2: actors\<group>\<actor>
                }
            }
        }
        if (animCount)
            LOG_INFO("Resolver: {} registered animation(s) across {} actor(s) (via animations\\community_behaviors\\).",
                     animCount, m_actorAnimations.size());

        if (!m_characterAnimNames.empty()) {
            std::size_t total = 0;
            for (const auto& [k, v] : m_characterAnimNames) total += v.size();
            LOG_INFO("Resolver: {} character-roster addition list(s) ingested ({} name(s) total) via animationnames\\.",
                     m_characterAnimNames.size(), total);
        }

        // FULLY built — publish the ready signal LAST (release), so any serve hook that acquires it sees
        // every field above completely written. Init runs on a background thread; hooks pass through to
        // vanilla until this fires, and the compile gate blocks on WaitReady().
        { std::lock_guard<std::mutex> lk(m_initMutex); m_ready.store(true, std::memory_order_release); }
        m_initCv.notify_all();
    }

    void Resolver::WaitReady() const
    {
        if (m_ready.load(std::memory_order_acquire)) return;
        std::unique_lock<std::mutex> lk(m_initMutex);
        m_initCv.wait(lk, [this] { return m_ready.load(std::memory_order_acquire); });
    }

    const std::vector<std::string>& Resolver::AnimationsForActor(std::string_view actorRoot) const
    {
        // No lock: m_actorAnimations is immutable after Init().
        static const std::vector<std::string> kEmpty;
        const auto it = m_actorAnimations.find(NormalizeKey(actorRoot));
        return it != m_actorAnimations.end() ? it->second : kEmpty;
    }

    const std::vector<std::string>& Resolver::AnimationsForCharacter(std::string_view behaviorFilename) const
    {
        // No lock: m_actorAnimations + m_sources are immutable after Init().
        static const std::vector<std::string> kEmpty;
        if (m_actorAnimations.empty()) return kEmpty;
        const std::string beh = NormalizeKey(behaviorFilename);  // e.g. "behaviors/0_master.hkx"
        // The character only knows its behavior file actor-relatively, so map it to an
        // actor by finding the animation-owning actor whose served behavior set contains
        // it: "meshes/" + <actorKey> + "/" + beh is a mapped graph. (Requires BR to also
        // serve that behavior — true for the vanilla-.hky setup; a pure-animation actor
        // with no BR behavior is a known gap.)
        for (const auto& [actorKey, anims] : m_actorAnimations) {
            const std::string candidate = NormalizeKey("meshes/" + actorKey + "/" + beh);
            if (m_sources.find(candidate) != m_sources.end()) return anims;
        }
        return kEmpty;
    }

    const BundleManifest* Resolver::ManifestFor(std::string_view bundleStem) const
    {
        // No lock: m_manifests is immutable after Init(). Keyed by lowercase bundle stem.
        const auto it = m_manifests.find(ToLower(std::string(bundleStem)));
        return it != m_manifests.end() ? &it->second : nullptr;
    }

    const std::vector<std::string>& Resolver::AnimationNamesForCharacter(std::string_view characterName) const
    {
        // No lock: m_characterAnimNames is immutable after Init(). Keyed by lowercase name.
        static const std::vector<std::string> kEmpty;
        if (m_characterAnimNames.empty() || characterName.empty()) return kEmpty;
        const auto it = m_characterAnimNames.find(ToLower(std::string(characterName)));
        return it != m_characterAnimNames.end() ? it->second : kEmpty;
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
            Resolve(k);
            if (progress) progress(++done, total);
        }
    }

    std::size_t Resolver::WriteSkeletonServe(const std::filesystem::path& dataRoot) const
    {
        namespace fs = std::filesystem;
        std::size_t written = 0;
        std::error_code ec;
        for (const auto& [key, bytes] : m_skeletonServe) {
            if (bytes.empty()) continue;
            const std::string rel = CB::core::discover::CacheDiskRel(key);
            if (rel.empty()) { LOG_WARN("Community Behaviors: skeleton key '{}' has unexpected shape — skipped.", key); continue; }
            const fs::path out = dataRoot / fs::path(rel);
            fs::create_directories(out.parent_path(), ec);
            std::ofstream f(out, std::ios::binary | std::ios::trunc);
            f.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
            f.flush();
            if (f.good()) { ++written; LOG_INFO("Community Behaviors: wrote compiled skeleton -> '{}'.", out.string()); }
            else LOG_ERROR("Community Behaviors: failed writing compiled skeleton '{}'.", out.string());
        }
        return written;
    }

    std::size_t Resolver::WriteNativeAnimations(const std::filesystem::path& dataRoot,
                                                const AnimExecutor* exec,
                                                const std::function<void()>* onUnit) const
    {
        namespace fs = std::filesystem;
        std::atomic<std::size_t> written{ 0 }, failed{ 0 };

        // Fresh motion accumulation for this compile pass (compiler = single motion producer; drained by
        // the adsf finalizer). Cleared up front so a re-run never carries stale records.
        m_motionSink.Clear();

        // Pre-warm the shared schema registry ON THIS (single) THREAD before any fan-out. SharedRegistry()'s
        // one-time load is guarded by a plain `state` int, NOT synchronized — two worker threads racing the
        // first call could both attempt the load. After this call state is settled and every worker only
        // READS it (a pure pointer return). On the serial path this is a harmless no-op — the cold path's
        // CompileAll already warmed it; the WARM path (ArmCacheFromDisk) does NOT run CompileAll, so this is
        // the call that warms it there. Do it regardless of exec so both paths are covered.
        (void)CB::core::schema::SharedRegistry();

        // Compile+write ONE native animation. A pure function of its own (outKey, yamlText) entry and the
        // immutable-after-Init m_skeletons; writes its own distinct file. No shared mutable state, so this
        // is safe to run concurrently and the parallel result is byte-identical to the serial one.
        auto compileOne = [&](const std::string& outKey, const std::string& yamlText) {
            std::error_code ec;
            // outKey: "meshes/actors/<actor>/animations/.../<name>.hkx" (original case). We only reach here
            // for a bundle that OPTED INTO animation compile (manifest compile_animations=true), so writing
            // to the REAL meshes path is the point — the compiled animation serves. The old meshes/CBanims/
            // redirect was a harness so the (then-always-on) recompile couldn't clobber vanilla loose/BSA
            // animations during validation; the per-bundle compile gate makes it unnecessary — a bundle that
            // doesn't opt in isn't compiled at all, so there is nothing to clobber.
            const fs::path out = dataRoot / fs::path(outKey);
            try {
                auto def = CB::core::anim::AnimationYamlLoader::LoadFromString(yamlText, outKey);
                /// @note The unit's actor motion (its record, or extracted from a folded root — which also strips the
                ///       root, so the binary compiled below serves without the travel the adsf now moves the actor by).
                const auto motion = CB::core::anim::ResolveRootMotion(def);
                // PRODUCE MOTION: the compiler is the single producer of root motion. If this native unit
                // carries an inline `motion:` block, emit it for the adsf finalizer to DRAIN (instead of
                // the adsf re-reading the YAML — which looked for a `.hkx.yaml` class these `.hkx` units
                // don't use, so it read nothing). Keyed to match the drain exactly: actorRoot = the serve
                // path up to "/animations/"; innerKey = the remainder, lowercased '/'-sep (== the form
                // motionsForRoot produced and DeriveProjectPatch's normAnim(animationName) looks up). Motion
                // is authored data (not compile-dependent), so emit on a successful PARSE, before compile.
                if (motion) {
                    const std::string low = ToLower(outKey);   // outKey is '/'-sep already
                    if (const auto ap = low.find("/animations/"); ap != std::string::npos)
                        m_motionSink.EmitMotion(outKey.substr(0, ap), low.substr(ap + 1), *motion);
                }
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
                const auto r   = CB::core::anim::CompileAnimation(def, 30, CB::core::codec::HKXHeader::SkyrimSE(), boneNames);
                if (!r.ok) {
                    failed.fetch_add(1, std::memory_order_relaxed);
                    LOG_ERROR("Community Behaviors: native animation compile FAILED '{}': {}", outKey, r.error);
                    return;
                }
                fs::create_directories(out.parent_path(), ec);
                std::ofstream f(out, std::ios::binary | std::ios::trunc);
                f.write(reinterpret_cast<const char*>(r.bytes.data()), static_cast<std::streamsize>(r.bytes.size()));
                f.flush();
                if (f.good()) { written.fetch_add(1, std::memory_order_relaxed); LOG_INFO("Community Behaviors: compiled native animation -> '{}'.", out.string()); }
                else          { failed.fetch_add(1, std::memory_order_relaxed);  LOG_ERROR("Community Behaviors: failed writing native animation '{}'.", out.string()); }
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
            // Capture each element by POINTER (m_nativeAnims is immutable after Init, so the address is
            // stable) — never by the range-for reference, which is rebound each iteration. The tick runs
            // on the worker thread, so onUnit must be thread-safe (it is — atomic increment + store).
            std::vector<std::function<void()>> tasks;
            tasks.reserve(m_nativeAnims.size());
            for (const auto& e : m_nativeAnims) {
                const auto* ep = &e;
                tasks.push_back([&compileOne, &tick, ep] { compileOne(ep->first, ep->second); tick(); });
            }
            (*exec)(std::move(tasks));
        } else {
            for (const auto& [outKey, yamlText] : m_nativeAnims) { compileOne(outKey, yamlText); tick(); }
        }

        const std::size_t w = written.load(std::memory_order_relaxed);
        const std::size_t fl = failed.load(std::memory_order_relaxed);
        if (w || fl)
            LOG_INFO("Community Behaviors: native animations — {} compiled, {} failed (compiled to the real "
                     "meshes path — opt-in bundles only, manifest compile_animations=true).", w, fl);
        // Phase 0 coverage: what the compiler produced for the adsf to drain. Nothing consumes the sink
        // yet (byte-neutral); this confirms the emit fires + the keying before the drain is switched over.
        LOG_INFO("Community Behaviors: motion sink — {} record(s) across {} actor root(s) (compiler-produced).",
                 m_motionSink.RecordCount(), m_motionSink.RootCount());
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

        // 1) Clear the prior session's COMPILED-GRAPH tree from the consolidated cache
        //    (Data\community_behaviors_cache\). The byte hook trusts anything on disk, so stale graph bytes must
        //    never survive a load-order change. SURGICAL, not a wholesale remove_all: the animdata/
        //    setdata .txt caches were written to this same community_behaviors_cache\ root EARLIER (at plugin
        //    load, before this kDataLoaded warm-up) and MUST survive — so wipe every entry under
        //    community_behaviors_cache\ EXCEPT those collocated text caches (+ the sentinel). The graph HKX all
        //    live in per-root subdirectories (actors\..., weapons\..., ...), so this removes them
        //    while leaving the root-level .txt files intact.
        {
            const fs::path cacheRoot = dataRoot / CB::core::discover::kConsolidatedCacheDir;
            if (fs::is_directory(cacheRoot, ec))
                for (fs::directory_iterator di(cacheRoot, ec), de; !ec && di != de; di.increment(ec)) {
                    const std::string fn = ToLower(di->path().filename().string());
                    if (fn == "animationdatasinglefile.txt" ||
                        fn == "animationsetdatasinglefile.txt" ||
                        fn == "cache.ready")
                        continue;   // preserve the collocated txt caches + sentinel
                    std::error_code dec;
                    fs::remove_all(di->path(), dec);
                }
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
                    const std::string fn = ToLower(di->path().filename().string());
                    if (fn.size() >= 7 && fn.compare(fn.size() - 7, 7, ".br.hkx") == 0)
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
            auto bytes = Resolve(k);   // Resolve = cached after CompileAll
            if (bytes && !bytes->empty()) {
                const std::string rel = CacheDiskRel(k);
                if (!rel.empty()) {
                    const fs::path out = dataRoot / fs::path(rel);
                    fs::create_directories(out.parent_path(), ec);
                    std::ofstream f(out, std::ios::binary | std::ios::trunc);
                    f.write(reinterpret_cast<const char*>(bytes->data()),
                            static_cast<std::streamsize>(bytes->size()));
                    f.flush();
                    if (f.good()) ++written;
                    else { LOG_ERROR("Community Behaviors: failed writing cache file '{}'.", out.string()); ++failedGraphs; }
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

        // 2c) Compile the bundle-authored native animations into LOOSE .hkx under Data\meshes\ (not
        //     community_behaviors_cache — actor animations resolve by the engine's startup loose scan). Persist
        //     across cache regens; the clean names are already rostered (folded at Init).
        WriteNativeAnimations(dataRoot, animExec, animOnUnit);

        // 3) Build the above-OAR redirect map (folderRoot -> owned characters) + mark ready.
        //    Shared with the reuse path (ArmCacheFromDisk) — it depends only on m_sources, not
        //    on the bytes just written, so a run that reuses an existing on-disk cache arms the
        //    exact same redirect without recompiling.
        BuildRedirectMap(dataRoot);

        // 4) Completion sentinel — written LAST, and ONLY when every graph compiled + wrote
        //    (failedGraphs == 0). Its presence is what CachePresent() trusts to skip the recompile on a
        //    later run, so a PARTIAL cache must never be stamped complete: leaving it un-sentinelled
        //    forces the next launch to recompile the missing graphs instead of arming a broken cache.
        //    (Content is a marker today; it becomes the cache fingerprint when real invalidation lands.)
        if (failedGraphs == 0) {
            std::error_code se;
            const fs::path sentinel = CacheSentinelPath(dataRoot);
            fs::create_directories(sentinel.parent_path(), se);
            // Stamp the sentinel with the WATERMARK VERSION. CachePresent() requires a match, so a
            // build that bumps kWatermarkValue invalidates an older cache and forces a regen — the
            // cached graphs' injected BR_Watermark then always matches this build (the probe can't
            // read a stale "NO"). A minimal content fingerprint until full invalidation lands.
            std::ofstream(sentinel, std::ios::binary | std::ios::trunc)
                << CB::core::resolve::kWatermarkValue << "\n";
        } else {
            LOG_WARN("Community Behaviors: cache incomplete ({} graph(s) failed to compile/write) — "
                     "completion sentinel withheld; the next launch will recompile.", failedGraphs);
        }

        LOG_INFO("Community Behaviors: materialized {}/{} graph cache file(s) under '{}\\{}'.",
                 written, total, dataRoot.string(), CB::core::discover::kConsolidatedCacheDir);
        return written;
    }

    // Title-case each space-separated word ("characters female" -> "Characters Female"), matching
    // vanilla's project character-ref folder convention. Case matters — Havok is case-sensitive.
    static std::string TitleCaseFolder(std::string s)
    {
        bool atStart = true;
        for (char& c : s) {
            if (atStart && c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
            atStart = (c == ' ' || c == '/');
        }
        return s;
    }

    /// @brief The character's proper-case string-data `name`, from the first layer that states it.
    /// @note  Every path KEY is lowercased at scan; this name re-cases the synthesized project's characterFilenames
    ///        back to vanilla, which Havok matches CASE-SENSITIVELY (a miscased ref binds nothing: universal A-pose).
    static std::string ReadCharacterName(
        const std::vector<CB::core::common::LayerSource>& layers)
    {
        for (const auto& L : layers)
            if (L.source)
                if (const auto cs = CB::core::decompile::ReadCharacterStrings(*L.source); cs && !cs->name.empty())
                    return cs->name;
        return {};
    }

    void Resolver::BuildRedirectMap(const std::filesystem::path& dataRoot)
    {
        namespace fs = std::filesystem;

        // folderRoot -> [{charStem, cacheCharRef}] for each owned CHARACTER. The synthesized
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

            const std::string charRel = k.substr(root.size() + 1);  // "characters/defaultmale.hkx"
            const std::string charStem = fs::path(k).stem().string();  // "defaultmale"

            // The value the synthesized project's characterFilenames[0] holds: the VANILLA character
            // path (backslashed), e.g. "Characters\DefaultMale.hkx". Havok matches this string
            // CASE-SENSITIVELY (see the case directive in CLAUDE.md), but the master stores every path
            // KEY lowercased — so charRel is lowercase, and shipping it verbatim silently breaks the
            // character↔animation-data association (universal A-pose, BR-31). Re-case it to vanilla:
            // the proper-cased stem is the character's own `name:` (the one field that survived the
            // lowercased master), and the folder follows vanilla's fixed title-case convention
            // ("Characters" / "Characters Female"). The ByteServe hook still lowercases the actual file
            // OPEN (BR-30), so the proper-cased ref loads from the lowercase on-disk cache while keeping
            // the vanilla-cased identity. Falls back to the lowercase ref if the name can't be recovered.
            std::string charRef = charRel;
            if (const auto slash = charRel.find_last_of('/'); slash != std::string::npos) {
                if (const std::string properName = ReadCharacterName(gs.layers);
                    !properName.empty() && ToLower(properName) == charStem)
                    charRef = TitleCaseFolder(charRel.substr(0, slash)) + "/" + properName + ".hkx";
            }
            std::string charRelBs = charRef;
            for (char& c : charRelBs) if (c == '/') c = '\\';
            if (charRef != charRel)
                LOG_INFO("Resolver: re-cased character ref '{}' -> '{}' for the synthesized project "
                         "(Havok is case-sensitive; BR-31).", charRel, charRef);

            m_projectRedirects[root].push_back({ charStem, charRelBs });
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
        // Present AND stamped with THIS build's watermark version — a version bump (or a legacy
        // marker-string sentinel) mismatches and forces a regen, so the served cache's watermark
        // always matches the running DLL.
        std::ifstream f(sentinel, std::ios::binary);
        std::string stamp;
        std::getline(f, stamp);
        while (!stamp.empty() && (stamp.back() == '\r' || stamp.back() == '\n' || stamp.back() == ' '))
            stamp.pop_back();
        return stamp == std::to_string(CB::core::resolve::kWatermarkValue);
    }

    void Resolver::ArmCacheFromDisk(const std::filesystem::path& dataRoot, const AnimExecutor* animExec)
    {
        // Reuse path: the compiled graphs + synthesized projects from a prior run are already on
        // disk (guarded by the completion sentinel CachePresent() checked). Arm ONLY the redirect
        // map so ProjectRedirect() serves them — no CompileAll, no clear, no graph re-write. The
        // opt-in compiled skeletons ARE (re)written here: they were compiled fresh in Init (cheap),
        // and the graph-cache clear/reuse doesn't cover them, so this guarantees they're on disk.
        WriteSkeletonServe(dataRoot);
        // Native animations are LOOSE (not in the wiped community_behaviors_cache), so a prior run's files usually
        // still exist — but rewrite them here too (cheap, few) so a reused cache from a build without
        // them, or a user who cleared meshes\, still gets them on disk before the scan.
        WriteNativeAnimations(dataRoot, animExec);
        BuildRedirectMap(dataRoot);
        LOG_INFO("Community Behaviors: reusing existing on-disk behavior cache under '{}\\Meshes' "
                 "(recompile skipped — set [Cache] bForceRegenerate=true or delete the cache to rebuild).",
                 dataRoot.string());
    }

    std::vector<CB::core::animdata::DeriveClipInput> Resolver::ClipInputsOf(
        const std::string& key, const std::vector<CB::core::common::LayerSource>& layers) const
    {
        auto* reg = CB::core::schema::SharedRegistry();
        if (!reg) return {};
        const RigBones rb = BonesFor(key);
        CB::core::decompile::ParsedHky graph;
        std::string err;
        if (!CB::core::decompile::MergeLayersReflective(layers, *reg, rb.table ? &rb.table->names : nullptr, graph, err)) {
            LOG_WARN("Resolver: clip read of '{}' failed: {}", key, err);
            return {};
        }
        return CB::core::compile::DeriveClipInputsFromGraph(graph.root);
    }

    std::size_t Resolver::FoldAuthorRoster(const std::string& characterStem, std::vector<std::string>& names) const
    {
        const auto ci = m_characterAnimNames.find(characterStem);
        if (ci == m_characterAnimNames.end()) return 0;
        std::unordered_set<std::string> have(names.begin(), names.end());
        std::size_t folded = 0;
        for (const auto& a : ci->second)
            if (have.insert(a).second) { names.push_back(a); ++folded; }
        return folded;
    }

    std::vector<CB::core::animdata::DeriveClipInput> Resolver::MergedClipInputs(const std::string& key) const
    {
        const auto it = m_sources.find(NormalizeKey(key));
        return it == m_sources.end() ? std::vector<CB::core::animdata::DeriveClipInput>{} : ClipInputsOf(it->first, it->second.layers);
    }

    std::vector<std::string> Resolver::ServedRoster(const std::string& characterKey) const
    {
        auto* reg = CB::core::schema::SharedRegistry();
        const auto it = m_sources.find(NormalizeKey(characterKey));
        if (!reg || it == m_sources.end()) return {};
        const RigBones rb = BonesFor(it->first);
        CB::core::decompile::ParsedHky graph;
        std::string err;
        if (!CB::core::decompile::MergeLayersReflective(it->second.layers, *reg, rb.table ? &rb.table->names : nullptr, graph, err)) {
            LOG_WARN("Resolver: roster read of '{}' failed: {}", it->first, err);
            return {};
        }
        CB::core::decompile::GraphEdit edit(graph, *reg);
        std::vector<std::string> names;
        bool found = false;
        edit.ForEach("hkbCharacterStringData", [&](CB::core::codec::io::SchemaObject& so) {
            if (!found) { names = so.FieldRef("animationNames").strs; found = true; }
        });
        FoldAuthorRoster(ToLower(fs::path(it->first).stem().string()), names);
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
        // vanilla project open — "community_behaviors_cache\<vanilla project path>" (Meshes-relative, original
        // case). The engine loads BR's synthesized project but interns it under the VANILLA identity
        // (the caller's descriptor is untouched), so the speed-sampler DB key and the animdata table
        // both resolve to the stock stem — no ".br", no alias.
        const std::string swap = CB::core::discover::CacheSwapPath(vanillaProjectPath);

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
                        CB::core::decompile::ParsedHky project;
                        auto* reg = CB::core::schema::SharedRegistry();
                        if (!reg || !CB::core::decompile::ParseHkyReflective(*pu->second, *reg, project, err) ||
                            !CB::core::decompile::SerializeGraph(project, bytes, err))
                            bytes.clear();
                    }
                    if (bytes.empty()) {
                        std::error_code fec;
                        const fs::path vanProj = fs::current_path(fec) / "Data" / "Meshes" / fs::path(std::string(vanillaProjectPath));
                        std::ifstream vf(vanProj, std::ios::binary);
                        if (vf) bytes.assign(std::istreambuf_iterator<char>(vf), std::istreambuf_iterator<char>());
                    }
                    if (!bytes.empty()) {
                        // Write to the ORIGINAL-CASE path the swap resolves to — NOT CacheDiskRel(projKey),
                        // which lowercases. MO2's USVFS matches case-sensitively, so the engine's Func3-cased
                        // project open ("...\DefaultMale.hkx") only finds a same-cased file on disk. (This is
                        // the one path the engine opens under a case we don't control; behaviors/characters
                        // below use BR's own lowercase refs and are self-consistent.)
                        const fs::path out = m_dataRoot / fs::path(CB::core::discover::CacheDiskRelRaw(vanillaProjectPath));
                        std::error_code ec;
                        fs::create_directories(out.parent_path(), ec);
                        std::ofstream f(out, std::ios::binary | std::ios::trunc);
                        f.write(reinterpret_cast<const char*>(bytes.data()),
                                static_cast<std::streamsize>(bytes.size()));
                        f.flush();
                        ok = f.good();
                        if (ok)
                            LOG_INFO("Community Behaviors: byte-served project '{}' -> vanilla character '{}' "
                                     "(cache '{}').", vanillaProjectPath, chosen->cacheCharRef, swap);
                        else
                            LOG_ERROR("Community Behaviors: failed writing byte-served project '{}'.", out.string());
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
        // MaterializeCacheToDisk / WriteSkeletonServe wrote. Same NormalizeKey() as Owns; the exists()
        // check is case-robust on the (case-insensitive) Windows FS even though the skeleton write may
        // use original case. m_dataRoot is set in BuildRedirectMap on both the materialize and reuse
        // paths before RedirectReady() flips, so a set root here means the cache pass has run.
        if (m_dataRoot.empty()) return false;   // not armed yet → treat as absent (hook falls through)
        const std::string key = NormalizeKey(servePath);
        std::error_code   ec;
        return std::filesystem::exists(m_dataRoot / std::filesystem::path(CacheDiskRel(key)), ec);
    }

    void Resolver::ResolveCharacter(const std::string& key, CB::core::decompile::GraphEdit& edit)
    {
        CB::core::codec::io::SchemaObject* sd = nullptr;
        edit.ForEach("hkbCharacterStringData", [&](CB::core::codec::io::SchemaObject& so) { if (!sd) sd = &so; });
        if (!sd) { LOG_WARN("Resolver: character '{}' has no string data — served as merged.", key); return; }
        const std::string cstem = ToLower(fs::path(key).stem().string());

        /// @note The author-facing roster drop (animationnames\<stem>.txt) folds in as an EXACT-case union: Havok binds
        ///       case-sensitively, so a lowercased dedup would drop the exact name a clip binds.
        std::vector<std::string>& names = sd->FieldRef("animationNames").strs;
        if (const std::size_t folded = FoldAuthorRoster(cstem, names))
            LOG_INFO("Resolver: folded {} author roster addition(s) into character '{}'.", folded, key);
        if (m_adsfDerive) m_characterRosters[cstem] = names;
        m_characterActor[cstem] = ActorPathOf(key);

        /// @note Cache-qualify the character's main behavior reference when CB serves that behavior, so the
        ///       redirected load reaches CB's compiled graph instead of the vanilla file.
        std::string& behavior = sd->FieldRef("behaviorFilename").str;
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
            auto* reg = CB::core::schema::SharedRegistry();
            if (!reg) throw std::runtime_error("schema registry unavailable");

            /// @note Bone names (weight maps, bone refs, bone-keyed rosters) resolve against the rig of the
            ///       characters that run this graph.
            const RigBones rb = BonesFor(key);
            const std::vector<std::string>* bones = rb.table ? &rb.table->names : nullptr;
            if (rb.table && rb.ambiguous)
                LOG_WARN("Resolver: '{}' is shared by characters whose rigs disagree on bone order — "
                         "bone names resolve against '{}'.", key, rb.rig);

            CB::core::decompile::ParsedHky graph;
            std::string err;
            if (!CB::core::decompile::MergeLayersReflective(gs.layers, *reg, bones, graph, err)) {
                LOG_ERROR("Community Behaviors: merge FAILED for '{}' — SERVING VANILLA (its mod edits are dropped): {}",
                          key, err);
                m_cache.emplace(key, nullptr);
                return nullptr;
            }
            CB::core::decompile::GraphEdit edit(graph, *reg);

            if (gs.isCharacter) {
                ResolveCharacter(key, edit);
            } else {
                ResolverFeatureLog fLog;
                /// @note animData (the contribution hook) is supplied only while adsf-derive collects, so a feature
                ///       can push extra clips into the same sink the first-class derive stage fills below.
                CB::feature::FeatureContext fctx{ .graphKey = key, .log = fLog,
                                                  .animData = (m_adsfDerive ? &m_clipSink : nullptr),
                                                  .symbols  = &m_symbols };
                /// @note The ENABLED set is data (WHICH run); ResolveRunOrder derives the ORDER from each feature's
                ///       RunsAfter/RunsBefore. The ER gate stays opt-in ([ERGate] bEnable): a fault there rewrites
                ///       every wildcard and would T-pose every actor.
                std::vector<std::string> enabledIds{ "cb.watermark", "cb.bdi-symbols" };
                if (m_erGateEnabled) enabledIds.emplace_back("engine-relay.wildcard-gate");
                auto& features = CB::feature::FeatureRegistry::Instance();
                for (const auto& [id, r] : features.Run(features.ResolveRunOrder(enabledIds, fLog), edit, fctx))
                    if (r.applied && r.mutations)
                        LOG_INFO("Resolver: feature {} → {} mutation(s) in '{}'.", id, r.mutations, key);

                /// @note adsf-derive is a first-class compile stage (compiler output, not a graph mutation): it
                ///       reads the clip generators AFTER the features, so a feature that adds clips gets derived.
                if (m_adsfDerive) {
                    const auto inputs = CB::core::compile::DeriveClipInputsFromGraph(graph.root);
                    for (const auto& in : inputs) m_clipSink.EmitClip(key, in);
                    if (!inputs.empty())
                        LOG_INFO("Resolver: adsf-derive — {} clip input(s) from '{}'.", inputs.size(), key);
                }

                /// @note Cache-qualify every child behavior reference CB owns, so the recursive above-OAR load pulls
                ///       CB's compiled sub-behaviors while unowned children resolve to their vanilla files.
                std::size_t qualified = 0;
                edit.ForEach("hkbBehaviorReferenceGenerator", [&](CB::core::codec::io::SchemaObject& so) {
                    std::string& name = so.FieldRef("behaviorName").str;
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

            std::vector<std::uint8_t> bytes;
            const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
            if (CB::core::decompile::SerializeGraph(graph, bytes, err)) {
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

    // ── the animationdata finalize (opt-in) ──────────────────────────────────────────
    // Starts from the PROVEN collated file ServeAnimData just wrote (every project already correct —
    // vanilla base + bundle deltas + motion) and OVERRIDES only the clip list of projects the sink
    // fully covers (a compiled character with a captured merged roster). An uncovered project keeps
    // its proven clips — never emptied, since an empty hasAnimData block desyncs the positional parser
    // and hangs the engine. For a covered project the clips come straight off the merged graph (load
    // order already resolved), resolved against the character's MERGED roster — so mod clips get their
    // real roster index instead of the collated path's synthetic high-band index.
    bool Resolver::DeriveAnimData(const std::filesystem::path& dataDir)
    {
        if (!m_adsfDerive) return false;
        namespace ad = CB::core::animdata;

        // ── start from the proven collated file (ServeAnimData ran first) ──
        const fs::path out = dataDir / "community_behaviors_cache" / "animationdatasinglefile.txt";
        std::string baseText;
        { std::ifstream f(out, std::ios::binary);
          if (f) baseText.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>()); }
        if (baseText.empty()) {
            LOG_WARN("Resolver: DeriveAnimData — no collated base at '{}' (did ServeAnimData run?).", out.string());
            return false;
        }
        ad::SingleFile sf;
        try { sf = ad::ParseSingleFile(baseText); }
        catch (const std::exception& e) {
            LOG_WARN("Resolver: DeriveAnimData — base parse failed ({}); leaving the collated file.", e.what());
            return false;
        }

        // ── project stem -> character stem (index.yaml), so a project whose name differs from its
        //    character file (ChickenProject.txt <- chicken.hkx) still finds its captured roster ──
        std::unordered_map<std::string, std::string> projToChar;
        if (auto rd = BundleReader::Open(dataDir / "community_behaviors" / "plugins" / "Skyrim.hky")) {
            if (const auto idxTxt = rd->read("meshes/animationdatasinglefile.txt/index.yaml")) {
                std::string ierr;
                for (const auto& h : ad::ParseAnimdataIndexYaml(*idxTxt, ierr)) {
                    if (h.character.empty()) continue;
                    std::string cs = h.character;
                    for (char& c : cs) if (c == '\\') c = '/';
                    const auto sl = cs.find_last_of('/');
                    std::string charStem = ToLower(sl == std::string::npos ? cs : cs.substr(sl + 1));
                    if (const auto dot = charStem.rfind('.'); dot != std::string::npos) charStem.erase(dot);
                    projToChar[ad::StemForProjectName(h.name)] = charStem;
                }
            }
        }

        // ── clips from the sink: group every graph's contributions by actor path ──
        std::map<std::string, std::vector<ad::DeriveClipInput>> byActor;
        for (const auto& [graphKey, clips] : m_clipSink.Contributions()) {
            const std::string actor = ActorPathOf(graphKey);
            if (actor.empty()) continue;
            auto& v = byActor[actor];
            v.insert(v.end(), clips.begin(), clips.end());
        }

        // ── override covered projects' clips (leave the rest — and their motion — proven) ──
        std::size_t overridden = 0;
        for (auto& p : sf.projects) {
            if (!p.hasAnimData) continue;
            const std::string projStem = ad::StemForProjectName(p.name);
            std::string charStem = projStem;
            if (const auto pc = projToChar.find(projStem); pc != projToChar.end()) charStem = pc->second;
            const auto ri = m_characterRosters.find(charStem);
            const auto ai = m_characterActor.find(charStem);
            if (ri == m_characterRosters.end() || ai == m_characterActor.end()) continue;  // no captured roster
            const auto ci = byActor.find(ai->second);
            if (ci == byActor.end()) continue;                                             // no graphs for this actor
            p.clips = ad::DeriveClipList(ci->second, ri->second, /*motionDurByIndex*/ {});
            ++overridden;
        }

        const std::string text = ad::EmitSingleFile(sf);
        std::error_code ec;
        std::filesystem::create_directories(out.parent_path(), ec);
        std::ofstream f(out, std::ios::binary | std::ios::trunc);
        if (!f) {
            LOG_ERROR("Resolver: DeriveAnimData — cannot write '{}'.", out.string());
            return false;
        }
        f.write(text.data(), static_cast<std::streamsize>(text.size()));
        LOG_INFO("Resolver: DeriveAnimData — overrode {} of {} project(s) with sink-derived clips "
                 "({} graph(s), {} clip input(s)) -> '{}'.",
                 overridden, sf.projects.size(), m_clipSink.GraphCount(), m_clipSink.ClipCount(), out.string());
        return true;
    }

}  // namespace CB
