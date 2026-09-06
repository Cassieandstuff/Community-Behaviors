#include "havok/sct/SkeletonCompiler.h"

#include "havok/classes/Animation.h"   // hkaSkeleton, hkaBone, hkaAnimationContainer
#include "havok/classes/Graph.h"       // hkRootLevelContainer, hkRootLevelContainerNamedVariant
#include "havok/classes/Physics.h"     // hkpRigidBody, hkpCapsuleShape (rigid-body compile)
#include "havok/classes/Resource.h"    // hkMemoryResourceContainer/Handle (resource tree; hkpShapeInfo is in Physics.h)
#include "havok/core/PackFileDeserializer.h"
#include "havok/core/PackFileSerializer.h"
#include "havok/sct/HavokFile.h"
#include "SkeletonMath.h"              // FK / compose / inverse (physics derivations)

#include "SchemaCompilerState.h"       // shared data-driven-compiler toggle + schema registry

#include <havok-io/HavokIo.h>          // io::SchemaObject
#include <havok-schema/HavokSchema.h>  // schema::SchemaRegistry

#include <algorithm>
#include <cmath>
#include <cstring>
#include <exception>
#include <memory>
#include <vector>

namespace havok::sct {

// Derive the RAGDOLL skeleton (hkaSkeleton #2) from the anim bones + per-bone physics: the subset of
// bones that have a `physics:` block, in anim order (topo — parent precedes child), each renamed
// "Ragdoll_<bone>", reparented to its NEAREST physics ancestor, with a local reference pose recomputed
// against that ragdoll-parent (skipping the non-physics bones between them). Pure bone-relationship
// derivation — no authored ragdoll-skeleton data.
std::shared_ptr<hkaSkeleton> DeriveRagdollSkeleton(const SkeletonData& anim) {
    const auto world = skmath::worldPoses(anim);

    std::vector<int> ragOfAnim(anim.bones.size(), -1);
    std::vector<int> animOfRag;
    for (int i = 0; i < static_cast<int>(anim.bones.size()); ++i)
        if (anim.bones[i].physics) { ragOfAnim[i] = static_cast<int>(animOfRag.size()); animOfRag.push_back(i); }

    const auto physicsAncestor = [&](int animIdx) {
        int p = anim.bones[animIdx].parentIndex;
        while (p >= 0 && !anim.bones[p].physics) p = anim.bones[p].parentIndex;
        return p;   // anim index of nearest ancestor WITH physics, or -1
    };

    auto skel = std::make_shared<hkaSkeleton>();
    for (int r = 0; r < static_cast<int>(animOfRag.size()); ++r) {
        const int ai   = animOfRag[static_cast<std::size_t>(r)];
        const int pAni = physicsAncestor(ai);
        skel->m_parentIndices.push_back(static_cast<std::int16_t>(pAni >= 0 ? ragOfAnim[pAni] : -1));
        hkaBone b;
        b.m_name            = "Ragdoll_" + anim.bones[static_cast<std::size_t>(ai)].name;
        b.m_lockTranslation = true;   // vanilla ragdoll bones lock translation (mapper transfers rotation
                                      // only; unlocked → limbs stretch/collapse under the pose map)
        skel->m_bones.push_back(std::move(b));
        // Authored ragdoll bind pose (vanilla-tuned) when present, else derive the local refpose from the
        // anim FK (a new custom ragdoll at anim proportions).
        const auto& ph = anim.bones[static_cast<std::size_t>(ai)].physics;
        skel->m_referencePose.push_back(
            (ph && ph->ragdollLocal) ? *ph->ragdollLocal
            : pAni >= 0 ? skmath::compose(skmath::inverse(world[static_cast<std::size_t>(pAni)]),
                                          world[static_cast<std::size_t>(ai)])
                        : world[static_cast<std::size_t>(ai)]);
    }
    skel->m_name = skel->m_bones.empty() ? "Ragdoll" : skel->m_bones[0].m_name;
    return skel;
}

namespace {

// Skyrim hkHalf = the top 16 bits of a float32 (NOT IEEE binary16). float -> half is a bit truncation.
std::uint16_t toHalf(float f) { std::uint32_t u; std::memcpy(&u, &f, 4); return static_cast<std::uint16_t>(u >> 16); }

inline float   vdot(const Vector4& a, const Vector4& b) { return a.x*b.x + a.y*b.y + a.z*b.z; }
inline Vector4 vsub(const Vector4& a, const Vector4& b) { return Vector4{a.x-b.x, a.y-b.y, a.z-b.z, 0}; }
inline Vector4 vcross(const Vector4& a, const Vector4& b) { return Vector4{a.y*b.z-a.z*b.y, a.z*b.x-a.x*b.z, a.x*b.y-a.y*b.x, 0}; }
inline Vector4 vnorm(const Vector4& v) { float m=std::sqrt(vdot(v,v)); return m>1e-9f? Vector4{v.x/m,v.y/m,v.z/m,0}:Vector4{0,0,1,0}; }

// FK world poses of a built hkaSkeleton (ragdoll) — parallel to skmath::worldPoses(SkeletonData).
inline std::vector<QSTransform> skeletonWorld(const hkaSkeleton& s) {
    std::vector<QSTransform> w(s.m_referencePose.size());
    for (std::size_t i = 0; i < w.size(); ++i) {
        const int p = i < s.m_parentIndices.size() ? s.m_parentIndices[i] : -1;
        w[i] = (p >= 0 && p < static_cast<int>(i)) ? skmath::compose(w[static_cast<std::size_t>(p)], s.m_referencePose[i])
                                                   : s.m_referencePose[i];
    }
    return w;
}

// Solid-capsule inertia (cylinder body + two hemisphere caps), verified byte-exact vs vanilla. `len` is
// the distance between the two capsule vertices (Havok's vertices are the hemisphere CENTERS), `r` the
// radius, `m` the mass. Returns {I_along-axis, I_perpendicular}. A near-zero len degenerates to a solid
// sphere (I = 2/5 m r²) — matching vanilla's choice to give short/round bodies isotropic inertia.
struct CapsuleInertia { float axis; float perp; };
CapsuleInertia capsuleInertia(float len, float r, float m) {
    const float pi = 3.14159265358979324f;
    const float vCyl = pi * r * r * len;          // cylinder volume
    const float vCap = (4.f / 3.f) * pi * r * r * r;  // two hemispheres = one sphere
    const float vTot = vCyl + vCap;
    if (vTot <= 1e-12f) return { 0.4f * m * r * r, 0.4f * m * r * r };
    const float mCyl = m * vCyl / vTot;
    const float mCap = m * vCap / vTot;
    const float axis = mCyl * (0.5f * r * r) + mCap * (0.4f * r * r);
    const float perp = mCyl * (len * len / 12.f + 0.25f * r * r)
                     + mCap * (0.4f * r * r + 0.25f * len * len + 0.375f * len * r);
    return { axis, perp };
}

} // namespace

// Derive the ragdoll RIGID BODIES from the anim bones + per-bone physics. Order matches
// DeriveRagdollSkeleton. Pure geometry/mass derivation + vanilla-constant boilerplate; the only authored
// inputs are mass / capsule / joint and (pending) the opaque collisionFilterInfo.
std::vector<std::shared_ptr<hkpRigidBody>> DeriveRigidBodies(const SkeletonData& anim) {
    const auto world = skmath::worldPoses(anim);
    // The bodies sit at the RAGDOLL bind pose (vanilla-tuned when authored, else anim-derived) — that's where
    // the NIF's bhk bodies are, so the motionState must live in this frame, not the anim frame.
    const auto ragSkel  = DeriveRagdollSkeleton(anim);
    const auto ragWorld = skeletonWorld(*ragSkel);
    std::vector<std::shared_ptr<hkpRigidBody>> bodies;

    // ragdoll-body index (order among physics bones) per anim bone, for the collision-filter derivation.
    std::vector<int> ragOfAnim(anim.bones.size(), -1);
    { int r = 0; for (std::size_t i = 0; i < anim.bones.size(); ++i) if (anim.bones[i].physics) ragOfAnim[i] = r++; }
    const auto physicsAncestor = [&](int animIdx) {
        int p = anim.bones[static_cast<std::size_t>(animIdx)].parentIndex;
        while (p >= 0 && !anim.bones[static_cast<std::size_t>(p)].physics) p = anim.bones[static_cast<std::size_t>(p)].parentIndex;
        return p;   // anim index of nearest physics ancestor, or -1
    };

    // nearest physics ancestor's anim index, for deriving a default capsule (origin -> child) when unauthored.
    const auto firstPhysChild = [&](int animIdx) -> int {
        for (int c = 0; c < static_cast<int>(anim.bones.size()); ++c) {
            int p = anim.bones[c].parentIndex;
            while (p >= 0 && !anim.bones[p].physics) p = anim.bones[p].parentIndex;
            if (p == animIdx) return c;
        }
        return -1;
    };

    for (int i = 0; i < static_cast<int>(anim.bones.size()); ++i) {
        const auto& bone = anim.bones[static_cast<std::size_t>(i)];
        if (!bone.physics) continue;
        const auto& ph = *bone.physics;

        auto rb = std::make_shared<hkpRigidBody>();
        rb->m_name = "Ragdoll_" + bone.name;

        // ── shape (capsule): authored endpoints, else derive origin -> first physics child ──
        auto cap = std::make_shared<hkpCapsuleShape>();
        cap->m_radius = ph.radius;
        if (ph.capsule) { cap->m_vertexA = ph.capsule->a; cap->m_vertexB = ph.capsule->b; }
        else {
            cap->m_vertexA = Vector4{0, 0, 0, 0};
            const int ch = firstPhysChild(i);
            cap->m_vertexB = ch >= 0 ? anim.bones[static_cast<std::size_t>(ch)].refPose.translation
                                     : Vector4{0, 0, 0, 0};
        }
        rb->m_collidable.m_shape = cap;

        // ── inertia (cylinder+caps) + motion type from capsule aspect ──
        const Vector4 ab{ cap->m_vertexB.x - cap->m_vertexA.x,
                          cap->m_vertexB.y - cap->m_vertexA.y,
                          cap->m_vertexB.z - cap->m_vertexA.z, 0 };
        const float len = std::sqrt(ab.x * ab.x + ab.y * ab.y + ab.z * ab.z);
        const CapsuleInertia ci = capsuleInertia(len, ph.radius, ph.mass);
        const float invM = ph.mass > 1e-9f ? 1.f / ph.mass : 0.f;

        // motionType: SPHERE_INERTIA(2) for near-round capsules (isotropic), else BOX_INERTIA(3). Vanilla's
        // sphere bodies (Hand/Neck/Head) all have len/r <= 0.09; its box bodies (incl. the borderline
        // Spine2) are >= 0.5 — so 0.25 cleanly separates them.
        const bool sphere = ph.radius > 1e-6f && len < 0.25f * ph.radius;
        rb->m_motion.m_type = sphere ? 2 : 3;

        Vector4 inv{};
        if (sphere) {
            // Sphere-inertia bodies use the PERPENDICULAR inertia for all three axes (verified byte-exact
            // vs vanilla Hand/Neck/Head) — not the along-axis value, not the average.
            const float I = ci.perp;
            inv = Vector4{ I > 1e-12f ? 1.f / I : 0.f, I > 1e-12f ? 1.f / I : 0.f, I > 1e-12f ? 1.f / I : 0.f, invM };
        } else {
            // dominant body axis of the capsule gets I_axis; the other two get I_perp.
            const float ax = std::fabs(ab.x), ay = std::fabs(ab.y), az = std::fabs(ab.z);
            const int dom = (ax >= ay && ax >= az) ? 0 : (ay >= az ? 1 : 2);
            float e[3] = { ci.perp, ci.perp, ci.perp };
            e[dom] = ci.axis;
            inv = Vector4{ e[0] > 1e-12f ? 1.f / e[0] : 0.f,
                           e[1] > 1e-12f ? 1.f / e[1] : 0.f,
                           e[2] > 1e-12f ? 1.f / e[2] : 0.f, invM };
        }
        rb->m_motion.m_inertiaAndMassInv = inv;

        // ── motion state: RAGDOLL-frame world pose + swept transform at rest ──
        auto& ms = rb->m_motion.m_motionState;
        const QSTransform& bindW = ragWorld[static_cast<std::size_t>(ragOfAnim[static_cast<std::size_t>(i)])];
        const hkTransform xf = skmath::toHkTransform(bindW);
        ms.m_transform = xf.m_data;
        const Vector4&    pos = bindW.translation;
        const Quaternion& rot = bindW.rotation;
        const Vector4 q{ rot.x, rot.y, rot.z, rot.w };
        ms.m_sweptTransform = { pos, pos, q, q, Vector4{0, 0, 0, 0} };  // cm0,cm1,rot0,rot1,cmLocal
        // objectRadius = bounding sphere of the capsule's axis-aligned AABB about its center:
        // h[k] = |B[k]-A[k]|/2 + r, radius = |h|. Verified vs vanilla (COM 17.68, Hand 8.33, Thigh 20.48).
        const Vector4 h{ 0.5f * std::fabs(ab.x) + ph.radius, 0.5f * std::fabs(ab.y) + ph.radius,
                         0.5f * std::fabs(ab.z) + ph.radius, 0 };
        ms.m_objectRadius = std::sqrt(h.x * h.x + h.y * h.y + h.z * h.z);

        // ── vanilla-constant boilerplate (captured from the vanilla character skeleton) ──
        rb->m_motion.m_deactivationIntegrateCounter = 15;
        rb->m_motion.m_gravityFactor = toHalf(1.0f);
        ms.m_linearDamping   = toHalf(0.0f);
        ms.m_angularDamping  = toHalf(0.05f);
        ms.m_timeFactor      = toHalf(1.0f);
        ms.m_maxLinearVelocity  = 127;
        ms.m_maxAngularVelocity = 127;
        ms.m_deactivationClass  = 2;
        rb->m_material.m_responseType = 1;
        rb->m_material.m_friction     = ph.friction    ? *ph.friction    : 0.3f;  // authored override (e.g. Head 0.8)
        rb->m_material.m_restitution  = ph.restitution ? *ph.restitution : 0.8f;
        rb->m_damageMultiplier   = 1.0f;
        rb->m_uid                = 0xFFFFFFFFu;
        rb->m_collidable.m_allowedPenetrationDepth = 0.1f;
        rb->m_collidable.m_broadPhaseHandle.m_type = 1;
        rb->m_collidable.m_broadPhaseHandle.m_objectQualityType = 4;
        rb->m_spuCollisionCallback.m_eventFilter = 3;
        rb->m_spuCollisionCallback.m_userFilter  = 1;

        // ── collision filter info: fully DERIVED from ragdoll topology (hkpGroupFilter layout,
        // Havok 2013 hkpGroupFilter.inl) ──
        //   filterInfo = (subSystemId<<5) | (subSystemDontCollideWith<<10) | (systemGroup<<16) | layer
        // Per the group-filter contract: each body gets a unique subSystemId; subSystemDontCollideWith =
        // the PARENT body's subSystemId (parent/child don't collide, everything else in the ragdoll does).
        // layer=0 and systemGroup=1 are the vanilla stored constants (the runtime reassigns systemGroup a
        // unique value per spawned actor). Only uniqueness + the parent link are collision-significant, so
        // subSystemId = ragdoll-body-index+1 is functionally identical to vanilla's tool-authored numbering.
        const int r    = ragOfAnim[static_cast<std::size_t>(i)];
        const int pAni = physicsAncestor(i);
        const int subId = r + 1;                                            // unique, 1-based (Havok: 1..31)
        const int dontColl = pAni >= 0 ? ragOfAnim[static_cast<std::size_t>(pAni)] + 1 : 0;  // parent's id
        rb->m_collidable.m_broadPhaseHandle.m_collisionFilterInfo =
            static_cast<std::uint32_t>((subId << 5) | (dontColl << 10) | (1 << 16) | 0);

        bodies.push_back(std::move(rb));
    }
    return bodies;
}

namespace {
constexpr float kDeg2Rad = 0.017453292519943295f;
constexpr float kConeMin = -100.0f;              // ragdoll cone-limit min sentinel (vanilla: -5729.6 deg)
constexpr float kFltMax  = 3.4028234663852886e38f;

// Build the two constraint frames. `tA`/`pA` are the twist/plane axes in CHILD body-local (orthonormalized
// here); frameA = [tA | pA | tA×pA | 0]; frameB places the SAME world joint frame in the PARENT body-local
// frame (frameA's pivot is the child origin). qC/qP + posC/posP are the child/parent bind world poses.
void buildFrames(Vector4 tA, Vector4 pA, const Quaternion& qC, const Quaternion& qP,
                 const Vector4& posC, const Vector4& posP, hkTransform& fA, hkTransform& fB) {
    tA = vnorm(tA);
    pA = vnorm(vsub(pA, Vector4{tA.x*vdot(pA,tA), tA.y*vdot(pA,tA), tA.z*vdot(pA,tA), 0}));  // ⊥ twist
    const Vector4 cA = vcross(tA, pA);
    fA.m_data = { tA, pA, cA, Vector4{0,0,0,0} };
    // frameB column j = R_parent^T * (R_child * frameA_col_j) ; translation = R_parent^T * (posC - posP).
    const Quaternion qPc = skmath::qconj(qP);
    for (int j = 0; j < 3; ++j)
        fB.m_data[static_cast<std::size_t>(j)] = skmath::qrot(qPc, skmath::qrot(qC, fA.m_data[static_cast<std::size_t>(j)]));
    fB.m_data[3] = skmath::qrot(qPc, vsub(posC, posP));
}

// The two constant transform/setup atoms + the ballSocket, shared by both constraint types.
void fillCommonAtoms(hkpSetLocalTransformsConstraintAtom& tr, const hkTransform& fA, const hkTransform& fB,
                     hkpSetupStabilizationAtom& st, hkpBallSocketConstraintAtom& bs) {
    tr.m_type = 2; tr.m_transformA = fA; tr.m_transformB = fB;
    st.m_type = 23; st.m_enabled = false; st.m_maxAngle = 1.8446744e19f;   // vanilla value; inert (disabled)
    bs.m_type = 5; bs.m_solvingMethod = 1; bs.m_bodiesToNotify = 0;
    bs.m_velocityStabilizationFactor = 48; bs.m_maxImpulse = kFltMax; bs.m_inertiaStabilizationFactor = 0;
}
} // namespace

std::vector<std::shared_ptr<hkReferencedObject>>
DeriveConstraints(const SkeletonData& anim, const std::vector<std::shared_ptr<hkpRigidBody>>& bodies) {
    const auto world = skmath::worldPoses(anim);
    // Constraint frames live in the RAGDOLL body frame (== anim frame for a custom ragdoll with no
    // authored ragdollLocal, so this is safe in both cases).
    const auto ragSkelC  = DeriveRagdollSkeleton(anim);
    const auto ragWorld  = skeletonWorld(*ragSkelC);

    // physics-bone bookkeeping: ragdoll index per anim bone + nearest physics ancestor + chain-continuation
    // child (for the toward-child default twist).
    std::vector<int> ragOfAnim(anim.bones.size(), -1);
    { int r = 0; for (std::size_t i = 0; i < anim.bones.size(); ++i) if (anim.bones[i].physics) ragOfAnim[i] = r++; }
    const auto physAncestor = [&](int a) { int p = anim.bones[a].parentIndex; while (p>=0 && !anim.bones[p].physics) p = anim.bones[p].parentIndex; return p; };
    // chain-continuation physics child: the physics child whose local direction is most in-line with the
    // bone's own +Z (so a branching bone like Spine2 follows Neck, not an arm); -1 if none (leaf).
    const auto chainChild = [&](int a) -> int {
        int best = -1; float bestDot = -2.f;
        const Vector4 up = skmath::qrot(world[a].rotation, Vector4{0,0,1,0});
        for (int c = 0; c < static_cast<int>(anim.bones.size()); ++c) {
            if (!anim.bones[c].physics) continue;
            if (physAncestor(c) != a) continue;
            const Vector4 dir = vnorm(vsub(world[c].translation, world[a].translation));
            const float d = vdot(dir, up);
            if (d > bestDot) { bestDot = d; best = c; }
        }
        return best;
    };

    std::vector<std::shared_ptr<hkReferencedObject>> out;
    for (int i = 0; i < static_cast<int>(anim.bones.size()); ++i) {
        const auto& bone = anim.bones[static_cast<std::size_t>(i)];
        if (!bone.physics || !bone.physics->joint) continue;    // root ragdoll bone (COM) has no joint
        const int pAni = physAncestor(i);
        if (pAni < 0) continue;
        const auto& j = *bone.physics->joint;
        const int rC = ragOfAnim[static_cast<std::size_t>(i)];
        const int rP = ragOfAnim[static_cast<std::size_t>(pAni)];

        // twist/plane axes (child-local): authored, else derived (toward chain child / +Z leaf, ⊥ plane).
        Vector4 tA, pA;
        if (j.twistAxis) tA = *j.twistAxis;
        else {
            const int gc = chainChild(i);
            tA = gc >= 0 ? vnorm(skmath::qrot(skmath::qconj(world[i].rotation), vsub(world[gc].translation, world[i].translation)))
                         : Vector4{0,0,1,0};
        }
        if (j.planeAxis) pA = *j.planeAxis;
        else {
            pA = vcross(tA, Vector4{0,0,1,0});
            if (vdot(pA,pA) < 1e-6f) pA = vcross(tA, Vector4{1,0,0,0});   // twist ∥ Z → use X
        }

        hkTransform fA, fB;
        const auto& wC = ragWorld[static_cast<std::size_t>(rC)];   // child/parent BODY bind poses (ragdoll frame)
        const auto& wP = ragWorld[static_cast<std::size_t>(rP)];
        buildFrames(tA, pA, wC.rotation, wP.rotation, wC.translation, wP.translation, fA, fB);

        auto ci = std::make_shared<hkpConstraintInstance>();
        ci->m_entities[0] = bodies[static_cast<std::size_t>(rC)];
        ci->m_entities[1] = bodies[static_cast<std::size_t>(rP)];
        ci->m_priority = 1; ci->m_wantRuntime = true;
        ci->m_name = "Ragdoll_" + bone.name;

        if (j.type == BoneJoint::Type::Hinge) {
            auto d = std::make_shared<hkpLimitedHingeConstraintData>();
            auto& at = d->m_atoms;
            fillCommonAtoms(at.m_transforms, fA, fB, at.m_setupStabilization, at.m_ballSocket);
            at.m_angMotor.m_type = 18; at.m_angMotor.m_isEnabled = false; at.m_angMotor.m_motorAxis = 0;
            at.m_angFriction.m_type = 17; at.m_angFriction.m_isEnabled = 1; at.m_angFriction.m_maxFrictionTorque = 0;
            at.m_angLimit.m_type = 14; at.m_angLimit.m_limitAxis = 0; at.m_angLimit.m_angularLimitsTauFactor = 1;
            at.m_angLimit.m_minAngle = j.angMin * kDeg2Rad; at.m_angLimit.m_maxAngle = j.angMax * kDeg2Rad;
            at.m_2dAng.m_type = 12; at.m_2dAng.m_freeRotationAxis = 0;
            ci->m_data = d;
        } else {
            auto d = std::make_shared<hkpRagdollConstraintData>();
            auto& at = d->m_atoms;
            fillCommonAtoms(at.m_transforms, fA, fB, at.m_setupStabilization, at.m_ballSocket);
            at.m_ragdollMotors.m_type = 19; at.m_ragdollMotors.m_isEnabled = false;
            at.m_angFriction.m_type = 17; at.m_angFriction.m_isEnabled = 1; at.m_angFriction.m_maxFrictionTorque = 0;
            at.m_twistLimit.m_type = 15; at.m_twistLimit.m_twistAxis = 0; at.m_twistLimit.m_refAxis = 1;
            at.m_twistLimit.m_angularLimitsTauFactor = 0.8f;
            at.m_twistLimit.m_minAngle = j.twistMin * kDeg2Rad; at.m_twistLimit.m_maxAngle = j.twistMax * kDeg2Rad;
            at.m_coneLimit.m_type = 16; at.m_coneLimit.m_twistAxisInA = 0; at.m_coneLimit.m_refAxisInB = 0;
            at.m_coneLimit.m_angleMeasurementMode = 0; at.m_coneLimit.m_memOffsetToAngleOffset = 56;
            at.m_coneLimit.m_angularLimitsTauFactor = 0.8f;
            at.m_coneLimit.m_minAngle = kConeMin; at.m_coneLimit.m_maxAngle = j.coneMax * kDeg2Rad;
            at.m_planesLimit.m_type = 16; at.m_planesLimit.m_twistAxisInA = 0; at.m_planesLimit.m_refAxisInB = 1;
            at.m_planesLimit.m_angleMeasurementMode = 1; at.m_planesLimit.m_angularLimitsTauFactor = 0.8f;
            at.m_planesLimit.m_minAngle = j.planeMin * kDeg2Rad; at.m_planesLimit.m_maxAngle = j.planeMax * kDeg2Rad;
            ci->m_data = d;
        }
        out.push_back(std::move(ci));
    }
    return out;
}

// Stage 2 (scatter) + Stage 5 (root assembly), animation-only. Pure graph build;
// the serializer decides object ordering.
static std::shared_ptr<hkRootLevelContainer> BuildSkeletonRoot(const SkeletonData& data) {
    auto skel = std::make_shared<hkaSkeleton>();
    skel->m_name = data.name.empty() ? "Skeleton" : data.name;

    const std::size_t n = data.bones.size();
    skel->m_parentIndices.reserve(n);
    skel->m_bones.reserve(n);
    skel->m_referencePose.reserve(n);
    for (const auto& b : data.bones) {
        skel->m_parentIndices.push_back(static_cast<std::int16_t>(b.parentIndex));
        hkaBone bone;
        bone.m_name = b.name;
        bone.m_lockTranslation = b.lockTranslation;
        skel->m_bones.push_back(std::move(bone));
        skel->m_referencePose.push_back(b.refPose);
    }
    // m_referenceFloats / m_floatSlots / m_localFrames: empty in the first cut.

    auto anim = std::make_shared<hkaAnimationContainer>();
    anim->m_skeletons.push_back(std::move(skel));

    auto root = std::make_shared<hkRootLevelContainer>();
    hkRootLevelContainerNamedVariant nv;
    nv.m_name      = "Merged Animation Container";  // the name vanilla skeleton.hkx uses
    nv.m_className = "hkaAnimationContainer";
    nv.m_variant   = std::move(anim);
    root->m_namedVariants.push_back(std::move(nv));
    return root;
}

namespace {

// The scatter half of BuildSkeletonRoot, returning the hkaSkeleton (reused by the fused compile).
std::shared_ptr<hkaSkeleton> buildAnimSkeleton(const SkeletonData& data) {
    auto skel = std::make_shared<hkaSkeleton>();
    skel->m_name = data.name.empty() ? "Skeleton" : data.name;
    const std::size_t n = data.bones.size();
    skel->m_parentIndices.reserve(n); skel->m_bones.reserve(n); skel->m_referencePose.reserve(n);
    for (const auto& b : data.bones) {
        skel->m_parentIndices.push_back(static_cast<std::int16_t>(b.parentIndex));
        hkaBone bone; bone.m_name = b.name; bone.m_lockTranslation = b.lockTranslation;
        skel->m_bones.push_back(std::move(bone));
        skel->m_referencePose.push_back(b.refPose);
    }
    return skel;
}

// The CharacterBumper — a single FIXED (motionType 5) collision body from the authored bumper block; not a
// bone, not in the ragdoll instance. Keeps the ragdoll clear of the character controller.
std::shared_ptr<hkpRigidBody> buildBumperBody(const SkeletonBumper& bp) {
    auto rb = std::make_shared<hkpRigidBody>();
    rb->m_name = "CharacterBumper";
    auto cap = std::make_shared<hkpCapsuleShape>();
    cap->m_radius = bp.radius; cap->m_vertexA = bp.capsule.a; cap->m_vertexB = bp.capsule.b;
    rb->m_collidable.m_shape = cap;
    rb->m_motion.m_type = 5;                          // FIXED
    rb->m_motion.m_inertiaAndMassInv = Vector4{0,0,0,0};   // mass 0, no inertia
    auto& ms = rb->m_motion.m_motionState;
    ms.m_transform = { Vector4{1,0,0,0}, Vector4{0,1,0,0}, Vector4{0,0,1,0}, bp.pos };  // identity rot @ pos
    const Vector4 q{0,0,0,1};
    ms.m_sweptTransform = { bp.pos, bp.pos, q, q, Vector4{0,0,0,0} };
    const Vector4 h{ 0.5f * std::fabs(bp.capsule.b.x - bp.capsule.a.x) + bp.radius,
                     0.5f * std::fabs(bp.capsule.b.y - bp.capsule.a.y) + bp.radius,
                     0.5f * std::fabs(bp.capsule.b.z - bp.capsule.a.z) + bp.radius, 0 };
    ms.m_objectRadius = std::sqrt(h.x*h.x + h.y*h.y + h.z*h.z);
    rb->m_motion.m_deactivationIntegrateCounter = 15;
    rb->m_motion.m_gravityFactor = toHalf(1.0f);
    ms.m_linearDamping = toHalf(0.0f); ms.m_angularDamping = toHalf(0.05f); ms.m_timeFactor = toHalf(1.0f);
    ms.m_maxLinearVelocity = 127; ms.m_maxAngularVelocity = 127; ms.m_deactivationClass = 2;
    rb->m_material.m_responseType = 1;
    rb->m_material.m_friction = bp.friction; rb->m_material.m_restitution = bp.restitution;
    rb->m_damageMultiplier = 1.0f; rb->m_uid = 0xFFFFFFFFu;
    rb->m_collidable.m_allowedPenetrationDepth = 0.1f;
    rb->m_collidable.m_broadPhaseHandle.m_type = 1;
    rb->m_collidable.m_broadPhaseHandle.m_objectQualityType = 4;
    rb->m_spuCollisionCallback.m_eventFilter = 3; rb->m_spuCollisionCallback.m_userFilter = 1;
    rb->m_collidable.m_broadPhaseHandle.m_collisionFilterInfo = 0;   // vanilla bumper filter = 0
    return rb;
}

// One anim↔ragdoll skeleton mapper. `aFromWorld`/`bFromWorld` are the A/B skeletons' bind world poses;
// mappings pair each ragdoll bone (by stripped name) to its anim bone; aFromBTransform = the bind-pose
// A-from-B offset (compose(inverse(worldB), worldA)) — identity when the two bind poses coincide.
std::shared_ptr<hkaSkeletonMapper>
buildMapper(const std::shared_ptr<hkaSkeleton>& skA, const std::shared_ptr<hkaSkeleton>& skB,
            const std::vector<QSTransform>& worldA, const std::vector<QSTransform>& worldB,
            const std::vector<std::pair<int,int>>& pairsAB,   // (indexInA, indexInB)
            const std::vector<std::int16_t>& unmapped) {
    auto m = std::make_shared<hkaSkeletonMapper>();
    auto& d = m->m_mapping;
    d.m_skeletonA = skA; d.m_skeletonB = skB;
    d.m_keepUnmappedLocal = true; d.m_mappingType = 0;
    d.m_extractedMotionMapping = QSTransform{ Vector4{0,0,0,0}, Quaternion{0,0,0,1}, Vector4{1,1,1,1} };
    for (auto [ia, ib] : pairsAB) {
        hkaSkeletonMapperDataSimpleMapping sm;
        sm.m_boneA = static_cast<std::int16_t>(ia);
        sm.m_boneB = static_cast<std::int16_t>(ib);
        // aFromBTransform = B expressed in A's bind frame = inverse(worldA) ∘ worldB (verified vs vanilla;
        // the opposite order inverts it → the ragdoll spins).
        sm.m_aFromBTransform = skmath::compose(skmath::inverse(worldA[static_cast<std::size_t>(ia)]),
                                               worldB[static_cast<std::size_t>(ib)]);
        d.m_simpleMappings.push_back(sm);
    }
    d.m_unmappedBones = unmapped;
    return m;
}

// ── Resource container (physics tree) ───────────────────────────────────────────────────────────
// Derive the runtime-load-bearing slice of vanilla's "Resource Data" variant: the CharacterBumper
// and the ragdoll-body resource tree, each physics node carrying an hkRigidBody + hkpShapeInfo
// handle whose variants point at the SAME body/shape objects the physics system and ragdoll instance
// already share. This is the tree the engine resolves to build the ragdoll driver — its absence is
// why the driver came back null (the incomplete-ragdoll crash).
//
// Vanilla also carries ~132 handle-LESS export-scene helper CONs (NPC/Cloak/Robe/Camera pivots): 3ds
// Max node hierarchy captured by the Havok Content Tools at export, NOT present in the skeleton source
// and NOT read at runtime. We omit them by design — this is a DERIVED physics tree, never a verbatim
// copy of the vanilla resource blob (the exact thing the "just paste the vanilla container" crowd does).
std::shared_ptr<hkpShapeInfo> makeShapeInfo(const std::shared_ptr<hkpShape>& shape) {
    auto si = std::make_shared<hkpShapeInfo>();
    si->m_shape = shape;
    si->m_isHierarchicalCompound = false;
    si->m_hkdShapesCollected     = false;
    si->m_transform.m_data = { Vector4{1,0,0,0}, Vector4{0,1,0,0}, Vector4{0,0,1,0}, Vector4{0,0,0,0} };  // identity
    return si;
}

std::shared_ptr<hkMemoryResourceHandle> makeHandle(const char* name, std::shared_ptr<hkReferencedObject> v) {
    auto h = std::make_shared<hkMemoryResourceHandle>();
    h->m_name    = name;
    h->m_variant = std::move(v);
    return h;
}

// Build one CON per ragdoll body (rigidbody + shapeInfo handle) nested by the ragdoll skeleton's
// parent hierarchy, plus a CharacterBumper CON, all hung off an unnamed root. Children sorted by name
// (vanilla convention). bodies[r] corresponds to ragSkel->m_bones[r] (identity boneToRigidBodyMap).
std::shared_ptr<hkMemoryResourceContainer>
DeriveResourceContainer(const std::shared_ptr<hkaSkeleton>& ragSkel,
                        const std::vector<std::shared_ptr<hkpRigidBody>>& bodies,
                        const std::shared_ptr<hkpRigidBody>& bumper) {
    const int n = static_cast<int>(ragSkel->m_bones.size());
    std::vector<std::shared_ptr<hkMemoryResourceContainer>> cons(static_cast<std::size_t>(n));
    for (int r = 0; r < n; ++r) {
        auto c = std::make_shared<hkMemoryResourceContainer>();
        c->m_name = ragSkel->m_bones[static_cast<std::size_t>(r)].m_name;          // "Ragdoll_<bone>"
        c->m_resourceHandles.push_back(makeHandle("hkRigidBody", bodies[static_cast<std::size_t>(r)]));
        c->m_resourceHandles.push_back(makeHandle("hkpShapeInfo",
                                                  makeShapeInfo(bodies[static_cast<std::size_t>(r)]->m_collidable.m_shape)));
        cons[static_cast<std::size_t>(r)] = std::move(c);
    }
    std::shared_ptr<hkMemoryResourceContainer> ragRoot;
    for (int r = 0; r < n; ++r) {
        const int p = ragSkel->m_parentIndices[static_cast<std::size_t>(r)];
        if (p >= 0 && p < n) cons[static_cast<std::size_t>(p)]->m_children.push_back(cons[static_cast<std::size_t>(r)]);
        else                 ragRoot = cons[static_cast<std::size_t>(r)];
    }
    const auto byName = [](const std::shared_ptr<hkMemoryResourceContainer>& a,
                           const std::shared_ptr<hkMemoryResourceContainer>& b) { return a->m_name < b->m_name; };
    for (auto& c : cons) std::sort(c->m_children.begin(), c->m_children.end(), byName);

    auto root = std::make_shared<hkMemoryResourceContainer>();
    root->m_name = "";
    if (bumper) {
        auto cb = std::make_shared<hkMemoryResourceContainer>();
        cb->m_name = "CharacterBumper";
        cb->m_resourceHandles.push_back(makeHandle("hkRigidBody", bumper));
        cb->m_resourceHandles.push_back(makeHandle("hkpShapeInfo", makeShapeInfo(bumper->m_collidable.m_shape)));
        root->m_children.push_back(std::move(cb));
    }
    if (ragRoot) root->m_children.push_back(ragRoot);
    std::sort(root->m_children.begin(), root->m_children.end(), byName);
    return root;
}

} // namespace

// FUSED compile — the full 6-variant skeleton.hkx from one SkeletonData (+ per-bone physics).
namespace {

// Data-driven (schema) ANIM-skeleton emit — the equivalent of BuildSkeletonRoot / the anim-only
// branch of CompileSkeletonFull, building the hkaSkeleton (+ hkaBone / referencePose) inside a
// hkaAnimationContainer via the Havok/ descriptors. Byte-gated == typed. This covers the anim
// skeleton only; the full ragdoll physics graph stays on the typed path (a separate milestone).
std::shared_ptr<io::SchemaObject> mkS(const schema::SchemaRegistry& reg, const char* cls) {
    const schema::ClassSchema* cs = reg.Find(cls);
    if (!cs) return nullptr;
    auto o = std::make_shared<io::SchemaObject>(&reg, cs);
    o->Init();
    return o;
}

std::shared_ptr<io::SchemaObject> AssembleSkeletonAnim(const SkeletonData& data, const schema::SchemaRegistry& reg) {
    auto pf = [](std::vector<std::uint8_t>& v, float f) { std::uint8_t b[4]; std::memcpy(b, &f, 4); v.insert(v.end(), b, b + 4); };

    auto skel = mkS(reg, "hkaSkeleton"); if (!skel) return nullptr;
    skel->FieldRef("name").str = data.name.empty() ? "Skeleton" : data.name;
    { std::vector<std::uint8_t>& pi = skel->FieldRef("parentIndices").raw;
      for (const auto& b : data.bones) { std::int16_t v = static_cast<std::int16_t>(b.parentIndex); std::uint8_t bb[2]; std::memcpy(bb, &v, 2); pi.insert(pi.end(), bb, bb + 2); } }
    { auto& bones = skel->FieldRef("bones").objs;
      for (const auto& b : data.bones) {
          auto bone = mkS(reg, "hkaBone"); if (!bone) return nullptr;
          bone->FieldRef("name").str = b.name;
          bone->FieldRef("lockTranslation").raw = { static_cast<std::uint8_t>(b.lockTranslation ? 1 : 0) };
          bones.push_back(bone);
      } }
    { std::vector<std::uint8_t>& rp = skel->FieldRef("referencePose").raw;   // qstransformarray: 48 B/bone
      for (const auto& b : data.bones) { const auto& t = b.refPose;
          pf(rp, t.translation.x); pf(rp, t.translation.y); pf(rp, t.translation.z); pf(rp, t.translation.w);
          pf(rp, t.rotation.x);    pf(rp, t.rotation.y);    pf(rp, t.rotation.z);    pf(rp, t.rotation.w);
          pf(rp, t.scale.x);       pf(rp, t.scale.y);       pf(rp, t.scale.z);       pf(rp, t.scale.w); } }
    // referenceFloats / floatSlots / localFrames intentionally empty (matches the typed first cut).

    auto container = mkS(reg, "hkaAnimationContainer"); if (!container) return nullptr;
    container->FieldRef("skeletons").objs.push_back(skel);

    auto root = mkS(reg, "hkRootLevelContainer"); if (!root) return nullptr;
    auto nv   = mkS(reg, "hkRootLevelContainerNamedVariant"); if (!nv) return nullptr;
    nv->FieldRef("name").str      = "Merged Animation Container";
    nv->FieldRef("className").str  = "hkaAnimationContainer";
    nv->FieldRef("variant").obj    = container;
    root->FieldRef("namedVariants").objs.push_back(nv);
    return root;
}

// ── Data-driven (schema) FULL ragdoll emit ────────────────────────────────────────────────────────
// The physics counterpart of AssembleSkeletonAnim: the full 6-variant skeleton.hkx (anim + ragdoll
// skeletons, rigid bodies, constraints, ragdoll instance, physics system, 2 mappers, resource tree)
// as an io::SchemaObject graph via the Havok/ descriptors. TRANSCRIBES the typed derivations
// (DeriveRigidBodies / DeriveConstraints / … — reused verbatim so the intricate inertia / collision-
// filter / frame math has ONE source of truth), reading each typed object's ACTUAL field values (which
// captures the typed constructor defaults for free) into the schema fields. Byte-gated == typed via
// `skeleton-full-schema-check`. Any nullptr (unregistered class) aborts → CompileSkeletonFull falls
// back to the typed emit.

// little-endian scalar/vector encoders → FieldValue.raw bytes
inline void apF (std::vector<std::uint8_t>& v, float f)        { std::uint8_t b[4]; std::memcpy(b, &f, 4); v.insert(v.end(), b, b + 4); }
inline void apV4(std::vector<std::uint8_t>& v, const Vector4& q){ apF(v, q.x); apF(v, q.y); apF(v, q.z); apF(v, q.w); }
inline void apHT(std::vector<std::uint8_t>& v, const hkTransform& t){ for (int i = 0; i < 4; ++i) apV4(v, t.m_data[static_cast<std::size_t>(i)]); }
inline void apQt(std::vector<std::uint8_t>& v, const QSTransform& t){ apV4(v, t.translation);
    apF(v, t.rotation.x); apF(v, t.rotation.y); apF(v, t.rotation.z); apF(v, t.rotation.w); apV4(v, t.scale); }
inline std::vector<std::uint8_t> e8 (std::uint8_t x) { return { x }; }
inline std::vector<std::uint8_t> e16(std::uint16_t x){ return { static_cast<std::uint8_t>(x & 0xff), static_cast<std::uint8_t>(x >> 8) }; }
inline std::vector<std::uint8_t> e32(std::uint32_t x){ std::vector<std::uint8_t> v(4); std::memcpy(v.data(), &x, 4); return v; }
inline std::vector<std::uint8_t> e64(std::uint64_t x){ std::vector<std::uint8_t> v(8); std::memcpy(v.data(), &x, 8); return v; }
inline std::vector<std::uint8_t> ef (float f)        { std::vector<std::uint8_t> v; apF(v, f); return v; }
inline std::vector<std::uint8_t> eV4(const Vector4& q){ std::vector<std::uint8_t> v; apV4(v, q); return v; }
// Raw 16-byte copy of a Vector4 (preserves EVERY bit incl. -0.0 padding; a float-by-float copy can be
// canonicalized -0.0 -> +0.0 by fast-math, which diverges from the typed WriteVector4's raw write).
inline std::vector<std::uint8_t> rawV4(const Vector4& q){ std::vector<std::uint8_t> v(16); std::memcpy(v.data(), &q, 16); return v; }

// hkTransform is a SIGNATURELESS inline value struct — a FieldKind::Struct with one `data` field
// (vector4 count:4 = 64B). It MUST be set via .obj (a nested SchemaObject); a .raw on a Struct field
// is ignored and Init's all-zero default struct serializes instead (right size, zero content).
std::shared_ptr<io::SchemaObject> mkS(const schema::SchemaRegistry& reg, const char* cls);   // fwd (defined above)
std::shared_ptr<io::SchemaObject> schemaHkTransform(const schema::SchemaRegistry& reg, const hkTransform& t) {
    auto o = mkS(reg, "hkTransform"); if (!o) return nullptr;
    auto& d = o->FieldRef("data").raw; d.clear(); apHT(d, t);
    return o;
}

// hkaSkeleton (anim OR ragdoll) → schema, from a typed hkaSkeleton.
std::shared_ptr<io::SchemaObject> schemaSkel(const schema::SchemaRegistry& reg, const hkaSkeleton& sk) {
    auto o = mkS(reg, "hkaSkeleton"); if (!o) return nullptr;
    o->FieldRef("name").str = sk.m_name;
    { auto& pi = o->FieldRef("parentIndices").raw;
      for (std::int16_t p : sk.m_parentIndices) { std::uint8_t b[2]; std::memcpy(b, &p, 2); pi.insert(pi.end(), b, b + 2); } }
    { auto& bones = o->FieldRef("bones").objs;
      for (const auto& b : sk.m_bones) { auto bo = mkS(reg, "hkaBone"); if (!bo) return nullptr;
          bo->FieldRef("name").str = b.m_name;
          bo->FieldRef("lockTranslation").raw = e8(b.m_lockTranslation ? 1 : 0); bones.push_back(bo); } }
    { auto& rp = o->FieldRef("referencePose").raw; for (const auto& t : sk.m_referencePose) apQt(rp, t); }
    return o;
}

// hkpCapsuleShape → schema; records typed-shape→schema in `shapeMap` for the shared resource-tree ref.
std::shared_ptr<io::SchemaObject> schemaShape(const schema::SchemaRegistry& reg, const std::shared_ptr<hkpShape>& sh,
        std::unordered_map<const hkpShape*, std::shared_ptr<io::SchemaObject>>& shapeMap) {
    if (auto it = shapeMap.find(sh.get()); it != shapeMap.end()) return it->second;
    auto cap = std::dynamic_pointer_cast<hkpCapsuleShape>(sh); if (!cap) return nullptr;
    auto o = mkS(reg, "hkpCapsuleShape"); if (!o) return nullptr;
    o->FieldRef("radius").raw  = ef(cap->m_radius);
    o->FieldRef("vertexA").raw = rawV4(cap->m_vertexA);
    o->FieldRef("vertexB").raw = rawV4(cap->m_vertexB);
    shapeMap[sh.get()] = o;
    return o;
}

// hkpRigidBody → schema (full collidable/material/motion/motionState chain); records typed→schema in maps.
std::shared_ptr<io::SchemaObject> schemaBody(const schema::SchemaRegistry& reg, const hkpRigidBody& rb,
        std::unordered_map<const hkpRigidBody*, std::shared_ptr<io::SchemaObject>>& bodyMap,
        std::unordered_map<const hkpShape*, std::shared_ptr<io::SchemaObject>>& shapeMap) {
    if (auto it = bodyMap.find(&rb); it != bodyMap.end()) return it->second;
    auto o = mkS(reg, "hkpRigidBody"); if (!o) return nullptr;
    o->FieldRef("name").str = rb.m_name;
    // collidable (hkpLinkedCollidable): shape ptr + broadPhaseHandle + allowedPenetrationDepth
    auto coll = mkS(reg, "hkpLinkedCollidable"); if (!coll) return nullptr;
    coll->FieldRef("shape").obj = schemaShape(reg, rb.m_collidable.m_shape, shapeMap);
    coll->FieldRef("allowedPenetrationDepth").raw = ef(rb.m_collidable.m_allowedPenetrationDepth);
    auto bph = mkS(reg, "hkpTypedBroadPhaseHandle"); if (!bph) return nullptr;
    bph->FieldRef("type").raw               = e8(static_cast<std::uint8_t>(rb.m_collidable.m_broadPhaseHandle.m_type));
    bph->FieldRef("objectQualityType").raw  = e8(static_cast<std::uint8_t>(rb.m_collidable.m_broadPhaseHandle.m_objectQualityType));
    bph->FieldRef("collisionFilterInfo").raw = e32(rb.m_collidable.m_broadPhaseHandle.m_collisionFilterInfo);
    coll->FieldRef("broadPhaseHandle").obj = bph;
    o->FieldRef("collidable").obj = coll;
    // material
    auto mat = mkS(reg, "hkpMaterial"); if (!mat) return nullptr;
    mat->FieldRef("responseType").raw = e8(static_cast<std::uint8_t>(rb.m_material.m_responseType));
    mat->FieldRef("friction").raw     = ef(rb.m_material.m_friction);
    mat->FieldRef("restitution").raw  = ef(rb.m_material.m_restitution);
    o->FieldRef("material").obj = mat;
    o->FieldRef("damageMultiplier").raw = ef(rb.m_damageMultiplier);
    o->FieldRef("uid").raw = e32(rb.m_uid);
    // spuCollisionCallback
    auto spu = mkS(reg, "hkpEntitySpuCollisionCallback"); if (!spu) return nullptr;
    spu->FieldRef("eventFilter").raw = e8(static_cast<std::uint8_t>(rb.m_spuCollisionCallback.m_eventFilter));
    spu->FieldRef("userFilter").raw  = e8(static_cast<std::uint8_t>(rb.m_spuCollisionCallback.m_userFilter));
    o->FieldRef("spuCollisionCallback").obj = spu;
    // motion (hkpMaxSizeMotion; fields from hkpMotion) + motionState (hkMotionState)
    auto mo = mkS(reg, "hkpMaxSizeMotion"); if (!mo) return nullptr;
    mo->FieldRef("type").raw                         = e8(static_cast<std::uint8_t>(rb.m_motion.m_type));
    mo->FieldRef("deactivationIntegrateCounter").raw = e8(static_cast<std::uint8_t>(rb.m_motion.m_deactivationIntegrateCounter));
    mo->FieldRef("inertiaAndMassInv").raw            = eV4(rb.m_motion.m_inertiaAndMassInv);
    mo->FieldRef("gravityFactor").raw                = e16(rb.m_motion.m_gravityFactor);
    auto ms = mkS(reg, "hkMotionState"); if (!ms) return nullptr;
    { auto& tf = ms->FieldRef("transform").raw;      tf.clear(); for (int i = 0; i < 4; ++i) apV4(tf, rb.m_motion.m_motionState.m_transform[static_cast<std::size_t>(i)]); }
    { auto& sw = ms->FieldRef("sweptTransform").raw; sw.clear(); for (int i = 0; i < 5; ++i) apV4(sw, rb.m_motion.m_motionState.m_sweptTransform[static_cast<std::size_t>(i)]); }
    ms->FieldRef("deltaAngle").raw        = eV4(rb.m_motion.m_motionState.m_deltaAngle);   // typed default carries -0.0 in w
    ms->FieldRef("objectRadius").raw      = ef(rb.m_motion.m_motionState.m_objectRadius);
    ms->FieldRef("linearDamping").raw     = e16(rb.m_motion.m_motionState.m_linearDamping);
    ms->FieldRef("angularDamping").raw    = e16(rb.m_motion.m_motionState.m_angularDamping);
    ms->FieldRef("timeFactor").raw        = e16(rb.m_motion.m_motionState.m_timeFactor);
    ms->FieldRef("maxLinearVelocity").raw  = e8(static_cast<std::uint8_t>(rb.m_motion.m_motionState.m_maxLinearVelocity));
    ms->FieldRef("maxAngularVelocity").raw = e8(static_cast<std::uint8_t>(rb.m_motion.m_motionState.m_maxAngularVelocity));
    ms->FieldRef("deactivationClass").raw  = e8(static_cast<std::uint8_t>(rb.m_motion.m_motionState.m_deactivationClass));
    mo->FieldRef("motionState").obj = ms;
    o->FieldRef("motion").obj = mo;
    bodyMap[&rb] = o;
    return o;
}

// A constraint atom SchemaObject filled from the typed atom via its schema field names. The atom
// structs are flat scalars (+ two hkTransforms on transforms); we set every named field from the
// typed value, so constructor defaults transcribe faithfully.
std::shared_ptr<io::SchemaObject> schemaConstraint(const schema::SchemaRegistry& reg, const hkpConstraintInstance& ci,
        std::unordered_map<const hkpRigidBody*, std::shared_ptr<io::SchemaObject>>& bodyMap,
        std::unordered_map<const hkpShape*, std::shared_ptr<io::SchemaObject>>& shapeMap) {
    auto o = mkS(reg, "hkpConstraintInstance"); if (!o) return nullptr;
    o->FieldRef("name").str      = ci.m_name;
    o->FieldRef("priority").raw  = e16(static_cast<std::uint16_t>(ci.m_priority));  // byte field; low byte used
    o->FieldRef("priority").raw.resize(1);
    o->FieldRef("wantRuntime").raw = e8(ci.m_wantRuntime ? 1 : 0);
    { auto& ents = o->FieldRef("entities").objs;
      for (int k = 0; k < 2; ++k) { auto b = std::static_pointer_cast<hkpRigidBody>(ci.m_entities[static_cast<std::size_t>(k)]);
          ents.push_back(b ? schemaBody(reg, *b, bodyMap, shapeMap) : nullptr); } }
    // common transforms atom
    auto setTr = [&](io::SchemaObject& atoms, const hkpSetLocalTransformsConstraintAtom& tr) {
        auto a = mkS(reg, "hkpSetLocalTransformsConstraintAtom");
        a->FieldRef("type").raw = e16(tr.m_type);
        a->FieldRef("transformA").obj = schemaHkTransform(reg, tr.m_transformA);
        a->FieldRef("transformB").obj = schemaHkTransform(reg, tr.m_transformB);
        atoms.FieldRef("transforms").obj = a;
    };
    auto setStab = [&](io::SchemaObject& atoms, const hkpSetupStabilizationAtom& st) {
        auto a = mkS(reg, "hkpSetupStabilizationAtom");
        a->FieldRef("type").raw = e16(st.m_type); a->FieldRef("enabled").raw = e8(st.m_enabled ? 1 : 0);
        a->FieldRef("maxAngle").raw = ef(st.m_maxAngle); atoms.FieldRef("setupStabilization").obj = a;
    };
    auto setBall = [&](io::SchemaObject& atoms, const hkpBallSocketConstraintAtom& bs) {
        auto a = mkS(reg, "hkpBallSocketConstraintAtom");
        a->FieldRef("type").raw = e16(bs.m_type); a->FieldRef("solvingMethod").raw = e8(static_cast<std::uint8_t>(bs.m_solvingMethod));
        a->FieldRef("bodiesToNotify").raw = e8(static_cast<std::uint8_t>(bs.m_bodiesToNotify));
        a->FieldRef("velocityStabilizationFactor").raw = e8(static_cast<std::uint8_t>(bs.m_velocityStabilizationFactor));
        a->FieldRef("maxImpulse").raw = ef(bs.m_maxImpulse); a->FieldRef("inertiaStabilizationFactor").raw = ef(bs.m_inertiaStabilizationFactor);
        atoms.FieldRef("ballSocket").obj = a;
    };
    auto setAngFric = [&](io::SchemaObject& atoms, const hkpAngFrictionConstraintAtom& af) {
        auto a = mkS(reg, "hkpAngFrictionConstraintAtom");
        a->FieldRef("type").raw = e16(af.m_type); a->FieldRef("isEnabled").raw = e8(static_cast<std::uint8_t>(af.m_isEnabled));
        a->FieldRef("firstFrictionAxis").raw = e8(static_cast<std::uint8_t>(af.m_firstFrictionAxis));
        a->FieldRef("numFrictionAxes").raw = e8(static_cast<std::uint8_t>(af.m_numFrictionAxes));
        a->FieldRef("maxFrictionTorque").raw = ef(af.m_maxFrictionTorque); atoms.FieldRef("angFriction").obj = a;
    };
    if (auto rd = std::dynamic_pointer_cast<hkpRagdollConstraintData>(ci.m_data)) {
        auto data = mkS(reg, "hkpRagdollConstraintData"); if (!data) return nullptr;
        auto atoms = mkS(reg, "hkpRagdollConstraintDataAtoms"); if (!atoms) return nullptr;
        const auto& at = rd->m_atoms;
        setTr(*atoms, at.m_transforms); setStab(*atoms, at.m_setupStabilization); setBall(*atoms, at.m_ballSocket); setAngFric(*atoms, at.m_angFriction);
        { auto a = mkS(reg, "hkpRagdollMotorConstraintAtom"); a->FieldRef("type").raw = e16(at.m_ragdollMotors.m_type);
          a->FieldRef("isEnabled").raw = e8(at.m_ragdollMotors.m_isEnabled ? 1 : 0); atoms->FieldRef("ragdollMotors").obj = a; }
        { auto a = mkS(reg, "hkpTwistLimitConstraintAtom"); a->FieldRef("type").raw = e16(at.m_twistLimit.m_type);
          a->FieldRef("isEnabled").raw = e8(static_cast<std::uint8_t>(at.m_twistLimit.m_isEnabled));
          a->FieldRef("twistAxis").raw = e8(static_cast<std::uint8_t>(at.m_twistLimit.m_twistAxis));
          a->FieldRef("refAxis").raw = e8(static_cast<std::uint8_t>(at.m_twistLimit.m_refAxis));
          a->FieldRef("minAngle").raw = ef(at.m_twistLimit.m_minAngle); a->FieldRef("maxAngle").raw = ef(at.m_twistLimit.m_maxAngle);
          a->FieldRef("angularLimitsTauFactor").raw = ef(at.m_twistLimit.m_angularLimitsTauFactor); atoms->FieldRef("twistLimit").obj = a; }
        auto cone = [&](const char* field, const hkpConeLimitConstraintAtom& c) {
            auto a = mkS(reg, "hkpConeLimitConstraintAtom"); a->FieldRef("type").raw = e16(c.m_type);
            a->FieldRef("isEnabled").raw = e8(static_cast<std::uint8_t>(c.m_isEnabled));
            a->FieldRef("twistAxisInA").raw = e8(static_cast<std::uint8_t>(c.m_twistAxisInA));
            a->FieldRef("refAxisInB").raw = e8(static_cast<std::uint8_t>(c.m_refAxisInB));
            a->FieldRef("angleMeasurementMode").raw = e8(static_cast<std::uint8_t>(c.m_angleMeasurementMode));
            a->FieldRef("memOffsetToAngleOffset").raw = e8(static_cast<std::uint8_t>(c.m_memOffsetToAngleOffset));
            a->FieldRef("minAngle").raw = ef(c.m_minAngle); a->FieldRef("maxAngle").raw = ef(c.m_maxAngle);
            a->FieldRef("angularLimitsTauFactor").raw = ef(c.m_angularLimitsTauFactor); atoms->FieldRef(field).obj = a;
        };
        cone("coneLimit", at.m_coneLimit); cone("planesLimit", at.m_planesLimit);
        data->FieldRef("atoms").obj = atoms; o->FieldRef("data").obj = data;
    } else if (auto hd = std::dynamic_pointer_cast<hkpLimitedHingeConstraintData>(ci.m_data)) {
        auto data = mkS(reg, "hkpLimitedHingeConstraintData"); if (!data) return nullptr;
        auto atoms = mkS(reg, "hkpLimitedHingeConstraintDataAtoms"); if (!atoms) return nullptr;
        const auto& at = hd->m_atoms;
        setTr(*atoms, at.m_transforms); setStab(*atoms, at.m_setupStabilization); setBall(*atoms, at.m_ballSocket); setAngFric(*atoms, at.m_angFriction);
        { auto a = mkS(reg, "hkpAngMotorConstraintAtom"); a->FieldRef("type").raw = e16(at.m_angMotor.m_type);
          a->FieldRef("isEnabled").raw = e8(at.m_angMotor.m_isEnabled ? 1 : 0);
          a->FieldRef("motorAxis").raw = e8(static_cast<std::uint8_t>(at.m_angMotor.m_motorAxis)); atoms->FieldRef("angMotor").obj = a; }
        { auto a = mkS(reg, "hkpAngLimitConstraintAtom"); a->FieldRef("type").raw = e16(at.m_angLimit.m_type);
          a->FieldRef("isEnabled").raw = e8(static_cast<std::uint8_t>(at.m_angLimit.m_isEnabled));
          a->FieldRef("limitAxis").raw = e8(static_cast<std::uint8_t>(at.m_angLimit.m_limitAxis));
          a->FieldRef("minAngle").raw = ef(at.m_angLimit.m_minAngle); a->FieldRef("maxAngle").raw = ef(at.m_angLimit.m_maxAngle);
          a->FieldRef("angularLimitsTauFactor").raw = ef(at.m_angLimit.m_angularLimitsTauFactor); atoms->FieldRef("angLimit").obj = a; }
        { auto a = mkS(reg, "hkp2dAngConstraintAtom"); a->FieldRef("type").raw = e16(at.m_2dAng.m_type);
          a->FieldRef("freeRotationAxis").raw = e8(static_cast<std::uint8_t>(at.m_2dAng.m_freeRotationAxis)); atoms->FieldRef("2dAng").obj = a; }
        data->FieldRef("atoms").obj = atoms; o->FieldRef("data").obj = data;
    } else return nullptr;
    return o;
}

// hkaSkeletonMapper → schema (mapping struct + simpleMappings + unmappedBones), skeletons via skelMap.
std::shared_ptr<io::SchemaObject> schemaMapper(const schema::SchemaRegistry& reg, const hkaSkeletonMapper& m,
        std::unordered_map<const hkaSkeleton*, std::shared_ptr<io::SchemaObject>>& skelMap) {
    auto o = mkS(reg, "hkaSkeletonMapper"); if (!o) return nullptr;
    auto md = mkS(reg, "hkaSkeletonMapperData"); if (!md) return nullptr;
    const auto& d = m.m_mapping;
    md->FieldRef("skeletonA").obj = d.m_skeletonA ? skelMap[d.m_skeletonA.get()] : nullptr;
    md->FieldRef("skeletonB").obj = d.m_skeletonB ? skelMap[d.m_skeletonB.get()] : nullptr;
    { auto& sm = md->FieldRef("simpleMappings").objs;
      for (const auto& s : d.m_simpleMappings) { auto so = mkS(reg, "hkaSkeletonMapperDataSimpleMapping"); if (!so) return nullptr;
          so->FieldRef("boneA").raw = e16(static_cast<std::uint16_t>(s.m_boneA));
          so->FieldRef("boneB").raw = e16(static_cast<std::uint16_t>(s.m_boneB));
          { auto& t = so->FieldRef("aFromBTransform").raw; t.clear(); apQt(t, s.m_aFromBTransform); } sm.push_back(so); } }
    { auto& ub = md->FieldRef("unmappedBones").raw; for (std::int16_t b : d.m_unmappedBones) { std::uint8_t bb[2]; std::memcpy(bb, &b, 2); ub.insert(ub.end(), bb, bb + 2); } }
    { auto& em = md->FieldRef("extractedMotionMapping").raw; em.clear(); apQt(em, d.m_extractedMotionMapping); }
    md->FieldRef("keepUnmappedLocal").raw = e8(d.m_keepUnmappedLocal ? 1 : 0);
    md->FieldRef("mappingType").raw = e32(static_cast<std::uint32_t>(d.m_mappingType));
    o->FieldRef("mapping").obj = md;
    return o;
}

// hkMemoryResourceContainer subtree → schema; handles' variants share the schema bodies/shapes.
std::shared_ptr<io::SchemaObject> schemaResource(const schema::SchemaRegistry& reg, const hkMemoryResourceContainer& c,
        std::unordered_map<const hkpRigidBody*, std::shared_ptr<io::SchemaObject>>& bodyMap,
        std::unordered_map<const hkpShape*, std::shared_ptr<io::SchemaObject>>& shapeMap) {
    auto o = mkS(reg, "hkMemoryResourceContainer"); if (!o) return nullptr;
    o->FieldRef("name").str = c.m_name;
    { auto& hs = o->FieldRef("resourceHandles").objs;
      for (const auto& h : c.m_resourceHandles) { auto ho = mkS(reg, "hkMemoryResourceHandle"); if (!ho) return nullptr;
          ho->FieldRef("name").str = h->m_name;
          if (auto body = std::dynamic_pointer_cast<hkpRigidBody>(h->m_variant)) ho->FieldRef("variant").obj = bodyMap[body.get()];
          else if (auto si = std::dynamic_pointer_cast<hkpShapeInfo>(h->m_variant)) {
              auto so = mkS(reg, "hkpShapeInfo");
              so->FieldRef("shape").obj = si->m_shape ? shapeMap[si->m_shape.get()] : nullptr;
              so->FieldRef("isHierarchicalCompound").raw = e8(si->m_isHierarchicalCompound ? 1 : 0);
              so->FieldRef("hkdShapesCollected").raw     = e8(si->m_hkdShapesCollected ? 1 : 0);
              so->FieldRef("transform").obj = schemaHkTransform(reg, si->m_transform);
              ho->FieldRef("variant").obj = so;
          }
          hs.push_back(ho); } }
    { auto& ch = o->FieldRef("children").objs; for (const auto& kid : c.m_children) ch.push_back(schemaResource(reg, *kid, bodyMap, shapeMap)); }
    return o;
}

std::shared_ptr<io::SchemaObject> AssembleSkeletonFull(const SkeletonData& data, const schema::SchemaRegistry& reg) {
    // Reuse the typed derivations verbatim (single source of truth for the physics math), then transcribe.
    auto animSkel = buildAnimSkeleton(data);
    auto bodies   = DeriveRigidBodies(data);
    if (bodies.empty()) return AssembleSkeletonAnim(data, reg);   // first-person / no-physics rig
    auto ragSkel  = DeriveRagdollSkeleton(data);
    auto ragCons  = DeriveConstraints(data, bodies);
    auto sysCons  = DeriveConstraints(data, bodies);
    std::shared_ptr<hkpRigidBody> bumper; if (data.bumper) bumper = buildBumperBody(*data.bumper);
    const auto wAnim = skmath::worldPoses(data);
    const auto wRag  = skeletonWorld(*ragSkel);

    std::unordered_map<std::string,int> animOf; for (int i = 0; i < static_cast<int>(data.bones.size()); ++i) animOf[data.bones[i].name] = i;
    std::vector<int> ragToAnim(ragSkel->m_bones.size(), -1); std::vector<char> mappedAnim(data.bones.size(), 0);
    for (std::size_t rr = 0; rr < ragSkel->m_bones.size(); ++rr) {
        const std::string& nm = ragSkel->m_bones[rr].m_name; const std::string bare = nm.rfind("Ragdoll_", 0) == 0 ? nm.substr(8) : nm;
        if (auto it = animOf.find(bare); it != animOf.end()) { ragToAnim[rr] = it->second; mappedAnim[static_cast<std::size_t>(it->second)] = 1; } }
    std::vector<std::pair<int,int>> pAR, pRA;
    for (int rr = 0; rr < static_cast<int>(ragToAnim.size()); ++rr) { if (ragToAnim[rr] < 0) continue; pAR.emplace_back(ragToAnim[rr], rr); pRA.emplace_back(rr, ragToAnim[rr]); }
    std::vector<std::int16_t> unmapped; for (int i = 0; i < static_cast<int>(data.bones.size()); ++i) if (!mappedAnim[static_cast<std::size_t>(i)]) unmapped.push_back(static_cast<std::int16_t>(i));
    auto mapper0 = buildMapper(animSkel, ragSkel, wAnim, wRag, pAR, {});
    auto mapper1 = buildMapper(ragSkel, animSkel, wRag, wAnim, pRA, unmapped);
    auto resRoot = DeriveResourceContainer(ragSkel, bodies, bumper);

    // ── transcribe (shared object maps keep refs identical across variants) ──
    std::unordered_map<const hkpRigidBody*, std::shared_ptr<io::SchemaObject>> bodyMap;
    std::unordered_map<const hkpShape*,     std::shared_ptr<io::SchemaObject>> shapeMap;
    std::unordered_map<const hkaSkeleton*,  std::shared_ptr<io::SchemaObject>> skelMap;
    auto sAnim = schemaSkel(reg, *animSkel); auto sRag = schemaSkel(reg, *ragSkel);
    if (!sAnim || !sRag) return nullptr;
    skelMap[animSkel.get()] = sAnim; skelMap[ragSkel.get()] = sRag;
    if (bumper && !schemaBody(reg, *bumper, bodyMap, shapeMap)) return nullptr;
    for (const auto& b : bodies) if (!schemaBody(reg, *b, bodyMap, shapeMap)) return nullptr;

    // hkaAnimationContainer (both skeletons)
    auto animCont = mkS(reg, "hkaAnimationContainer"); if (!animCont) return nullptr;
    animCont->FieldRef("skeletons").objs = { sAnim, sRag };
    // hkpPhysicsData → hkpPhysicsSystem
    auto sys = mkS(reg, "hkpPhysicsSystem"); if (!sys) return nullptr;
    { auto& rbs = sys->FieldRef("rigidBodies").objs; if (bumper) rbs.push_back(bodyMap[bumper.get()]); for (const auto& b : bodies) rbs.push_back(bodyMap[b.get()]); }
    { auto& cs = sys->FieldRef("constraints").objs; for (const auto& c : sysCons) { auto sc = schemaConstraint(reg, static_cast<const hkpConstraintInstance&>(*c), bodyMap, shapeMap); if (!sc) return nullptr; cs.push_back(sc); } }
    sys->FieldRef("name").str = "Ragdoll"; sys->FieldRef("active").raw = e8(1);
    auto phys = mkS(reg, "hkpPhysicsData"); if (!phys) return nullptr; phys->FieldRef("systems").objs = { sys };
    // hkaRagdollInstance
    auto ragInst = mkS(reg, "hkaRagdollInstance"); if (!ragInst) return nullptr;
    { auto& rbs = ragInst->FieldRef("rigidBodies").objs; for (const auto& b : bodies) rbs.push_back(bodyMap[b.get()]); }
    { auto& cs = ragInst->FieldRef("constraints").objs; for (const auto& c : ragCons) { auto sc = schemaConstraint(reg, static_cast<const hkpConstraintInstance&>(*c), bodyMap, shapeMap); if (!sc) return nullptr; cs.push_back(sc); } }
    { auto& bm = ragInst->FieldRef("boneToRigidBodyMap").raw; for (int i = 0; i < static_cast<int>(bodies.size()); ++i) { auto b = e32(static_cast<std::uint32_t>(i)); bm.insert(bm.end(), b.begin(), b.end()); } }
    ragInst->FieldRef("skeleton").obj = sRag;
    // mappers
    auto sMap0 = schemaMapper(reg, *mapper0, skelMap); auto sMap1 = schemaMapper(reg, *mapper1, skelMap); if (!sMap0 || !sMap1) return nullptr;
    // resource tree
    auto sRes = schemaResource(reg, *resRoot, bodyMap, shapeMap); if (!sRes) return nullptr;

    auto root = mkS(reg, "hkRootLevelContainer"); if (!root) return nullptr;
    auto addVar = [&](const char* nm, const char* cls, std::shared_ptr<io::SchemaObject> v) -> bool {
        auto nv = mkS(reg, "hkRootLevelContainerNamedVariant"); if (!nv || !v) return false;
        nv->FieldRef("name").str = nm; nv->FieldRef("className").str = cls; nv->FieldRef("variant").obj = v;
        root->FieldRef("namedVariants").objs.push_back(nv); return true;
    };
    if (!addVar("Merged Animation Container", "hkaAnimationContainer", animCont) ||
        !addVar("Physics Data", "hkpPhysicsData", phys) ||
        !addVar("RagdollInstance", "hkaRagdollInstance", ragInst) ||
        !addVar("SkeletonMapper", "hkaSkeletonMapper", sMap0) ||
        !addVar("SkeletonMapper", "hkaSkeletonMapper", sMap1) ||
        !addVar("Resource Data", "hkMemoryResourceContainer", sRes)) return nullptr;
    return root;
}

}  // namespace

CompileResult CompileSkeletonFull(const SkeletonData& data, const HKXHeader& header) {
    CompileResult r;
    try {
        // ── the anim skeleton (always) + physics bodies ──
        auto animSkel = buildAnimSkeleton(data);
        auto bodies   = DeriveRigidBodies(data);

        // NO physics bones → NO ragdoll. The first-person rig (skeletonfirst.hkx) has no physics;
        // vanilla ships it as anim-skeleton-ONLY (1 hkaSkeleton, no RagdollInstance / Physics Data /
        // mappers / Resource Data). Emitting a body-less ragdoll instance anyway makes the engine flag
        // the character ragdoll-capable but leaves hkbRagdollDriver null → the per-frame ragdoll update
        // derefs the null driver → CTD (crashed the 1st-person player). Match vanilla: emit only the
        // "Merged Animation Container" with the single anim skeleton.
        if (bodies.empty()) {
            // Data-driven path (opt-in): the anim-only skeleton via the Havok/ descriptors, byte-identical
            // to the typed emit below; any failure falls through to typed.
            if (SchemaCompileEnabled()) {
                if (schema::SchemaRegistry* reg = SchemaCompileRegistry()) {
                    try {
                        if (auto sroot = AssembleSkeletonAnim(data, *reg)) {
                            PackFileSerializer ser;
                            BinaryWriterEx bw(/*bigEndian*/ false, /*uSizeLong*/ true);
                            ser.Serialize(sroot, bw, header);
                            r.bytes = bw.Take();
                            r.ok = true;
                            return r;
                        }
                    } catch (const std::exception&) { /* fall through to typed anim-only */ }
                }
            }
            auto animOnly = std::make_shared<hkaAnimationContainer>();
            animOnly->m_skeletons.push_back(animSkel);
            auto root = std::make_shared<hkRootLevelContainer>();
            hkRootLevelContainerNamedVariant nv;
            nv.m_name = "Merged Animation Container"; nv.m_className = "hkaAnimationContainer"; nv.m_variant = animOnly;
            root->m_namedVariants.push_back(std::move(nv));
            PackFileSerializer ser;
            BinaryWriterEx bw(/*bigEndian*/ false, /*uSizeLong*/ true);
            ser.Serialize(root, bw, header);
            r.bytes = bw.Take();
            r.ok = true;
            return r;
        }

        // Data-driven (schema) full-ragdoll path (opt-in): assemble the 6-variant graph via the Havok/
        // descriptors, proven byte-identical to the typed emit below. Any failure falls through to typed.
        if (SchemaCompileEnabled()) {
            if (schema::SchemaRegistry* reg = SchemaCompileRegistry()) {
                try {
                    if (auto sroot = AssembleSkeletonFull(data, *reg)) {
                        PackFileSerializer ser;
                        BinaryWriterEx bw(/*bigEndian*/ false, /*uSizeLong*/ true);
                        ser.Serialize(sroot, bw, header);
                        r.bytes = bw.Take();
                        r.ok = true;
                        return r;
                    }
                } catch (const std::exception&) { /* fall through to the typed ragdoll emit */ }
            }
        }

        // ── ragdoll skeleton + world poses ──
        auto ragSkel  = DeriveRagdollSkeleton(data);
        const auto wAnim = skmath::worldPoses(data);
        const auto wRag  = skeletonWorld(*ragSkel);

        // ── constraints ──
        // Vanilla gives the ragdoll INSTANCE and the physics SYSTEM SEPARATE constraint objects
        // (34 total = 17 each) over the SAME shared bodies — not one aliased set. Sharing the 17
        // between the two (the old behavior) makes the engine's ragdoll-driver setup fail to take
        // ownership of constraints already owned by the sim system. DeriveConstraints builds fresh
        // objects each call, so two calls give the two independent sets faithfully.
        auto ragConstraints = DeriveConstraints(data, bodies);   // ragdoll instance's set
        auto sysConstraints = DeriveConstraints(data, bodies);   // physics system's set (separate objects)

        // ragdoll bone r -> anim bone index (strip "Ragdoll_"); ragdoll bones are in physics-bone order.
        std::unordered_map<std::string,int> animOf;
        for (int i = 0; i < static_cast<int>(data.bones.size()); ++i) animOf[data.bones[i].name] = i;
        std::vector<int> ragToAnim(ragSkel->m_bones.size(), -1);
        std::vector<char> mappedAnim(data.bones.size(), 0);
        for (std::size_t rr = 0; rr < ragSkel->m_bones.size(); ++rr) {
            const std::string& nm = ragSkel->m_bones[rr].m_name;         // "Ragdoll_<X>"
            const std::string bare = nm.rfind("Ragdoll_", 0) == 0 ? nm.substr(8) : nm;
            auto it = animOf.find(bare);
            if (it != animOf.end()) { ragToAnim[rr] = it->second; mappedAnim[static_cast<std::size_t>(it->second)] = 1; }
        }

        // ── ragdoll instance (skeleton + bodies + constraints + boneToRigidBodyMap = identity) ──
        auto ragInst = std::make_shared<hkaRagdollInstance>();
        ragInst->m_rigidBodies = bodies;
        ragInst->m_constraints = ragConstraints;
        ragInst->m_skeleton    = ragSkel;
        ragInst->m_boneToRigidBodyMap.resize(bodies.size());
        for (int i = 0; i < static_cast<int>(bodies.size()); ++i) ragInst->m_boneToRigidBodyMap[i] = i;

        // ── physics data / system (CharacterBumper first, then the 18 ragdoll bodies — vanilla order) ──
        auto sys = std::make_shared<hkpPhysicsSystem>();
        std::shared_ptr<hkpRigidBody> bumperBody;
        if (data.bumper) { bumperBody = buildBumperBody(*data.bumper); sys->m_rigidBodies.push_back(bumperBody); }
        sys->m_rigidBodies.insert(sys->m_rigidBodies.end(), bodies.begin(), bodies.end());
        sys->m_constraints = sysConstraints;
        sys->m_name = "Ragdoll";
        sys->m_active = true;
        auto phys = std::make_shared<hkpPhysicsData>();
        phys->m_systems.push_back(sys);

        // ── the 2 skeleton mappers ──
        std::vector<std::pair<int,int>> pairsAnimRag, pairsRagAnim;   // (A,B)
        for (int rr = 0; rr < static_cast<int>(ragToAnim.size()); ++rr) {
            if (ragToAnim[rr] < 0) continue;
            pairsAnimRag.emplace_back(ragToAnim[rr], rr);   // #0 A=anim,B=ragdoll
            pairsRagAnim.emplace_back(rr, ragToAnim[rr]);   // #1 A=ragdoll,B=anim
        }
        std::vector<std::int16_t> unmappedAnim;              // anim bones with no ragdoll counterpart
        for (int i = 0; i < static_cast<int>(data.bones.size()); ++i) if (!mappedAnim[static_cast<std::size_t>(i)]) unmappedAnim.push_back(static_cast<std::int16_t>(i));
        auto mapper0 = buildMapper(animSkel, ragSkel, wAnim, wRag, pairsAnimRag, {});            // ragdoll->anim
        auto mapper1 = buildMapper(ragSkel, animSkel, wRag, wAnim, pairsRagAnim, unmappedAnim);  // anim->ragdoll

        // ── the resource tree (physics slice — bumper + ragdoll handles) ──
        auto resRoot = DeriveResourceContainer(ragSkel, bodies, bumperBody);

        // ── assemble the root (6 variants) ──
        auto animCont = std::make_shared<hkaAnimationContainer>();
        animCont->m_skeletons.push_back(animSkel);
        animCont->m_skeletons.push_back(ragSkel);

        auto root = std::make_shared<hkRootLevelContainer>();
        auto addVar = [&](const char* name, const char* cls, std::shared_ptr<hkReferencedObject> v) {
            hkRootLevelContainerNamedVariant nv; nv.m_name = name; nv.m_className = cls; nv.m_variant = std::move(v);
            root->m_namedVariants.push_back(std::move(nv));
        };
        addVar("Merged Animation Container", "hkaAnimationContainer", animCont);
        addVar("Physics Data",  "hkpPhysicsData",     phys);
        addVar("RagdollInstance", "hkaRagdollInstance", ragInst);
        addVar("SkeletonMapper", "hkaSkeletonMapper",  mapper0);
        addVar("SkeletonMapper", "hkaSkeletonMapper",  mapper1);
        addVar("Resource Data", "hkMemoryResourceContainer", resRoot);

        PackFileSerializer ser;
        BinaryWriterEx bw(/*bigEndian*/ false, /*uSizeLong*/ true);
        ser.Serialize(root, bw, header);
        r.bytes = bw.Take();
        r.ok = true;
    } catch (const std::exception& e) {
        r.ok = false; r.error = e.what(); r.bytes.clear();
    }
    return r;
}

CompileResult CompileSkeleton(const SkeletonData& data, const HKXHeader& header) {
    CompileResult r;
    try {
        // Data-driven path (opt-in): assemble the anim skeleton via the Havok/ descriptors, proven
        // byte-identical to BuildSkeletonRoot below. Any failure falls through to typed.
        if (SchemaCompileEnabled()) {
            if (schema::SchemaRegistry* reg = SchemaCompileRegistry()) {
                try {
                    if (auto sroot = AssembleSkeletonAnim(data, *reg)) {
                        PackFileSerializer ser;
                        BinaryWriterEx bw;
                        ser.Serialize(sroot, bw, header);
                        r.bytes = bw.Data();
                        r.ok    = true;
                        return r;
                    }
                } catch (const std::exception&) { /* fall through to the typed builder */ }
            }
        }
        auto root = BuildSkeletonRoot(data);
        PackFileSerializer ser;
        BinaryWriterEx bw;
        ser.Serialize(root, bw, header);
        r.bytes = bw.Data();
        r.ok    = true;
    } catch (const std::exception& e) {
        r.ok    = false;
        r.error = e.what();
        r.bytes.clear();
    }
    return r;
}

// Full-skeleton COMPILE-OVER-BASE (Stage A spine). Read a base skeleton.hkx as its complete object
// graph, REBUILD only the animation hkaSkeleton's bone arrays from the merged neutral data, and CARRY
// everything else untouched — the ragdoll skeleton, rigid bodies, constraints, physics system, ragdoll
// instance, both skeleton mappers, the resource tree, plus the anim skeleton's float slots / local
// frames. This is how existing content (vanilla + XPMSSE + bone-add plugins) serves: the physics is
// carried, the bone list is authored/merged. Re-serialized with the BASE's own header, so an IDENTITY
// bone set reproduces the base byte-for-byte (the drift gate); appended bones ride preserve-and-append
// and the carried ragdoll/mappers stay valid because they reference the frozen prefix indices.
CompileResult CompileSkeletonOverBase(const SkeletonData&              animBones,
                                      const std::vector<std::uint8_t>& baseBytes) {
    CompileResult r;
    try {
        PackFileDeserializer des;
        BinaryReaderEx br(/*bigEndian*/ false, /*uSizeLong*/ true, baseBytes);
        auto root = std::dynamic_pointer_cast<hkRootLevelContainer>(des.Deserialize(br));
        if (!root) { r.ok = false; r.error = "base is not an hkRootLevelContainer"; return r; }

        std::shared_ptr<hkaSkeleton> anim;
        for (auto& nv : root->m_namedVariants)
            if (auto ac = std::dynamic_pointer_cast<hkaAnimationContainer>(nv.m_variant))
                if (!ac->m_skeletons.empty()) { anim = ac->m_skeletons[0]; break; }
        if (!anim) { r.ok = false; r.error = "base has no animation skeleton (hkaAnimationContainer.m_skeletons[0])"; return r; }

        // Rebuild the three parallel bone arrays; leave m_name / m_referenceFloats / m_floatSlots /
        // m_localFrames and every other variant untouched.
        anim->m_parentIndices.clear();
        anim->m_bones.clear();
        anim->m_referencePose.clear();
        const std::size_t n = animBones.bones.size();
        anim->m_parentIndices.reserve(n);
        anim->m_bones.reserve(n);
        anim->m_referencePose.reserve(n);
        for (const auto& b : animBones.bones) {
            anim->m_parentIndices.push_back(static_cast<std::int16_t>(b.parentIndex));
            hkaBone bone;
            bone.m_name            = b.name;
            bone.m_lockTranslation = b.lockTranslation;
            anim->m_bones.push_back(std::move(bone));
            anim->m_referencePose.push_back(b.refPose);
        }

        PackFileSerializer ser;
        BinaryWriterEx     bw(/*bigEndian*/ false, /*uSizeLong*/ true);
        ser.Serialize(root, bw, des._header);   // the BASE's own header → byte-exact on identity
        r.bytes = bw.Take();
        r.ok    = true;
    } catch (const std::exception& e) {
        r.ok    = false;
        r.error = e.what();
        r.bytes.clear();
    }
    return r;
}

CompileResult CompileSkeletonToFile(const SkeletonData& data,
                                    const std::filesystem::path& outPath,
                                    bool validate, const HKXHeader& header) {
    CompileResult r = CompileSkeleton(data, header);
    if (!r.ok) return r;

    if (validate) {
        // Round-trip: the bytes must deserialize back to a well-formed
        // hkRootLevelContainer carrying an hkaAnimationContainer variant.
        try {
            PackFileDeserializer des;
            BinaryReaderEx br(/*bigEndian*/ false, /*uSizeLong*/ true, r.bytes);
            auto root = std::dynamic_pointer_cast<hkRootLevelContainer>(des.Deserialize(br));
            if (!root || root->m_namedVariants.empty() ||
                !std::dynamic_pointer_cast<hkaAnimationContainer>(root->m_namedVariants[0].m_variant)) {
                r.ok    = false;
                r.error = "skeleton packfile did not validate (no hkaAnimationContainer root variant)";
                return r;
            }
        } catch (const std::exception& e) {
            r.ok    = false;
            r.error = std::string("skeleton packfile did not validate: ") + e.what();
            return r;
        }
    }

    std::string err;
    if (!WriteHavokFile(outPath, r.bytes, &err)) { r.ok = false; r.error = err; }
    return r;
}

} // namespace havok::sct
