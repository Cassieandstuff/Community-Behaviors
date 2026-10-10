#include "BaseUnitCompiler.h"

#include <codec/format/SkeletonYaml.h>
#include <codec/serialization/HkyArchive.h>
#include <decompile/BehaviorDecompile.h>
#include <interface/reflection/HavokSchema.h>

#include <algorithm>
#include <cctype>
#include <exception>
#include <filesystem>

namespace bconv {

namespace {
std::string lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}
}  // namespace

BaseUnitCompiler::BaseUnitCompiler(std::shared_ptr<const CB::core::codec::HkyArchive> base) : m_base(std::move(base)) {
    auto* reg = CB::core::schema::SharedRegistry();
    if (!m_base || !reg) return;
    using Kind = CB::core::codec::HkyArchive::UnitKind;
    std::unordered_map<std::string, std::string> charPrefix;   // normalized key -> archive prefix
    for (const auto& u : m_base->units()) {
        if (u.kind == Kind::Skeleton)  m_skeletonUnits.insert(lower(u.prefix));
        if (u.kind == Kind::Character) charPrefix.emplace(lower(u.prefix), u.prefix);
    }
    for (const auto& u : m_base->units()) {
        if (u.kind != Kind::Project) continue;
        const auto src = m_base->source(u.prefix);
        if (!src) continue;
        m_rigs.AddProject(lower(u.prefix), CB::core::decompile::ReadProjectCharacters(*src, *reg),
            [&](const std::string& charUnit) -> std::optional<CB::core::skeleton::CharacterRig> {
                const auto it = charPrefix.find(charUnit);
                if (it == charPrefix.end()) return std::nullopt;
                const auto cs = m_base->source(it->second);
                const auto s  = cs ? CB::core::decompile::ReadCharacterStrings(*cs) : std::nullopt;
                if (!s) return std::nullopt;
                return CB::core::skeleton::CharacterRig{ s->rigName, s->behaviorFilename };
            },
            [&](const std::string& rig) { return m_skeletonUnits.count(rig) != 0; });
    }
    m_rigs.Finalize();
}

const std::vector<std::string>& BaseUnitCompiler::BonesFor(const std::string& serveKey) {
    static const std::vector<std::string> kEmpty;
    const auto* rigs = m_rigs.RigsFor(serveKey);
    if (!rigs || rigs->empty() || !m_base) return kEmpty;
    const std::string& rig = rigs->front();

    std::lock_guard<std::mutex> lk(m_mu);
    if (const auto it = m_boneCache.find(rig); it != m_boneCache.end()) return it->second;
    std::vector<std::string>& names = m_boneCache[rig];   // empty = negative cache

    /// @note filesUnder (normalized, for read) and filesUnderOrig (original case, for the bone NAME) walk the
    ///       same range, so index i aligns — bone names keep their exact case.
    const std::string bl = m_base->file(rig + "/bonelist.yaml").value_or("");
    const auto norm = m_base->filesUnder(rig + "/bones/");
    const auto orig = m_base->filesUnderOrig(rig + "/bones/");
    std::vector<std::pair<std::string, std::string>> boneFiles;
    for (std::size_t i = 0; i < norm.size() && i < orig.size(); ++i) {
        if (norm[i].size() < 5 || norm[i].compare(norm[i].size() - 5, 5, ".yaml") != 0) continue;
        if (auto t = m_base->file(norm[i]))
            boneFiles.emplace_back(std::filesystem::path(orig[i]).stem().string(), *t);
    }
    if (bl.empty() && boneFiles.empty()) return names;
    CB::core::skeleton::SkeletonData sk;
    std::string err;
    if (!CB::core::skeleton::LoadSkeletonYamlFromTexts(bl, boneFiles, sk, &err)) return names;
    for (const auto& b : sk.bones) names.push_back(b.name);
    return names;
}

bool BaseUnitCompiler::Parse(const std::string& prefix, CB::core::decompile::ParsedHky& out, std::string& err) {
    auto* reg = CB::core::schema::SharedRegistry();
    if (!m_base || !reg) { err = "no base bundle / schema registry"; return false; }
    const auto src = m_base->source(prefix);
    if (!src) { err = "no base unit " + prefix; return false; }
    const auto& bones = BonesFor(prefix);
    return CB::core::decompile::ParseHkyReflective(*src, *reg, out, err, bones.empty() ? nullptr : &bones);
}

bool BaseUnitCompiler::Compile(const std::string& prefix, std::vector<std::uint8_t>& out, std::string& err) {
    CB::core::decompile::ParsedHky g;
    return Parse(prefix, g, err) && CB::core::decompile::SerializeGraph(g, out, err);
}

}  // namespace bconv
