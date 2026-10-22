#pragma once
#include <codec/format/AnimDataYaml.h>         // ProjectHeader
#include <codec/serialization/HavokIo.h>       // SchemaObject
#include <compile/AnimDataDeriver.h>           // DeriveClipInput
#include <interface/AnimationDef.h>

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace CB::engine::animdata {

/// @brief Where the projection reads the load order's merged data. Keys are normalized serve paths
///        ("meshes/actors/character/behaviors/0_master.hkx": lowercase, '/').
struct ProjectionSource {
    /// @brief The character's SERVED roster (merged animationNames, original case) — the positions engine binds by.
    std::function<std::vector<std::string>(const std::string& characterKey)> roster;
    /// @brief A behavior unit's clip generators, merged across every layer of the load order.
    std::function<std::vector<DeriveClipInput>(const std::string& unitKey)> clips;
    /// @brief Every behavior unit the load order serves.
    std::function<std::vector<std::string>()> behaviorUnits;
    /// @brief Load these animations (canonical paths) before `animation` is asked for them — the parallel hook.
    std::function<void(const std::vector<std::string>& canonPaths)> prefetch;
    /// @brief One animation by canonical path, its `motion` already resolved (ResolveRootMotion); nullptr if absent.
    std::function<const CB::engine::anim::AnimationDef*(const std::string& canonPath)> animation;
};

struct ProjectionStats {
    std::size_t projects = 0, clips = 0, motions = 0, unresolved = 0;
    std::size_t preloads = 0;   ///< preload animations added to project file lists (AnimationDef::preload)
    std::vector<std::string> unresolvedSample;   ///< "project: clip (animation)" — clips whose animation the roster lacks
};

/// @brief Project the AnimationDataSingleFile from the load order: per project, its clips are the clip generators of
///        its behavior units (its `Behaviors\…` assets plus every served behavior in those folders — mod behaviors
///        like DMCO_Dodge1 are no project asset), and its motions are its clips' animations' actor motion.
/// @note `animIndex` belongs to the ANIMATION: its position in the character's served roster (vanilla animations keep
///       Bethesda's index; mod-added ones follow the vanilla roster). The engine uses it only as the per-project
///       clip↔motion join key (Adsf_RemapClipAnimIndexToMotionSlot), so every clip of one animation shares one
///       motion record. A clip binds the roster slot whose FILENAME matches (the engine's LoadClips key) and plays that
///       slot's own file — so a shared graph binds each gendered roster to its own file.
/// @return the root object (class AnimationDataSingleFile), or nullptr with `err` set.
std::shared_ptr<CB::engine::codec::io::SchemaObject> ProjectAnimData(const CB::engine::schema::SchemaRegistry& reg,
                                                                   const std::vector<ProjectHeader>& headers,
                                                                   const ProjectionSource& src, ProjectionStats* stats,
                                                                   std::string& err);

} // namespace CB::engine::animdata
