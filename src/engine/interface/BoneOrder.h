#pragma once
/// @file
/// @brief The skeleton bone-order compatibility shim (schema/metadata/compatshim/boneorder): the order a skeleton's
///        bones take when CB binds them, by role.
/// @note Compiled behaviors, animation bindings and skeleton mappers address bones by INDEX, so a skeleton unit stores
///       no order of its own for the bones the shim knows: they take the shim's order, and the bones it doesn't list
///       follow in the unit's own order.

#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace CB::engine::schema { class SchemaRegistry; }

namespace CB::engine::skeleton {

/// @brief One skeleton's shim entry: per role (`animation`, `ragdoll`), its known bones in order (the shim's layers
///        concatenated: vanilla, then each skeleton mod's appended bones).
struct BoneOrderEntry {
    std::unordered_map<std::string, std::vector<std::string>> roles;
};

/// @brief The shim's entry for the skeleton unit at `unitPath` (Data-relative, e.g. "meshes/actors/horse/character
///        assets/skeleton.hkx", matched by path identity), or nullptr when the registry carries no boneorder shim or
///        the shim does not list the path.
/// @note The shim text is parsed once per registry.
const BoneOrderEntry* FindBoneOrder(const schema::SchemaRegistry& reg, std::string_view unitPath);

/// @brief The bind order of `present` (a skeleton's bones, in its own order): the bones `known` lists, in its order,
///        then the rest in `present`'s order.
/// @param known the shim's order for this skeleton and role; null = none known (the result is `present`).
std::vector<std::string> BindBoneOrder(const std::vector<std::string>* known, const std::vector<std::string>& present);

}  // namespace CB::engine::skeleton
