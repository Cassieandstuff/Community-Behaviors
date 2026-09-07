#include "PCH.h"
#include <Hooks/hookslib.h>   // repo RE hook toolkit — InstallCallDetour

#include "AnimationDataServer.h"
#include "BundleReader.h"
#include "GraphClipSink.h"   // sink->Contributions() (adsf-derive feature, opt-in validation)

#include <havok/anim/AnimDataYaml.h>          // animdata::ParseMotionYaml (editable motion overrides)
#include <havok/anim/AnimationYamlLoader.h>   // native animation.yaml -> AnimationDef (its inline motion:)
#include <havok/sct/AnimDataFromBehavior.h>   // DeriveClipInputsFromBehavior / DeriveProjectPatch
#include <havok/model/BehaviorData.h>
#include <havok/model/yaml/YamlBehaviorLoader.h>

#include <MinHook.h>                   // per-project loader gate (block engine's read on BR's materialize)

#include <tuple>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstdint>
#include <exception>
#include <fstream>
#include <iterator>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <system_error>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace fs = std::filesystem;

namespace CB::adserve {

    namespace {

        std::string ToLower(std::string s)
        {
            std::transform(s.begin(), s.end(), s.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            return s;
        }

        std::string TrimLine(const std::string& line)
        {
            const auto b = line.find_first_not_of(" \t\r\n");
            if (b == std::string::npos) return {};
            const auto e = line.find_last_not_of(" \t\r\n");
            return line.substr(b, e - b + 1);
        }


        std::string ReadFile(const fs::path& p)
        {
            std::ifstream f(p, std::ios::binary);
            if (!f) return {};
            return std::string((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        }

        bool WriteFile(const fs::path& p, const std::string& data)
        {
            std::error_code ec;
            fs::create_directories(p.parent_path(), ec);
            std::ofstream f(p, std::ios::binary | std::ios::trunc);
            if (!f) return false;
            f.write(data.data(), static_cast<std::streamsize>(data.size()));
            return static_cast<bool>(f);
        }

        // Load order (optional): .hky plugin name (lowercased) -> priority, higher wins.
        std::unordered_map<std::string, int> LoadOrder(const fs::path& ini)
        {
            std::unordered_map<std::string, int> order;
            std::ifstream f(ini);
            std::string   line;
            int           idx = 0;
            while (std::getline(f, line)) {
                const std::string entry = TrimLine(line);
                if (entry.empty() || entry[0] == '#' || entry[0] == ';') continue;
                order[ToLower(entry)] = ++idx;
            }
            return order;
        }

        // actor root of a lowercased path ("meshes/actors/character/behaviors/x.hkx", or a base
        // header's "actors/canine/characters dog/dog.hkx") -> "actors/<actor>" — the key the scan
        // and the base headers agree on. Empty if the path isn't an actor behavior/character path.
        std::string ActorRootLower(const std::string& pathLower)
        {
            const auto ap = pathLower.find("actors/");
            if (ap == std::string::npos) return {};
            const std::string rest = pathLower.substr(ap);
            for (const char* seg : { "/behaviors", "/characters" })
                if (const auto b = rest.find(seg); b != std::string::npos) return rest.substr(0, b);
            return {};
        }

        // Roster-from-scan: enumerate a MOD bundle's behavior + character units as adsf `assets:`
        // entries, grouped by actor root, in the bundle's ORIGINAL case (disk dev bundles preserve
        // it; a packed bundle yields lowercase — tolerated because `assets:` are func3 path loads,
        // resolved case-insensitively, and the union onto the base never overwrites its
        // canonically-cased entries). Each unit "meshes/actors/<actor>/<sub>/<name>.hkx" becomes the
        // project-relative "<Sub>\<name>.hkx" (backslashed). Deduped case-insensitively per root.
        void CollectScanAssets(const BundleReader& rd,
                               std::map<std::string, std::vector<std::string>>& byRoot)
        {
            const auto addUnit = [&](const std::string& unitPrefix) {
                const std::string low  = ToLower(unitPrefix);
                const std::string root = ActorRootLower(low);          // "actors/character"
                if (root.empty()) return;
                const auto ap = low.find("actors/");
                std::string rel = unitPrefix.substr(ap + root.size() + 1);  // orig-case "Behaviors/0_Master.hkx"
                for (char& c : rel) if (c == '/') c = '\\';                 // -> "Behaviors\0_Master.hkx"
                auto& v = byRoot[root];
                for (const auto& e : v) if (ToLower(e) == ToLower(rel)) return;
                v.push_back(std::move(rel));
            };
            for (const auto& u : rd.behaviorUnits())  addUnit(u);
            for (const auto& u : rd.characterUnits()) addUnit(u);
        }

        // Collect a bundle's animationdata patches: one ProjectPatch per <Project>~<n> dir
        // under <bundle>/animationdata/. Returns false if the bundle carries none. Reads through
        // BundleReader so the bundle may be an unpacked dir OR a packed .hky (a packed bundle
        // yields lowercase dir/file names).
        bool LoadBundlePatches(const BundleReader& b, std::vector<animdata::ProjectPatch>& out,
                               std::string& warn)
        {
            bool any = false;
            for (const std::string& dirName : b.subdirs("animationdata")) {   // e.g. "defaultmale~1"
                const std::string projDir = std::string("animationdata/") + dirName;
                std::vector<std::pair<std::string, std::string>> files;
                for (const std::string& fn : b.files(projDir, ".txt"))
                    if (const auto t = b.read(projDir + "/" + fn)) files.emplace_back(fn, *t);
                if (files.empty()) continue;

                try {
                    animdata::ProjectPatch pp = animdata::AssembleProjectPatch(dirName, files);
                    if (!pp.additions.empty()) {
                        out.push_back(std::move(pp));
                        any = true;
                    }
                } catch (const std::exception& e) {
                    warn += "    bad patch dir '" + dirName + "': " + e.what() + "\n";
                }
            }
            return any;
        }

        bool EndsWith(const std::string& s, const std::string& suf)
        {
            return s.size() >= suf.size() && s.compare(s.size() - suf.size(), suf.size(), suf) == 0;
        }

        // DERIVE a bundle's animationdata delta from its BEHAVIORS — for a bundle that ships new
        // clips but NO Nemesis animationdata patch (the FNIS converter output). The clips ARE the
        // graph's hkbClipGenerators; the animationdata records are derived from them (the same
        // DeriveClipList the offline oracle is gated on), NOT hand-authored in the Nemesis format.
        // Each addition carries a SYMBOL, so the caller's high-band allocator assigns the real
        // (safe) animIndex.
        //
        // The bundle declares WHICH characters it extends via its animationnames/<char>.txt drops
        // (the same roster the behaviors were folded into); its behavior units carry the clips. So
        // we derive one ProjectPatch per (behavior unit x declared character). Returns {} if the
        // bundle adds no clips to any project.
        std::vector<animdata::ProjectPatch> DeriveBundleAnimData(
            const BundleReader& rd, const std::string& stem, const fs::path& dataDir,
            const BundleReader* master)
        {
            std::vector<std::string> chars;   // character stems this bundle extends ("defaultmale")
            for (const std::string& fn : rd.files("animationnames", ".txt")) {
                std::string s = ToLower(fn);
                if (EndsWith(s, ".txt")) s.erase(s.size() - 4);
                if (!s.empty()) chars.push_back(std::move(s));
            }
            const auto units = rd.behaviorUnits();
            if (chars.empty() || units.empty()) return {};

            std::vector<animdata::ProjectPatch> out;

            // Root motion authored INLINE in BR-native animations (animation.yaml `motion:` -> AnimationDef.
            // motion), collected per actor root (memoized — a character has many behavior units sharing one
            // root) and keyed by the animation's actor-root-relative path (lowercase, '/'-sep). That is the
            // form a clip's animationName normalizes to in DeriveProjectPatch, so each derived clip pairs with
            // its real root motion. Empty for a bundle that authors no inline motion (placeholder kept).
            std::unordered_map<std::string, std::unordered_map<std::string, animdata::MotionRecord>> motionsByRoot;
            const auto motionsForRoot = [&](const std::string& actorRoot)
                -> const std::unordered_map<std::string, animdata::MotionRecord>& {
                auto [it, fresh] = motionsByRoot.try_emplace(actorRoot);
                if (!fresh) return it->second;
                const std::string animDir = actorRoot + "/animations";
                for (const std::string& path : rd.filesUnderOrig(animDir, ".yaml")) {
                    if (!EndsWith(ToLower(path), ".hkx.yaml")) continue;   // full-animation unit only
                    const auto text = rd.read(path);
                    if (!text) continue;
                    havok::anim::AnimationDef def;
                    try { def = havok::anim::AnimationYamlLoader::LoadFromString(*text, path); }
                    catch (const std::exception& e) {
                        LOG_WARN("AnimData: native animation '{}' parse failed (motion skipped): {}", path, e.what());
                        continue;
                    }
                    if (!def.motion) continue;
                    std::string rel = (ToLower(path).rfind(ToLower(actorRoot) + "/", 0) == 0)
                                          ? path.substr(actorRoot.size() + 1) : path;
                    if (EndsWith(ToLower(rel), ".yaml")) rel.erase(rel.size() - 5);   // -> "animations/…/x.hkx"
                    for (char& c : rel) c = (c == '\\') ? '/' : static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                    it->second.emplace(std::move(rel), std::move(*def.motion));
                }
                return it->second;
            };

            for (const std::string& unitPrefix : units) {
                auto src = rd.unitSource(unitPrefix);
                if (!src) continue;

                // A bundle unit is EITHER a full new graph (fnis.hkx, skyparkour_behavior.hkx — carries
                // behavior.yaml) OR a DELTA over a vanilla unit (horsebehavior.hkx, 0_master.hkx — node
                // files only, NO behavior.yaml root). A delta can't load alone (the loader needs a root)
                // and its clips only make sense against the base graph, so merge it OVER the Skyrim.hky
                // master's same unit — exactly like the compile path. Master opened lazily by the caller.
                std::shared_ptr<const havok::model::IUnitSource> baseSrc;
                if (master && !src->read("behavior.yaml").has_value()) {  // delta unit -> needs the base root
                    auto ms = master->unitSource(unitPrefix);
                    if (ms && ms->read("behavior.yaml").has_value()) baseSrc = std::move(ms);
                }

                std::vector<std::shared_ptr<const havok::model::IUnitSource>> sources;
                if (baseSrc) sources.push_back(baseSrc);
                sources.push_back(src);

                havok::model::BehaviorData data;
                try {
                    data = havok::model::YamlBehaviorLoader::LoadMerged(sources);
                } catch (const std::exception& e) {
                    LOG_WARN("AnimData: derive skipped unit '{}' in '{}' — load failed: {}", unitPrefix, stem, e.what());
                    continue;
                }
                auto clips = havok::sct::DeriveClipInputsFromBehavior(data);

                // With a base merged in, `clips` is the FULL graph (vanilla + the delta's new clips). The
                // vanilla clips are ALREADY in the base animationdatasinglefile, and the merge below does
                // NOT dedupe (a duplicate row is the double-merge signature — see the integrity check), so
                // SUBTRACT the base's own clips: keep only the names the delta actually introduces.
                if (baseSrc) {
                    std::unordered_set<std::string> baseNames;
                    try {
                        auto baseData = havok::model::YamlBehaviorLoader::LoadMerged(
                            std::vector<std::shared_ptr<const havok::model::IUnitSource>>{ baseSrc });
                        for (const auto& bc : havok::sct::DeriveClipInputsFromBehavior(baseData))
                            baseNames.insert(ToLower(bc.name));
                    } catch (const std::exception&) { /* base underivable -> keep all (best effort) */ }
                    clips.erase(std::remove_if(clips.begin(), clips.end(),
                                    [&](const auto& c) { return baseNames.count(ToLower(c.name)) != 0; }),
                                clips.end());
                }
                if (clips.empty()) continue;

                // animationNames resolve relative to the ACTOR root (the tree up to "/behaviors/"):
                // e.g. unit meshes/actors/character/behaviors/community_behaviors/fnis.hkx -> the clip's
                // "Animations\…\x.hkx" lives at meshes/actors/character/Animations/…/x.hkx.
                std::string actorRoot = unitPrefix;
                if (const auto p = ToLower(unitPrefix).find("/behaviors"); p != std::string::npos)
                    actorRoot = unitPrefix.substr(0, p);
                const auto readAnim = [&dataDir, actorRoot](const std::string& animName)
                    -> std::vector<std::uint8_t> {
                    std::string rel = animName;
                    for (char& c : rel) if (c == '\\') c = '/';
                    std::ifstream f(dataDir / fs::path(actorRoot) / fs::path(rel), std::ios::binary);
                    if (!f) return {};
                    return std::vector<std::uint8_t>((std::istreambuf_iterator<char>(f)),
                                                     std::istreambuf_iterator<char>());
                };

                const auto& animMotions = motionsForRoot(actorRoot);
                for (const std::string& ch : chars) {
                    auto clipsCopy = clips;   // DeriveProjectPatch appends annotation triggers in place
                    auto pp = havok::sct::DeriveProjectPatch(ch, clipsCopy, stem, readAnim,
                                                             animMotions.empty() ? nullptr : &animMotions);
                    if (!pp.additions.empty()) {
                        LOG_INFO("AnimData: derived {} clip record(s) for project '{}' from '{}' (unit {}).",
                                 pp.additions.size(), ch, stem, unitPrefix);
                        out.push_back(std::move(pp));
                    }
                }
            }
            return out;
        }

        // ── BR-native animationdata deltas (the converter's stage-2 output) ───────────
        // A converted mod ships per-clip yaml under meshes/animationdatasinglefile.txt/clips/<projStem>/
        // (each keyed by `animation:`, the decomposed form the master uses) with sibling motion under
        // .../motion/<projStem>/. This is JUST the Nemesis-patch-dir information in the clean BR form (no
        // mod codes, no verbatim copy) — so it flows through the SAME high-band merge as a Nemesis patch:
        // each clip+motion is one PatchAddition; AllocateHighBandAndMerge assigns the safe synthetic
        // animIndex and pairs the clip with its motion.
        //
        // Why high band, NOT a roster-position index: the animationdatasinglefile animIndex is an INTERNAL
        // clip<->motion link, NOT a roster position — the engine matches an animationdata clip record to
        // its behaviour clip generator by NAME (case-insensitive), and the animation actually PLAYED is
        // bound by the behaviour graph's animationName against the character's animationNames roster (the
        // animationnames fold, a separate leg). Proven in-game: Nemesis mods served at high band (32k+)
        // had working root motion; a roster-position index instead broke it (the mod's motion landed at an
        // index the engine's separately-built served roster didn't agree with). So we keep the safe high
        // band and let the roster leg bind the animation.

        // True iff the bundle ships BR-native animationdata clip deltas (vs a Nemesis patch dir / FNIS graph).
        bool HasNativeAnimDeltas(const BundleReader& rd) {
            return !rd.filesUnder("meshes/animationdatasinglefile.txt/clips", ".yaml").empty();
        }

        // Read the native clip+motion deltas into per-project ProjectPatches (symbol form) — one
        // PatchAddition per clip, carrying its (optional) real motion. The symbol is a placeholder;
        // MergeProjectPatch replaces it with the allocated high-band index (same value on clip + motion).
        std::vector<animdata::ProjectPatch> ReadBundleAnimDeltas(const BundleReader& rd, const std::string& stem) {
            const std::string clipsRoot  = "meshes/animationdatasinglefile.txt/clips";
            const std::string motionRoot = "meshes/animationdatasinglefile.txt/motion";
            const auto nameOf = [](const std::string& path) {
                std::string s = path.substr(path.find_last_of('/') + 1);
                if (const auto dot = s.rfind('.'); dot != std::string::npos) s.erase(dot);
                return s;
            };
            const auto stemOf = [](const std::string& path) {
                const auto last = path.find_last_of('/');
                if (last == std::string::npos || last == 0) return std::string();
                const auto prev = path.find_last_of('/', last - 1);
                return ToLower(path.substr(prev + 1, last - prev - 1));   // project stem (matched lowercase); nameOf keeps clip-name case
            };
            std::map<std::string, animdata::ProjectPatch> byProj;   // projStem -> patch
            int sym = 0;
            for (const std::string& path : rd.filesUnderOrig(clipsRoot, ".yaml")) {   // ORIGINAL case — clip name is case-sensitive
                const auto y = rd.read(path);
                if (!y) continue;
                std::string e;
                animdata::ClipGenerator c = animdata::ParseClipYaml(*y, e);
                const std::string fname = nameOf(path);          // filename == clip name unless disambiguated in-body
                if (c.name.empty()) c.name = fname;
                if (!e.empty()) LOG_WARN("AnimData: native clip '{}': {}", path, e);

                const std::string projStem = stemOf(path);
                const std::string symbol   = stem + "$" + std::to_string(sym++);

                animdata::PatchAddition add;
                add.symbol       = symbol;
                add.clip         = std::move(c);
                add.clip.animIndex = symbol;                     // placeholder; the merge assigns the real index
                add.hasClip      = true;
                if (const auto m = rd.read(motionRoot + "/" + projStem + "/" + fname + ".yaml")) {
                    std::string me;
                    animdata::MotionRecord mr = animdata::ParseMotionSidecar(*m, me);
                    if (!me.empty()) LOG_WARN("AnimData: native motion '{}/{}': {}", projStem, fname, me);
                    mr.animIndex   = symbol;
                    add.motion     = std::move(mr);
                    add.hasMotion  = true;
                }

                auto& pp = byProj[projStem];
                if (pp.projectName.empty()) pp.projectName = projStem;   // MergeProjectPatch matches case-insensitively, ±".txt"
                pp.additions.push_back(std::move(add));
            }
            std::vector<animdata::ProjectPatch> out;
            out.reserve(byProj.size());
            for (auto& [k, pp] : byProj) out.push_back(std::move(pp));
            return out;
        }

        // Final integrity pass over the merged singlefile (diagnostic only — logs, never mutates).
        // Catches the two REAL merge faults, both distinct from vanilla's legitimate structure:
        //   (1) MOD-BAND COLLISION — two records landing on the same HIGH-band animIndex
        //       (>= modBandFloor, the allocator's floor). Every mod clip+motion gets a UNIQUE high
        //       index from AllocateHighBandAndMerge, so a repeat there means the merge double-
        //       assigned → the engine can resolve a mod clip to the wrong motion.
        //   (2) IDENTICAL-ROW DUPLICATE — a byte-for-byte identical clip (or motion) record twice
        //       in one project: the signature of a double-merge (merging onto an already-merged
        //       base, the BR-1 Pandora-pre-merge hazard). Vanilla has ZERO of these.
        // NOT flagged (the old check's false alarm): a shared animIndex across DISTINCT-named
        // vanilla clips is NORMAL — one animation reused by many clips, each with its own
        // triggers/crop (e.g. animIndex 1089 = MT_SprintForward + _CameraControl + H2H_Sprint_Pose).
        // The engine binds clips by NAME, so every such row is load-bearing; deduping would delete
        // real clips. Likewise vanilla motion records are NOT sorted by animIndex, and that is fine.
        void VerifyMergeIntegrity(const animdata::SingleFile& sf, long long modBandFloor)
        {
            const auto pureInt = [](const std::string& s, long long& out) {
                if (s.empty()) return false;
                for (char c : s) if (c < '0' || c > '9') return false;   // animdata indices are non-negative
                out = std::atoll(s.c_str());
                return true;
            };
            const auto clipKey = [](const animdata::ClipGenerator& c) {
                std::string k = c.name; k += '\x1f'; k += c.animIndex; k += '\x1f';
                k += c.playbackSpeed; k += '\x1f'; k += c.cropStart; k += '\x1f'; k += c.cropEnd;
                for (const auto& t : c.triggers) { k += '\x1f'; k += t; }
                return k;
            };
            const auto motionKey = [](const animdata::MotionRecord& m) {
                std::string k = m.animIndex; k += '\x1f'; k += m.duration; k += '\x1e';
                for (const auto& t : m.translations) { k += '\x1f'; k += t; }
                k += '\x1d';
                for (const auto& r : m.rotations) { k += '\x1f'; k += r; }
                return k;
            };

            std::size_t projCollide = 0, projIdentical = 0;
            for (const auto& p : sf.projects) {
                // (1) mod-band index collisions (clips + motions)
                std::unordered_map<long long, int> clipHi, motHi;
                long long idx = 0;
                for (const auto& c : p.clips)
                    if (pureInt(c.animIndex, idx) && idx >= modBandFloor) ++clipHi[idx];
                for (const auto& m : p.motions)
                    if (pureInt(m.animIndex, idx) && idx >= modBandFloor) ++motHi[idx];
                std::size_t clipColl = 0, motColl = 0;
                for (const auto& [k, n] : clipHi) if (n > 1) ++clipColl;
                for (const auto& [k, n] : motHi)  if (n > 1) ++motColl;

                // (2) byte-identical duplicate rows (clips + motions)
                std::unordered_set<std::string> clipSeen, motSeen;
                std::size_t clipDupRow = 0, motDupRow = 0;
                for (const auto& c : p.clips)   if (!clipSeen.insert(clipKey(c)).second) ++clipDupRow;
                for (const auto& m : p.motions) if (!motSeen.insert(motionKey(m)).second) ++motDupRow;

                if (clipColl || motColl) {
                    ++projCollide;
                    LOG_WARN("AnimData: project '{}' — mod-band index collision ({} clip idx + {} motion idx "
                             ">= {} shared by >1 record) — a mod clip may resolve to the wrong motion.",
                             p.name, clipColl, motColl, modBandFloor);
                }
                if (clipDupRow || motDupRow) {
                    ++projIdentical;
                    LOG_WARN("AnimData: project '{}' — {} identical clip row(s) + {} identical motion row(s) "
                             "(byte-for-byte) — signature of a double-merge onto a pre-merged base.",
                             p.name, clipDupRow, motDupRow);
                }
            }
            if (!projCollide && !projIdentical)
                LOG_INFO("AnimData: merge integrity OK ({} project(s): no mod-band collisions, no duplicate rows).",
                         sf.projects.size());
        }

        // Allocate every mod addition a safe HIGH-band animIndex (just under 2^15, above the roster,
        // Pandora's band) and merge, then verify ordering. The high band is REQUIRED: a mod clip's
        // animIndex shares the char-setup animation-binding index space, so a low (roster-range)
        // index collides and crashes char-setup (confirmed in-game 2026-08-11: low ~1656+ crash,
        // ~32568+ work). SHARED by the Nemesis-delta path AND the derived-clip path — both arrive
        // as ProjectPatches in `deltas` (already sorted base-first by priority).
        void AllocateHighBandAndMerge(
            animdata::SingleFile&                                             base,
            std::vector<std::pair<int, std::vector<animdata::ProjectPatch>>>& deltas,
            animdata::MergeStats&                                            stats)
        {
            std::size_t totalAdds = 0;
            for (const auto& [prio, patches] : deltas)
                for (const auto& pp : patches) totalAdds += pp.additions.size();
            long long nextIndex = std::max(animdata::GlobalMaxIndex(base) + 1,
                                           32768LL - static_cast<long long>(totalAdds));
            const long long modBandFloor = nextIndex;   // additions allocate upward from here
            // Ceiling guard: additions run UPWARD from nextIndex, so on a very large load order
            // (nextIndex forced up by GlobalMaxIndex) the top allocation can cross 2^15 — leaving the
            // in-game-verified safe band and setting bit15 of the 16-bit binding index. Surface it
            // rather than silently allocate out of band (VerifyMergeIntegrity only checks collisions).
            if (nextIndex + static_cast<long long>(totalAdds) > 32767LL)
                LOG_WARN("AnimData: high-band allocation ({} adds from {}) would cross the proven safe "
                         "ceiling 32767 — indices may leave the in-game-verified band.", totalAdds, nextIndex);
            LOG_INFO("AnimData: allocating {} mod animIndices from {} (high band, < 2^15).", totalAdds, nextIndex);
            for (auto& [prio, patches] : deltas)
                for (const auto& pp : patches)
                    if (!animdata::MergeProjectPatch(base, pp, stats, nextIndex))
                        LOG_WARN("AnimData: patch targets unknown project '{}' — skipped.", pp.projectName);

            VerifyMergeIntegrity(base, modBandFloor);
        }

        // Per-project (dev) form is opt-in (retail-untested engine path). When enabled, ServeAnimData
        // also writes BR's merged animdata as DirList.txt + <Project>.txt + BoundAnims\Anims_<Project>.txt.
        std::atomic<bool> s_perProjectMode{ false };

        void EmitPerProjectForm(const animdata::SingleFile& merged, const fs::path& dataDir)
        {
            std::error_code ec;
            // Write as LOOSE files at the engine's real read path — loose wins over the BSA (which
            // ships only a partial per-project set), so the engine reads BR's complete merged form
            // directly, no open-redirect hook needed. This is the opt-in experiment's serving path.
            const fs::path adDir = dataDir / "Meshes" / "AnimationData";
            fs::create_directories(adDir / "BoundAnims", ec);
            WriteFile(adDir / "DirList.txt", animdata::EmitDirList(merged));
            for (const auto& p : merged.projects) {
                WriteFile(adDir / p.name, animdata::EmitProjectClips(p));            // <Project>.txt
                if (!p.motions.empty())
                    WriteFile(adDir / "BoundAnims" / ("Anims_" + p.name),            // Anims_<Project>.txt
                              animdata::EmitProjectMotion(p));
            }
            LOG_INFO("AnimData: emitted per-project form ({} project(s)) -> {}.", merged.projects.size(), adDir.string());
        }

    }  // namespace

    ServeResult ServeAnimData(const fs::path& dataDir, const fs::path& loadOrderIni,
                              const GraphClipSink* sink, bool rosterFromScan)
    {
        ServeResult r;

        // ── adsf-derive feature (opt-in) — VALIDATION cut ────────────────────────────
        // When the caller opted in, `sink` holds every graph's feature-derived clip inputs (pushed
        // during CompileAll). This first cut only REPORTS what the unified derive produced — grouped
        // by actor root (the tree up to "/behaviors/"), so each row is what a project's clip cache
        // would be sourced from. It intentionally does NOT alter the emitted file yet: the proven
        // collated merge below still owns the output. Compare this log against the collated result
        // in-engine; once they agree, a focused follow-up makes the sink drive the emit.
        if (sink) {
            std::map<std::string, std::pair<std::size_t, std::set<std::string>>> byActor;  // root -> (clips, uniq anims)
            for (const auto& [graphKey, clips] : sink->Contributions()) {
                std::string root = graphKey;
                if (const auto p = ToLower(graphKey).find("/behaviors"); p != std::string::npos)
                    root = graphKey.substr(0, p);
                auto& [n, anims] = byActor[root];
                n += clips.size();
                for (const auto& c : clips) anims.insert(ToLower(c.animationName));
            }
            LOG_INFO("AnimData[adsf-derive]: sink holds {} clip input(s) across {} graph(s), {} actor root(s):",
                     sink->ClipCount(), sink->GraphCount(), byActor.size());
            for (const auto& [root, tally] : byActor)
                LOG_INFO("AnimData[adsf-derive]:   {} -> {} clip(s), {} unique animation(s).",
                         root, tally.first, tally.second.size());
            LOG_INFO("AnimData[adsf-derive]: VALIDATION-ONLY — the collated merge below still drives the emit.");
        }

        std::error_code ec;
        const auto      order = LoadOrder(loadOrderIni);

        // Gather (priority, patches) from BR .hky bundles. Each bundle carries a mod's
        // animationdata deltas alongside its behaviors + set-data, so all legs stay consistent.
        std::vector<std::pair<int, std::vector<animdata::ProjectPatch>>> deltas;
        std::size_t                                                      bundleCount = 0;

        // Per-project HEADER deltas from mod bundles' index.yaml — a bundle can add `assets:` to an
        // existing project (e.g. a new animation the engine should load for that project) or introduce a
        // whole new project. Merged into the master's base headers before compose (ascending priority).
        std::vector<std::pair<int, std::vector<animdata::ProjectHeader>>> indexDeltas;

        // Roster-from-scan (opt-in): owned assets (behaviors + characters) grouped by actor root,
        // ORIGINAL case, collected from each MOD bundle below; unioned onto the base headers by
        // actor root inside the compose. Empty (and inert) unless rosterFromScan is set.
        std::map<std::string, std::vector<std::string>> scanAssetsByRoot;

        // Editable motion overrides — (priority, project, records) collected from every mod
        // bundle's animation/<Project>/motion/*.yaml. These OVERRIDE existing motion records by
        // animIndex (root-motion edits authored in YAML); they never add a new index, so the
        // animIndex band char-setup depends on is untouched. The build-time derive owns new-clip
        // motion (with proper high-band alignment); this is the runtime "grab all motion/*.yaml".
        std::vector<std::tuple<int, std::string, std::vector<animdata::MotionRecord>>> motionYaml;

        // Each <Mod>.hky is EITHER an unpacked dir OR a packed .hky, read the same way through
        // BundleReader. The Skyrim.hky MASTER (stem "skyrim") ships only the merge base (no
        // animationdata delta), so it is opened lazily below — ONLY once some mod contributes —
        // to skip decompressing the 18 MB master on an animationdata-free launch.
        fs::path masterBundle;

        const fs::path pluginsRoot = dataDir / "community_behaviors" / "plugins";

        // Resolve the master bundle path UP FRONT (independent of directory-iteration order) so a
        // bundle that DERIVES its animationdata can merge each delta unit over the master's base
        // unit (a delta unit — horsebehavior.hkx, 0_master.hkx — has no behavior.yaml root and can't
        // load alone). Opened lazily below, only when a delta unit is actually encountered, to keep
        // the 18 MB master out of an animationdata-free / all-full-graph launch.
        if (fs::is_directory(pluginsRoot, ec))
            for (fs::directory_iterator bi(pluginsRoot, ec), bend; !ec && bi != bend; bi.increment(ec))
                if (ToLower(bi->path().extension().string()) == ".hky" &&
                    ToLower(bi->path().stem().string()) == "skyrim") { masterBundle = bi->path(); break; }
        std::optional<BundleReader> deriveMaster;   // lazily opened base for delta-unit derive

        if (fs::is_directory(pluginsRoot, ec)) {
            for (fs::directory_iterator bi(pluginsRoot, ec), bend; !ec && bi != bend; bi.increment(ec)) {
                const fs::path bundle = bi->path();
                if (ToLower(bundle.extension().string()) != ".hky") continue;
                const std::string stem = ToLower(bundle.stem().string());  // "bfco" / "skyrim"
                if (stem == "skyrim") { masterBundle = bundle; continue; }  // base only, opened lazily

                auto rd = BundleReader::Open(bundle);
                if (!rd) { LOG_WARN("AnimData: cannot read bundle '{}' — skipped.", bundle.string()); continue; }

                const auto oit  = order.find(stem);  // loadorder lists bare stems ("bfco")
                const int  prio = (oit != order.end()) ? oit->second : 0;

                // Roster-from-scan: gather this mod bundle's served behaviors/characters as adsf
                // `assets:` entries (opt-in). Runs for EVERY mod bundle regardless of which delta
                // form it ships below (native/legacy/derive), so it MUST precede the native `continue`.
                if (rosterFromScan) CollectScanAssets(*rd, scanAssetsByRoot);

                // HEADER delta (index.yaml) — collected for EVERY bundle (native + legacy), merged into
                // the base headers before compose. This is how a mod adds assets to a project (the
                // animation-serve route) or ships a new project header.
                if (const auto ix = rd->read("meshes/animationdatasinglefile.txt/index.yaml")) {
                    std::string ierr;
                    auto hs = animdata::ParseAnimdataIndexYaml(*ix, ierr);
                    if (!ierr.empty()) LOG_WARN("AnimData: index.yaml parse in '{}': {}", stem, ierr);
                    if (!hs.empty()) {
                        LOG_INFO("AnimData: bundle '{}' ships {} index.yaml header delta(s).", stem, hs.size());
                        indexDeltas.push_back({ prio, std::move(hs) });
                        ++bundleCount;
                    }
                }

                // BR-NATIVE delta (converter stage-2): the Nemesis-patch information in clean BR form.
                // Flows through the SAME high-band merge as a Nemesis patch dir (proven to serve root
                // motion) — clip+motion per addition, the animation bound by the roster leg. Supersedes
                // the Nemesis-patch / FNIS-derive / motion-override paths for THIS bundle.
                if (HasNativeAnimDeltas(*rd)) {
                    auto patches = ReadBundleAnimDeltas(*rd, stem);
                    std::size_t nclips = 0;
                    for (const auto& pp : patches) nclips += pp.additions.size();
                    if (nclips) {
                        LOG_INFO("AnimData: bundle '{}' ships {} native clip delta(s) across {} project(s).",
                                 stem, nclips, patches.size());
                        deltas.push_back({ prio, std::move(patches) });
                        ++bundleCount;
                    }
                    continue;   // native bundle: skip the legacy patch/derive + motion-override paths
                }

                std::vector<animdata::ProjectPatch> patches;
                std::string                         warn;
                if (LoadBundlePatches(*rd, patches, warn)) {
                    if (!warn.empty())
                        LOG_WARN("AnimData: skipped malformed patch dir(s) in '{}':\n{}", stem, warn);
                    deltas.push_back({ prio, std::move(patches) });
                    ++bundleCount;
                } else {
                    // No shipped Nemesis animationdata — DERIVE the delta from this bundle's
                    // behaviors (the FNIS-converter output ships clips but no patch dir). Same
                    // high-band merge below; the records come from the graph, not the Nemesis format.
                    if (!deriveMaster && !masterBundle.empty()) deriveMaster = BundleReader::Open(masterBundle);
                    auto derived = DeriveBundleAnimData(*rd, stem, dataDir,
                                                        deriveMaster ? &*deriveMaster : nullptr);
                    if (!derived.empty()) {
                        deltas.push_back({ prio, std::move(derived) });
                        ++bundleCount;
                    }
                }

                // Collect this bundle's editable motion overrides from the per-motion folder:
                // meshes/animationdatasinglefile.txt/motion/<project>/<key>.yaml (one record per file,
                // same convention the master ships). project = the parent-dir name; key = the filename
                // (a clip-name label, or "unnamed_<N>" for a raw-index block). A bundle may ship ONLY
                // motion deltas (no Nemesis patch dir), so this is collected independent of LoadBundlePatches.
                // (A native bundle already applied its motion above, so this only runs for legacy bundles.)
                std::unordered_map<std::string, std::vector<animdata::MotionRecord>> modMotion;  // project -> records
                for (const std::string& path : rd->filesUnder("meshes/animationdatasinglefile.txt/motion", ".yaml")) {
                    const auto y = rd->read(path);
                    if (!y) continue;
                    const auto last = path.find_last_of('/');
                    const auto prev = (last == std::string::npos || last == 0) ? std::string::npos : path.find_last_of('/', last - 1);
                    if (prev == std::string::npos) continue;
                    std::string proj = path.substr(prev + 1, last - prev - 1);          // parent dir = project
                    std::string key  = path.substr(last + 1);                           // "<key>.yaml"
                    if (const auto dot = key.rfind('.'); dot != std::string::npos) key.erase(dot);
                    std::string merr;
                    animdata::MotionRecord rec = animdata::ParseMotionSidecar(*y, merr);
                    if (key.rfind("unnamed_", 0) == 0) rec.animIndex = key.substr(8);
                    else                               rec.animation = key;             // clip-name label
                    if (!merr.empty()) LOG_WARN("AnimData: motion parse in '{}'/{}/{}: {}", stem, proj, key, merr);
                    modMotion[proj].push_back(std::move(rec));
                }
                for (auto& [proj, recs] : modMotion)
                    if (!recs.empty()) motionYaml.emplace_back(prio, proj, std::move(recs));
            }
        }

        if (deltas.empty() && motionYaml.empty() && indexDeltas.empty())
            return r;  // no .hky animationdata — leave the game's own file
        r.attempted = true;
        r.bundles   = deltas.size();
        LOG_INFO("AnimData: {} .hky bundle(s) contribute animationdata.", bundleCount);

        // Base MUST be PURE VANILLA. The VFS-winning meshes\animationdatasinglefile.txt is whatever
        // mod provides it — and an upstream behavior tool (Pandora) writes its ALREADY-MERGED output
        // there, so merging onto it DOUBLE-adds every mod's clips (broken root motion, 2026-08-11).
        // NAME-GATE the base to the Skyrim.hky master (congruent with Skyrim.esm).
        //
        // The master ships the decomposed animationdatasinglefile.txt/ FOLDER (index.yaml +
        // clips/ baked + motion/ authored), not the monolithic .txt. COMPOSE the base from it
        // (AssembleAnimdata + resolve motion animIndices against clips) — byte-for-byte vanilla,
        // gated offline by havok-core-cli animdata-tree-roundtrip. Falls back to a legacy loose .txt
        // only if the folder is absent.
        std::optional<BundleReader> masterReader;
        if (!masterBundle.empty()) {
            masterReader = BundleReader::Open(masterBundle);
            if (!masterReader) LOG_WARN("AnimData: cannot read master bundle '{}'.", masterBundle.string());
        }
        animdata::SingleFile base;
        std::string          baseFrom;
        bool                 composed = false;
        if (masterReader) {
            const std::string dir = "meshes/animationdatasinglefile.txt";
            if (const auto idxTxt = masterReader->read(dir + "/index.yaml")) {
                std::string ierr;
                auto        headers = animdata::ParseAnimdataIndexYaml(*idxTxt, ierr);
                if (!ierr.empty()) LOG_WARN("AnimData: base index.yaml parse: {}", ierr);

                // ── Merge mod index.yaml header deltas onto the base (ascending priority) ──────────
                // For a project already in the base: UNION its `assets:` (append the mod's new asset
                // paths, deduped case-insensitively, base order first) — that's how a mod adds an
                // animation for the engine to load. A delta project with a name not in the base is added
                // whole (a new project). fieldX/character/hasAnimData from the base are kept unless the
                // base lacked the project. (One-way header merge; clip/motion bodies merge separately.)
                if (!indexDeltas.empty()) {
                    std::stable_sort(indexDeltas.begin(), indexDeltas.end(),
                                     [](const auto& a, const auto& b) { return a.first < b.first; });
                    std::unordered_map<std::string, std::size_t> byName;   // ToLower(name) -> index in headers
                    for (std::size_t i = 0; i < headers.size(); ++i) byName[ToLower(headers[i].name)] = i;
                    std::size_t assetsAdded = 0, projAdded = 0;
                    for (const auto& [prio, hs] : indexDeltas)
                        for (const auto& dh : hs) {
                            const auto it = byName.find(ToLower(dh.name));
                            if (it == byName.end()) {                      // new project
                                byName[ToLower(dh.name)] = headers.size();
                                headers.push_back(dh);
                                ++projAdded;
                                continue;
                            }
                            auto& base_h = headers[it->second];
                            std::unordered_set<std::string> have;
                            for (const auto& a : base_h.assets) have.insert(ToLower(a));
                            for (const auto& a : dh.assets)
                                if (have.insert(ToLower(a)).second) { base_h.assets.push_back(a); ++assetsAdded; }
                            if (dh.hasAnimData) base_h.hasAnimData = true;
                            if (base_h.character.empty() && !dh.character.empty()) base_h.character = dh.character;
                        }
                    if (assetsAdded || projAdded)
                        LOG_INFO("AnimData: merged index.yaml deltas — +{} asset(s), +{} project(s).", assetsAdded, projAdded);
                }

                // ── Roster-from-scan (opt-in) ────────────────────────────────────────────────
                // UNION the owned havok assets (behaviors + characters CB actually serves, gathered
                // from the mod bundles above) onto each base project by ACTOR ROOT, so the emitted
                // adsf `assets:` — the roster func3 enumerates — reflects the hky contents without a
                // hand-authored index.yaml. Additive + case-insensitive dedup: the base's canonical
                // per-project list is never rewritten (protects the ESM-cased char->adsf bind); only
                // genuinely-new asset paths ride in. A scanned actor root with NO matching base
                // header is a NEW project — deferred (its canonical name must come from unit content),
                // logged not synthesized, so this first cut only augments existing projects.
                if (rosterFromScan && !scanAssetsByRoot.empty()) {
                    std::size_t                     scanAdded = 0, unmatched = 0;
                    std::unordered_set<std::string> matchedRoots;
                    for (auto& h : headers) {
                        if (h.character.empty()) continue;
                        const std::string root = ActorRootLower(ToLower(h.character));
                        const auto        it   = scanAssetsByRoot.find(root);
                        if (it == scanAssetsByRoot.end()) continue;
                        matchedRoots.insert(root);
                        std::unordered_set<std::string> have;
                        for (const auto& a : h.assets) have.insert(ToLower(a));
                        for (const auto& a : it->second)
                            if (have.insert(ToLower(a)).second) { h.assets.push_back(a); ++scanAdded; }
                    }
                    for (const auto& [root, assets] : scanAssetsByRoot)
                        if (!matchedRoots.count(root)) ++unmatched;
                    LOG_INFO("AnimData: roster-from-scan unioned {} asset(s) onto matching project(s); "
                             "{} scanned actor root(s) had no base project (new-project synth is a follow-up).",
                             scanAdded, unmatched);
                }
                std::map<std::string, std::vector<animdata::ClipGenerator>> clipsByStem;
                std::map<std::string, std::vector<animdata::MotionRecord>>  motionByStem;
                std::map<std::string, std::vector<std::string>>             rostersByStem;
                // A per-clip/per-motion path is ".../clips/<stem>/<name>.yaml": name = filename, stem =
                // its parent dir. (nameOf strips the ".yaml"; stemOf takes the parent segment.)
                // filesUnder now returns ORIGINAL-CASE paths, so nameOf preserves the clip NAME's case
                // (load-bearing — the engine matches it to the mixed-case behaviour clip generator), while
                // stemOf is LOWERCASED to key clipsByStem the way AssembleAnimdata looks it up
                // (StemForProjectName == lowercase).
                const auto nameOf = [](const std::string& path) {
                    std::string s = path.substr(path.find_last_of('/') + 1);
                    if (const auto dot = s.rfind('.'); dot != std::string::npos) s.erase(dot);
                    return s;
                };
                const auto stemOf = [](const std::string& path) {
                    const auto last = path.find_last_of('/');
                    if (last == std::string::npos || last == 0) return std::string();
                    const auto prev = path.find_last_of('/', last - 1);
                    return ToLower(path.substr(prev + 1, last - prev - 1));
                };
                // Rosters come from each project's `character:` header ref (index.yaml) — load once.
                for (const auto& h : headers) {
                    if (h.character.empty()) continue;
                    if (const auto rz = masterReader->read("meshes/" + h.character + "/animations.txt")) {
                        std::vector<std::string> roster;
                        std::istringstream in(*rz); std::string line;
                        while (std::getline(in, line)) { const std::string t = TrimLine(line); if (!t.empty()) roster.push_back(t); }
                        if (!roster.empty()) rostersByStem[animdata::StemForProjectName(h.name)] = std::move(roster);
                    }
                }
                for (const std::string& path : masterReader->filesUnderOrig(dir + "/clips", ".yaml"))
                    if (const auto y = masterReader->read(path)) {
                        std::string e; animdata::ClipGenerator c = animdata::ParseClipYaml(*y, e);
                        if (c.name.empty()) c.name = nameOf(path);   // filename is the name unless disambiguated (in-body) — ORIGINAL case
                        clipsByStem[stemOf(path)].push_back(std::move(c));
                        if (!e.empty()) LOG_WARN("AnimData: base clip '{}': {}", path, e);
                    }
                for (const std::string& path : masterReader->filesUnderOrig(dir + "/motion", ".yaml"))
                    if (const auto y = masterReader->read(path)) {
                        std::string e; animdata::MotionRecord m = animdata::ParseMotionSidecar(*y, e);
                        const std::string key = nameOf(path);
                        if (key.rfind("unnamed_", 0) == 0) m.animIndex = key.substr(8);   // hybrid block
                        else                                m.animation = key;            // clip-name label
                        motionByStem[stemOf(path)].push_back(std::move(m));
                        if (!e.empty()) LOG_WARN("AnimData: base motion '{}': {}", path, e);
                    }
                base     = animdata::AssembleAnimdata(headers, clipsByStem, motionByStem, rostersByStem);
                baseFrom = "Skyrim.hky/" + dir + "/ (composed)";
                composed = true;
            }
        }
        if (!composed) {
            const fs::path vfsBase  = dataDir / "meshes" / "animationdatasinglefile.txt";
            std::string    baseText;
            if (masterReader)
                if (const auto t = masterReader->read("meshes/animationdatasinglefile.txt")) baseText = *t;
            if (!baseText.empty()) {
                baseFrom = "Skyrim.hky/meshes/animationdatasinglefile.txt (legacy loose)";
            } else {
                baseFrom = vfsBase.string();
                baseText = ReadFile(vfsBase);
                LOG_WARN("AnimData: no animationdatasinglefile.txt/ folder in master (bundle '{}') — "
                         "falling back to the loose file '{}'. If an upstream tool already merged the mods "
                         "there, this DOUBLE-merges; ship the decomposed Skyrim.hky master.",
                         masterBundle.string(), vfsBase.string());
            }
            if (baseText.empty()) {
                r.error = "master animationdata base not found (bundle: " + masterBundle.string() + ")";
                LOG_ERROR("AnimData: {}", r.error);
                return r;
            }
            try {
                base = animdata::ParseSingleFile(baseText);
            } catch (const std::exception& e) {
                r.error = std::string("base parse failed: ") + e.what();
                LOG_ERROR("AnimData: {}", r.error);
                return r;
            }
        }
        LOG_INFO("AnimData: base = '{}' ({} projects).", baseFrom, base.projects.size());
        r.baseProjects = base.projects.size();

        // Merge base-first: ascending priority so a higher-priority bundle's patches append
        // last. stable_sort keeps discovery order among equal (unlisted, prio 0) bundles.
        std::stable_sort(deltas.begin(), deltas.end(),
                         [](const auto& a, const auto& b) { return a.first < b.first; });
        // Allocate the safe high-band animIndices, merge every ProjectPatch (Nemesis-shipped, BR-native,
        // AND graph-derived alike), and verify the merged file's ordering — one reusable final step.
        AllocateHighBandAndMerge(base, deltas, r.stats);
        r.mergedProjects = base.projects.size();

        // ── Editable motion overrides (animation/<Project>/motion/*.yaml) ─────────────
        // Replace a base motion's data (duration + translation/rotation samples) by animIndex
        // within the matching project. OVERRIDE ONLY: the record must already exist (same
        // animIndex) — no new index is introduced and no count changes, so the animIndex band
        // char-setup reads stays byte-stable. New-clip motion is the build-time derive's job
        // (it high-band-aligns motion with clips); this leg lets an author retune existing root
        // motion in YAML. Apply ascending-priority so a higher-priority bundle's edit wins.
        if (!motionYaml.empty()) {
            std::stable_sort(motionYaml.begin(), motionYaml.end(),
                             [](const auto& a, const auto& b) { return std::get<0>(a) < std::get<0>(b); });
            std::size_t overridden = 0, missed = 0;
            for (const auto& entry : motionYaml) {
                const std::string& proj = std::get<1>(entry);
                auto pit = std::find_if(base.projects.begin(), base.projects.end(),
                    [&](const animdata::Project& p) {
                        std::string n = ToLower(p.name);
                        if (n.size() >= 4 && n.compare(n.size() - 4, 4, ".txt") == 0) n.erase(n.size() - 4);
                        return n == ToLower(proj);
                    });
                if (pit == base.projects.end()) {
                    LOG_WARN("AnimData: motion.yaml targets unknown project '{}' — skipped.", proj);
                    continue;
                }
                // A motion override keys on the clip NAME (the de-hardwired form); resolve it to this
                // project's animIndex via its clips. A record still carrying a raw index uses it as-is.
                std::unordered_map<std::string, std::string> clipIdxByName;
                for (const auto& c : pit->clips) if (!c.name.empty()) clipIdxByName.emplace(c.name, c.animIndex);
                std::unordered_map<std::string, animdata::MotionRecord*> byIdx;
                for (auto& m : pit->motions) byIdx[m.animIndex] = &m;
                for (const auto& rec : std::get<2>(entry)) {
                    std::string idx = rec.animIndex;
                    if (idx.empty() && !rec.animation.empty()) {
                        const auto ci = clipIdxByName.find(rec.animation);
                        if (ci != clipIdxByName.end()) idx = ci->second;
                    }
                    auto mi = byIdx.find(idx);
                    if (mi == byIdx.end()) { ++missed; continue; }  // no such index — derive owns additions
                    mi->second->duration     = rec.duration;
                    mi->second->translations = rec.translations;
                    mi->second->rotations    = rec.rotations;
                    ++overridden;
                }
            }
            if (overridden || missed)
                LOG_INFO("AnimData: applied {} motion override(s) from motion.yaml ({} skipped: no matching "
                         "animIndex — additions are the build-time derive's job).", overridden, missed);
        }

        // NO ".br" project aliases. BR serves its compiled project via byte-substitution under the
        // EXACT vanilla path (the engine opens "<Stem>.hkx" and gets BR's bytes) — so the loaded
        // project's basename stays "<Stem>", the AnimationClipData table keys on the stock "<Stem>"
        // entry, and no alias is needed. Both the descriptor (speed-sampler key) and the loaded-asset
        // name are vanilla; nothing ever sees ".br". (Root cause + fix: BR-1.)
        const std::string merged    = animdata::EmitSingleFile(base);
        const fs::path    cachePath = dataDir / "community_behaviors_cache" / "animationdatasinglefile.txt";
        if (!WriteFile(cachePath, merged)) {
            r.error = "failed to write merged cache: " + cachePath.string();
            LOG_ERROR("AnimData: {}", r.error);
            return r;
        }

        if (s_perProjectMode.load(std::memory_order_acquire))
            EmitPerProjectForm(base, dataDir);

        r.ok        = true;
        r.cachePath = cachePath.string();
        LOG_INFO("AnimData: merged {} bundle(s) onto {} base project(s) "
                 "(+{} clips, +{} motions across {} project-patch(es)); wrote {}.",
                 r.bundles, r.baseProjects, r.stats.clipsAdded, r.stats.motionsAdded,
                 r.stats.projectsPatched, r.cachePath);
        return r;
    }

    // ── Deterministic redirect: hook the loader's file-open call site ─────────────
    namespace {

        // FUN_140536ec0's single-file open: open(RCX = path BSFixedString value, RDX = &out,
        // R8 = 0, R9 = 0) -> int (0 ok). Same open fn (0x140d0a100) as set-data.
        using OpenAnimDataFn = std::int32_t (*)(std::uintptr_t a_path, std::uintptr_t a_out,
                                                std::uint64_t a_r8, std::uint64_t a_r9);

        REL::Relocation<OpenAnimDataFn> _openAnimData;
        std::atomic<bool>               s_redirectActive{ false };
        RE::BSFixedString               s_cachePath;
        bool                            s_hookInstalled = false;

        std::int32_t Hook_OpenAnimData(std::uintptr_t a_path, std::uintptr_t a_out,
                                       std::uint64_t a_r8, std::uint64_t a_r9)
        {
            if (s_redirectActive.load(std::memory_order_acquire)) {
                const std::uintptr_t ours = *reinterpret_cast<std::uintptr_t*>(&s_cachePath);
                if (ours) {
                    static std::atomic<bool> logged{ false };
                    bool                     expected = false;
                    if (logged.compare_exchange_strong(expected, true))
                        LOG_INFO("AnimData: intercepted the engine's animationdata open — serving BR's merged cache.");
                    return _openAnimData(ours, a_out, a_r8, a_r9);
                }
            }
            return _openAnimData(a_path, a_out, a_r8, a_r9);
        }

    }  // namespace

    bool EnablePerProjectAnimData()
    {
        const bool ae = REL::Module::get().version()[1] >= 6;  // 1.6.x = AE
        if (!ae) { LOG_WARN("AnimData: per-project flag patch is AE-only."); return false; }

        // RE (AE, decrypted dump): ShouldLoadCollatedAnimTextData (0x140541FA0) is
        // `MOVZX EAX, byte ptr [0x1420107E0]; RET`. The shipped byte is 0x01 (load the collated
        // AnimationDataSingleFile.txt). Patching it to 0x00 routes the loader down the per-project
        // branch — Meshes\AnimationData\DirList.txt + <Project>.txt + BoundAnims\Anims_<Project>.txt.
        // RETAIL-UNTESTED engine path; caller gates this behind an explicit opt-in.
        constexpr std::uintptr_t kFlagRVA = 0x20107E0;
        const std::uintptr_t     addr = REL::Module::get().base() + kFlagRVA;
        const std::uint8_t       cur  = *reinterpret_cast<const std::uint8_t*>(addr);
        if (cur > 1) {
            LOG_WARN("AnimData: collated flag at rva 0x{:X} = 0x{:02X} (not 0/1) — refusing to patch.", kFlagRVA, cur);
            return false;
        }
        REL::safe_write<std::uint8_t>(addr, std::uint8_t{ 0 });
        s_perProjectMode.store(true, std::memory_order_release);
        LOG_INFO("AnimData: per-project animationdata ENABLED (flag rva 0x{:X}: 0x{:02X}->0x00). "
                 "The loader gate will materialize the per-project files before the engine reads.", kFlagRVA, cur);
        return true;
    }

    void DisablePerProjectAnimData()
    {
        s_perProjectMode.store(false, std::memory_order_release);
        if (REL::Module::get().version()[1] < 6) return;
        constexpr std::uintptr_t kFlagRVA = 0x20107E0;
        REL::safe_write<std::uint8_t>(REL::Module::get().base() + kFlagRVA, std::uint8_t{ 1 });  // back to collated
        LOG_INFO("AnimData: per-project reverted to collated (flag rva 0x{:X} -> 0x01).", kFlagRVA);
    }

    // ── Per-project loader gate ───────────────────────────────────────────────────
    // Rather than race to materialize before the engine reads animdata, GATE the read. Hook the
    // AnimationClipDataSingleton ctor (FUN_140536ec0) and, the first time it fires, run BR's merge
    // + per-project materialize SYNCHRONOUSLY on the engine's thread, then let the ctor proceed to
    // read the now-ready files. Correctness by ordering, not by timing — the engine physically
    // cannot read until BR is done. Keeps plugin load trivial (this just installs the hook).
    namespace {
        using ClipDataCtorFn = void* (*)(void*);
        ClipDataCtorFn    s_origClipDataCtor = nullptr;
        std::atomic<bool> s_gateMaterialized{ false };

        void* Hook_ClipDataCtor(void* a_this)
        {
            bool expected = false;
            if (s_gateMaterialized.compare_exchange_strong(expected, true)) {
                const auto r = ServeAnimData("Data", "Data/community_behaviors/loadorder.txt");
                if (r.attempted && !r.ok)
                    LOG_WARN("AnimData: loader-gate materialize did not complete: {}", r.error);
                else
                    LOG_INFO("AnimData: loader gate — per-project files materialized; engine read proceeds.");
            }
            return s_origClipDataCtor(a_this);
        }
    }  // namespace

    bool InstallPerProjectGate()
    {
        const MH_STATUS init = MH_Initialize();
        if (init != MH_OK && init != MH_ERROR_ALREADY_INITIALIZED) {
            LOG_WARN("AnimData: MH_Initialize failed ({}) — per-project gate NOT installed.", static_cast<int>(init));
            return false;
        }
        // AnimationClipDataSingleton ctor = FUN_140536ec0 (RVA 0x536EC0); entry is a clean 5-byte
        // `MOV [rsp+8],rcx` — a valid detour target, called once to build the clip singleton.
        constexpr std::uintptr_t kClipDataCtorRVA = 0x536EC0;
        void* const target = reinterpret_cast<void*>(REL::Module::get().base() + kClipDataCtorRVA);
        if (MH_CreateHook(target, reinterpret_cast<void*>(&Hook_ClipDataCtor),
                          reinterpret_cast<void**>(&s_origClipDataCtor)) != MH_OK ||
            MH_EnableHook(target) != MH_OK) {
            LOG_WARN("AnimData: failed to install per-project loader gate @ rva 0x{:X}.", kClipDataCtorRVA);
            return false;
        }
        LOG_INFO("AnimData: per-project loader gate installed @ rva 0x{:X} "
                 "(engine's animdata read blocks on BR's merge+materialize).", kClipDataCtorRVA);
        return true;
    }

    bool InstallAnimDataHook()
    {
        const bool ae = REL::Module::get().version()[1] >= 6;  // 1.6.x = AE
        if (!ae) {
            LOG_WARN("AnimData: deterministic redirect hook is AE-only — SE will use the global-write fallback.");
            return false;
        }

        // RE (AE, decrypted dump): FUN_140536ec0 opens the single file via a 5-byte
        // `CALL 0x140d0a100` at 0x140536f8e (RVA 0x536F8E), right after `MOV RCX,[0x14315c918]`
        // (the single-file path global). It is the FIRST of the loader's three opens (the other
        // two load secondary per-project listing paths) — detour only this one.
        constexpr std::uintptr_t kOpenCallSiteRVA = 0x536F8E;
        const std::uintptr_t     site = REL::Module::get().base() + kOpenCallSiteRVA;

        // InstallCallDetour guards the 0xE8 (CALL) opcode; 0 => not a CALL (different
        // game version) and the caller falls back to the safe global-write path.
        const std::uintptr_t orig = hooks::InstallCallDetour<5>(
            site, Hook_OpenAnimData, "AnimData open redirect (AE)");
        if (!orig) return false;
        _openAnimData    = orig;
        s_hookInstalled  = true;
        return true;
    }

    void ArmAnimDataRedirect(const ServeResult& result)
    {
        if (!s_hookInstalled) {
            RedirectAnimDataGlobal(result);  // deterministic hook unavailable — fall back
            return;
        }
        if (!result.ok) {
            LOG_INFO("AnimData: no merged cache this launch — animationdata redirect stays inactive.");
            return;
        }
        s_cachePath = RE::BSFixedString("community_behaviors_cache/animationdatasinglefile.txt");
        s_redirectActive.store(true, std::memory_order_release);
        LOG_INFO("AnimData: redirect armed -> \"community_behaviors_cache/animationdatasinglefile.txt\" (serves {}).",
                 result.cachePath);
    }

    void RedirectAnimDataGlobal(const ServeResult& result)
    {
        // RE (AE 1.6, decrypted dump): the animationdata loader FUN_140536ec0 does
        // `MOV RCX,[0x14315c918]; CALL <open>`, where 0x14315c918 is a global BSFixedString set
        // by static-init FUN_140090050 to "Meshes/AnimationDataSingleFile.txt". RVA = 0x315C918.
        constexpr std::uintptr_t     kSingleFileGlobalRVA = 0x315C918;
        static constexpr const char* kRedirectPath = "community_behaviors_cache/animationdatasinglefile.txt";

        const bool ae = REL::Module::get().version()[1] >= 6;
        if (!ae) {
            LOG_WARN("AnimData: global redirect is AE-only for now — SE offset not mapped; the merged "
                     "cache was written but the game will not read it on SE.");
            return;
        }

        const std::uintptr_t base   = REL::Module::get().base();
        auto&                global = *reinterpret_cast<RE::BSFixedString*>(base + kSingleFileGlobalRVA);

        const char* cur = global.c_str();
        LOG_INFO("AnimData: engine single-file path global (rva 0x{:X}) currently = \"{}\".",
                 kSingleFileGlobalRVA, (cur && *cur) ? cur : "(empty)");

        if (!result.ok) {
            LOG_INFO("AnimData: no merged cache this launch — leaving the engine path global unchanged.");
            return;
        }

        global = RE::BSFixedString(kRedirectPath);
        LOG_INFO("AnimData: redirected engine single-file path global -> \"{}\" (serves {}).",
                 kRedirectPath, result.cachePath);
    }

}  // namespace CB::adserve
