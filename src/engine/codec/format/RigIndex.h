#pragma once

#include <functional>
#include <optional>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

namespace CB::engine::skeleton {

/// @brief A character's rig and root behavior, as its string data names them (project-dir relative).
struct CharacterRig { std::string rig, behavior; };

/// @brief Which skeleton (rig) each behavior graph / animation binds, harvested from the data itself:
///        a project's characterFilenames -> each character's rig / behavior (both project-dir relative).
/// @note  All keys are normalized serve paths (lowercase, '/'): unit identity is path identity, and the
///        engine opens these files case-insensitively. Bone NAMES never pass through here.
class RigIndex {
public:
    /// @brief Bind every character a project lists.
    /// @param projUnit            normalized project unit path ("meshes/actors/horse/horseproject.hkx").
    /// @param characterFilenames  that project's characterFilenames.
    /// @param loadChar            character unit key -> its rig / behavior, or nullopt if absent/unreadable.
    /// @param hasSkeleton         rig unit key -> whether a skeleton unit exists at that path.
    /// @param warn                diagnostic sink (a rig with no skeleton unit).
    void AddProject(const std::string& projUnit, const std::vector<std::string>& characterFilenames,
                    const std::function<std::optional<CharacterRig>(const std::string&)>& loadChar,
                    const std::function<bool(const std::string&)>& hasSkeleton,
                    const std::function<void(const std::string&)>& warn = {});

    /// @brief Sort every binding list; call once after the last AddProject so lookups are deterministic.
    void Finalize();

    /// @brief The rigs a serve key binds: the characters whose root behavior IS the key, else whose root
    ///        behavior shares its folder (referenced graphs), else of the deepest project folder above it
    ///        (animations). Null when no character binds it. Several rigs = a shared graph.
    const std::vector<std::string>* RigsFor(const std::string& key) const;

    /// @brief Every rig unit some character binds (each backed by a skeleton unit).
    const std::set<std::string>& BoundRigs() const { return m_bound; }

    /// @brief Normalize `rel` (either slash, any case) onto `dir`, collapsing "." / "..".
    static std::string JoinRel(const std::string& dir, const std::string& rel);
    static std::string DirOf(const std::string& key);

private:
    std::unordered_map<std::string, std::vector<std::string>> m_byBehavior, m_byBehaviorDir, m_byProjectDir;
    std::set<std::string> m_bound;
};

}  // namespace CB::engine::skeleton
