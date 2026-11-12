#include "BaseUnitCompiler.h"

#include <hky/codec/resource/PathKey.h>
#include <hky/codec/serialization/HkyArchive.h>
#include <hky/decompile/BehaviorDecompile.h>
#include <hky/interface/reflection/HavokSchema.h>

#include <algorithm>
#include <exception>
#include <filesystem>

namespace CB::converter {

using hky::codec::resource::PathKey;

BaseUnitCompiler::BaseUnitCompiler(std::shared_ptr<const hky::codec::HkyArchive> base) : m_base(std::move(base)) {
    auto* reg = hky::schema::SharedRegistry();
    if (!m_base || !reg) return;
    using Kind = hky::codec::HkyArchive::UnitKind;
    std::unordered_map<std::string, std::string> charPrefix;   // PathKey -> archive prefix
    for (const auto& u : m_base->units()) {
        if (u.kind == Kind::Skeleton)  m_skeletonUnits.insert(PathKey(u.prefix));
        if (u.kind == Kind::Character) charPrefix.emplace(PathKey(u.prefix), u.prefix);
    }
    for (const auto& u : m_base->units()) {
        if (u.kind != Kind::Project) continue;
        const auto src = m_base->source(u.prefix);
        if (!src) continue;
        m_rigs.AddProject(PathKey(u.prefix), hky::decompile::ReadProjectCharacters(*src, *reg),
            [&](const std::string& charUnit) -> std::optional<hky::skeleton::CharacterRig> {
                const auto it = charPrefix.find(charUnit);
                if (it == charPrefix.end()) return std::nullopt;
                const auto cs = m_base->source(it->second);
                const auto s  = cs ? hky::decompile::ReadCharacterStrings(*cs) : std::nullopt;
                if (!s) return std::nullopt;
                return hky::skeleton::CharacterRig{ s->rigName, s->behaviorFilename };
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

    auto* reg = hky::schema::SharedRegistry();
    const auto src = reg ? m_base->source(rig) : nullptr;
    if (!src) return names;
    if (auto bones = hky::decompile::ReadSkeletonBones(*src, *reg, rig)) names = std::move(*bones);
    return names;
}

bool BaseUnitCompiler::Parse(const std::string& prefix, hky::decompile::ParsedHky& out, std::string& err) {
    auto* reg = hky::schema::SharedRegistry();
    if (!m_base || !reg) { err = "no base bundle / schema registry"; return false; }
    const auto src = m_base->source(prefix);
    if (!src) { err = "no base unit " + prefix; return false; }
    const auto& bones = BonesFor(prefix);
    return hky::decompile::ParseHkyReflective(*src, *reg, out, err, bones.empty() ? nullptr : &bones, prefix);
}

bool BaseUnitCompiler::Compile(const std::string& prefix, std::vector<std::uint8_t>& out, std::string& err) {
    hky::decompile::ParsedHky g;
    return Parse(prefix, g, err) && hky::decompile::SerializeGraph(g, out, err);
}

}  // namespace CB::converter
