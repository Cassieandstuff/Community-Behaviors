#include <rules/CapsuleFromBound.h>

#include <codec/serialization/HavokIo.h>

#include <niffer/Niffer.h>

#include <format>
#include <optional>
#include <vector>

namespace CB::engine::rules {

namespace {

/// @brief Havok units per game unit (bhkWorld's world scale).
constexpr float kHavokPerGameUnit = 0.0142875f;
/// @brief The extra data the controller sizes its capsule from (FUN_140e2c300 looks it up by this name).
constexpr std::string_view kBoundName = "BBX";

struct Bound {
    float x = 0, y = 0, z = 0;   ///< half-extents, game units
};

/// @brief The BBX of the NIF `bytes`, or nullopt (no such extra data, or the file does not load).
std::optional<Bound> ReadBound(const std::vector<std::uint8_t>& bytes) {
    auto nif = niffer::NifFile::Load(bytes);
    if (!nif) return std::nullopt;
    for (niffer::Block& b : nif->blocks) {
        const auto bound = niffer::view::BSBound::From(b);
        if (!bound || nif->String(bound->Name()) != kBoundName) continue;
        const auto d = bound->Dimensions();
        return Bound{ d.X(), d.Y(), d.Z() };
    }
    return std::nullopt;
}

/// @brief `rel` with its extension replaced by `ext` (".nif").
std::string WithExt(const std::string& rel, const char* ext) {
    const auto slash = rel.find_last_of('/');
    const auto dot   = rel.find_last_of('.');
    return (dot == std::string::npos || (slash != std::string::npos && dot < slash) ? rel : rel.substr(0, dot)) + ext;
}

/// @brief The characterControllerInfo of character unit `hky`, or null.
io::SchemaObject* ControllerInfo(decompile::ParsedHky& hky) {
    if (!hky.root) return nullptr;
    const auto data = io::FindVariant(*hky.root, "hkbCharacterData");
    return data ? data->Child("characterControllerInfo") : nullptr;
}

struct CapsuleRecord final : Record {
    std::vector<std::string> characters;   ///< every character Apply wrote (each held the default bits before)
};

}  // namespace

CapsuleFromBound::CapsuleFromBound(const schema::RuleDef&, std::string&) {}

bool CapsuleFromBound::Apply(Context& ctx, std::unique_ptr<Record>& out, std::string& err) {
    auto record = std::make_unique<CapsuleRecord>();
    for (const Character& c : ctx.Characters()) {
        if (!ctx.InScope(c.key)) continue;
        decompile::ParsedHky* hky = ctx.Unit(c.key, err);
        if (!hky) return false;
        io::SchemaObject* info = ControllerInfo(*hky);
        if (!info) continue;
        if (info->GetFloatBits("capsuleHeight") != kDefaultCapsuleHeightBits || info->GetFloatBits("capsuleRadius") != kDefaultCapsuleRadiusBits) {
            ++m_authored;
            continue;
        }
        if (c.rig.empty()) { ++m_noRig; continue; }
        const auto bytes = ctx.LooseFile(WithExt(c.rig, ".nif"));
        if (!bytes) { ++m_noNif; continue; }
        const auto bound = ReadBound(*bytes);
        if (!bound || !(bound->z > 0.0f)) { ++m_noBound; continue; }
        /// @note vanilla's long-body test (bhkCharacterControllerCinfo): a z half-extent below either other one.
        if (bound->z < bound->x || bound->z < bound->y) { ++m_long; continue; }
        info->SetFloat("capsuleHeight", 2.0f * bound->z * kHavokPerGameUnit);
        info->SetFloat("capsuleRadius", 0.5f * (bound->x + bound->y) * kHavokPerGameUnit);
        ctx.Touch(c.key);
        record->characters.push_back(c.key);
        ++m_written;
    }
    if (!record->characters.empty()) out = std::move(record);
    return true;
}

bool CapsuleFromBound::Inverse(Context& ctx, const Record& record, std::string& err) {
    for (const std::string& key : static_cast<const CapsuleRecord&>(record).characters) {
        const std::string unit = ctx.Find(key);
        if (unit.empty()) { err = key + ": not in the unit set"; return false; }
        decompile::ParsedHky* hky = ctx.Unit(unit, err);
        if (!hky) return false;
        io::SchemaObject* info = ControllerInfo(*hky);
        if (!info) { err = key + ": no characterControllerInfo"; return false; }
        info->SetFloatBits("capsuleHeight", kDefaultCapsuleHeightBits);
        info->SetFloatBits("capsuleRadius", kDefaultCapsuleRadiusBits);
        ctx.Touch(unit);
    }
    return true;
}

std::string CapsuleFromBound::Summary() const {
    return std::format("{} capsule(s) authored from the skeleton's BBX; left as they were: {} already authored, {} long bound(s), "
                       "{} without a rig, {} without a skeleton.nif, {} without a BBX",
                       m_written, m_authored, m_long, m_noRig, m_noNif, m_noBound);
}

}  // namespace CB::engine::rules
