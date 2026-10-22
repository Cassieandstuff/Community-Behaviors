#include <compile/AnimDataProjection.h>

#include <codec/resource/PathKey.h>   // path identity (PathKey) + the LoadClips slot rule (ClipSlotKey)

#include <algorithm>
#include <cstdlib>
#include <map>
#include <optional>
#include <stdexcept>
#include <set>
#include <unordered_map>

namespace CB::engine::animdata {
namespace {

using CB::engine::codec::io::SchemaObject;
using CB::engine::codec::resource::ClipSlotKey;
using CB::engine::codec::resource::PathKey;

std::string Trim(std::string s) {
    while (!s.empty() && (s.back() == '\r' || s.back() == '\n' || s.back() == ' ' || s.back() == '\t')) s.pop_back();
    std::size_t b = 0;
    while (b < s.size() && (s[b] == ' ' || s[b] == '\t')) ++b;
    return s.substr(b);
}

std::string ParentOf(const std::string& path) {
    const auto s = path.find_last_of('/');
    return s == std::string::npos ? std::string() : path.substr(0, s);
}

std::shared_ptr<SchemaObject> Make(const CB::engine::schema::SchemaRegistry& reg, const char* cls) {
    const auto* cs = reg.Find(cls);
    if (!cs) throw std::runtime_error(std::string("metadata class '") + cls + "' not in the schema");
    auto o = std::make_shared<SchemaObject>(&reg, cs);
    o->Init();
    return o;
}

/// @brief One project's projected block: its clips (through DeriveClipList) and the motions they reference.
struct ProjectBuild {
    std::vector<ClipGenerator> clips;
    std::vector<MotionRecord>  motions;
};

} // namespace

std::shared_ptr<CB::engine::codec::io::SchemaObject> ProjectAnimData(const CB::engine::schema::SchemaRegistry& reg,
                                                                   const std::vector<ProjectHeader>& headers,
                                                                   const ProjectionSource& src, ProjectionStats* stats,
                                                                   std::string& err) {
    try {
        /// @note Phase 1 — per project: its roster (canonical) and its merged clip inputs, canonicalized; collect every
        ///       animation any clip plays so the caller can load them all in parallel before phase 2 reads them.
        struct Pending {
            std::string                  actorRoot;
            std::vector<std::string>     roster;        ///< each slot as the roster spells it (project-relative, original case)
            std::vector<std::string>     rosterCanon;   ///< each slot's own file (canonical path)
            std::vector<std::string>     rosterKeys;    ///< each slot's ClipSlotKey
            std::unordered_map<std::string, int> slot;  ///< ClipSlotKey -> first slot (a duplicate binds its first)
            std::vector<DeriveClipInput> clips;         ///< animationName rewritten to its ClipSlotKey
        };
        std::vector<std::optional<Pending>> pending(headers.size());
        std::set<std::string>               needed;
        std::map<std::string, std::vector<std::string>> unitsByFolder;
        for (const auto& u : src.behaviorUnits()) unitsByFolder[ParentOf(u)].push_back(u);
        for (auto& [_, v] : unitsByFolder) std::sort(v.begin(), v.end());

        for (std::size_t h = 0; h < headers.size(); ++h) {
            const ProjectHeader& hd = headers[h];
            if (!hd.hasAnimData || hd.character.empty()) continue;
            /// @note The project's folder comes from its `character:` ref; its CHARACTER is the one its own asset list
            ///       names ("Characters Wolf\Wolf.hkx") — the ref is a co-location guess that picks the dog for the wolf.
            const std::string actorRoot = ParentOf(ParentOf("meshes/" + PathKey(hd.character)));
            std::string charKey = "meshes/" + PathKey(hd.character);
            for (const auto& asset : hd.assets)
                if (const std::string a = PathKey(asset); a.rfind("characters", 0) == 0) { charKey = actorRoot + "/" + a; break; }
            Pending p;
            p.actorRoot = actorRoot;
            for (const auto& a : src.roster(charKey)) {
                p.roster.push_back(a);
                p.rosterCanon.push_back(CanonicalAnimPath(actorRoot, a));
                p.rosterKeys.push_back(ClipSlotKey(a));
                p.slot.emplace(p.rosterKeys.back(), static_cast<int>(p.rosterKeys.size()) - 1);
            }

            /// @note Units: the project's `Behaviors\…` assets in asset order, then every other served behavior in those
            ///       folders (Dog and Wolf share actors/canine but not their behavior folders — per-folder scoping).
            std::vector<std::string> units;
            std::set<std::string>    seen, folders;
            for (const auto& asset : hd.assets) {
                const std::string a = PathKey(asset);
                if (a.rfind("behaviors", 0) != 0 || a.size() < 4 || a.compare(a.size() - 4, 4, ".hkx") != 0) continue;
                const std::string key = actorRoot + "/" + a;
                if (seen.insert(key).second) units.push_back(key);
                folders.insert(ParentOf(key));
            }
            for (const auto& f : folders)
                if (const auto it = unitsByFolder.find(f); it != unitsByFolder.end())
                    for (const auto& u : it->second)
                        if (seen.insert(u).second) units.push_back(u);
            for (const auto& u : units)
                for (auto c : src.clips(u)) {
                    if (!c.animationName.empty()) {
                        c.animationName = ClipSlotKey(c.animationName);
                        if (const auto s = p.slot.find(c.animationName); s != p.slot.end())
                            needed.insert(p.rosterCanon[static_cast<std::size_t>(s->second)]);
                    }
                    p.clips.push_back(std::move(c));
                }
            pending[h] = std::move(p);
        }
        if (src.prefetch) src.prefetch(std::vector<std::string>(needed.begin(), needed.end()));

        /// @note Phase 2 — clip records (annotation triggers + the trigger clamp from each animation), then one motion
        ///       record per referenced roster slot.
        auto root = Make(reg, "AnimationDataSingleFile");
        auto& projects = root->FieldRef("projects").objs;
        for (std::size_t h = 0; h < headers.size(); ++h) {
            const ProjectHeader& hd = headers[h];
            auto po = Make(reg, "AnimDataProject");
            po->FieldRef("name").str        = hd.name;
            po->FieldRef("fieldX").str      = hd.fieldX.empty() ? "1" : hd.fieldX;
            po->FieldRef("assetPaths").strs = hd.assets;
            po->FieldRef("hasAnimData").str = hd.hasAnimData ? "1" : "0";
            if (pending[h]) {
                Pending& p = *pending[h];
                /// @note A clip plays its SLOT's file (the gendered one for a shared graph), so annotations, duration and
                ///       motion come from that file.
                std::unordered_map<int, double> motionDur;
                for (auto& c : p.clips) {
                    const auto s = p.slot.find(c.animationName);
                    if (s == p.slot.end()) continue;
                    const auto* def = src.animation(p.rosterCanon[static_cast<std::size_t>(s->second)]);
                    if (!def) continue;
                    /// @note Every annotation track, in track order (vanilla lists e.g. the dog's whines from track 1+).
                    for (const auto& track : def->annotationTracks)
                        for (const auto& a : track.annotations)
                            c.triggers.push_back({ Trim(a.text), static_cast<double>(a.time), false, true });
                    motionDur[s->second] = static_cast<double>(def->duration);
                }
                std::vector<std::string> unresolved;
                const auto clips = DeriveClipList(p.clips, p.rosterKeys, motionDur, &unresolved);
                std::set<int> referenced;
                for (const auto& c : clips) {
                    auto co = Make(reg, "AnimDataClip");
                    co->FieldRef("name").str          = c.name;
                    co->FieldRef("animIndex").str     = c.animIndex;
                    co->FieldRef("playbackSpeed").str = c.playbackSpeed;
                    co->FieldRef("cropStart").str     = c.cropStart;
                    co->FieldRef("cropEnd").str       = c.cropEnd;
                    co->FieldRef("triggers").strs     = c.triggers;
                    po->FieldRef("clips").objs.push_back(co);
                    if (const int i = std::atoi(c.animIndex.c_str()); i >= 0) referenced.insert(i);
                }
                for (const int i : referenced) {
                    const auto* def = src.animation(p.rosterCanon[static_cast<std::size_t>(i)]);
                    /// @note A motionMatching clip's motion goes to CB's motion matching, never here: its per-frame
                    ///       samples once took defaultmale's frame B to 184228 lines, past the engine's 65535-line read.
                    if (!def || !def->motion || def->motionTarget == CB::engine::anim::AnimationDef::MotionTarget::MotionMatching) continue;
                    auto mo = Make(reg, "AnimDataMotion");
                    mo->FieldRef("animIndex").str    = std::to_string(i);
                    mo->FieldRef("duration").str     = def->motion->duration;
                    mo->FieldRef("translations").strs = def->motion->translations;
                    mo->FieldRef("rotations").strs    = def->motion->rotations;
                    po->FieldRef("motions").objs.push_back(mo);
                }
                /// @note A preload animation one of the project's clips plays joins its file list, spelled as the roster
                ///       spells it, after the vanilla entries; a file the list already names (path identity) is not added again.
                auto&                 assets = po->FieldRef("assetPaths").strs;
                std::set<std::string> listed;
                for (const auto& a : assets) listed.insert(PathKey(a));
                std::size_t preloads = 0;
                for (const int i : referenced) {
                    const auto* def = src.animation(p.rosterCanon[static_cast<std::size_t>(i)]);
                    if (!def || !def->preload) continue;
                    const std::string& a = p.roster[static_cast<std::size_t>(i)];
                    if (listed.insert(PathKey(a)).second) assets.push_back(a), ++preloads;
                }
                if (stats) {
                    stats->preloads += preloads;
                    ++stats->projects;
                    stats->clips += clips.size();
                    stats->motions += po->FieldRef("motions").objs.size();
                    stats->unresolved += unresolved.size();
                    for (const auto& u : unresolved)
                        if (stats->unresolvedSample.size() < 12) stats->unresolvedSample.push_back(hd.name + ": " + u);
                }
            }
            projects.push_back(po);
        }
        return root;
    } catch (const std::exception& e) {
        err = e.what();
        return nullptr;
    }
}

} // namespace CB::engine::animdata
