#pragma once

#include <rules/Rules.h>

#include <cstddef>
#include <cstdint>
#include <string>

namespace CB::engine::rules {

/// @brief `op: CapsuleFromBound` — authors each character's `characterControllerInfo` capsule (capsuleHeight,
///        capsuleRadius) from the BBX bound of its skeleton.nif: the rig the character names (`skeleton.hkx`) with a
///        `.nif` extension, read from the unit set's source (Context::LooseFile).
/// @note  Why: Skyrim never reads these fields; every vanilla character holds Havok's default (1.7, 0.4) and the
///        controller sizes its capsule from the skeleton's BBX instead (bhkCharacterControllerCinfo, 0x140e9cdf0). CB's
///        controller reads the character's fields, so the rule moves the BBX's capsule into them.
/// @note  The capsule is vanilla's for an upright bound: radius = the mean of the BBX's x and y half-extents, height =
///        twice its z half-extent (the capsule's whole height, caps included), in Havok units (x 0.0142875) and unscaled
///        (the runtime applies the actor's scale, as vanilla does).
/// @note  Left as it is: a character that already authors a capsule (not the exact default bits), one with no rig, no
///        skeleton.nif or no BBX, and a LONG bound (x or y half-extent above z: vanilla builds three capsules along
///        its length, which one height and radius cannot hold).
class CapsuleFromBound final : public IOp {
public:
    CapsuleFromBound(const schema::RuleDef& rule, std::string& err);
    bool        Apply(Context& ctx, std::unique_ptr<Record>& out, std::string& err) override;
    bool        Inverse(Context& ctx, const Record& record, std::string& err) override;
    std::string Summary() const override;

private:
    std::size_t m_written = 0, m_authored = 0, m_noRig = 0, m_noNif = 0, m_noBound = 0, m_long = 0;
};

/// @brief The bit patterns of Havok's default characterControllerInfo capsule (1.7, 0.4), which every vanilla
///        character holds: the "not authored" value.
inline constexpr std::uint32_t kDefaultCapsuleHeightBits = 0x3fd9999a;
inline constexpr std::uint32_t kDefaultCapsuleRadiusBits = 0x3ecccccd;

}  // namespace CB::engine::rules
