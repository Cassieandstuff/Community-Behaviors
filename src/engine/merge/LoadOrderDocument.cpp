#include <merge/LoadOrderDocument.h>

#include <codec/format/LoadOrderText.h>
#include <codec/resource/PathKey.h>
#include <decompile/SkeletonImport.h>
#include <interface/MasterTable.h>
#include <merge/LoadPlan.h>

#include <RymlInclude.h>
#include <vfs/Vfs.h>

#include <algorithm>
#include <unordered_map>

namespace CB::engine::merge {

namespace {

using codec::resource::PathKey;

constexpr std::string_view kPluginsDir = "community_behaviors/plugins/";

/// @brief A bundle's declared masters (manifest.json `masters`, PathKey'd), empty without a readable manifest.
/// @note  Read with ryml (JSON is YAML flow style), so the engine takes no JSON dependency; a JSONC comment the plugin's
///        reader tolerates fails here and reads as no masters, with a warning.
std::vector<std::string> ManifestMasters(const codec::HkyArchive& arc, std::vector<std::string>& warnings) {
    std::vector<std::string> out;
    auto                     text = arc.file("manifest.json");
    if (!text) return out;
    common::EnsureRymlThrows();
    try {
        c4::yml::Tree t    = c4::yml::parse_in_place(c4::to_substr(*text));
        const auto    root = t.crootref();
        if (!root.is_map() || !root.has_child("masters")) return out;
        for (const auto m : root["masters"])
            if (m.has_val()) out.push_back(PathKey(std::string(m.val().str, m.val().len)));
    } catch (const std::exception& e) {
        warnings.push_back("manifest.json: " + std::string(e.what()));
    }
    return out;
}

std::string Stem(std::string_view file) {
    const auto slash = file.find_last_of('/');
    std::string_view name = slash == std::string_view::npos ? file : file.substr(slash + 1);
    if (const auto dot = name.find_last_of('.'); dot != std::string_view::npos) name = name.substr(0, dot);
    return std::string(name);
}

}  // namespace

bool LoadOrderDocument::Open(const vfs::FileSystem& data, const schema::SchemaRegistry& reg, std::string* err) {
    m_reg = &reg;
    m_bundles.clear();
    m_warnings.clear();
    for (const auto& path : data.List(kPluginsDir)) {
        const std::string key = PathKey(path);
        if (key.size() <= kPluginsDir.size() || key.find('/', kPluginsDir.size()) != std::string::npos) continue;
        if (!codec::resource::HasExt(key, ".hky")) continue;
        Bundle b;
        b.stem = PathKey(Stem(path));
        const auto winner = data.Winner(path);
        b.provider = winner ? winner->layer : std::string();
        const auto loose = data.LoosePath(path);
        if (!loose) {
            m_warnings.push_back(path + ": not a loose file (a bundle inside an archive is not read)");
            continue;
        }
        const std::u8string u8 = loose->u8string();
        b.file = std::string(u8.begin(), u8.end());
        /// @warning HkyArchive opens a narrow path: a mod folder named outside the ANSI code page cannot be opened.
        try {
            b.archive = codec::HkyArchive::LoadFromFile(loose->string(), b.error);
        } catch (const std::exception& e) {
            b.error = e.what();
        }
        if (b.archive) b.masters = ManifestMasters(*b.archive, m_warnings);
        else m_warnings.push_back(b.file + ": " + b.error);
        m_bundles.push_back(std::move(b));
    }
    if (m_bundles.empty()) {
        if (err) *err = "no .hky bundle under community_behaviors/plugins/";
        return false;
    }

    std::unordered_map<std::string, std::vector<std::string>> masters;
    for (const auto& b : m_bundles) masters[b.stem] = b.masters;
    codec::LoadOrder order;
    if (const auto text = data.Read("community_behaviors/loadorder.txt"))
        order = codec::ParseLoadOrder(std::string_view(reinterpret_cast<const char*>(text->data()), text->size()));
    const LoadPlan plan = PlanLoadOrder(masters, order);
    for (const auto& w : plan.warnings) m_warnings.push_back(w);
    for (auto& b : m_bundles) {
        const auto r = plan.rank.find(b.stem);
        b.rank    = r == plan.rank.end() ? 0 : r->second;
        b.dropped = plan.dropped.count(b.stem) != 0;
        if (const auto mt = plan.masterTable.find(b.stem); mt != plan.masterTable.end()) m_masterTable[b.stem] = mt->second;
    }
    std::stable_sort(m_bundles.begin(), m_bundles.end(), [](const Bundle& a, const Bundle& b) { return a.rank < b.rank; });
    IndexBundles();
    return true;
}

void LoadOrderDocument::SetProject(std::shared_ptr<codec::HkyArchive> project, const std::string& stem) {
    std::erase_if(m_bundles, [](const Bundle& b) { return b.provider == "project"; });
    if (project) {
        Bundle b;
        b.stem     = PathKey(stem);
        b.provider = "project";
        b.rank     = m_bundles.empty() ? 1 : m_bundles.back().rank + 1;
        b.masters  = ManifestMasters(*project, m_warnings);
        b.archive  = std::move(project);
        const bool haveSkyrim = std::any_of(m_bundles.begin(), m_bundles.end(), [](const Bundle& x) { return x.stem == "skyrim"; });
        m_masterTable[b.stem] = codec::formid::BuildMasterTable(b.stem, b.masters, haveSkyrim);
        m_bundles.push_back(std::move(b));
    }
    IndexBundles();
}

void LoadOrderDocument::IndexBundles() {
    m_files.clear();
    m_units.clear();
    m_rigIndex = {};
    m_rigBones.clear();
    for (std::size_t i = 0; i < m_bundles.size(); ++i) {
        const Bundle& b = m_bundles[i];
        if (!b.archive || b.dropped) continue;
        const auto keys = b.archive->filesUnder(""), orig = b.archive->filesUnderOrig("");
        for (std::size_t k = 0; k < keys.size(); ++k) {
            FileEntry& f = m_files[keys[k]];
            f.bundles.push_back(i);
            f.spelled = orig[k];
        }
        for (const auto& u : b.archive->units()) {
            Unit& unit = m_units[u.prefix];
            unit.key   = u.prefix;
            unit.kind  = u.kind;
            unit.layers.push_back(b.stem);
            const auto spelled = b.archive->filesUnderOrig(u.prefix + "/");
            unit.path = spelled.empty() ? u.prefix : spelled.front().substr(0, u.prefix.size());
        }
    }
    if (!m_reg) return;

    /// @note The Resolver's rig binding: each project's characters name their rig, a skeleton unit's path.
    for (const auto& [key, unit] : m_units) {
        if (unit.kind != codec::HkyArchive::UnitKind::Project) continue;
        const auto src = WinnerSource(unit);
        if (!src) continue;
        m_rigIndex.AddProject(key, decompile::ReadProjectCharacters(*src, *m_reg),
            [&](const std::string& charUnit) -> std::optional<skeleton::CharacterRig> {
                const auto cs = Character(charUnit);
                if (!cs) return std::nullopt;
                return skeleton::CharacterRig{ cs->rigName, cs->behaviorFilename };
            },
            [&](const std::string& rig) {
                const auto it = m_units.find(rig);
                return it != m_units.end() && it->second.kind == codec::HkyArchive::UnitKind::Skeleton;
            },
            [&](const std::string& m) { m_warnings.push_back(m); });
    }
    m_rigIndex.Finalize();
    for (const auto& rig : m_rigIndex.BoundRigs()) {
        const auto it  = m_units.find(rig);
        const auto src = it == m_units.end() ? nullptr : WinnerSource(it->second);
        std::string err;
        auto names = src ? decompile::ReadSkeletonBones(*src, *m_reg, rig, "animation", &err) : std::nullopt;
        if (names) m_rigBones[rig] = std::move(*names);
        else m_warnings.push_back("rig '" + rig + "' has no animation skeleton bones" + (err.empty() ? "" : ": " + err));
    }
}

const LoadOrderDocument::Bundle* LoadOrderDocument::BundleNamed(std::string_view stem) const {
    for (auto it = m_bundles.rbegin(); it != m_bundles.rend(); ++it)
        if (it->stem == stem && it->archive && !it->dropped) return &*it;
    return nullptr;
}

std::shared_ptr<const common::IUnitSource> LoadOrderDocument::WinnerSource(const Unit& u) const {
    if (u.layers.empty()) return nullptr;
    const Bundle* b = BundleNamed(u.layers.back());
    return b ? b->archive->source(u.key) : nullptr;
}

std::vector<std::string> LoadOrderDocument::Files(std::string_view prefix) const {
    std::string p = PathKey(std::string(prefix));
    if (!p.empty() && p.back() != '/') p += '/';
    std::vector<std::string> out;
    for (auto it = m_files.lower_bound(p); it != m_files.end() && it->first.starts_with(p); ++it) out.push_back(it->second.spelled);
    std::sort(out.begin(), out.end());
    return out;
}

const std::vector<std::string>* LoadOrderDocument::MasterTable(std::string_view stem) const {
    const auto it = m_masterTable.find(PathKey(std::string(stem)));
    return it == m_masterTable.end() ? nullptr : &it->second;
}

std::vector<std::string> LoadOrderDocument::Providers(std::string_view path) const {
    std::vector<std::string> out;
    if (const auto it = m_files.find(PathKey(std::string(path))); it != m_files.end())
        for (const std::size_t i : it->second.bundles) out.push_back(m_bundles[i].stem);
    return out;
}

std::optional<std::string> LoadOrderDocument::Text(std::string_view path) const {
    const std::string key = PathKey(std::string(path));
    const auto        it  = m_files.find(key);
    if (it == m_files.end() || it->second.bundles.empty()) return std::nullopt;
    return m_bundles[it->second.bundles.back()].archive->file(key);
}

std::vector<LoadOrderDocument::Unit> LoadOrderDocument::Units(std::string_view prefix) const {
    const std::string     p = PathKey(std::string(prefix));
    std::vector<Unit> out;
    for (auto it = m_units.lower_bound(p); it != m_units.end() && it->first.starts_with(p); ++it) out.push_back(it->second);
    return out;
}

std::optional<LoadOrderDocument::Unit> LoadOrderDocument::UnitAt(std::string_view key) const {
    const auto it = m_units.find(PathKey(std::string(key)));
    if (it == m_units.end()) return std::nullopt;
    return it->second;
}

std::vector<std::string> LoadOrderDocument::Rigs(std::string_view key) const {
    const auto* rigs = m_rigIndex.RigsFor(PathKey(std::string(key)));
    return rigs ? *rigs : std::vector<std::string>{};
}

std::optional<std::vector<std::string>> LoadOrderDocument::RigBones(std::string_view key) const {
    const auto* rigs = m_rigIndex.RigsFor(PathKey(std::string(key)));
    if (!rigs) return std::nullopt;
    for (const auto& r : *rigs)
        if (const auto it = m_rigBones.find(r); it != m_rigBones.end()) return it->second;
    return std::nullopt;
}

bool LoadOrderDocument::MergedGraph(std::string_view key, decompile::ParsedHky& out, std::string* err) const {
    const auto u = UnitAt(key);
    const auto fail = [&](std::string why) {
        if (err) *err = std::move(why);
        return false;
    };
    if (!u) return fail("no unit '" + std::string(key) + "' in the load order");
    if (!m_reg) return fail("the document has no schema registry");
    if (u->kind == codec::HkyArchive::UnitKind::Project || u->kind == codec::HkyArchive::UnitKind::Skeleton)
        return fail("'" + u->key + "' is a project or skeleton unit: its winner replaces it whole, nothing merges");
    std::vector<common::LayerSource> layers;
    for (const auto& stem : u->layers) {
        const Bundle* b = BundleNamed(stem);
        if (!b) continue;
        const auto mt = m_masterTable.find(stem);
        layers.push_back({ b->archive->source(u->key), stem, mt == m_masterTable.end() ? std::vector<std::string>{} : mt->second });
    }
    const auto  bones = RigBones(u->key);
    std::string why;
    if (decompile::MergeLayersReflective(layers, *m_reg, bones ? &*bones : nullptr, out, why)) return true;
    return fail(why);
}

bool LoadOrderDocument::GraphBytes(std::string_view key, std::vector<std::uint8_t>& out, std::string* err) const {
    std::vector<trace::ObjectOrigin> origins;
    return GraphBytes(key, out, origins, err);
}

bool LoadOrderDocument::GraphBytes(std::string_view key, std::vector<std::uint8_t>& out, std::vector<trace::ObjectOrigin>& origins,
                                   std::string* err) const {
    const auto  u = UnitAt(key);
    std::string why;
    decompile::ParsedHky graph;
    bool        parsed = false;
    if (!u) why = "no unit '" + std::string(key) + "' in the load order";
    else if (!m_reg) why = "the document has no schema registry";
    else if (u->kind == codec::HkyArchive::UnitKind::Project || u->kind == codec::HkyArchive::UnitKind::Skeleton) {
        const auto src = WinnerSource(*u);
        if (!src) why = "'" + u->key + "' has no readable winner";
        else parsed = decompile::ParseHkyReflective(*src, *m_reg, graph, why, nullptr, u->key);
    } else parsed = MergedGraph(u->key, graph, &why);
    if (parsed && decompile::SerializeGraph(graph, out, origins, why)) return true;
    if (err) *err = std::move(why);
    return false;
}

std::optional<std::vector<std::string>> LoadOrderDocument::SkeletonBones(std::string_view key) const {
    const auto u = UnitAt(key);
    if (!u || u->kind != codec::HkyArchive::UnitKind::Skeleton || !m_reg) return std::nullopt;
    if (const auto it = m_rigBones.find(u->key); it != m_rigBones.end()) return it->second;
    const auto  src = WinnerSource(*u);
    std::string err;
    return src ? decompile::ReadSkeletonBones(*src, *m_reg, u->key, "animation", &err) : std::nullopt;
}

std::optional<skeleton::SkeletonData> LoadOrderDocument::Skeleton(std::string_view key, std::string* err) const {
    const auto u = UnitAt(key);
    if (!u || u->kind != codec::HkyArchive::UnitKind::Skeleton || !m_reg) {
        if (err) *err = "no skeleton unit '" + std::string(key) + "'";
        return std::nullopt;
    }
    const auto src = WinnerSource(*u);
    skeleton::SkeletonData out;
    if (!src || !skeleton::ReadSkeletonUnit(*src, *m_reg, u->key, out, err)) return std::nullopt;
    return out;
}

std::optional<decompile::CharacterStrings> LoadOrderDocument::Character(std::string_view key) const {
    const auto u = UnitAt(key);
    if (!u) return std::nullopt;
    /// @note A patch layer states only what it changes (ReadCharacterStrings): each string comes from the highest layer
    ///       that states it, and the roster is every layer's, base first (patch rosters are additions).
    std::optional<decompile::CharacterStrings> out;
    for (auto it = u->layers.rbegin(); it != u->layers.rend(); ++it) {
        const Bundle* b   = BundleNamed(*it);
        const auto    src = b ? b->archive->source(u->key) : nullptr;
        const auto    cs  = src ? decompile::ReadCharacterStrings(*src) : std::nullopt;
        if (!cs) continue;
        if (!out) out.emplace();
        if (out->name.empty()) out->name = cs->name;
        if (out->rigName.empty()) out->rigName = cs->rigName;
        if (out->behaviorFilename.empty()) out->behaviorFilename = cs->behaviorFilename;
        out->animationNames.insert(out->animationNames.begin(), cs->animationNames.begin(), cs->animationNames.end());
    }
    if (out) {
        std::vector<std::string> unique;
        for (auto& n : out->animationNames)
            if (std::find(unique.begin(), unique.end(), n) == unique.end()) unique.push_back(std::move(n));
        out->animationNames = std::move(unique);
    }
    return out;
}

std::vector<std::string> LoadOrderDocument::ProjectCharacters(std::string_view key) const {
    const auto u   = UnitAt(key);
    const auto src = u ? WinnerSource(*u) : nullptr;
    return src && m_reg ? decompile::ReadProjectCharacters(*src, *m_reg) : std::vector<std::string>{};
}

}  // namespace CB::engine::merge
