#include "PCH.h"
#include <Hooks/hookslib.h>   // repo RE hook toolkit — InstallCallDetour

#include "AnimationSetDataServer.h"
#include "BundleReader.h"
#include "CompileGate.h"   // CB::EnsureCompiledAndArmed — lazy compile driven by the first open

#include <havok/anim/AnimSetDataYaml.h>   // asd::ParseMovesetsYaml (moved to havok-core)

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstdint>
#include <exception>
#include <fstream>
#include <iterator>
#include <map>
#include <optional>
#include <sstream>
#include <string>
#include <system_error>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace fs = std::filesystem;

namespace CB::asdserve {

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
        // Same file/format the behavior Resolver uses.
        std::unordered_map<std::string, int> LoadOrder(const fs::path& ini)
        {
            std::unordered_map<std::string, int> order;
            std::ifstream f(ini);
            std::string line;
            int idx = 0;
            while (std::getline(f, line)) {
                const std::string entry = TrimLine(line);
                if (entry.empty() || entry[0] == '#' || entry[0] == ';') continue;
                order[ToLower(entry)] = ++idx;
            }
            return order;
        }

        // Build a delta SingleFile from one bundle's meshes/animationsetdata/ tree (split
        // form: <project>data/<set>.txt, each a "V3" set-content record). Returns false if
        // the bundle contributes no set-data. Malformed set files are skipped with a warning
        // appended to `warn` (Nemesis patch-form files carrying <!-- … --> markers land here —
        // they start with "V3" but fail the strict parse). Reads through BundleReader so the
        // bundle may be an unpacked dir OR a packed .hky (a packed bundle yields lowercase dir/
        // file names; MergeInto matches case-insensitively, so the merge is unchanged).
        bool LoadBundleDelta(const BundleReader& b, asd::SingleFile& out, std::string& warn)
        {
            constexpr const char* kRoot = "meshes/animationsetdata";
            bool any = false;
            for (const std::string& dirName : b.subdirs(kRoot)) {   // e.g. "defaultmaledata"
                // Reconstruct the single-file header "<Dir>\<Dir-minus-'data'>.txt". Case is
                // whatever the bundle stored; MergeInto matches case-insensitively so this
                // still binds to the correctly-cased vanilla project.
                std::string stem = dirName;
                const std::string low = ToLower(dirName);
                if (low.size() > 4 && low.compare(low.size() - 4, 4, "data") == 0)
                    stem = dirName.substr(0, dirName.size() - 4);

                asd::Project proj;
                proj.header = dirName + "\\" + stem + ".txt";

                const std::string projDir = std::string(kRoot) + "/" + dirName;
                for (const std::string& fn : b.files(projDir, ".txt")) {
                    const auto text = b.read(projDir + "/" + fn);
                    if (!text || text->rfind("V3", 0) != 0) continue;  // project index isn't a set (no "V3")
                    try {
                        asd::SetFile set = asd::ParseSetFile(*text);
                        set.name = fn;                              // "1HMDual.txt"
                        proj.sets.push_back(std::move(set));
                    } catch (const std::exception& e) {
                        warn += "    bad set '" + fn + "': " + e.what() + "\n";
                    }
                }

                if (!proj.sets.empty()) {
                    out.projects.push_back(std::move(proj));
                    any = true;
                }
            }
            return any;
        }

        // A setdata project name maps to its character stem by dropping a trailing "Project"
        // ("HorseProject" -> "horse", "DragonProject" -> "dragon"); humanoid projects have no
        // suffix ("DefaultMale" -> "defaultmale"). Used only to source a YAML moveset's CRC
        // registration from the mod's roster additions — a miss just yields no derived CRCs
        // (the moveset still merges; a delta onto a vanilla set keeps the vanilla registration).
        std::string ProjectToCharStem(const std::string& project)
        {
            std::string s = ToLower(project);
            const std::string suf = "project";
            if (s.size() > suf.size() && s.compare(s.size() - suf.size(), suf.size(), suf) == 0)
                s.erase(s.size() - suf.size());
            return s;
        }

    }  // namespace

    ServeResult ServeSetData(const fs::path& dataDir, const fs::path& loadOrderIni)
    {
        ServeResult r;

        std::error_code ec;
        const auto      order = LoadOrder(loadOrderIni);

        // Gather (priority, delta) from BR .hky bundles ONLY — the primary format, each
        // bundle carrying a mod's behaviors AND its set-data together so the two stay
        // consistent. Legacy Nemesis mods are consumed OFFLINE (br-nemesis-to-hky) into
        // .hky bundles, NOT side-loaded at runtime: a runtime set-data side-load adds a
        // mod's moveset table without its behaviors and desyncs it from the behavior graph
        // (the 2026-08-10 char-setup crash). Runtime Nemesis side-loading stays off the
        // load order until .hky parity is proven and behaviors are ingested too.
        std::vector<std::pair<int, asd::SingleFile>> deltas;
        std::size_t hkyCount = 0;

        // Editable YAML movesets (animation/<Project>/movesets.yaml), collected here with EMPTY
        // CRCs; their registration is derived from the mod's roster additions once charActorRoot
        // is built (below), then folded into `deltas`. Kept separate so that derivation can run
        // after the actor-root map exists.
        std::vector<std::tuple<int, std::string, asd::SingleFile>> yamlDeltas;  // (prio, project, delta)

        // Roster (animationNames) additions per character, unioned across ALL mod bundles — the
        // same additions the served compiled character file now bakes into its roster. Used by
        // the coverage guard below so the served cache never registers a CRC the character's
        // roster can't resolve (that mismatch is the char-setup set-data-bind rep-stosq crash).
        std::unordered_map<std::string, std::vector<std::string>> rosterPaths;  // charName(lower) -> paths

        // Open every MOD bundle once — a <Mod>.hky is EITHER an unpacked dir OR a packed .hky
        // file, read the same way through BundleReader. The Skyrim.hky MASTER (stem "skyrim")
        // is NOT opened here: it ships no set-data delta or roster, only the merge base + the
        // character units the guard needs — so it is opened lazily below, ONLY once some mod
        // contributes, to skip decompressing the 18 MB master on a set-data-free launch.
        struct OpenBundle { std::string stem; BundleReader reader; };
        std::vector<OpenBundle> modBundles;
        fs::path                masterBundle;   // Skyrim.hky path (dir or file), opened lazily

        const fs::path pluginsRoot = dataDir / "community_behaviors" / "plugins";
        if (fs::is_directory(pluginsRoot, ec)) {
            for (fs::directory_iterator bi(pluginsRoot, ec), bend; !ec && bi != bend; bi.increment(ec)) {
                const fs::path bundle = bi->path();
                if (ToLower(bundle.extension().string()) != ".hky") continue;
                const std::string stem = ToLower(bundle.stem().string());  // "bfco" / "skyrim"
                if (stem == "skyrim") { masterBundle = bundle; continue; }  // base + char units, lazily

                auto rd = BundleReader::Open(bundle);
                if (!rd) { LOG_WARN("SetData: cannot read bundle '{}' — skipped.", bundle.string()); continue; }

                // Roster additions (animationnames/<char>.txt) — collected even if the bundle
                // ships no set-data (a character's roster is the union across bundles).
                for (const std::string& fn : rd->files("animationnames", ".txt")) {
                    const std::string charName = ToLower(fs::path(fn).stem().string());
                    if (const auto txt = rd->read(std::string("animationnames/") + fn)) {
                        std::istringstream in(*txt);
                        std::string        line;
                        auto&              dst = rosterPaths[charName];
                        while (std::getline(in, line)) {
                            const std::string t = TrimLine(line);
                            if (!t.empty()) dst.push_back(t);
                        }
                    }
                }

                const auto oit  = order.find(stem);  // loadorder lists bare stems ("bfco")
                const int  prio = (oit != order.end()) ? oit->second : 0;

                asd::SingleFile delta;
                std::string     warn;
                if (LoadBundleDelta(*rd, delta, warn)) {
                    if (!warn.empty())
                        LOG_WARN("SetData: skipped malformed set file(s) in '{}':\n{}", stem, warn);
                    deltas.push_back({ prio, std::move(delta) });
                    ++hkyCount;
                }

                // Editable YAML movesets from the decomposed folder: meshes/animationsetdatasingle
                // file.txt/movesets/<project>.yaml (the same convention the master ships; the old
                // per-actor animdata/movesets/ layout is gone). The project = the file stem. CRCs
                // derive later from the roster.
                for (const std::string& path : rd->filesUnder("meshes/animationsetdatasinglefile.txt/movesets", ".yaml")) {
                    const auto y = rd->read(path);
                    if (!y) continue;
                    std::string proj = path.substr(path.find_last_of('/') + 1);   // "<project>.yaml"
                    if (const auto dot = proj.rfind('.'); dot != std::string::npos) proj.erase(dot);
                    std::string      yerr;
                    asd::SingleFile  ymov = asd::ParseMovesetsYaml(*y, proj, yerr);
                    if (!yerr.empty())
                        LOG_WARN("SetData: movesets.yaml parse in '{}'/{}: {}", stem, proj, yerr);
                    if (!ymov.projects.empty()) {
                        yamlDeltas.emplace_back(prio, proj, std::move(ymov));
                        LOG_INFO("SetData: read movesets for project '{}' from '{}'.", proj, stem);
                    }
                }

                modBundles.push_back({ stem, std::move(*rd) });
            }
        }

        if (deltas.empty() && yamlDeltas.empty()) return r;  // nothing contributes — leave the game's file
        r.attempted = true;
        LOG_INFO("SetData: {} .hky bundle(s) contribute set-data ({} split-text, {} movesets.yaml).",
                 hkyCount + yamlDeltas.size(), hkyCount, yamlDeltas.size());

        // Map each character file stem -> its ACTOR ROOT (backslashed, e.g. "meshes\actors\horse"),
        // from the character units across the master + every mod bundle. The coverage guard needs
        // this to reconstruct the full animation path a roster addition CRCs to: roster paths are
        // actor-relative ("Animations\X.hkx") and the set-data CRC (TripleForAnimation) is over
        // "<actorRoot>\<rel>". The horse's root is "meshes\actors\horse", NOT the humanoid
        // "meshes\actors\character" — hardcoding the latter mis-CRC'd every creature. Char units sit
        // two components under their actor root (<root>/characters[ x]/<stem>.hkx), so strip the last
        // two. Now sourced from the packed master too (its 344 vanilla characters carry these roots).
        std::unordered_map<std::string, std::string> charActorRoot;  // charStem(lower) -> "meshes\actors\..."
        const auto ingestCharUnits = [&](const BundleReader& rd) {
            for (std::string rel : rd.characterUnits()) {
                rel = ToLower(rel);                             // "meshes/actors/horse/characters/horse.hkx"
                const auto s1 = rel.find_last_of('/');
                const auto s2 = (s1 == std::string::npos || s1 == 0) ? std::string::npos : rel.find_last_of('/', s1 - 1);
                if (s2 == std::string::npos) continue;
                std::string root = rel.substr(0, s2);          // "meshes/actors/horse"
                for (char& c : root) if (c == '/') c = '\\';
                std::string stem = rel.substr(s1 + 1);         // "horse.hkx"
                if (const auto dot = stem.rfind('.'); dot != std::string::npos) stem.erase(dot);  // "horse"
                charActorRoot.emplace(stem, root);             // first (base) wins; deltas match anyway
            }
        };

        // Open the master now (lazily) — reused for the base read below. dir or packed .hky.
        std::optional<BundleReader> masterReader;
        if (!masterBundle.empty()) {
            masterReader = BundleReader::Open(masterBundle);
            if (masterReader) ingestCharUnits(*masterReader);
            else LOG_WARN("SetData: cannot read master bundle '{}'.", masterBundle.string());
        }
        for (const auto& ob : modBundles) ingestCharUnits(ob.reader);

        // Fold the YAML movesets into `deltas`, deriving each set's CRC registration from the
        // project character's roster additions (TripleForAnimation over "<actorRoot>\<rosterRel>",
        // the exact form validated against vanilla). Over-registration is benign; a project with
        // no roster additions (or an unmapped character) simply gets no derived CRCs — the moveset
        // still merges, and a delta onto a vanilla set keeps that set's own registration.
        for (auto& [prio, project, ymov] : yamlDeltas) {
            const std::string charStem = ProjectToCharStem(project);
            std::vector<asd::CrcTriple> crcs;
            const auto ri = rosterPaths.find(charStem);
            const auto ai = charActorRoot.find(charStem);
            if (ri != rosterPaths.end() && ai != charActorRoot.end()) {
                std::set<std::tuple<std::uint32_t, std::uint32_t, std::uint32_t>> seen;
                for (const auto& rel : ri->second) {
                    std::string full = ai->second + "\\";
                    for (const char c : rel) full += (c == '/') ? '\\' : c;
                    const asd::CrcTriple t = asd::TripleForAnimation(full);
                    if (seen.emplace(t.folder, t.file, t.ext).second) crcs.push_back(t);
                }
            }
            if (!crcs.empty())
                for (auto& pr : ymov.projects)
                    for (auto& s : pr.sets)
                        if (s.crcs.empty()) s.crcs = crcs;
            LOG_INFO("SetData: movesets.yaml '{}' -> {} set(s), {} derived CRC(s) (char '{}').",
                     project, ymov.projects.empty() ? 0 : ymov.projects.front().sets.size(),
                     crcs.size(), charStem);
            deltas.push_back({ prio, std::move(ymov) });
        }
        r.bundles = deltas.size();

        // Base MUST be PURE VANILLA — merging onto the VFS-winning file DOUBLE-adds a mod's
        // set-data when an upstream tool (or the mod's own loose merged file, e.g. BFCO's
        // 801974) already merged it there. NAME-GATE the base to the Skyrim.hky master.
        //
        // The master no longer ships the monolithic .txt — it ships the decomposed
        // animationsetdatasinglefile.txt/ FOLDER (index.yaml + movesets/ authored + crcs/ baked).
        // COMPOSE the base from that folder: it reproduces vanilla byte-for-byte (gated offline via
        // havok-core-cli setdata-tree-roundtrip). Falls back to a legacy loose .txt (older master or
        // the VFS-winning file) only if the folder is absent, with a warning.
        asd::SingleFile base;
        std::string     baseFrom;
        bool            composed = false;
        if (masterReader) {
            const std::string dir = "meshes/animationsetdatasinglefile.txt";
            if (const auto idxTxt = masterReader->read(dir + "/index.yaml")) {
                std::string ierr;
                const auto  headers = asd::ParseSetdataIndexYaml(*idxTxt, ierr);
                if (!ierr.empty()) LOG_WARN("SetData: base index.yaml parse: {}", ierr);
                std::map<std::string, asd::SingleFile>                                    movesetsByStem;
                std::map<std::string, std::map<std::string, std::vector<asd::CrcTriple>>> crcsByStem;
                const auto stemOf = [](const std::string& path) {
                    std::string s = path.substr(path.find_last_of('/') + 1);
                    if (const auto dot = s.rfind('.'); dot != std::string::npos) s.erase(dot);
                    return s;
                };
                for (const std::string& path : masterReader->filesUnder(dir + "/movesets", ".yaml"))
                    if (const auto y = masterReader->read(path)) {
                        const std::string stem = stemOf(path);
                        std::string       yerr;
                        movesetsByStem[stem] = asd::ParseMovesetsYaml(*y, stem, yerr);
                        if (!yerr.empty()) LOG_WARN("SetData: base movesets '{}': {}", stem, yerr);
                    }
                for (const std::string& path : masterReader->filesUnder(dir + "/crcs", ".yaml"))
                    if (const auto y = masterReader->read(path)) {
                        const std::string stem = stemOf(path);
                        std::string       yerr;
                        crcsByStem[stem] = asd::ParseSetdataCrcsYaml(*y, yerr);
                        if (!yerr.empty()) LOG_WARN("SetData: base crcs '{}': {}", stem, yerr);
                    }
                base     = asd::AssembleSetdata(headers, movesetsByStem, crcsByStem);
                baseFrom = "Skyrim.hky/" + dir + "/ (composed)";
                composed = true;
            }
        }
        if (!composed) {
            const fs::path vfsBase  = dataDir / "meshes" / "animationsetdatasinglefile.txt";
            std::string    baseText;
            if (masterReader)
                if (const auto t = masterReader->read("meshes/animationsetdatasinglefile.txt")) baseText = *t;
            if (!baseText.empty()) {
                baseFrom = "Skyrim.hky/meshes/animationsetdatasinglefile.txt (legacy loose)";
            } else {
                baseFrom = vfsBase.string();
                baseText = ReadFile(vfsBase);
                LOG_WARN("SetData: no animationsetdatasinglefile.txt/ folder in master (bundle '{}') — "
                         "falling back to the loose file '{}'. If an upstream tool already merged the mods "
                         "there, this DOUBLE-merges; ship the decomposed Skyrim.hky master.",
                         masterBundle.string(), vfsBase.string());
            }
            if (baseText.empty()) {
                r.error = "master set-data base not found (bundle: " + masterBundle.string() + ")";
                LOG_ERROR("SetData: {}", r.error);
                return r;
            }
            try {
                base = asd::ParseSingleFile(baseText);
            } catch (const std::exception& e) {
                r.error = std::string("base parse failed: ") + e.what();
                LOG_ERROR("SetData: {}", r.error);
                return r;
            }
        }
        LOG_INFO("SetData: base = '{}' ({} projects).", baseFrom, base.projects.size());
        r.baseProjects = base.projects.size();

        // ── Coverage guard (part 1) ──────────────────────────────────────────────────
        // Snapshot the VANILLA CRCs per character BEFORE the merge appends deltas: a CRC
        // present in vanilla is in that character's baked animationNames, so it always
        // resolves. `crcKey` packs (folder,file); ext is a constant (.hkx). `charOf` maps a
        // project header ("DefaultMaleData\\DefaultMale.txt") to its character ("defaultmale").
        const auto charOf = [](const std::string& header) {
            const auto bs = header.find_last_of("\\/");
            std::string fn = (bs == std::string::npos) ? header : header.substr(bs + 1);
            const auto dot = fn.rfind('.');
            if (dot != std::string::npos) fn = fn.substr(0, dot);
            return ToLower(fn);
        };
        const auto crcKey = [](std::uint32_t folder, std::uint32_t file) {
            return (static_cast<std::uint64_t>(folder) << 32) | file;
        };
        std::unordered_map<std::string, std::unordered_set<std::uint64_t>> covered;  // charName -> {crcKey}
        for (const auto& proj : base.projects) {
            auto& cov = covered[charOf(proj.header)];
            for (const auto& s : proj.sets)
                for (const auto& c : s.crcs) cov.insert(crcKey(c.folder, c.file));
        }

        // Merge base-first: ascending priority so a higher-priority bundle wins on conflict
        // (delta-wins in MergeInto), matching the behavior-graph merge. stable_sort keeps
        // discovery order among equal (e.g. unlisted, prio 0) bundles.
        std::stable_sort(deltas.begin(), deltas.end(),
                         [](const auto& a, const auto& b) { return a.first < b.first; });
        for (auto& [prio, delta] : deltas) {
            const asd::MergeStats s = asd::MergeInto(base, delta);
            r.stats.projectsAdded += s.projectsAdded;
            r.stats.setsAdded += s.setsAdded;
            r.stats.attacksAdded += s.attacksAdded;
            r.stats.crcsAdded += s.crcsAdded;
            r.stats.equipEventsAdded += s.equipEventsAdded;
            r.stats.conditionsAdded += s.conditionsAdded;
        }
        r.mergedProjects = base.projects.size();

        // ── Coverage guard (part 2) ──────────────────────────────────────────────────
        // Each character's roster additions (animationnames, the same set BR bakes into the
        // served compiled character) extend what its set-data may safely reference. Compute the
        // CRC of each addition using THAT character's actor root (not a hardcoded humanoid one),
        // and pool them into a GLOBAL roster-covered set. Global is both correct and safe here:
        // roster additions live under per-actor animation folders, so their CRCs don't collide
        // across actors — and it sidesteps the project-vs-character keying gap (a project header
        // yields the PROJECT stem "horseproject" while a roster file is the CHARACTER stem
        // "horse"; per-character keying silently failed to cover every creature whose project
        // name differs from its character name — the horse combos/T-pose regression). Vanilla
        // coverage stays per-project-precise below.
        std::unordered_set<std::uint64_t> rosterCovered;
        for (const auto& [charName, paths] : rosterPaths) {
            const auto ar = charActorRoot.find(charName);
            const std::string root = (ar != charActorRoot.end()) ? ar->second : "meshes\\actors\\character";
            for (const auto& p : paths) {
                std::string full = root + "\\";
                for (const char c : p) full += (c == '/') ? '\\' : c;
                const asd::CrcTriple t = asd::TripleForAnimation(full);
                rosterCovered.insert(crcKey(t.folder, t.file));
            }
        }
        // ── Coverage guard (part 3) ──────────────────────────────────────────────────
        // DROP a merged CRC only if NEITHER the project's own vanilla coverage NOR any
        // character's roster covers it — that CRC references an animation in no roster and is
        // exactly the char-setup set-data-bind rep-stosq crash; dropping degrades gracefully.
        std::size_t guardDropped = 0;
        std::unordered_map<std::string, int> dropByChar;
        for (auto& proj : base.projects) {
            const std::string cn  = charOf(proj.header);
            const auto        it  = covered.find(cn);
            const std::unordered_set<std::uint64_t>* cov = (it != covered.end()) ? &it->second : nullptr;
            for (auto& s : proj.sets) {
                const auto newEnd = std::remove_if(s.crcs.begin(), s.crcs.end(),
                    [&](const asd::CrcTriple& c) {
                        const std::uint64_t k = crcKey(c.folder, c.file);
                        return !((cov && cov->count(k)) || rosterCovered.count(k));
                    });
                const auto removed = static_cast<std::size_t>(std::distance(newEnd, s.crcs.end()));
                if (removed) { s.crcs.erase(newEnd, s.crcs.end()); guardDropped += removed; dropByChar[cn] += static_cast<int>(removed); }
            }
        }
        if (guardDropped) {
            LOG_WARN("SetData guard: dropped {} unrostered CRC(s) from the served cache to prevent the "
                     "char-setup set-data-bind crash (set-data referenced an animation absent from the "
                     "character's animationNames roster — most often a project whose mod ships no "
                     "character-string-data patch, e.g. HorsePower's horse locomotion).", guardDropped);
            for (const auto& [cn, n] : dropByChar)
                LOG_WARN("SetData guard:   {}: {} CRC(s) dropped (roster incomplete for this character).", cn, n);
        }

        // NO ".br" set-data aliases. BR now serves the compiled project via byte-substitution under
        // the EXACT vanilla path (ByteServe), so the loaded character interns its vanilla project name
        // at character+0x248 — the string both the set-data (FUN_1403e1b20) and anim-data
        // (FUN_1403e1f80) binds CRC. The set-data singleton is keyed by the header's vanilla file stem
        // ("DefaultMale"), which is exactly what a byte-served character queries. No rename → no ".br"
        // key → the stock entry serves. (This mirrors dropping the animdata ".br" alias; both were only
        // needed while the project was renamed to "<Stem>.br.hkx".)

        // Emit + persist to BR's contained cache (see header for the MO2 routing rationale).
        const std::string merged    = asd::EmitSingleFile(base);
        const fs::path    cachePath = dataDir / "community_behaviors_cache" / "animationsetdatasinglefile.txt";
        if (!WriteFile(cachePath, merged)) {
            r.error = "failed to write merged cache: " + cachePath.string();
            LOG_ERROR("SetData: {}", r.error);
            return r;
        }

        r.ok = true;
        r.cachePath = cachePath.string();
        LOG_INFO("SetData: merged {} bundle(s) onto {} base project(s) -> {} project(s) "
                 "(+{} proj, +{} sets, +{} attacks, +{} crcs, +{} equip-events, +{} conditions); wrote {}.",
                 r.bundles, r.baseProjects, r.mergedProjects, r.stats.projectsAdded, r.stats.setsAdded,
                 r.stats.attacksAdded, r.stats.crcsAdded, r.stats.equipEventsAdded, r.stats.conditionsAdded,
                 r.cachePath);
        return r;
    }

    // ── Deterministic redirect: hook the loader's file-open call site ─────────────

    namespace {

        // The engine's set-data file open: FUN_14053b000 calls it as
        //   open(RCX = path BSFixedString value, RDX = &out, R8 = 0, R9 = 0) -> int (0 ok)
        // The path is the single pointer a BSFixedString stores (the game loads it via
        // MOV RCX,[global]); passing our own BSFixedString's stored pointer is ABI-identical.
        using OpenSetDataFn = std::int32_t (*)(std::uintptr_t a_path, std::uintptr_t a_out,
                                               std::uint64_t a_r8, std::uint64_t a_r9);

        REL::Relocation<OpenSetDataFn> _openSetData;                 // original (via write_call)
        std::atomic<bool>              s_setDataRedirectActive{ false };
        RE::BSFixedString              s_setDataCachePath;           // interned redirect path
        bool                           s_setDataHookInstalled = false;

        std::int32_t Hook_OpenSetData(std::uintptr_t a_path, std::uintptr_t a_out,
                                      std::uint64_t a_r8, std::uint64_t a_r9)
        {
            // First open of the set-data file drives the compile gate: block here (engine parked in
            // our hook) until BR has compiled + armed, so the redirect below is live for THIS open
            // and the graph load ordered after us sees the merged serve. No-op after the first call.
            CB::EnsureCompiledAndArmed();

            if (s_setDataRedirectActive.load(std::memory_order_acquire)) {
                // A BSFixedString is one pointer; pass ours in place of the engine's path.
                const std::uintptr_t ours = *reinterpret_cast<std::uintptr_t*>(&s_setDataCachePath);
                if (ours) {
                    static std::atomic<bool> logged{ false };
                    bool expected = false;
                    if (logged.compare_exchange_strong(expected, true))
                        LOG_INFO("SetData: intercepted the engine's set-data open — serving BR's merged cache.");
                    return _openSetData(ours, a_out, a_r8, a_r9);
                }
            }
            return _openSetData(a_path, a_out, a_r8, a_r9);  // pass through untouched
        }

    }  // namespace

    bool InstallSetDataHook()
    {
        const bool ae = REL::Module::get().version()[1] >= 6;  // 1.6.x = AE
        if (!ae) {
            LOG_WARN("SetData: deterministic redirect hook is AE-only — SE will use the global-write fallback.");
            return false;
        }

        // RE (AE, decrypted dump): FUN_14053b000 opens the set-data file via a 5-byte
        // `CALL 0x140d0a100` at 0x14053b084 (RVA 0x53B084), right after loading the
        // single-file path global. Detour that call so we can swap the path.
        constexpr std::uintptr_t kOpenCallSiteRVA = 0x53B084;
        const std::uintptr_t     site = REL::Module::get().base() + kOpenCallSiteRVA;

        // InstallCallDetour guards the 0xE8 (CALL) opcode; 0 => not a CALL (different
        // game version) and the caller falls back to the safe data-only global write.
        const std::uintptr_t orig = hooks::InstallCallDetour<5>(
            site, Hook_OpenSetData, "SetData open redirect (AE)");
        if (!orig) return false;
        _openSetData = orig;
        s_setDataHookInstalled = true;
        return true;
    }

    void ArmSetDataRedirect(const ServeResult& result)
    {
        // Serves ONLY .hky-bundled set-data (the runtime Nemesis side-load that caused the
        // 2026-08-10 char-setup crash was removed). A .hky is expected to carry its behaviors
        // alongside its set-data, so the moveset table and behavior graph stay consistent; a
        // set-data-ONLY bundle whose behaviors aren't also served would re-introduce that
        // crash (OAR Unk3 -> char-setup hkArray resize) — bundle completeness is the author's
        // contract. Safe when idle: with no .hky set-data present, ServeSetData produces no
        // cache and this leaves the redirect inactive.
        if (!s_setDataHookInstalled) {
            RedirectSetDataGlobal(result);  // deterministic hook unavailable — fall back
            return;
        }
        if (!result.ok) {
            LOG_INFO("SetData: no merged cache this launch — set-data redirect stays inactive.");
            return;
        }
        // Intern the cache path once; the hook reads its stored pointer.
        s_setDataCachePath = RE::BSFixedString("community_behaviors_cache/animationsetdatasinglefile.txt");
        s_setDataRedirectActive.store(true, std::memory_order_release);
        LOG_INFO("SetData: redirect armed -> \"community_behaviors_cache/animationsetdatasinglefile.txt\" (serves {}).",
                 result.cachePath);
    }

    void RedirectSetDataGlobal(const ServeResult& result)
    {
        // RE (AE 1.6, decrypted dump): the AnimationClipDataSingleton set-data loader
        // FUN_14053b000 does `MOV RCX,[0x14315c930]; CALL <open>`, where 0x14315c930 is a
        // global BSFixedString set by a static initializer to "Meshes/AnimationSetData
        // SingleFile.txt". RVA = 0x315C930 (VA - 0x140000000). Confirmed via CommonLibSSE
        // (AnimationFileManagerSingleton::AnimationFileInfo documents the same crc scheme).
        constexpr std::uintptr_t kSingleFileGlobalRVA = 0x315C930;
        // Redirect target: BR's merged cache, Data-relative with forward slashes (the form
        // BSResource loose-file resolution expects; the original path is likewise Data-rel).
        static constexpr const char* kRedirectPath = "community_behaviors_cache/animationsetdatasinglefile.txt";

        const bool ae = REL::Module::get().version()[1] >= 6;  // 1.6.x = AE
        if (!ae) {
            LOG_WARN("SetData: set-data global redirect is AE-only for now — SE offset not mapped; "
                     "the merged cache was written but the game will not read it on SE.");
            return;
        }

        const std::uintptr_t base = REL::Module::get().base();
        auto& global = *reinterpret_cast<RE::BSFixedString*>(base + kSingleFileGlobalRVA);

        // Log the CURRENT value first — in-game confirmation that the RVA/RE is correct
        // (it should read "Meshes/AnimationSetDataSingleFile.txt") and that the global is
        // populated by the time we run (static init has executed).
        const char* cur = global.c_str();
        LOG_INFO("SetData: engine single-file path global (rva 0x{:X}) currently = \"{}\".",
                 kSingleFileGlobalRVA, (cur && *cur) ? cur : "(empty)");

        if (!result.ok) {
            LOG_INFO("SetData: no merged cache this launch — leaving the engine path global unchanged.");
            return;
        }

        global = RE::BSFixedString(kRedirectPath);
        LOG_INFO("SetData: redirected engine single-file path global -> \"{}\" (serves {}).",
                 kRedirectPath, result.cachePath);
    }

}  // namespace CB::asdserve
