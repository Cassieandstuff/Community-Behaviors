#pragma once
// SkeletonCompiler — SkeletonData -> validated skeleton .hkx (animation skeleton,
// first cut). The WRITE half of the skeleton slice and the exact mirror of
// LoadSkeletonsFromHkx: scatter the neutral per-bone data (name / parentIndex /
// refPose) into hkaSkeleton, wrap it in hkaAnimationContainer + hkRootLevelContainer,
// and serialize. Mirrors CharacterCompiler's Compile/CompileToFile shape.
//
// Scope of the first cut: animation skeleton only — no ragdoll, no float slots,
// no local frames. Those are later stages (physics / mirror / attach nodes).

#include "havok/core/PackFileTypes.h"     // HKXHeader
#include "havok/sct/BehaviorCompiler.h"   // CompileResult
#include "havok/sct/SkeletonImport.h"     // SkeletonData

#include <filesystem>
#include <memory>

namespace havok {
class hkaSkeleton;             // fwd (defined in classes/Animation.h)
class hkpRigidBody;            // fwd (defined in classes/Physics.h)
class hkReferencedObject;      // fwd (base of hkpConstraintInstance)
}

namespace havok::sct {

// Build an animation-skeleton packfile from neutral SkeletonData. `data.bones`
// must already be in final index order with parentIndex referring to earlier
// entries (parent < child); the caller (index-assignment stage / SkeletonImport)
// owns that ordering.
CompileResult CompileSkeleton(const SkeletonData& data,
                              const HKXHeader& header = HKXHeader::SkyrimSE());

CompileResult CompileSkeletonToFile(const SkeletonData&          data,
                                    const std::filesystem::path& outPath,
                                    bool                         validate = true,
                                    const HKXHeader&             header = HKXHeader::SkyrimSE());

// Full-skeleton compile-over-base: read `baseBytes` (a complete skeleton.hkx), rebuild ONLY the
// animation skeleton's bones from `animBones`, and carry everything else (ragdoll skeleton, physics,
// both mappers, resource tree, float slots, local frames) untouched. Re-serialized with the base's own
// header so an IDENTITY `animBones` reproduces the base byte-for-byte. This is the serve path for
// existing content (vanilla + XPMSSE + bone-add plugins): physics carried, bone list authored/merged.
CompileResult CompileSkeletonOverBase(const SkeletonData&              animBones,
                                      const std::vector<std::uint8_t>& baseBytes);

// Stage 4/5 (physics compile): derive the RAGDOLL skeleton (subset of bones with a physics block,
// reparented to nearest physics ancestor, local refpose from FK). Defined in SkeletonCompiler.cpp.
std::shared_ptr<::havok::hkaSkeleton> DeriveRagdollSkeleton(const SkeletonData& anim);

// Stage 4/5: derive the ragdoll RIGID BODIES (one hkpRigidBody per physics bone). Everything derives
// from bone↔body geometry except the authored knobs (mass, capsule endpoints, joint) and the one
// genuinely-opaque per-body knob collisionFilterInfo (Skyrim-custom encoding). Order matches
// DeriveRagdollSkeleton (physics bones in anim order). Body[i].motionState = FK world pose; shape =
// capsule (authored a/b + radius, else derived bone→child); inertia = cylinder+caps tensor from
// shape+mass (verified byte-exact vs vanilla); motionType = sphere for near-round capsules else box;
// material/damping/quality = vanilla-constant compiler defaults.
std::vector<std::shared_ptr<::havok::hkpRigidBody>> DeriveRigidBodies(const SkeletonData& anim);

// Stage 4/5: derive the ragdoll CONSTRAINTS — one hkpConstraintInstance per NON-root ragdoll bone, tying
// its body to its parent ragdoll body. `bodies` is DeriveRigidBodies' output (ragdoll order). Type + limit
// atoms come from the bone's `physics.joint`; the frame axes come from the authored twist_axis/plane_axis
// (else a toward-child + perpendicular default); frameB derives from frameA + the two bind poses. All other
// atoms are vanilla-constant compiler defaults. Returned as hkReferencedObject (the physics system /
// ragdoll instance hold them base-typed).
std::vector<std::shared_ptr<::havok::hkReferencedObject>>
DeriveConstraints(const SkeletonData& anim, const std::vector<std::shared_ptr<::havok::hkpRigidBody>>& bodies);

// Stage 4/5: the FUSED skeleton compile — the full 6-variant skeleton.hkx from a single SkeletonData
// carrying per-bone physics (+ optional bumper). Assembles: Merged Animation Container [anim skeleton +
// derived ragdoll skeleton], Physics Data [system: bodies + constraints], RagdollInstance, and the 2
// SkeletonMappers (anim↔ragdoll). Resource Data is omitted (derived/inert). Everything derives from the
// anim bones + authored physics; the ragdoll bind pose, when absent, is derived from anim (functional).
// This is the shippable YAML→skeleton.hkx path (rig == ragdoll, vanilla structure).
CompileResult CompileSkeletonFull(const SkeletonData& data,
                                  const HKXHeader& header = HKXHeader::SkyrimSE());

} // namespace havok::sct
