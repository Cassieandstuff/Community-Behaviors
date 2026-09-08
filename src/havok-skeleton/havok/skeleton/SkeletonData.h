#pragma once
#include "havok/core/HkTypes.h"   // Vector4 / QSTransform (havok-framing — havok-core-FREE)

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

// SkeletonData — the plain, engine-free skeleton model: names, parent indices,
// bind-pose transforms, and the AUTHORED ragdoll-physics knobs. It is the neutral
// currency between the schema-native skeleton READER (SkeletonImport) and WRITER
// (SkeletonCompiler), and the surface a tool (the Scene Editor) authors against.
//
// Deliberately NOT hkaSkeleton / hkp* — consumers get plain data; the Havok class
// model stays an implementation detail of the codec. This model was lifted out of
// the quarantined havok-core (havok::sct) unchanged EXCEPT for the namespace: a
// skeleton .hkx is a generic Havok packfile, so the codec is a first-class member
// of the data-driven Havok stack (havok::skeleton), not a tool-scoped import.

namespace havok::skeleton {

// Per-bone ragdoll PHYSICS — the minimal AUTHORED surface. Everything else (body transform, inertia,
// constraint frames, capsule endpoints, mappers, resource tree, collision filter) is DERIVED at compile
// from bone↔bone / bone↔body relationships (see sop/behavior-relay-skeleton-format.md). Present ⇒ this
// bone is a ragdoll body.
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
    // geometry (see framecheck). ABSENT ⇒ compiler derives a default (twist toward child, plane ⊥).
    // frameB derives from frameA + the two bind poses regardless.
    std::optional<Vector4> twistAxis;   // frameA column 0
    std::optional<Vector4> planeAxis;   // frameA column 1
};
// The collision CAPSULE, endpoints in the bone's LOCAL frame. Genuinely non-derivable AUTHORED intent:
// limb capsules are inset from the joint by hand-tuned amounts (so elbows/knees don't interpenetrate),
// and torso capsules are width-oriented volumes unrelated to any bone→child vector. Optional — when
// absent the compiler derives a default (bone-origin → child-origin), so a NEW/custom skeleton author
// never writes it; it only appears when decompiling existing tuned content. `radius` stays on BonePhysics.
struct BoneCapsule {
    Vector4 a{};   // endpoint A (bone-local)
    Vector4 b{};   // endpoint B (bone-local)
};
struct BonePhysics {
    float                      mass   = 0.f;   // hand-tuned per bone
    float                      radius = 0.f;   // capsule thickness
    std::optional<BoneCapsule> capsule;        // authored endpoints; absent ⇒ derive bone-origin → child
    std::optional<BoneJoint>   joint;          // absent on the ragdoll ROOT (e.g. COM — no parent constraint)
    // Material friction/restitution are near-constant (0.3 / 0.8) compiler defaults — an authored
    // OVERRIDE only where a body deviates (e.g. NPC Head friction 0.8 so heads don't slide). Absent ⇒ default.
    std::optional<float>       friction;
    std::optional<float>       restitution;
    // The ragdoll bind pose: this bone's LOCAL reference pose in the RAGDOLL skeleton (relative to its
    // ragdoll parent). The vanilla ragdoll is a separately hand-tuned skeleton (shorter limbs, COM at the
    // mass-center) — measurably different from the anim pose, and the NIF's bhk bodies sit at THIS pose, so
    // it's functionally required (mappers/bodies/constraints all live in this frame). Absent ⇒ derive from
    // anim (a new custom ragdoll at anim proportions — functional, self-consistent).
    std::optional<QSTransform> ragdollLocal;
};

// The CharacterBumper — a single FIXED collision body per skeleton (not a bone) that keeps the ragdoll
// clear of the character controller. Persistent authored content, not derivable — represented at skeleton
// scope. Everything else about it (motionType FIXED, mass 0, filter 0) is a compiler constant.
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

} // namespace havok::skeleton
