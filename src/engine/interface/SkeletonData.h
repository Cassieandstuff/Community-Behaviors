#pragma once
#include "common/HkTypes.h"   // Vector4 / QSTransform (portable value types, base tier)

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

/// @file
/// @brief SkeletonData — the plain, engine-free skeleton model a tool READS (SkeletonImport: LoadSkeletonsFromHkx,
///        ReadSkeletonPhysics, ReadSkeletonUnit; the CB-API facade): names, parent indices, bind-pose transforms and a
///        per-bone summary of the ragdoll physics.
/// @note Read-only: CB serves a skeleton from its reflective unit, never rebuilt from this model. Deliberately NOT
///       hkaSkeleton / hkp* — consumers get plain data; the Havok class model stays an implementation detail.

namespace CB::engine::skeleton {
using namespace CB::engine::common;

/// @brief A ragdoll bone's physics, summarized from the skeleton's bodies and constraints (ReadSkeletonPhysics).
///        Present ⇒ this bone is a ragdoll body.
struct BoneJoint {                     // the constraint to this bone's PARENT ragdoll bone (limits in DEGREES)
    enum class Type { Ragdoll, Hinge };
    Type  type = Type::Ragdoll;
    // ragdoll: twist[min,max], cone (one-sided max; min is Havok's ~-100rad sentinel), plane[min,max]
    float twistMin = 0, twistMax = 0;
    float coneMax  = 0;
    float planeMin = 0, planeMax = 0;
    // hinge: single angular limit
    float angMin = 0, angMax = 0;
    // Constraint frame axes (frameA col0 / col1, in the CHILD body-local frame): the joint's rotation
    // axes — authored rigger intent (hinge bend axis, shoulder twist), NOT derivable from bind-pose
    // geometry (see framecheck).
    std::optional<Vector4> twistAxis;   // frameA column 0
    std::optional<Vector4> planeAxis;   // frameA column 1
};
/// @brief The collision CAPSULE, endpoints in the bone's LOCAL frame. Limb capsules are inset from the joint by
///        hand-tuned amounts and torso capsules are width-oriented, so neither follows the bone→child vector.
///        `radius` stays on BonePhysics.
struct BoneCapsule {
    Vector4 a{};   // endpoint A (bone-local)
    Vector4 b{};   // endpoint B (bone-local)
};
struct BonePhysics {
    float                      mass   = 0.f;   // hand-tuned per bone
    float                      radius = 0.f;   // capsule thickness
    std::optional<BoneCapsule> capsule;        // the body's capsule endpoints, when it has a capsule
    std::optional<BoneJoint>   joint;          // absent on the ragdoll ROOT (e.g. COM — no parent constraint)
    // Material friction/restitution: near-constant (0.3 / 0.8) in vanilla, set only where a body deviates
    // (e.g. NPC Head friction 0.8 so heads don't slide). Absent ⇒ the near-constant value.
    std::optional<float>       friction;
    std::optional<float>       restitution;
    // The ragdoll bind pose: this bone's LOCAL reference pose in the RAGDOLL skeleton (relative to its
    // ragdoll parent). The vanilla ragdoll is a separately hand-tuned skeleton (shorter limbs, COM at the
    // mass-center), measurably different from the anim pose; the bodies, mappers and constraints live in it.
    std::optional<QSTransform> ragdollLocal;
};

/// @brief The CharacterBumper — a single FIXED collision body per skeleton (not a bone) that keeps the ragdoll
///        clear of the character controller; represented at skeleton scope.
struct SkeletonBumper {
    Vector4     pos{};             // body position (root-local)
    BoneCapsule capsule{};         // capsule endpoints (body-local)
    float       radius = 0.f;
    float       friction = 0.5f;
    float       restitution = 0.4f;
};

struct SkeletonBoneData {
    std::string                name;
    int                        parentIndex = -1;     // -1 = root
    QSTransform                refPose{};            // bind pose, bone-local
    bool                       lockTranslation = false;  // hkaBone::m_lockTranslation (byte-exact rebuild needs it)
    std::optional<BonePhysics> physics;              // present ⇒ ragdoll body (authored knobs only)
};

struct SkeletonData {
    std::string                   name;
    std::vector<SkeletonBoneData> bones;
    std::optional<SkeletonBumper> bumper;   // authored FIXED bumper body (skeleton scope), if present
};

} // namespace CB::engine::skeleton
