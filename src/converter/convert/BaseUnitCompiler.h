#pragma once

#include <codec/format/RigIndex.h>

#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_set>
#include <vector>

namespace CB::core::codec { class HkyArchive; }
namespace CB::core::decompile { struct ParsedHky; }

namespace bconv {

/// @brief Reads a base bundle's units with the bone table of the rig their characters bind (RigIndex — the same
///        binding the runtime Resolver uses).
/// @note  Bone-named fields (weight maps, bone refs, bone-keyed rosters) only resolve against the right rig; a
///        hardcoded character skeleton left every creature graph unreadable (or read against the wrong bones).
class BaseUnitCompiler {
public:
    explicit BaseUnitCompiler(std::shared_ptr<const CB::core::codec::HkyArchive> base);

    /// @brief Bone names of the rig bound to `serveKey` (first sorted rig of a shared graph); empty if none.
    const std::vector<std::string>& BonesFor(const std::string& serveKey);

    /// @brief Parse the base unit at `prefix` (a serve key), bones bound through its rig.
    bool Parse(const std::string& prefix, CB::core::decompile::ParsedHky& out, std::string& err);
    /// @brief Parse + serialize the base unit at `prefix` to packfile bytes.
    bool Compile(const std::string& prefix, std::vector<std::uint8_t>& out, std::string& err);

private:
    std::shared_ptr<const CB::core::codec::HkyArchive> m_base;
    CB::core::skeleton::RigIndex                      m_rigs;
    std::unordered_set<std::string>                   m_skeletonUnits;   ///< normalized skeleton unit keys
    std::map<std::string, std::vector<std::string>>   m_boneCache;       ///< rig -> names (empty = negative)
    std::mutex                                        m_mu;              ///< guards m_boneCache
};

}  // namespace bconv
