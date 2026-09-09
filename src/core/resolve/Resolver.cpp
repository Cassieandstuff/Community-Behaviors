#include "PCH.h"

#include "core/resolve/Resolver.h"
#include "features/ERGate.h"
#include "core/discover/ServeKey.h"
#include "core/resolve/Watermark.h"
#include "FeatureRegistry.h"   // compile-time graph features (ER wildcard gate, …); features/core/hpp on the path

#include <havok/model/HavokEnums.h>   // TransitionFlags / ResolveEnum (wildcard-gate flag clear)

#include <havok/model/yaml/YamlBehaviorLoader.h>
#include <havok/model/yaml/CharacterYamlLoader.h>
#include <havok/model/CompileTrace.h>   // schema-driven compile-trace (opt-in via [Debug] bCompileTrace)
#include "SimpleIni.h"                  // [Debug] bCompileTrace toggle (mirrors CB::debug::kFlags row)
#include <havok/model/yaml/UnitSource.h>
#include <havok/model/yaml/HkyArchive.h>
#include <havok/sct/BehaviorCompiler.h>
#include <havok/sct/CharacterCompiler.h>
#include <havok/sct/ProjectCompiler.h>
#include <havok/skeleton/SkeletonImport.h>   // SkeletonData — schema-native skeleton codec (havok-skeleton)
#include <havok/skeleton/SkeletonYaml.h>     // LoadSkeletonLayer / MergeBoneAdditions (bone-add layers)
#include <havok/skeleton/SkeletonCompiler.h> // CompileSkeletonFull (Stage D serve)
#include <havok/anim/AnimationYamlLoader.h>  // native animation YAML (in a .hky) -> AnimationDef
#include <havok/anim/AnimationCompiler.h>    // havok::anim::CompileAnimation (native anim -> loose .hkx)
#include <havok/anim/AnimationData.h>        // animdata::SingleFile / EmitSingleFile (DeriveAnimData)
#include <havok/anim/AnimDataYaml.h>         // AssembleAnimdata / ParseAnimdataIndexYaml / ParseMotionSidecar / StemForProjectName
#include <havok/anim/AnimDataDeriver.h>      // DeriveClipList (sink clip inputs + roster -> ClipGenerators)
#include "core/discover/BundleReader.h"               // read the shipped vanilla skeleton base from Skyrim.hky

#include <map>
#include <optional>
#include <set>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <exception>
#include <fstream>
#include <iterator>
#include <memory>
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

        // The first `characterFilenames:` list entry from a project.yaml, VERBATIM (original case, e.g.
        // "Characters\DefaultMale.hkx"). The decompiled project preserves the exact string the engine
        // binds the character to setdata/animdata by (case-sensitively). Minimal parse — no YAML lib.
        std::string FirstCharFilename(const std::string& projectYaml)
        {
            const auto key = projectYaml.find("characterFilenames:");
            if (key == std::string::npos) return {};
            // Bound the "- " search to characterFilenames:' OWN block — the contiguous run of following
            // lines that are blank or list items. Stop at the first line whose first non-space char is
            // neither '-' nor a line break: that's the next key. An empty or flow-style ("[...]") list
            // has no "- " in its block, and an unbounded find would walk into a later key's list and
            // return an unrelated string — an invisible character->setdata/animdata mis-bind.
            std::size_t boundary = std::string::npos;   // start of the next sibling key, or npos
            std::size_t lineStart = projectYaml.find('\n', key);
            while (lineStart != std::string::npos) {
                lineStart += 1;                          // first char of the next line
                std::size_t p = lineStart;
                while (p < projectYaml.size() && (projectYaml[p] == ' ' || projectYaml[p] == '\t')) ++p;
                if (p >= projectYaml.size()) break;
                const char ch = projectYaml[p];
                if (ch == '\n' || ch == '\r' || ch == '-') {   // blank line or list item: still the block
                    lineStart = projectYaml.find('\n', p);
                    continue;
                }
                boundary = lineStart;                    // a non-list line = the next key
                break;
            }
            const auto dash = projectYaml.find("- ", key);
            if (dash == std::string::npos || (boundary != std::string::npos && dash >= boundary)) return {};
            auto end = projectYaml.find('\n', dash);
            std::string v = projectYaml.substr(dash + 2, (end == std::string::npos ? projectYaml.size() : end) - (dash + 2));
            while (!v.empty() && (v.back() == '\r' || v.back() == ' ' || v.back() == '\t')) v.pop_back();
            while (!v.empty() && (v.front() == ' ' || v.front() == '\t')) v.erase(v.begin());
            // strip optional surrounding quotes
            if (v.size() >= 2 && (v.front() == '\'' || v.front() == '"') && v.back() == v.front()) v = v.substr(1, v.size() - 2);
            return v;
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
        // characters — mirrors havok::model CharacterYamlLoader::xmlUnescape. Author-drop
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

        using servekey::NormalizeKey;
        using servekey::FolderRootOf;
        using servekey::CacheDiskRel;
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
        // CB::features (SOP)"). The transform runs from the compile loop via the
        // FeatureRegistry (below). This host-side adapter bridges a feature's logging to the
        // plugin logger — features never touch LOG_* directly (the pure-Apply rule keeps them
        // offline-reproducible).
        struct ResolverFeatureLog final : CB::features::IFeatureLog {
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

    void Resolver::Init(const fs::path& dataDir, const fs::path& loadOrderIni)
    {
        // Route havok-core's non-fatal merge notices (same-slot positional-array
        // collisions, where load-order last-writer drops a mod's differing edit) into
        // the BR log. havok-core has no logger of its own; this is opt-in and once is
        // enough (persists for every later Resolve/LoadMerged).
        havok::model::YamlBehaviorLoader::SetDiagnosticSink(
            [](const std::string& m) { LOG_WARN("{}", m); });

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
            std::shared_ptr<const havok::model::IUnitSource> source;
            bool                                             isChar;
        };
        std::unordered_map<std::string, std::vector<LayerBuild>> layers;
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
            // authoritative roster is the vanilla animations.txt (unioned by CharacterYamlLoader) plus
            // any explicit animationnames\<char>.txt drop and the rosterref clip membrane. Force-adding
            // every file here was over-inclusive: a full master carries ~5900 anims under
            // meshes\actors\character\animations\, but only ~1656 belong to defaultmale — the rest are
            // other character variants, DLC/creature-under-character projects, first-person, or
            // DAR/behavior-only clips. Folding all of them into defaultmale/defaultfemale inflated the
            // roster ~3.5x; char-setup binds against roster.size() (== the OAR synchronized offset), so
            // the surplus entries overran the binding/offset math → the AutoplayBehavior char-setup CTD.
            // (These recompiled natives are also STAGED to the dead-end meshes\CBanims\ — not served —
            // so rostering them bound nothing anyway.) When the real in-memory serve lands, binding will
            // be driven from the authoritative roster, not from raw file discovery.
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
                // ordered first and the merge tried to use a delta bundle (no behavior.yaml) as
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
                {
                    std::vector<std::string> mfWarn;
                    BundleManifest mf = BundleManifest::Load(bundle, bundle.stem().string(), mfWarn);
                    for (const auto& w : mfWarn) LOG_WARN("Resolver: manifest — {}", w);
                    logManifest(mf);
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
                    if (std::error_code rfe; ui->is_regular_file(rfe)) {
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
                    // A skeleton tree (bonelist.yaml) is NOT a behavior graph — it serves via the
                    // skeleton path. Skip it here, else the has-subdir fallback below accepts it
                    // (it has bones/), the compile fails on it, and byteserve claims its .hkx open.
                    if (fs::exists(unit / "bonelist.yaml", uec)) continue;
                    // The vanilla base carries behavior.yaml (or character.yaml); a mod
                    // delta usually does not (just changed/new node subfolders — or, for
                    // a character roster delta, a bare animations.txt). Accept any —
                    // LoadMerged requires the yaml only on the base (lowest) layer.
                    bool valid = fs::exists(unit / "behavior.yaml", uec) ||
                                 fs::exists(unit / "character.yaml", uec) ||
                                 fs::exists(unit / "animations.txt", uec);
                    if (!valid)
                        for (fs::directory_iterator si(unit, uec), sEnd; !uec && si != sEnd; si.increment(uec))
                            if (si->is_directory(uec)) { valid = true; break; }
                    if (!valid) continue;

                    // Serve key = the unit's path RELATIVE TO THE BUNDLE (its game data
                    // path + graph), e.g. "meshes/actors/character/behaviors/0_master.hkx".
                    const std::string key = NormalizeKey(unit.lexically_relative(bundle).generic_string());
                    if (key.empty() || key[0] == '.') continue;

                    layers[key].push_back({ plugName,
                                            std::make_shared<havok::model::DiskUnitSource>(unit),
                                            fs::exists(unit / "character.yaml", uec) });
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
                auto arc = havok::model::HkyArchive::LoadFromFile(bundle.string(), err);
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
                        // Projects are NOT served — BR synthesizes its own .br.hkx project in
                        // ProjectRedirect (its char ref points at the cache); a vanilla project
                        // graph would only compete with that. Behaviors + characters serve. But FIRST
                        // capture the project's ORIGINAL-CASE characterFilenames ref from project.yaml —
                        // ProjectRedirect re-emits the vanilla identity from it (the case-sensitive bind
                        // the engine does on that string; a lowercased ref A-poses vanilla-only actors).
                        if (u.kind == havok::model::HkyArchive::UnitKind::Project) {
                            if (auto src = arc->source(u.prefix))
                                if (auto py = src->read("project.yaml"))
                                    if (std::string ref = FirstCharFilename(*py); !ref.empty())
                                        m_projectOrigCharRef[NormalizeKey(u.prefix)] = std::move(ref);
                            continue;
                        }
                        // Skeletons are NOT a behavior/character graph — they compile through the dedicated
                        // skeleton path (m_skeletonServe, layer-driven). Adding them here made Owns() true so
                        // ByteServe REDIRECTED every skeleton load into community_behaviors_cache, but no graph compile
                        // ever wrote that file — the redirect hit a missing file and the skeleton failed to
                        // load: universal A-pose + no root motion. Leave unlayered skeletons to vanilla.
                        if (u.kind == havok::model::HkyArchive::UnitKind::Skeleton) continue;
                        const std::string key = NormalizeKey(u.prefix);   // already lower/'/'-normal
                        if (key.empty() || key[0] == '.') continue;
                        layers[key].push_back({ plugName, arc->source(u.prefix),
                                                u.kind == havok::model::HkyArchive::UnitKind::Character });
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
                    {
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

        // Skeleton SERVE (Stage D) + bone-name tables. BR ships EVERY vanilla skeleton as a YAML tree
        // in Skyrim.hky (bonelist.yaml + bones/<name>.yaml); a bundle's skeleton/<actorpath>/bones/
        // layer (e.g. XPMSSE) EXTENDS it. For each actor with a layer we compile base-YAML + adds via
        // CompileSkeletonFull (anim + ragdoll + constraints + mappers) and serve at the unit path. The
        // whole thing is PATH-DRIVEN off the .hky's Data-mirrored tree — nested creatures
        // (dlc01/chaurusflyer, canine dog/wolf) and every skeleton VARIANT of an actor just work. No
        // layer ⇒ no serve, so add-no-bones setups are untouched. (Replaces the old read-binary +
        // CompileSkeletonOverBase path — the binaries are gone; BR owns the skeleton fully from YAML.)
        {
            const fs::path pluginsRoot = dataDir / "community_behaviors" / "plugins";
            std::optional<BundleReader> master;          // Skyrim.hky — the base-skeleton authority
            std::vector<BundleReader>   allBundles;      // every bundle — scanned for skeleton/ layers
            {
                std::error_code me;
                if (fs::is_directory(pluginsRoot, me))
                    for (fs::directory_iterator bi(pluginsRoot, me), bend; !me && bi != bend; bi.increment(me)) {
                        if (ToLower(bi->path().stem().string()) == "skyrim") master = BundleReader::Open(bi->path());
                        if (auto br = BundleReader::Open(bi->path())) allBundles.push_back(std::move(*br));
                    }
            }

            // Read a base skeleton YAML unit (meshes/actors/.../skeleton*.hkx) from the master into a
            // SkeletonData. filesUnder (normalized, for read()) + filesUnderOrig (original case, for the
            // bone NAME) walk the same range — index i aligns — so bone names keep their exact case (the
            // clip/skin/roster name identity; a lowercased bone would orphan its skin binding, BR-16).
            auto readBaseUnit = [&](const std::string& unit, havok::skeleton::SkeletonData& out) -> bool {
                if (!master) return false;
                const std::string bl = master->read(unit + "/bonelist.yaml").value_or("");
                const auto norm = master->filesUnder(unit + "/bones/", ".yaml");
                const auto orig = master->filesUnderOrig(unit + "/bones/", ".yaml");
                std::vector<std::pair<std::string, std::string>> boneFiles;
                for (std::size_t i = 0; i < norm.size() && i < orig.size(); ++i)
                    if (auto t = master->read(norm[i]))
                        boneFiles.emplace_back(fs::path(orig[i]).stem().string(), *t);
                if (bl.empty() && boneFiles.empty()) return false;
                std::string err;
                if (!havok::skeleton::LoadSkeletonYamlFromTexts(bl, boneFiles, out, &err)) {
                    LOG_WARN("Resolver: base skeleton '{}' YAML load failed: {}", unit, err);
                    return false;
                }
                return true;
            };

            // 1) Collect bone-add layers across bundles, keyed by actorpath (nested). A layer file lives
            //    at "skeleton/<actorpath>/bones/<name>.yaml"; original-case stem = the bone name.
            std::map<std::string, std::vector<std::pair<std::string, std::string>>> layerTexts;
            std::map<std::string, std::string> layerBonelist;   // actorpath -> bonelist.yaml text (added-bone order)
            for (auto& br : allBundles) {
                const auto norm = br.filesUnder("skeleton/", ".yaml");
                const auto orig = br.filesUnderOrig("skeleton/", ".yaml");
                for (std::size_t i = 0; i < norm.size() && i < orig.size(); ++i) {
                    const std::string& n  = norm[i];
                    if (n.rfind("skeleton/", 0) != 0) continue;
                    // A layer's bonelist.yaml (skeleton/<actorpath>/bonelist.yaml) records the SOURCE bone
                    // order — load-bearing so HKX-target animations bind added bones to the right indices.
                    // bl/bp > 8 rejects a rootless drop ("skeleton/bonelist.yaml", "skeleton/bones/x.yaml"):
                    // there the delimiter sits at index 8, so `bl - 9`/`bp - 9` would underflow (size_t wraps
                    // to SIZE_MAX and substr clamps to a bogus non-empty tail, defeating the empty guards).
                    if (const auto bl = n.rfind("/bonelist.yaml"); bl != std::string::npos && bl > 8 && bl + 14 == n.size()) {
                        const std::string actorpath = n.substr(9, bl - 9);   // after "skeleton/", before "/bonelist.yaml"
                        if (!actorpath.empty()) if (auto t = br.read(n)) layerBonelist[actorpath] = std::move(*t);
                        continue;
                    }
                    const auto bp = n.find("/bones/");
                    if (bp == std::string::npos || bp <= 8) continue;
                    const std::string actorpath = n.substr(9, bp - 9);   // after "skeleton/", before "/bones/"
                    if (actorpath.empty()) continue;
                    if (auto t = br.read(n)) layerTexts[actorpath].emplace_back(fs::path(orig[i]).stem().string(), *t);
                }
            }
            const auto bonelistFor = [&](const std::string& ap) -> const std::string& {
                static const std::string kEmpty;
                auto it = layerBonelist.find(ap);
                return it == layerBonelist.end() ? kEmpty : it->second;
            };

            // 2) SERVE: for each actor with a layer, compile every skeleton variant (base-YAML + adds).
            // DIAGNOSTIC opt-out (marker file Data\community_behaviors\noskeletonserve.enable): skip the
            // skeleton SERVE entirely so skeleton opens fall through to the mod-provided loose skeleton
            // (e.g. the installed XPMSSE skeleton.hkx, which carries the full 3ds-Max export-helper
            // resource tree BR's derived physics-only container omits). If the A-pose clears with this
            // set, the runtime DOES rely on that export tree and BR must reproduce it. The bone-name
            // table below is still built (behaviors need it), so only the served skeleton changes.
            const bool skipSkelServe = std::filesystem::exists("Data/community_behaviors/noskeletonserve.enable");
            if (skipSkelServe)
                LOG_WARN("Resolver: SKELETON SERVE DISABLED (noskeletonserve.enable) — skeleton opens fall "
                         "through to loose files. Diagnostic only.");
            for (auto& [actorpath, texts] : layerTexts) {
                if (skipSkelServe) break;
                std::vector<havok::skeleton::SkeletonBoneAdd> adds;
                havok::skeleton::LoadSkeletonLayerFromTexts(texts, adds, bonelistFor(actorpath), nullptr);
                if (adds.empty() || !master) continue;
                std::set<std::string> units;   // "meshes/actors/<actorpath>/<variant>/skeleton*.hkx"
                for (const std::string& f : master->filesUnder("meshes/actors/" + actorpath + "/", ".yaml"))
                    if (const auto b = f.rfind("/bonelist.yaml"); b != std::string::npos && b + 14 == f.size())
                        units.insert(f.substr(0, b));
                for (const std::string& unit : units) {
                    havok::skeleton::SkeletonData sk;
                    if (!readBaseUnit(unit, sk)) continue;
                    std::string merr;
                    if (!havok::skeleton::MergeBoneAdditions(sk, adds, &merr))
                        LOG_WARN("Resolver: skeleton '{}' bone-add merge: {} — base unchanged.", unit, merr);
                    auto cr = havok::skeleton::CompileSkeletonFull(sk);
                    if (cr.ok) {
                        m_skeletonServe[NormalizeKey(unit)] = std::move(cr.bytes);
                        LOG_INFO("Resolver: SERVING skeleton '{}' (+{} added, {} total).", unit, adds.size(), sk.bones.size());
                    } else
                        LOG_WARN("Resolver: skeleton '{}' compile failed: {} — not served.", unit, cr.error);
                }
            }

            // 3) BONE-NAME TABLE (behavior bone-index resolution): for each behavior actor, read its
            //    primary skeleton from the master YAML (+ this actor's adds, if any). Keyed by the
            //    ACTOR PATH (segments between actors/ and /behaviors/, so nested creatures stay
            //    distinct — dlc02/netch vs dlc02/riekling). The skeleton unit is found by searching
            //    the actor's whole subtree for a bonelist.yaml — nested creatures and variant
            //    folders ("character assets", "characterassets", "character assets dog") all resolve.
            std::set<std::string> behActors;
            for (const auto& [key, ls] : layers)
                if (std::string ap = ActorPathOf(key); !ap.empty())
                    behActors.insert(std::move(ap));

            for (const std::string& actor : behActors) {
                // First skeleton unit under this actor's subtree (deterministic: filesUnder is sorted).
                if (!master) break;
                std::string unit;
                for (const std::string& f : master->filesUnder("meshes/actors/" + actor + "/", ".yaml"))
                    if (const auto b = f.rfind("/bonelist.yaml"); b != std::string::npos && b + 14 == f.size()) {
                        unit = f.substr(0, b); break;
                    }
                if (unit.empty()) continue;

                havok::skeleton::SkeletonData sk;
                if (!readBaseUnit(unit, sk)) continue;

                std::size_t nAdds = 0;
                if (auto it = layerTexts.find(actor); it != layerTexts.end()) {
                    std::vector<havok::skeleton::SkeletonBoneAdd> adds;
                    havok::skeleton::LoadSkeletonLayerFromTexts(it->second, adds, bonelistFor(actor), nullptr);
                    nAdds = adds.size();
                    std::string merr; havok::skeleton::MergeBoneAdditions(sk, adds, &merr);
                }
                havok::sct::BoneNameTable& tbl = m_skeletons[actor];
                for (const auto& b : sk.bones) tbl.names.push_back(b.name);
                tbl.Reindex();
                LOG_INFO("Resolver: skeleton '{}' = {} bone(s){}.", actor, tbl.names.size(),
                         nAdds ? (" incl. " + std::to_string(nAdds) + " added") : std::string{});
            }
        }

        // Resolve the master DAG into a base-first rank per bundle (masters drive order;
        // loadorder.txt breaks peer ties only) and the set of bundles skipped because a
        // declared master is absent. This REPLACES the old flat loadorder.txt priority —
        // the missing/out-of-order-master conditions the old warn-only pass merely reported
        // are now enforced: a master always sorts base-first, a dependent with no base drops.
        const LoadPlan plan = PlanLoadOrder(m_manifests, order);

        // Record each graph's layers, base-first by the master-DAG rank, for the merge to
        // overlay. The base (front) layer's isChar flag decides the compile path (character
        // vs behavior graph). Layers from a skipped bundle are dropped. Sources are COPIED
        // (cheap shared_ptr) rather than moved so `layers` survives for the conflict scan.
        for (auto& [key, ls] : layers) {
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
            for (auto& l : ls) gs.layers.push_back(l.source);
            gs.isCharacter = !ls.empty() && ls.front().isChar;
            const std::size_t n = gs.layers.size();
            m_sources.emplace(key, std::move(gs));
            LOG_INFO("Resolver: watching '{}'  <-  {} layer(s)", key, n);
        }
        LOG_INFO("Resolver: {} .hky plugin(s), {} graph(s) mapped ({} packed archive(s)).",
                 plugins, m_sources.size(), m_archives.size());

        // Node-granular conflict report: which bundles edit the SAME node in the same graph.
        // Reuses havok-core's NodeContributions (the EXACT merge grouping) so a reported
        // overlap is precisely a node the merge combines. Classify each via the master DAG:
        // the lowest-ranked contributor is where the node first appears (base-first order); a
        // later contributor that MASTERS that base is a legitimate OVERRIDE (load order decides
        // the winner — e.g. two combat mods both editing a vanilla state, both mastering
        // Skyrim). A contributor that does NOT master the base independently introduced the same
        // identity — a namespace CLASH (the bug: two unrelated mods' node #2 collide). Every
        // overlap is recorded (with the flag) for the MO2 manager; only clashes warn.
        {
            std::size_t clashes = 0;
            for (const auto& [key, ls] : layers) {
                if (ls.size() < 2) continue;                        // single bundle -> no overlap
                std::vector<std::shared_ptr<const havok::model::IUnitSource>> srcs;
                srcs.reserve(ls.size());
                for (const auto& l : ls) srcs.push_back(l.source);
                for (auto& nc : havok::model::YamlBehaviorLoader::NodeContributions(srcs)) {
                    NodeConflict c;
                    c.servePath = key;
                    c.section   = std::move(nc.section);
                    c.cls       = std::move(nc.cls);
                    c.key       = std::move(nc.key);
                    c.bundles.reserve(nc.layers.size());
                    for (std::size_t idx : nc.layers) c.bundles.push_back(ls[idx].stem);
                    const std::string& base = c.bundles.front();    // lowest rank = first to introduce it
                    for (std::size_t i = 1; i < c.bundles.size(); ++i) {
                        const std::string& later = c.bundles[i];
                        if (later == base) continue;
                        const auto it = plan.ancestors.find(later);
                        const bool mastersBase = it != plan.ancestors.end() && it->second.count(base);
                        if (!mastersBase) { c.clash = true; break; }  // introduced base's node independently
                    }
                    if (c.clash) {
                        ++clashes;
                        std::string names;
                        for (const auto& b : c.bundles) { if (!names.empty()) names += ", "; names += b; }
                        LOG_WARN("Resolver: node CLASH — graph '{}' node '{}' ({}) touched by unrelated bundles "
                                 "[{}]; none masters the introducer (declare a master to make it an intended "
                                 "override, or rename to avoid the collision).", key, c.key, c.cls, names);
                    }
                    m_conflicts.push_back(std::move(c));
                }
            }
            LOG_INFO("Resolver: node-conflict scan — {} cross-bundle overlap(s), {} clash(es).",
                     m_conflicts.size(), clashes);
        }

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

        // Compile-trace (opt-in [Debug] bCompileTrace in settings.ini — renders in the converter's
        // Debug tab, see CB::debug::kFlags): route the schema-driven probe trace of every behavior
        // compile to a greppable log file. Zero cost when off (no sink -> trace::Enabled() is a null
        // pointer test at the tap). Probes ride on the deployed schema tree (Havok/core/Schema/debug/
        // *.yaml) — edit them to steer.
        std::shared_ptr<std::ofstream> traceLog;
        bool traceOn = false;
        {
            CSimpleIniA ini;
            if (ini.LoadFile("Data/SKSE/Plugins/Community Behaviors/settings.ini") >= 0)
                traceOn = ini.GetBoolValue("Debug", "bCompileTrace", false);
        }
        if (traceOn) {
            traceLog = std::make_shared<std::ofstream>("Data/community_behaviors/compile_trace.log", std::ios::binary);
            havok::model::trace::SetSink([traceLog](std::string_view l) {
                traceLog->write(l.data(), static_cast<std::streamsize>(l.size())); traceLog->put('\n');
            });
            std::string pw;
            const std::size_t np = havok::model::trace::LoadProbes("Data/Community Behaviors/Havok/core/Schema/metadata/debug", &pw);
            if (!pw.empty()) LOG_WARN("Community Behaviors: compile-trace probe load: {}", pw);
            LOG_INFO("Community Behaviors: COMPILE-TRACE ON ({} probe file(s)) -> Data\\community_behaviors\\compile_trace.log", np);
        }

        const std::size_t total = keys.size();
        std::size_t       done  = 0;
        for (const auto& k : keys) {
            Resolve(k);
            if (progress) progress(++done, total);
        }

        if (traceOn) {
            traceLog->flush();
            havok::model::trace::SetSink({});
            havok::model::trace::ClearProbes();
            LOG_INFO("Community Behaviors: compile-trace written.");
        }
    }

    std::size_t Resolver::WriteSkeletonServe(const std::filesystem::path& dataRoot) const
    {
        namespace fs = std::filesystem;
        std::size_t written = 0;
        std::error_code ec;
        for (const auto& [key, bytes] : m_skeletonServe) {
            if (bytes.empty()) continue;
            const std::string rel = servekey::CacheDiskRel(key);
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

    std::size_t Resolver::WriteNativeAnimations(const std::filesystem::path& dataRoot) const
    {
        namespace fs = std::filesystem;
        std::size_t written = 0, failed = 0;
        std::error_code ec;
        for (const auto& [outKey, yamlText] : m_nativeAnims) {
            // outKey: "meshes/actors/<actor>/animations/.../<name>.hkx" (original case). STOP-GAP OUTPUT:
            // write under meshes/CBanims/ instead of the real actor path, so the recompiled natives do NOT
            // clobber the vanilla loose/BSA animations while we validate — the engine won't auto-load them
            // from here (wrong path), so a relocate is a deliberate manual step, and it also lets us watch
            // for an OAR fight without risk. (This mirrors how behaviors were served pre-byteserve; the real
            // serve — behavior_cache / in-memory crc — is a later feature.) Insert "CBanims" after "meshes/".
            std::string stagedRel = outKey;
            if (stagedRel.rfind("meshes/", 0) == 0) stagedRel = "meshes/CBanims/" + stagedRel.substr(7);
            else                                    stagedRel = "meshes/CBanims/" + stagedRel;
            const fs::path out = dataRoot / fs::path(stagedRel);
            try {
                const auto def = havok::anim::AnimationYamlLoader::LoadFromString(yamlText, outKey);
                // INVERSE MEMBRANE: resolve this clip's per-track bone references through the SERVED
                // skeleton (the same actor-path -> m_skeletons lookup the graph compile uses), so its
                // hkaAnimationBinding.transformTrackToBoneIndices follows the actor's bones by name.
                const std::vector<std::string>* boneNames = nullptr;
                if (const std::string actor = ActorPathOf(outKey); !actor.empty())
                    if (const auto it = m_skeletons.find(actor); it != m_skeletons.end())
                        boneNames = &it->second.names;
                const auto r   = havok::anim::CompileAnimation(def, 30, havok::HKXHeader::SkyrimSE(), boneNames);
                if (!r.ok) {
                    ++failed;
                    LOG_ERROR("Community Behaviors: native animation compile FAILED '{}': {}", outKey, r.error);
                    continue;
                }
                fs::create_directories(out.parent_path(), ec);
                std::ofstream f(out, std::ios::binary | std::ios::trunc);
                f.write(reinterpret_cast<const char*>(r.bytes.data()), static_cast<std::streamsize>(r.bytes.size()));
                f.flush();
                if (f.good()) { ++written; LOG_INFO("Community Behaviors: compiled native animation -> '{}'.", out.string()); }
                else          { ++failed;  LOG_ERROR("Community Behaviors: failed writing native animation '{}'.", out.string()); }
            } catch (const std::exception& e) {
                ++failed;
                LOG_ERROR("Community Behaviors: native animation '{}' — {}", outKey, e.what());
            }
        }
        if (written || failed)
            LOG_INFO("Community Behaviors: native animations — {} compiled, {} failed (STAGED under "
                     "Data\\meshes\\CBanims\\ — relocate manually to serve).", written, failed);
        return written;
    }

    std::size_t Resolver::MaterializeCacheToDisk(
        const std::filesystem::path&                                        dataRoot,
        const std::function<void(std::size_t, std::size_t)>&                progress)
    {
        namespace fs = std::filesystem;
        std::error_code ec;

        // The above-OAR serving layout writes two kinds of BR output, both UNDER each
        // actor's own vanilla behavior root <dataRoot>/<folderRoot> (keys are meshes-
        // prefixed, so folderRoot already begins "meshes/..."):
        //   • compiled graphs   -> <folderRoot>/community_behaviors_cache/<subdir>/<file>.hkx
        //   • synthesized project-> <folderRoot>/<charStem>.br.hkx     (a NEW loose file)
        // Both are NEW paths, so no vanilla file is ever overwritten. The redirect hook
        // points the engine's project load at the .br.hkx; its base dir stays the vanilla
        // root, so the cache-qualified child refs resolve into community_behaviors_cache\ and
        // the untouched refs resolve to vanilla.

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
            const fs::path cacheRoot = dataRoot / servekey::kConsolidatedCacheDir;
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
        fs::remove_all(dataRoot / "Meshes" / servekey::kConsolidatedCacheDir, ec);
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
        WriteNativeAnimations(dataRoot);

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
                << watermark::kWatermarkValue << "\n";
        } else {
            LOG_WARN("Community Behaviors: cache incomplete ({} graph(s) failed to compile/write) — "
                     "completion sentinel withheld; the next launch will recompile.", failedGraphs);
        }

        LOG_INFO("Community Behaviors: materialized {}/{} graph cache file(s) under '{}\\{}'.",
                 written, total, dataRoot.string(), servekey::kConsolidatedCacheDir);
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

    // The character's proper-case `name:` — the ONE cased field that survived the lowercased master
    // (every path KEY is lowercased at scan, and the packed .hky stores lowercase). Read from the
    // first layer whose character.yaml carries it (a light text scan, not a full merge). Used to
    // re-case the synthesized project's characterFilenames back to vanilla, which Havok matches
    // CASE-SENSITIVELY (a miscased ref silently binds nothing → universal A-pose; see the case
    // directive in CLAUDE.md).
    static std::string ReadCharacterName(
        const std::vector<std::shared_ptr<const havok::model::IUnitSource>>& layers)
    {
        for (const auto& L : layers) {
            if (!L) continue;
            const auto txt = L->read("character.yaml");
            if (!txt) continue;
            const std::string& t = *txt;
            const auto cp = t.find("character:");
            const auto np = t.find("name:", cp == std::string::npos ? std::size_t{0} : cp);
            if (np == std::string::npos) continue;
            const auto q1 = t.find_first_of("'\"", np);
            if (q1 == std::string::npos) continue;
            const char quote = t[q1];
            const auto q2 = t.find(quote, q1 + 1);
            if (q2 == std::string::npos) continue;
            if (auto name = t.substr(q1 + 1, q2 - q1 - 1); !name.empty()) return name;
        }
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
        return dataRoot / servekey::kConsolidatedCacheDir / "cache.ready";
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
        return stamp == std::to_string(watermark::kWatermarkValue);
    }

    void Resolver::ArmCacheFromDisk(const std::filesystem::path& dataRoot)
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
        WriteNativeAnimations(dataRoot);
        BuildRedirectMap(dataRoot);
        LOG_INFO("Community Behaviors: reusing existing on-disk behavior cache under '{}\\Meshes' "
                 "(recompile skipped — set [Cache] bForceRegenerate=true or delete the cache to rebuild).",
                 dataRoot.string());
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
        const std::string swap = servekey::CacheSwapPath(vanillaProjectPath);

        // Synthesize the project once, on demand, into the consolidated cache at
        // Data\Meshes\community_behaviors_cache\<projKey>. It points at the VANILLA character path (chosen
        // charStem/ref); the ByteServe hook redirects that character open — and every behavior open
        // below it — into the same cache. Tiny packfile (~KB, no graph compile), safe on the
        // actor-load thread. Memoized + guarded.
        {
            std::lock_guard<std::mutex> lock(m_synthMutex);
            if (!m_synthesizedProjects[swap]) {
                bool ok = false;
                try {
                    // KEEP THE VANILLA IDENTITY, ORIGINAL CASE. The project's characterFilenames string is
                    // the descriptor the engine binds the character to its setdata/animdata by — compared
                    // CASE-SENSITIVELY in engine code (havok packfiles/systems are case-sensitive
                    // everywhere except the USVFS file lookup). A lowercased ref ("characters\defaultmale.hkx"
                    // vs vanilla "Characters\DefaultMale.hkx") loads the file but breaks that bind -> A-pose.
                    // BR lost the case (bundle + keys are lowercased). PRIMARY: recover the ref from the BASE
                    // PROJECT UNIT's project.yaml (captured at Init, m_projectOrigCharRef) — original case,
                    // and it works for BSA-only setups because the base ships the project. SECONDARY: read
                    // the loose vanilla project if present. Lowercased fallback only if neither is available.
                    havok::sct::ProjectSpec spec;                       // constants universal (field study)
                    bool gotVanillaIdentity = false;
                    if (auto oc = m_projectOrigCharRef.find(projKey); oc != m_projectOrigCharRef.end() && !oc->second.empty()) {
                        spec.characterFilenames = { oc->second };       // base project unit, ORIGINAL case
                        gotVanillaIdentity = true;
                    }
                    if (!gotVanillaIdentity) {
                        std::error_code fec;
                        const fs::path vanProj = fs::current_path(fec) / "Data" / "Meshes" / fs::path(std::string(vanillaProjectPath));
                        std::ifstream vf(vanProj, std::ios::binary);
                        if (vf) {
                            std::vector<std::uint8_t> vb((std::istreambuf_iterator<char>(vf)), std::istreambuf_iterator<char>());
                            if (!vb.empty()) {
                                const auto pr2 = havok::sct::ReadProject(vb);
                                if (pr2.ok && !pr2.spec.characterFilenames.empty()) { spec = pr2.spec; gotVanillaIdentity = true; }
                            }
                        }
                    }
                    if (!gotVanillaIdentity) {
                        spec.characterFilenames = { chosen->cacheCharRef };  // lowercased fallback (BSA-only setups)
                        LOG_WARN("Community Behaviors: could not read vanilla project '{}' for original-case identity — "
                                 "using lowercased char ref (setdata/animdata bind may fail on a case-sensitive engine path).",
                                 vanillaProjectPath);
                    }
                    const auto pr = havok::sct::BuildProject(spec);
                    if (pr.ok) {
                        // Write to the ORIGINAL-CASE path the swap resolves to — NOT CacheDiskRel(projKey),
                        // which lowercases. MO2's USVFS matches case-sensitively, so the engine's Func3-cased
                        // project open ("...\DefaultMale.hkx") only finds a same-cased file on disk. (This is
                        // the one path the engine opens under a case we don't control; behaviors/characters
                        // below use BR's own lowercase refs and are self-consistent.)
                        const fs::path out = m_dataRoot / fs::path(servekey::CacheDiskRelRaw(vanillaProjectPath));
                        std::error_code ec;
                        fs::create_directories(out.parent_path(), ec);
                        std::ofstream f(out, std::ios::binary | std::ios::trunc);
                        f.write(reinterpret_cast<const char*>(pr.bytes.data()),
                                static_cast<std::streamsize>(pr.bytes.size()));
                        f.flush();
                        ok = f.good();
                        if (ok)
                            LOG_INFO("Community Behaviors: byte-served project '{}' -> vanilla character '{}' "
                                     "(cache '{}').", vanillaProjectPath, chosen->cacheCharRef, swap);
                        else
                            LOG_ERROR("Community Behaviors: failed writing byte-served project '{}'.", out.string());
                    } else {
                        LOG_ERROR("Community Behaviors: project synth FAILED for '{}': {}", vanillaProjectPath, pr.error);
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

        // Base + deltas for this graph, base first — LoadMerged overlays them into one
        // graph (a later layer's same-named node overrides an earlier). Each layer is a
        // disk dir OR a packed-.hky subtree behind the same IUnitSource, so the merge is
        // identical whether the bundle shipped unpacked or as a single compressed .hky.
        const GraphSources& gs = srcIt->second;

        std::shared_ptr<const std::vector<std::uint8_t>> result;
        try {
            const auto t0 = std::chrono::steady_clock::now();

            // Character files (characters/*.hkx = hkbCharacterData) compile via a
            // different path than behavior graphs — flagged at Init by the base unit
            // carrying character.yaml. LoadMerged overlays every layer: the base gives the
            // full character, each mod delta UNIONS its animationNames additions, so the
            // served character carries the complete merged roster the set-data needs —
            // no runtime animationNames injection required.
            if (gs.isCharacter) {
                auto cdata = havok::model::CharacterYamlLoader::LoadMerged(gs.layers);

                // Author-facing roster drop: fold in any animationnames\<stem>.txt additions
                // for this character (keyed by the serve-path file stem, e.g. "defaultmale"
                // for .../characters/defaultmale.hkx). This is the flat, no-deep-path way for
                // a bundle to extend a character's animationNames — the same union LoadMerged
                // does for unit-path animations.txt, deduped case-insensitively.
                if (const auto ci = m_characterAnimNames.find(ToLower(fs::path(key).stem().string()));
                    ci != m_characterAnimNames.end() && !ci->second.empty()) {
                    std::unordered_set<std::string> have;
                    have.reserve(cdata.animations.size() * 2 + 16);
                    for (const auto& a : cdata.animations) have.insert(ToLower(a));
                    std::size_t folded = 0;
                    for (const auto& a : ci->second)
                        if (have.insert(ToLower(a)).second) { cdata.animations.push_back(a); ++folded; }
                    if (folded)
                        LOG_INFO("Resolver: folded {} author roster addition(s) into character '{}'.", folded, key);
                }

                // Capture the FINAL merged roster + actor path for DeriveAnimData (opt-in). The roster is
                // the animationNames space every clip of this character binds into; the actor path (from
                // this character's serve key) is how DeriveAnimData gathers the project's clips from the
                // sink. Keyed by character stem ("defaultmale"). Runs under m_mutex (held for Resolve's
                // body); read after CompileAll with no writers.
                if (m_adsfFromFeature) {
                    const std::string cstem = ToLower(fs::path(key).stem().string());
                    m_characterRosters[cstem] = cdata.animations;
                    m_characterActor[cstem]   = ActorPathOf(key);
                }

                // Cache-qualify the character's main behavior reference (e.g.
                // "Behaviors\0_Master.hkx") when BR serves that behavior, so the redirected
                // above-OAR load reaches BR's compiled 0_master under community_behaviors_cache\
                // instead of the vanilla file. Left vanilla if we don't own it. (Runtime-only:
                // the offline byte-gate calls havok-core directly and never sees this.)
                if (const std::string q = QualifyChildRef(key, cdata.character.behavior);
                    q != cdata.character.behavior) {
                    LOG_INFO("Resolver: cache-qualified character '{}' behavior ref '{}' -> '{}'.",
                             key, cdata.character.behavior, q);
                    cdata.character.behavior = q;
                }

                // Capture the finished cdata (author-drop + qualify applied) + actor, so
                // CompleteCharacterRosters() can re-serve this character with the rosterref-completed
                // animationNames after CompileAll (the clip pool isn't full until every graph resolved).
                m_characterData[key]     = std::make_shared<havok::model::CharacterData>(cdata);
                m_characterActor[ToLower(fs::path(key).stem().string())] = ActorPathOf(key);

                const auto   r  = havok::sct::CompileCharacter(cdata);
                const double ms = std::chrono::duration<double, std::milli>(
                                      std::chrono::steady_clock::now() - t0).count();
                if (r.ok) {
                    result = std::make_shared<const std::vector<std::uint8_t>>(std::move(r.bytes));
                    LOG_INFO("Resolver: compiled character '{}' — {} layer(s), {} anim(s), {} bytes in {:.1f} ms.",
                             key, gs.layers.size(), cdata.animations.size(), result->size(), ms);
                } else {
                    LOG_ERROR("Resolver: character compile FAILED for '{}': {}", key, r.error);
                }
                m_cache.emplace(key, result);
                return result;
            }

            auto data = havok::model::YamlBehaviorLoader::LoadMerged(gs.layers);

            // Compile-trace tap: name-annotated variable-table / binding / topology records for this
            // merged graph (guarded — trace::Enabled() is a null pointer test, so off = free).
            if (havok::model::trace::Enabled()) havok::model::TraceGraph(data, key);

            // Skeleton for bone-name resolution: if the unit didn't carry one, use this actor's
            // merged bone list (base + skeleton-extender appends, folded in Init). Actor key =
            // ActorPathOf(serve path) — the SAME derivation Init used to build the table, so nested
            // creatures (dlc02/netch, ambient/chicken) match instead of collapsing onto a namespace
            // segment. Bone-index fields authored as NAMES then resolve; numeric ones are unaffected.
            if (data.boneNames.empty() && !m_skeletons.empty()) {
                if (const std::string actor = ActorPathOf(key); !actor.empty())
                    if (const auto it = m_skeletons.find(actor); it != m_skeletons.end())
                        data.boneNames = it->second.names;
            }

            // ROSTER MEMBRANE (rosterref) — collect this graph's clip animationNames into the actor's
            // pool. The schema tags hkbClipGenerator.animationName `rosterref: animationNames`; every such
            // value across an actor's served graphs must land in that actor's character animationNames or
            // the clip A-poses (and OAR's synchronized offset = roster.size() must count them all).
            // CompleteCharacterRosters() folds this pool into each character after CompileAll. Always-on —
            // this is graph-clip collection, independent of the opt-in adsf-derive serve.
            if (const std::string actor = ActorPathOf(key); !actor.empty()) {
                auto& pool = m_actorClipAnims[actor];
                for (const auto& [nm, clip] : data.clips)
                    if (!clip.animationName.empty() && pool.seen.insert(ToLower(clip.animationName)).second)
                        pool.names.push_back(clip.animationName);
            }

            // Union mod-declared symbols into this graph's data before compiling —
            // the compile-time equivalent of BDI's runtime append (spec §6). New
            // events/variables land in the string data with fresh indices; names the
            // graph already declares are left alone.
            if (data.graphData) {
                if (const std::size_t added = m_symbols.InjectInto(*data.graphData, key))
                    LOG_INFO("Resolver: injected {} mod symbol(s) into '{}'.", added, key);

                // Watermark: stamp an INT32 "BR_Watermark" variable (= plugin version) into every
                // compiled graph. A runtime GetGraphVariableInt on the actor's root graph proves BR
                // served it (vanilla graphs lack the variable); the value flags a stale cache. Append
                // only if absent, so it never shifts an existing variable index.
                {
                    auto& vars = data.graphData->variables;
                    const bool present = std::any_of(vars.begin(), vars.end(),
                        [](const havok::model::VariableInfoDef& v) { return v.name == watermark::kWatermarkVar; });
                    if (!present) {
                        havok::model::VariableInfoDef wm;
                        wm.name  = watermark::kWatermarkVar;
                        wm.type  = "VARIABLE_TYPE_INT32";
                        wm.value = watermark::kWatermarkValue;
                        vars.push_back(std::move(wm));
                    }
                }

                // Compile-time graph features (data-driven, ordered). Each self-registered
                // IGraphFeature under features/ mutates this graph; the enabled set + order are
                // DATA. See CLAUDE.md "Compiler features — CB::features (SOP)".
                {
                    ResolverFeatureLog          fLog;
                    // animData is supplied only when the adsf-derive path is opted in, so the
                    // contributor feature's AppliesTo (ctx.animData != nullptr) is a no-op otherwise.
                    CB::features::FeatureContext    fctx{ .graphKey = key, .log = fLog,
                                                      .animData = (m_adsfFromFeature ? &m_clipSink : nullptr) };

                    // ENABLED set (from settings) — WHICH features run. The ORDER is no longer written
                    // here: ResolveRunOrder topo-sorts by each feature's declared RunsAfter/RunsBefore,
                    // so a new feature composes in by declaring its dependencies (e.g. a future
                    // "animation-relay.clip-index-binding" runs-after "true-cinematics.reference-wiring"
                    // and runs-before "engine-relay.wildcard-gate") without editing this list. The ER
                    // wildcard gate stays OPT-IN ([ERGate] bEnable, default OFF until proven in-engine)
                    // — a fault there rewrites every wildcard and would T-pose every actor.
                    std::vector<std::string> enabledIds;
                    if (m_erGateEnabled)   enabledIds.emplace_back("engine-relay.wildcard-gate");
                    if (m_adsfFromFeature) enabledIds.emplace_back("animation-relay.adsf-derive");

                    auto& reg    = CB::features::FeatureRegistry::Instance();
                    auto  runIds = reg.ResolveRunOrder(enabledIds, fLog);
                    for (const auto& [id, r] : reg.Run(runIds, data, fctx))
                        if (r.applied && r.mutations)
                            LOG_INFO("Resolver: feature {} → {} mutation(s) in '{}'.", id, r.mutations, key);
                }
            }

            // Cache-qualify every hkbBehaviorReferenceGenerator child reference BR owns, so
            // the recursive above-OAR load pulls BR's compiled sub-behaviors (under
            // community_behaviors_cache\) while unowned children (e.g. crossbow_direction_behavior)
            // resolve to their vanilla files. This is what carries the redirect down the whole
            // graph tree without any deep hook. Runtime-only — the offline byte-gate is
            // unaffected (it compiles via havok-core directly, not through the Resolver).
            {
                std::size_t qualified = 0;
                for (auto& [nodeName, ref] : data.behaviorReferences) {
                    if (const std::string q = QualifyChildRef(key, ref.behaviorName);
                        q != ref.behaviorName) {
                        ref.behaviorName = q;
                        ++qualified;
                    }
                }
                if (qualified)
                    LOG_INFO("Resolver: cache-qualified {} behavior reference(s) in '{}'.", qualified, key);
            }

            const auto r    = havok::sct::CompileBehavior(data);
            const auto t1   = std::chrono::steady_clock::now();
            const double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();

            if (r.ok) {
                result = std::make_shared<const std::vector<std::uint8_t>>(std::move(r.bytes));
                LOG_INFO("Resolver: compiled '{}' — {} layer(s) merged, {} bytes in {:.1f} ms.",
                         key, gs.layers.size(), result->size(), ms);
            } else {
                LOG_ERROR("Resolver: compile FAILED for '{}': {}", key, r.error);
            }
        } catch (const std::exception& e) {
            LOG_ERROR("Resolver: exception resolving '{}': {}", key, e.what());
        }

        m_cache.emplace(key, result);
        return result;
    }

    // ── roster membrane finalize (rosterref) ──────────────────────────────────────────
    // The inverse of the skeleton membrane. During CompileAll every graph's hkbClipGenerator
    // animationNames (schema: `rosterref: animationNames`) were collected per actor into
    // m_actorClipAnims. Here — after the whole load order resolved, so the pool is complete — fold that
    // pool into each character's animationNames and re-serve the character. A clip whose animationName is
    // absent from the roster A-poses (char-setup can't bind it); OAR's synchronized offset =
    // roster.size() also needs every bound clip counted. Append-only (existing indices preserved, so the
    // collated animationdata stays valid whether or not DeriveAnimData reorders). Base (Skyrim.hky) and
    // mod characters flow through identically — both compiled here, both folded from the same pool.
    // Must run AFTER CompileAll and BEFORE MaterializeCacheToDisk (which writes m_cache to disk).
    std::size_t Resolver::CompleteCharacterRosters()
    {
        std::size_t completed = 0, appendedTotal = 0;
        for (auto& [key, cdataPtr] : m_characterData) {
            if (!cdataPtr) continue;
            const std::string actor = ActorPathOf(key);
            const auto pi = m_actorClipAnims.find(actor);
            if (pi == m_actorClipAnims.end() || pi->second.names.empty()) continue;

            auto& cdata = *cdataPtr;
            std::unordered_set<std::string> have;
            have.reserve(cdata.animations.size() * 2 + 16);
            for (const auto& a : cdata.animations) have.insert(ToLower(a));

            std::size_t appended = 0;
            for (const auto& nm : pi->second.names)
                if (have.insert(ToLower(nm)).second) { cdata.animations.push_back(nm); ++appended; }
            if (appended == 0) continue;

            const auto r = havok::sct::CompileCharacter(cdata);
            if (!r.ok) {
                LOG_ERROR("Resolver: roster-complete recompile FAILED for character '{}': {}", key, r.error);
                continue;
            }
            m_cache[key] = std::make_shared<const std::vector<std::uint8_t>>(std::move(r.bytes));
            const std::string cstem = ToLower(fs::path(key).stem().string());
            m_characterRosters[cstem] = cdata.animations;
            ++completed; appendedTotal += appended;
            LOG_INFO("Resolver: roster membrane — completed character '{}' (+{} clip anim(s), roster now {}).",
                     key, appended, cdata.animations.size());
        }
        if (completed)
            LOG_INFO("Resolver: roster membrane — completed {} character(s); {} animationName(s) folded from clips.",
                     completed, appendedTotal);

        // Trace the FINAL merged roster of every character (the other side of the clip->animation
        // membrane): one record per slot, so a clip's `anim` bindIdx=N can be cross-referenced to the
        // animation that actually sits at roster slot N. A clip whose bindIdx points at a foreign idle
        // (walk-backward, a companion's root-motion idle) shows up as bindIdx=N vs slot N = that idle.
        if (havok::model::trace::Enabled()) {
            for (const auto& [key, cdataPtr] : m_characterData) {
                if (!cdataPtr) continue;
                int i = 0;
                for (const auto& a : cdataPtr->animations)
                    havok::model::trace::Rec("roster", key, "animationNames", a, "-", "slot",
                                             "idx=" + std::to_string(i++));
            }
        }
        return completed;
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
        if (!m_adsfFromFeature) return false;
        namespace ad = havok::animdata;

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
