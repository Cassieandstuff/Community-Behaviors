#include "PCH.h"
#include <platform/game/Resource.h>     // the SpeedDataOpen call site

#include "core/serve/SpeedDataServer.h"
#include "core/discover/BundleReader.h"

#include <codec/format/LoadOrderText.h>        // ReadLoadOrder — bundle priority (a later line wins, as everywhere)
#include <codec/resource/PathKey.h>
#include <codec/serialization/MetaText.h>       // ParseMetaYaml / WriteMetaText — the schema-driven speeddata codec
#include <interface/reflection/HavokSchema.h>
#include <disk/Disk.h>
#include "conditions/RymlInclude.h"

#include <algorithm>
#include <atomic>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace fs = std::filesystem;

namespace CB::core::serve {

    namespace {

        using CB::engine::codec::resource::HasExt;
        using CB::engine::codec::resource::PathKey;
        using CB::engine::codec::io::SchemaObject;
        namespace disk = CB::engine::disk;

        constexpr const char* kRoot = "meshes/speeddatasinglefile.txt/";

        /// @brief index.yaml's `projects:` — the master file's project paths, in its order.
        std::vector<std::string> ReadIndex(const std::string& text)
        {
            std::vector<std::string> out;
            std::string              storage = text;
            c4::yml::Tree            tree    = c4::yml::parse_in_place(c4::to_substr(storage));
            const auto               root    = tree.crootref();
            if (!root.is_map() || !root.has_child("projects")) return out;
            for (const auto n : root["projects"].children())
                if (n.has_val()) out.emplace_back(n.val().str, n.val().len);
            return out;
        }

        std::string FieldText(const SchemaObject& o, const char* name)
        {
            const auto i = o.IndexOf(name);
            return i ? o.Values()[*i].str : std::string();
        }

        /// @brief The engine binary-searches iState as SIGNED ints: a table out of order answers misses.
        /// @return whether the states had to be reordered.
        bool SortStates(SchemaObject& project)
        {
            const auto i = project.IndexOf("states");
            if (!i) return false;
            auto&      states = project.FieldAt(*i).objs;
            const auto key    = [](const auto& o) {
                return std::stoll(FieldText(static_cast<const SchemaObject&>(*o), "iState"));
            };
            if (std::is_sorted(states.begin(), states.end(), [&](const auto& a, const auto& b) { return key(a) < key(b); }))
                return false;
            std::stable_sort(states.begin(), states.end(), [&](const auto& a, const auto& b) { return key(a) < key(b); });
            return true;
        }

    }  // namespace

    SpeedDataServeResult ServeSpeedData(const fs::path& dataDir, const fs::path& loadOrderIni)
    {
        SpeedDataServeResult r;
        r.attempted = true;
        try {
            std::error_code ec;
            const fs::path  pluginsRoot = dataDir / "community_behaviors" / "plugins";
            const auto      order       = CB::engine::codec::ReadLoadOrder(loadOrderIni).value_or(CB::engine::codec::LoadOrder{});

            std::vector<std::pair<int, BundleReader>> bundles;
            std::vector<std::string>                  names;
            if (fs::is_directory(pluginsRoot, ec))
                for (fs::directory_iterator bi(pluginsRoot, ec), bend; !ec && bi != bend; bi.increment(ec)) {
                    if (!HasExt(bi->path().filename().string(), ".hky")) continue;
                    const std::string stem = PathKey(bi->path().stem().string());
                    auto              rd   = BundleReader::Open(bi->path());
                    if (!rd) { LOG_WARN("SpeedData: cannot read bundle '{}' — skipped.", bi->path().string()); continue; }
                    bundles.emplace_back(stem == "skyrim" ? -1 : order.Rank(stem), std::move(*rd));
                }
            std::stable_sort(bundles.begin(), bundles.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
            if (bundles.empty() || bundles.front().first != -1) {
                r.error = "no Skyrim.hky master (its index is the speeddata's project list)";
                return r;
            }
            const auto index = bundles.front().second.read(std::string(kRoot) + "index.yaml");
            const auto paths = index ? ReadIndex(*index) : std::vector<std::string>{};
            if (paths.empty()) { r.error = "master carries no speeddatasinglefile.txt/index.yaml"; return r; }

            const auto* reg = CB::engine::schema::SharedRegistry();
            if (!reg) { r.error = "schema registry not armed"; return r; }

            /// @note Project identity is the engine's key (the path's stem) resolved as path identity — the loader
            ///       pools it in a BSFixedString, so a mod spelling the stem in other case still names the project.
            std::vector<std::string>                                  orderKeys;
            std::map<std::string, std::pair<std::size_t, std::string>> winner;   // key -> (bundle, file)
            for (const auto& p : paths) {
                const std::string key = PathKey(CB::engine::codec::io::SpeedDataProjectStem(p));
                if (!winner.count(key)) orderKeys.push_back(key);
                winner[key] = { 0, std::string(kRoot) + "speeddata/" + CB::engine::codec::io::SpeedDataProjectStem(p) + ".yaml" };
            }
            for (std::size_t b = 0; b < bundles.size(); ++b)
                for (const auto& f : bundles[b].second.filesUnderOrig(std::string(kRoot) + "speeddata/", ".yaml")) {
                    const std::string key = PathKey(fs::path(f).stem().string());
                    if (!winner.count(key)) orderKeys.push_back(key);
                    winner[key] = { b, f };
                }

            /// @note Stale tables: a mod bundle touching a project's actor folder (its graphs or animations) without
            ///       shipping that project's speeddata leaves vanilla's curves under changed locomotion.
            std::map<std::string, std::string> projectFolder;   // key -> "meshes/actors/character/"
            for (const auto& u : bundles.front().second.units(CB::engine::codec::HkyArchive::UnitKind::Project)) {
                const auto slash = u.find_last_of('/');
                projectFolder[PathKey(fs::path(u).stem().string())] = slash == std::string::npos ? std::string() : u.substr(0, slash + 1);
            }

            auto root = std::make_shared<SchemaObject>(reg, reg->Find("SpeedDataSingleFile"));
            root->Init();
            const auto pi = root->IndexOf("projects");
            if (!pi) { r.error = "SpeedDataSingleFile descriptor has no `projects`"; return r; }
            for (const auto& key : orderKeys) {
                const auto& [b, file] = winner[key];
                const auto  text      = bundles[b].second.read(file);
                if (!text) { LOG_WARN("SpeedData: '{}' listed but absent — project skipped (lookups answer identity).", file); continue; }
                std::string err;
                auto        proj = CB::engine::codec::io::ParseMetaYaml(*reg, "SpeedDataProject", *text, err);
                if (!proj) { LOG_WARN("SpeedData: '{}' unreadable ({}) — project skipped.", file, err); continue; }
                if (SortStates(*proj)) LOG_INFO("SpeedData: '{}' lists its iStates out of order — sorted (the engine binary-searches them).", file);
                if (b > 0) ++r.fromMods;
                else if (const auto pf = projectFolder.find(key); pf != projectFolder.end() && !pf->second.empty())
                    for (std::size_t m = 1; m < bundles.size(); ++m)
                        if (!bundles[m].second.filesUnder(pf->second, "").empty()) {
                            LOG_INFO("SpeedData: {} keeps vanilla's speed table, but a bundle changes '{}' — its SpeedSampled "
                                     "curves may be stale.", FieldText(*proj, "path"), pf->second);
                            break;
                        }
                root->FieldAt(*pi).objs.push_back(std::move(proj));
                ++r.projects;
            }

            std::string out, err;
            if (!CB::engine::codec::io::WriteMetaText(*root, out, err)) { r.error = err; return r; }
            const fs::path cachePath = dataDir / "community_behaviors_cache" / "speeddatasinglefile.txt";
            if (std::string werr; !disk::WriteFile(cachePath, out, { .atomic = true }, &werr)) {
                r.error = "failed to write the speed data cache: " + werr;
                return r;
            }
            r.ok        = true;
            r.cachePath = cachePath.string();
        } catch (const std::exception& e) {
            r.error = e.what();
        }
        return r;
    }

    namespace {

        RE::BSFixedString s_cachePath;

        std::int32_t OnOpenSpeedData(CB::platform::game::SpeedDataOpen::Next next, std::uintptr_t a_path, std::uintptr_t a_out,
                                     std::uint64_t a_r8, std::uint64_t a_r9)
        {
            static std::once_flag once;
            static bool           served = false;
            std::call_once(once, [] {
                const auto r = ServeSpeedData("Data", "Data/community_behaviors/loadorder.txt");
                if (!r.ok) {
                    LOG_WARN("SpeedData: merge did not complete ({}) — vanilla speed data stays.", r.error);
                    return;
                }
                s_cachePath = RE::BSFixedString("community_behaviors_cache/speeddatasinglefile.txt");
                served      = true;
                LOG_INFO("SpeedData: serving {} project(s) ({} from mod bundles) from {}.", r.projects, r.fromMods, r.cachePath);
            });
            if (served)
                if (const std::uintptr_t ours = *reinterpret_cast<std::uintptr_t*>(&s_cachePath))
                    return next(ours, a_out, a_r8, a_r9);
            return next(a_path, a_out, a_r8, a_r9);
        }

    }  // namespace

    void InstallSpeedDataHook()
    {
        CB::platform::game::SpeedDataOpen::Subscribe(&OnOpenSpeedData, CB::platform::hooks::Priority::Normal, "SpeedDataServe");
    }

}  // namespace CB::core::serve
