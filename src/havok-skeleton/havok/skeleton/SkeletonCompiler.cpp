// SkeletonCompiler — schema-native skeleton .hkx emit. The de-typed port of havok-core's byte-exact
// sct/SkeletonCompiler: the derivation math (ragdoll skeleton / rigid bodies / constraints / mappers /
// resource tree) is ported verbatim into lean value structs (no typed hka*/hkp* holders), then emitted
// straight into an io::SchemaObject graph via the Havok/ class descriptors and serialized. Every field
// value and constructor-default constant the typed path relied on is reproduced explicitly, so the
// output stays byte-identical (gated by havok-core-cli skeleton-full-parity / skeleton-parity).

#include "havok/skeleton/SkeletonCompiler.h"
#include "havok/skeleton/SkeletonMath.h"

#include <havok-io/HavokIo.h>          // io::SchemaObject + MakeSchemaFactory
#include <havok-schema/HavokSchema.h>  // schema::SharedRegistry / SchemaRegistry

#include "havok/core/BinaryReaderEx.h"
#include "havok/core/BinaryWriterEx.h"
#include "havok/core/PackFileDeserializer.h"
#include "havok/core/PackFileSerializer.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <exception>
#include <fstream>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace havok::skeleton {

namespace {

using havok::io::SchemaObject;
using skmath::Cols4;

// ── little-endian field encoders → FieldValue.raw bytes ─────────────────────────
void apF (std::vector<std::uint8_t>& v, float f)         { std::uint8_t b[4]; std::memcpy(b, &f, 4); v.insert(v.end(), b, b + 4); }
void apV4(std::vector<std::uint8_t>& v, const Vector4& q) { apF(v, q.x); apF(v, q.y); apF(v, q.z); apF(v, q.w); }
void apCols(std::vector<std::uint8_t>& v, const Cols4& c) { for (const auto& col : c) apV4(v, col); }
void apQt(std::vector<std::uint8_t>& v, const QSTransform& t) {
    apV4(v, t.translation);
    apF(v, t.rotation.x); apF(v, t.rotation.y); apF(v, t.rotation.z); apF(v, t.rotation.w);
    apV4(v, t.scale);
}
std::vector<std::uint8_t> e8 (std::uint8_t x)  { return { x }; }
std::vector<std::uint8_t> e16(std::uint16_t x) { return { static_cast<std::uint8_t>(x & 0xff), static_cast<std::uint8_t>(x >> 8) }; }
std::vector<std::uint8_t> e32(std::uint32_t x) { std::vector<std::uint8_t> v(4); std::memcpy(v.data(), &x, 4); return v; }
std::vector<std::uint8_t> ef (float f)         { std::vector<std::uint8_t> v; apF(v, f); return v; }
std::vector<std::uint8_t> eV4(const Vector4& q){ std::vector<std::uint8_t> v; apV4(v, q); return v; }
// Raw 16-byte Vector4 copy (preserves every bit incl. -0.0 padding).
std::vector<std::uint8_t> rawV4(const Vector4& q){ std::vector<std::uint8_t> v(16); std::memcpy(v.data(), &q, 16); return v; }

// Skyrim hkHalf = top 16 bits of a float32 (bit truncation, NOT IEEE binary16).
std::uint16_t toHalf(float f) { std::uint32_t u; std::memcpy(&u, &f, 4); return static_cast<std::uint16_t>(u >> 16); }

constexpr float kDeg2Rad = 0.017453292519943295f;
constexpr float kConeMin = -100.0f;              // ragdoll cone-limit min sentinel
constexpr float kFltMax  = 3.4028234663852886e38f;

// ── derived value structs (lean; the port's stand-ins for the typed hka*/hkp* holders) ──
struct DBody {
    std::string   name;
    Vector4       vertexA{}, vertexB{}; float radius = 0.f;   // capsule shape
    std::uint8_t  motionType = 3;
    Vector4       inertiaAndMassInv{};
    Cols4         transform{};                                // motionState.transform (4 cols)
    std::array<Vector4,5> swept{};                            // sweptTransform (cm0,cm1,rot0,rot1,cmLocal)
    float         objectRadius = 0.f;
    std::uint32_t filterInfo = 0;
    float         friction = 0.3f, restitution = 0.8f;
};
struct DCon {
    std::string name; int childBody = -1, parentBody = -1; bool hinge = false;
    Cols4 fA{}, fB{};
    float twistMin=0,twistMax=0,coneMax=0,planeMin=0,planeMax=0;   // ragdoll (radians)
    float angMin=0,angMax=0;                                       // hinge (radians)
};

// ── geometry / inertia ──────────────────────────────────────────────────────────
struct CapsuleInertia { float axis; float perp; };
CapsuleInertia capsuleInertia(float len, float r, float m) {
    const float pi = 3.14159265358979324f;
    const float vCyl = pi * r * r * len;
    const float vCap = (4.f / 3.f) * pi * r * r * r;
    const float vTot = vCyl + vCap;
    if (vTot <= 1e-12f) return { 0.4f * m * r * r, 0.4f * m * r * r };
    const float mCyl = m * vCyl / vTot;
    const float mCap = m * vCap / vTot;
    const float axis = mCyl * (0.5f * r * r) + mCap * (0.4f * r * r);
    const float perp = mCyl * (len * len / 12.f + 0.25f * r * r)
                     + mCap * (0.4f * r * r + 0.25f * len * len + 0.375f * len * r);
    return { axis, perp };
}

// ── ragdoll topology helpers over a SkeletonData ─────────────────────────────────
struct RagdollTopo {
    std::vector<int> ragOfAnim;   // anim idx -> ragdoll idx (or -1)
    std::vector<int> animOfRag;   // ragdoll idx -> anim idx
};
RagdollTopo ragdollTopo(const SkeletonData& anim) {
    RagdollTopo t; t.ragOfAnim.assign(anim.bones.size(), -1);
    for (int i = 0; i < static_cast<int>(anim.bones.size()); ++i)
        if (anim.bones[i].physics) { t.ragOfAnim[i] = static_cast<int>(t.animOfRag.size()); t.animOfRag.push_back(i); }
    return t;
}
int physicsAncestor(const SkeletonData& a, int animIdx) {
    int p = a.bones[animIdx].parentIndex;
    while (p >= 0 && !a.bones[p].physics) p = a.bones[p].parentIndex;
    return p;
}

// ── derive the ragdoll skeleton (as a SkeletonData: names/parents/refposes) ──────
SkeletonData deriveRagdollSkeleton(const SkeletonData& anim, const RagdollTopo& topo) {
    const auto world = skmath::worldPoses(anim);
    SkeletonData rag;
    for (int r = 0; r < static_cast<int>(topo.animOfRag.size()); ++r) {
        const int ai   = topo.animOfRag[static_cast<std::size_t>(r)];
        const int pAni = physicsAncestor(anim, ai);
        SkeletonBoneData b;
        b.name            = "Ragdoll_" + anim.bones[static_cast<std::size_t>(ai)].name;
        b.lockTranslation = true;
        b.parentIndex     = pAni >= 0 ? topo.ragOfAnim[pAni] : -1;
        const auto& ph = anim.bones[static_cast<std::size_t>(ai)].physics;
        b.refPose = (ph && ph->ragdollLocal) ? *ph->ragdollLocal
                  : pAni >= 0 ? skmath::compose(skmath::inverse(world[static_cast<std::size_t>(pAni)]),
                                                world[static_cast<std::size_t>(ai)])
                              : world[static_cast<std::size_t>(ai)];
        rag.bones.push_back(std::move(b));
    }
    rag.name = rag.bones.empty() ? "Ragdoll" : rag.bones[0].name;
    return rag;
}

// ── derive the ragdoll rigid bodies (order == ragdoll order) ─────────────────────
std::vector<DBody> deriveBodies(const SkeletonData& anim, const RagdollTopo& topo, const SkeletonData& rag) {
    const auto ragWorld = skmath::worldPoses(rag);

    // First bone (ANY bone, not necessarily a physics bone) whose nearest physics-ancestor is animIdx —
    // its local translation is the default capsule's far endpoint (origin → child). Matches havok-core.
    const auto firstPhysChild = [&](int animIdx) -> int {
        for (int c = 0; c < static_cast<int>(anim.bones.size()); ++c)
            if (physicsAncestor(anim, c) == animIdx) return c;
        return -1;
    };

    std::vector<DBody> out;
    for (int i = 0; i < static_cast<int>(anim.bones.size()); ++i) {
        const auto& bone = anim.bones[static_cast<std::size_t>(i)];
        if (!bone.physics) continue;
        const auto& ph = *bone.physics;
        const int r = topo.ragOfAnim[static_cast<std::size_t>(i)];

        DBody b;
        b.name   = "Ragdoll_" + bone.name;
        b.radius = ph.radius;
        if (ph.capsule) { b.vertexA = ph.capsule->a; b.vertexB = ph.capsule->b; }
        else {
            b.vertexA = Vector4{0,0,0,0};
            const int ch = firstPhysChild(i);
            b.vertexB = ch >= 0 ? anim.bones[static_cast<std::size_t>(ch)].refPose.translation : Vector4{0,0,0,0};
        }
        const Vector4 ab{ b.vertexB.x - b.vertexA.x, b.vertexB.y - b.vertexA.y, b.vertexB.z - b.vertexA.z, 0 };
        const float len = std::sqrt(ab.x*ab.x + ab.y*ab.y + ab.z*ab.z);
        const CapsuleInertia ci = capsuleInertia(len, ph.radius, ph.mass);
        const float invM = ph.mass > 1e-9f ? 1.f / ph.mass : 0.f;
        const bool sphere = ph.radius > 1e-6f && len < 0.25f * ph.radius;
        b.motionType = sphere ? 2 : 3;
        if (sphere) {
            const float I = ci.perp;
            const float inv = I > 1e-12f ? 1.f / I : 0.f;
            b.inertiaAndMassInv = Vector4{ inv, inv, inv, invM };
        } else {
            const float ax = std::fabs(ab.x), ay = std::fabs(ab.y), az = std::fabs(ab.z);
            const int dom = (ax >= ay && ax >= az) ? 0 : (ay >= az ? 1 : 2);
            float e[3] = { ci.perp, ci.perp, ci.perp }; e[dom] = ci.axis;
            b.inertiaAndMassInv = Vector4{ e[0] > 1e-12f ? 1.f/e[0] : 0.f, e[1] > 1e-12f ? 1.f/e[1] : 0.f,
                                           e[2] > 1e-12f ? 1.f/e[2] : 0.f, invM };
        }
        const QSTransform& bindW = ragWorld[static_cast<std::size_t>(r)];
        b.transform = skmath::toCols(bindW);
        const Vector4&    pos = bindW.translation;
        const Quaternion& rot = bindW.rotation;
        const Vector4 q{ rot.x, rot.y, rot.z, rot.w };
        b.swept = { pos, pos, q, q, Vector4{0,0,0,0} };
        const Vector4 h{ 0.5f*std::fabs(ab.x)+ph.radius, 0.5f*std::fabs(ab.y)+ph.radius, 0.5f*std::fabs(ab.z)+ph.radius, 0 };
        b.objectRadius = std::sqrt(h.x*h.x + h.y*h.y + h.z*h.z);

        const int pAni  = physicsAncestor(anim, i);
        const int subId = r + 1;
        const int dontColl = pAni >= 0 ? topo.ragOfAnim[static_cast<std::size_t>(pAni)] + 1 : 0;
        b.filterInfo = static_cast<std::uint32_t>((subId << 5) | (dontColl << 10) | (1 << 16) | 0);
        b.friction    = ph.friction    ? *ph.friction    : 0.3f;
        b.restitution = ph.restitution ? *ph.restitution : 0.8f;
        out.push_back(std::move(b));
    }
    return out;
}

DBody deriveBumper(const SkeletonBumper& bp) {
    DBody b;
    b.name = "CharacterBumper";
    b.radius = bp.radius; b.vertexA = bp.capsule.a; b.vertexB = bp.capsule.b;
    b.motionType = 5;                               // FIXED
    b.inertiaAndMassInv = Vector4{0,0,0,0};
    b.transform = { Vector4{1,0,0,0}, Vector4{0,1,0,0}, Vector4{0,0,1,0}, bp.pos };  // identity rot @ pos
    const Vector4 q{0,0,0,1};
    b.swept = { bp.pos, bp.pos, q, q, Vector4{0,0,0,0} };
    const Vector4 h{ 0.5f*std::fabs(bp.capsule.b.x - bp.capsule.a.x)+bp.radius,
                     0.5f*std::fabs(bp.capsule.b.y - bp.capsule.a.y)+bp.radius,
                     0.5f*std::fabs(bp.capsule.b.z - bp.capsule.a.z)+bp.radius, 0 };
    b.objectRadius = std::sqrt(h.x*h.x + h.y*h.y + h.z*h.z);
    b.filterInfo = 0;
    b.friction = bp.friction; b.restitution = bp.restitution;
    return b;
}

// ── derive the ragdoll constraints (one per non-root physics bone with a joint) ──
void buildFrames(Vector4 tA, Vector4 pA, const Quaternion& qC, const Quaternion& qP,
                 const Vector4& posC, const Vector4& posP, Cols4& fA, Cols4& fB) {
    tA = skmath::vnorm(tA);
    pA = skmath::vnorm(skmath::vsub(pA, Vector4{tA.x*skmath::vdot(pA,tA), tA.y*skmath::vdot(pA,tA), tA.z*skmath::vdot(pA,tA), 0}));
    const Vector4 cA = skmath::vcross(tA, pA);
    fA = { tA, pA, cA, Vector4{0,0,0,0} };
    const Quaternion qPc = skmath::qconj(qP);
    for (int j = 0; j < 3; ++j) fB[static_cast<std::size_t>(j)] = skmath::qrot(qPc, skmath::qrot(qC, fA[static_cast<std::size_t>(j)]));
    fB[3] = skmath::qrot(qPc, skmath::vsub(posC, posP));
}

std::vector<DCon> deriveConstraints(const SkeletonData& anim, const RagdollTopo& topo, const SkeletonData& rag) {
    const auto world    = skmath::worldPoses(anim);
    const auto ragWorld = skmath::worldPoses(rag);

    const auto chainChild = [&](int a) -> int {
        int best = -1; float bestDot = -2.f;
        const Vector4 up = skmath::qrot(world[a].rotation, Vector4{0,0,1,0});
        for (int c = 0; c < static_cast<int>(anim.bones.size()); ++c) {
            if (!anim.bones[c].physics || physicsAncestor(anim, c) != a) continue;
            const Vector4 dir = skmath::vnorm(skmath::vsub(world[c].translation, world[a].translation));
            const float d = skmath::vdot(dir, up);
            if (d > bestDot) { bestDot = d; best = c; }
        }
        return best;
    };

    std::vector<DCon> out;
    for (int i = 0; i < static_cast<int>(anim.bones.size()); ++i) {
        const auto& bone = anim.bones[static_cast<std::size_t>(i)];
        if (!bone.physics || !bone.physics->joint) continue;
        const int pAni = physicsAncestor(anim, i);
        if (pAni < 0) continue;
        const auto& j = *bone.physics->joint;
        const int rC = topo.ragOfAnim[static_cast<std::size_t>(i)];
        const int rP = topo.ragOfAnim[static_cast<std::size_t>(pAni)];

        Vector4 tA, pA;
        if (j.twistAxis) tA = *j.twistAxis;
        else {
            const int gc = chainChild(i);
            tA = gc >= 0 ? skmath::vnorm(skmath::qrot(skmath::qconj(world[i].rotation), skmath::vsub(world[gc].translation, world[i].translation)))
                         : Vector4{0,0,1,0};
        }
        if (j.planeAxis) pA = *j.planeAxis;
        else {
            pA = skmath::vcross(tA, Vector4{0,0,1,0});
            if (skmath::vdot(pA,pA) < 1e-6f) pA = skmath::vcross(tA, Vector4{1,0,0,0});
        }

        DCon c;
        c.name = "Ragdoll_" + bone.name; c.childBody = rC; c.parentBody = rP;
        c.hinge = (j.type == BoneJoint::Type::Hinge);
        const auto& wC = ragWorld[static_cast<std::size_t>(rC)];
        const auto& wP = ragWorld[static_cast<std::size_t>(rP)];
        buildFrames(tA, pA, wC.rotation, wP.rotation, wC.translation, wP.translation, c.fA, c.fB);
        if (c.hinge) { c.angMin = j.angMin * kDeg2Rad; c.angMax = j.angMax * kDeg2Rad; }
        else {
            c.twistMin = j.twistMin * kDeg2Rad; c.twistMax = j.twistMax * kDeg2Rad;
            c.coneMax  = j.coneMax  * kDeg2Rad;
            c.planeMin = j.planeMin * kDeg2Rad; c.planeMax = j.planeMax * kDeg2Rad;
        }
        out.push_back(std::move(c));
    }
    return out;
}

// ── SchemaObject emit ────────────────────────────────────────────────────────────
std::shared_ptr<SchemaObject> mkS(const schema::SchemaRegistry& reg, const char* cls) {
    const schema::ClassSchema* cs = reg.Find(cls);
    if (!cs) return nullptr;
    auto o = std::make_shared<SchemaObject>(&reg, cs);
    o->Init();
    return o;
}

std::shared_ptr<SchemaObject> emitHkTransform(const schema::SchemaRegistry& reg, const Cols4& c) {
    auto o = mkS(reg, "hkTransform"); if (!o) return nullptr;
    auto& d = o->FieldRef("data").raw; d.clear(); apCols(d, c);
    return o;
}

std::shared_ptr<SchemaObject> emitSkel(const schema::SchemaRegistry& reg, const SkeletonData& sk) {
    auto o = mkS(reg, "hkaSkeleton"); if (!o) return nullptr;
    o->FieldRef("name").str = sk.name.empty() ? "Skeleton" : sk.name;
    { auto& pi = o->FieldRef("parentIndices").raw;
      for (const auto& b : sk.bones) { std::int16_t p = static_cast<std::int16_t>(b.parentIndex); std::uint8_t bb[2]; std::memcpy(bb, &p, 2); pi.insert(pi.end(), bb, bb + 2); } }
    { auto& bones = o->FieldRef("bones").objs;
      for (const auto& b : sk.bones) { auto bo = mkS(reg, "hkaBone"); if (!bo) return nullptr;
          bo->FieldRef("name").str = b.name;
          bo->FieldRef("lockTranslation").raw = e8(b.lockTranslation ? 1 : 0); bones.push_back(bo); } }
    { auto& rp = o->FieldRef("referencePose").raw; for (const auto& b : sk.bones) apQt(rp, b.refPose); }
    return o;
}

std::shared_ptr<SchemaObject> emitCapsule(const schema::SchemaRegistry& reg, const DBody& b) {
    auto o = mkS(reg, "hkpCapsuleShape"); if (!o) return nullptr;
    o->FieldRef("radius").raw  = ef(b.radius);
    o->FieldRef("vertexA").raw = rawV4(b.vertexA);
    o->FieldRef("vertexB").raw = rawV4(b.vertexB);
    return o;
}

// hkpRigidBody + its capsule shape. Returns the body SO; the shape SO is put into `shapeOut`.
std::shared_ptr<SchemaObject> emitBody(const schema::SchemaRegistry& reg, const DBody& b,
                                       std::shared_ptr<SchemaObject>& shapeOut) {
    auto o = mkS(reg, "hkpRigidBody"); if (!o) return nullptr;
    o->FieldRef("name").str = b.name;
    auto cap = emitCapsule(reg, b); if (!cap) return nullptr; shapeOut = cap;
    auto coll = mkS(reg, "hkpLinkedCollidable"); if (!coll) return nullptr;
    coll->FieldRef("shape").obj = cap;
    coll->FieldRef("allowedPenetrationDepth").raw = ef(0.1f);
    auto bph = mkS(reg, "hkpTypedBroadPhaseHandle"); if (!bph) return nullptr;
    bph->FieldRef("type").raw                = e8(1);
    bph->FieldRef("objectQualityType").raw   = e8(4);
    bph->FieldRef("collisionFilterInfo").raw = e32(b.filterInfo);
    coll->FieldRef("broadPhaseHandle").obj = bph;
    o->FieldRef("collidable").obj = coll;
    auto mat = mkS(reg, "hkpMaterial"); if (!mat) return nullptr;
    mat->FieldRef("responseType").raw = e8(1);
    mat->FieldRef("friction").raw     = ef(b.friction);
    mat->FieldRef("restitution").raw  = ef(b.restitution);
    o->FieldRef("material").obj = mat;
    o->FieldRef("damageMultiplier").raw = ef(1.0f);
    o->FieldRef("uid").raw = e32(0xFFFFFFFFu);
    auto spu = mkS(reg, "hkpEntitySpuCollisionCallback"); if (!spu) return nullptr;
    spu->FieldRef("eventFilter").raw = e8(3);
    spu->FieldRef("userFilter").raw  = e8(1);
    o->FieldRef("spuCollisionCallback").obj = spu;
    auto mo = mkS(reg, "hkpMaxSizeMotion"); if (!mo) return nullptr;
    mo->FieldRef("type").raw                         = e8(b.motionType);
    mo->FieldRef("deactivationIntegrateCounter").raw = e8(15);
    mo->FieldRef("inertiaAndMassInv").raw            = eV4(b.inertiaAndMassInv);
    mo->FieldRef("gravityFactor").raw                = e16(toHalf(1.0f));
    auto ms = mkS(reg, "hkMotionState"); if (!ms) return nullptr;
    { auto& tf = ms->FieldRef("transform").raw;      tf.clear(); apCols(tf, b.transform); }
    { auto& sw = ms->FieldRef("sweptTransform").raw; sw.clear(); for (const auto& v : b.swept) apV4(sw, v); }
    ms->FieldRef("deltaAngle").raw        = eV4(Vector4{0,0,0,0});   // typed default: all +0.0
    ms->FieldRef("objectRadius").raw      = ef(b.objectRadius);
    ms->FieldRef("linearDamping").raw     = e16(toHalf(0.0f));
    ms->FieldRef("angularDamping").raw    = e16(toHalf(0.05f));
    ms->FieldRef("timeFactor").raw        = e16(toHalf(1.0f));
    ms->FieldRef("maxLinearVelocity").raw  = e8(127);
    ms->FieldRef("maxAngularVelocity").raw = e8(127);
    ms->FieldRef("deactivationClass").raw  = e8(2);
    mo->FieldRef("motionState").obj = ms;
    o->FieldRef("motion").obj = mo;
    return o;
}

std::shared_ptr<SchemaObject> emitConstraint(const schema::SchemaRegistry& reg, const DCon& c,
                                             const std::vector<std::shared_ptr<SchemaObject>>& bodySOs) {
    auto o = mkS(reg, "hkpConstraintInstance"); if (!o) return nullptr;
    o->FieldRef("name").str = c.name;
    o->FieldRef("priority").raw    = e8(1);
    o->FieldRef("wantRuntime").raw = e8(1);
    { auto& ents = o->FieldRef("entities").objs;
      ents.push_back(c.childBody  >= 0 ? bodySOs[static_cast<std::size_t>(c.childBody)]  : nullptr);
      ents.push_back(c.parentBody >= 0 ? bodySOs[static_cast<std::size_t>(c.parentBody)] : nullptr); }

    const auto setTr = [&](SchemaObject& atoms) {
        auto a = mkS(reg, "hkpSetLocalTransformsConstraintAtom");
        a->FieldRef("type").raw = e16(2);
        a->FieldRef("transformA").obj = emitHkTransform(reg, c.fA);
        a->FieldRef("transformB").obj = emitHkTransform(reg, c.fB);
        atoms.FieldRef("transforms").obj = a;
    };
    const auto setStab = [&](SchemaObject& atoms) {
        auto a = mkS(reg, "hkpSetupStabilizationAtom");
        a->FieldRef("type").raw = e16(23); a->FieldRef("enabled").raw = e8(0);
        a->FieldRef("maxAngle").raw = ef(1.8446744e19f); atoms.FieldRef("setupStabilization").obj = a;
    };
    const auto setBall = [&](SchemaObject& atoms) {
        auto a = mkS(reg, "hkpBallSocketConstraintAtom");
        a->FieldRef("type").raw = e16(5); a->FieldRef("solvingMethod").raw = e8(1);
        a->FieldRef("bodiesToNotify").raw = e8(0);
        a->FieldRef("velocityStabilizationFactor").raw = e8(48);
        a->FieldRef("maxImpulse").raw = ef(kFltMax); a->FieldRef("inertiaStabilizationFactor").raw = ef(0.f);
        atoms.FieldRef("ballSocket").obj = a;
    };
    const auto setAngFric = [&](SchemaObject& atoms) {
        auto a = mkS(reg, "hkpAngFrictionConstraintAtom");
        a->FieldRef("type").raw = e16(17); a->FieldRef("isEnabled").raw = e8(1);
        a->FieldRef("firstFrictionAxis").raw = e8(0); a->FieldRef("numFrictionAxes").raw = e8(0);
        a->FieldRef("maxFrictionTorque").raw = ef(0.f); atoms.FieldRef("angFriction").obj = a;
    };

    if (!c.hinge) {
        auto data = mkS(reg, "hkpRagdollConstraintData"); if (!data) return nullptr;
        auto atoms = mkS(reg, "hkpRagdollConstraintDataAtoms"); if (!atoms) return nullptr;
        setTr(*atoms); setStab(*atoms); setBall(*atoms); setAngFric(*atoms);
        { auto a = mkS(reg, "hkpRagdollMotorConstraintAtom"); a->FieldRef("type").raw = e16(19);
          a->FieldRef("isEnabled").raw = e8(0); atoms->FieldRef("ragdollMotors").obj = a; }
        { auto a = mkS(reg, "hkpTwistLimitConstraintAtom"); a->FieldRef("type").raw = e16(15);
          a->FieldRef("isEnabled").raw = e8(0); a->FieldRef("twistAxis").raw = e8(0); a->FieldRef("refAxis").raw = e8(1);
          a->FieldRef("minAngle").raw = ef(c.twistMin); a->FieldRef("maxAngle").raw = ef(c.twistMax);
          a->FieldRef("angularLimitsTauFactor").raw = ef(0.8f); atoms->FieldRef("twistLimit").obj = a; }
        const auto cone = [&](const char* field, std::uint8_t refAxisInB, std::uint8_t angleMode,
                              std::uint8_t memOff, float mn, float mx) {
            auto a = mkS(reg, "hkpConeLimitConstraintAtom"); a->FieldRef("type").raw = e16(16);
            a->FieldRef("isEnabled").raw = e8(0); a->FieldRef("twistAxisInA").raw = e8(0);
            a->FieldRef("refAxisInB").raw = e8(refAxisInB); a->FieldRef("angleMeasurementMode").raw = e8(angleMode);
            a->FieldRef("memOffsetToAngleOffset").raw = e8(memOff);
            a->FieldRef("minAngle").raw = ef(mn); a->FieldRef("maxAngle").raw = ef(mx);
            a->FieldRef("angularLimitsTauFactor").raw = ef(0.8f); atoms->FieldRef(field).obj = a;
        };
        cone("coneLimit",   0, 0, 56, kConeMin,    c.coneMax);
        cone("planesLimit", 1, 1, 0,  c.planeMin,  c.planeMax);
        data->FieldRef("atoms").obj = atoms; o->FieldRef("data").obj = data;
    } else {
        auto data = mkS(reg, "hkpLimitedHingeConstraintData"); if (!data) return nullptr;
        auto atoms = mkS(reg, "hkpLimitedHingeConstraintDataAtoms"); if (!atoms) return nullptr;
        setTr(*atoms); setStab(*atoms); setBall(*atoms); setAngFric(*atoms);
        { auto a = mkS(reg, "hkpAngMotorConstraintAtom"); a->FieldRef("type").raw = e16(18);
          a->FieldRef("isEnabled").raw = e8(0); a->FieldRef("motorAxis").raw = e8(0); atoms->FieldRef("angMotor").obj = a; }
        { auto a = mkS(reg, "hkpAngLimitConstraintAtom"); a->FieldRef("type").raw = e16(14);
          a->FieldRef("isEnabled").raw = e8(0); a->FieldRef("limitAxis").raw = e8(0);
          a->FieldRef("minAngle").raw = ef(c.angMin); a->FieldRef("maxAngle").raw = ef(c.angMax);
          a->FieldRef("angularLimitsTauFactor").raw = ef(1.f); atoms->FieldRef("angLimit").obj = a; }
        { auto a = mkS(reg, "hkp2dAngConstraintAtom"); a->FieldRef("type").raw = e16(12);
          a->FieldRef("freeRotationAxis").raw = e8(0); atoms->FieldRef("2dAng").obj = a; }
        data->FieldRef("atoms").obj = atoms; o->FieldRef("data").obj = data;
    }
    return o;
}

// One anim↔ragdoll mapper. skA/skB are the emitted skeleton SOs; worldA/worldB their bind world poses.
std::shared_ptr<SchemaObject> emitMapper(const schema::SchemaRegistry& reg,
        const std::shared_ptr<SchemaObject>& skA, const std::shared_ptr<SchemaObject>& skB,
        const std::vector<QSTransform>& worldA, const std::vector<QSTransform>& worldB,
        const std::vector<std::pair<int,int>>& pairsAB, const std::vector<std::int16_t>& unmapped) {
    auto o = mkS(reg, "hkaSkeletonMapper"); if (!o) return nullptr;
    auto md = mkS(reg, "hkaSkeletonMapperData"); if (!md) return nullptr;
    md->FieldRef("skeletonA").obj = skA;
    md->FieldRef("skeletonB").obj = skB;
    { auto& sm = md->FieldRef("simpleMappings").objs;
      for (auto [ia, ib] : pairsAB) { auto so = mkS(reg, "hkaSkeletonMapperDataSimpleMapping"); if (!so) return nullptr;
          so->FieldRef("boneA").raw = e16(static_cast<std::uint16_t>(ia));
          so->FieldRef("boneB").raw = e16(static_cast<std::uint16_t>(ib));
          const QSTransform aFromB = skmath::compose(skmath::inverse(worldA[static_cast<std::size_t>(ia)]),
                                                     worldB[static_cast<std::size_t>(ib)]);
          { auto& t = so->FieldRef("aFromBTransform").raw; t.clear(); apQt(t, aFromB); } sm.push_back(so); } }
    { auto& ub = md->FieldRef("unmappedBones").raw;
      for (std::int16_t b : unmapped) { std::uint8_t bb[2]; std::memcpy(bb, &b, 2); ub.insert(ub.end(), bb, bb + 2); } }
    { auto& em = md->FieldRef("extractedMotionMapping").raw; em.clear();
      apQt(em, QSTransform{ Vector4{0,0,0,0}, Quaternion{0,0,0,1}, Vector4{1,1,1,1} }); }
    md->FieldRef("keepUnmappedLocal").raw = e8(1);
    md->FieldRef("mappingType").raw = e32(0);
    o->FieldRef("mapping").obj = md;
    return o;
}

std::shared_ptr<SchemaObject> emitShapeInfo(const schema::SchemaRegistry& reg, const std::shared_ptr<SchemaObject>& shape) {
    auto o = mkS(reg, "hkpShapeInfo"); if (!o) return nullptr;
    o->FieldRef("shape").obj = shape;
    o->FieldRef("isHierarchicalCompound").raw = e8(0);
    o->FieldRef("hkdShapesCollected").raw     = e8(0);
    o->FieldRef("transform").obj = emitHkTransform(reg, Cols4{ Vector4{1,0,0,0}, Vector4{0,1,0,0}, Vector4{0,0,1,0}, Vector4{0,0,0,0} });
    return o;
}

std::shared_ptr<SchemaObject> emitResourceCon(const schema::SchemaRegistry& reg, const std::string& name,
        const std::shared_ptr<SchemaObject>& body, const std::shared_ptr<SchemaObject>& shape) {
    auto o = mkS(reg, "hkMemoryResourceContainer"); if (!o) return nullptr;
    o->FieldRef("name").str = name;
    auto& hs = o->FieldRef("resourceHandles").objs;
    { auto h = mkS(reg, "hkMemoryResourceHandle"); h->FieldRef("name").str = "hkRigidBody"; h->FieldRef("variant").obj = body; hs.push_back(h); }
    { auto h = mkS(reg, "hkMemoryResourceHandle"); h->FieldRef("name").str = "hkpShapeInfo"; h->FieldRef("variant").obj = emitShapeInfo(reg, shape); hs.push_back(h); }
    return o;
}

// ── assembly ─────────────────────────────────────────────────────────────────────
bool addVar(const schema::SchemaRegistry& reg, SchemaObject& root, const char* nm, const char* cls,
            const std::shared_ptr<SchemaObject>& v) {
    auto nv = mkS(reg, "hkRootLevelContainerNamedVariant"); if (!nv || !v) return false;
    nv->FieldRef("name").str = nm; nv->FieldRef("className").str = cls; nv->FieldRef("variant").obj = v;
    root.FieldRef("namedVariants").objs.push_back(nv);
    return true;
}

std::shared_ptr<SchemaObject> assembleAnim(const SkeletonData& data, const schema::SchemaRegistry& reg) {
    auto skel = emitSkel(reg, data); if (!skel) return nullptr;
    auto container = mkS(reg, "hkaAnimationContainer"); if (!container) return nullptr;
    container->FieldRef("skeletons").objs.push_back(skel);
    auto root = mkS(reg, "hkRootLevelContainer"); if (!root) return nullptr;
    if (!addVar(reg, *root, "Merged Animation Container", "hkaAnimationContainer", container)) return nullptr;
    return root;
}

std::shared_ptr<SchemaObject> assembleFull(const SkeletonData& data, const schema::SchemaRegistry& reg) {
    const RagdollTopo topo = ragdollTopo(data);
    if (topo.animOfRag.empty()) return assembleAnim(data, reg);   // no physics → anim-only (first-person rig)

    const SkeletonData rag = deriveRagdollSkeleton(data, topo);
    const std::vector<DBody> bodies = deriveBodies(data, topo, rag);
    const std::vector<DCon>  cons   = deriveConstraints(data, topo, rag);
    const auto wAnim = skmath::worldPoses(data);
    const auto wRag  = skmath::worldPoses(rag);

    // emit skeletons + bodies (+ shapes), shared across variants
    auto sAnim = emitSkel(reg, data); auto sRag = emitSkel(reg, rag);
    if (!sAnim || !sRag) return nullptr;

    std::vector<std::shared_ptr<SchemaObject>> bodySOs(bodies.size()), shapeSOs(bodies.size());
    for (std::size_t i = 0; i < bodies.size(); ++i) { bodySOs[i] = emitBody(reg, bodies[i], shapeSOs[i]); if (!bodySOs[i]) return nullptr; }
    std::shared_ptr<SchemaObject> bumperBody, bumperShape;
    if (data.bumper) { bumperBody = emitBody(reg, deriveBumper(*data.bumper), bumperShape); if (!bumperBody) return nullptr; }

    // hkaAnimationContainer (both skeletons)
    auto animCont = mkS(reg, "hkaAnimationContainer"); if (!animCont) return nullptr;
    animCont->FieldRef("skeletons").objs = { sAnim, sRag };

    // hkpPhysicsData → system (CharacterBumper first, then bodies; its OWN constraint set)
    auto sys = mkS(reg, "hkpPhysicsSystem"); if (!sys) return nullptr;
    { auto& rbs = sys->FieldRef("rigidBodies").objs; if (bumperBody) rbs.push_back(bumperBody); for (auto& b : bodySOs) rbs.push_back(b); }
    { auto& cs = sys->FieldRef("constraints").objs; for (const auto& c : cons) { auto sc = emitConstraint(reg, c, bodySOs); if (!sc) return nullptr; cs.push_back(sc); } }
    sys->FieldRef("name").str = "Ragdoll"; sys->FieldRef("active").raw = e8(1);
    auto phys = mkS(reg, "hkpPhysicsData"); if (!phys) return nullptr; phys->FieldRef("systems").objs = { sys };

    // hkaRagdollInstance (bodies only; its OWN separate constraint set; identity boneToRigidBodyMap)
    auto ragInst = mkS(reg, "hkaRagdollInstance"); if (!ragInst) return nullptr;
    { auto& rbs = ragInst->FieldRef("rigidBodies").objs; for (auto& b : bodySOs) rbs.push_back(b); }
    { auto& cs = ragInst->FieldRef("constraints").objs; for (const auto& c : cons) { auto sc = emitConstraint(reg, c, bodySOs); if (!sc) return nullptr; cs.push_back(sc); } }
    { auto& bm = ragInst->FieldRef("boneToRigidBodyMap").raw; for (int i = 0; i < static_cast<int>(bodies.size()); ++i) { auto b = e32(static_cast<std::uint32_t>(i)); bm.insert(bm.end(), b.begin(), b.end()); } }
    ragInst->FieldRef("skeleton").obj = sRag;

    // the two mappers (anim↔ragdoll), pairing by stripped name
    std::unordered_map<std::string,int> animOf; for (int i = 0; i < static_cast<int>(data.bones.size()); ++i) animOf[data.bones[i].name] = i;
    std::vector<int> ragToAnim(rag.bones.size(), -1); std::vector<char> mappedAnim(data.bones.size(), 0);
    for (std::size_t rr = 0; rr < rag.bones.size(); ++rr) {
        const std::string& nm = rag.bones[rr].name; const std::string bare = nm.rfind("Ragdoll_", 0) == 0 ? nm.substr(8) : nm;
        if (auto it = animOf.find(bare); it != animOf.end()) { ragToAnim[rr] = it->second; mappedAnim[static_cast<std::size_t>(it->second)] = 1; } }
    std::vector<std::pair<int,int>> pAR, pRA;
    for (int rr = 0; rr < static_cast<int>(ragToAnim.size()); ++rr) { if (ragToAnim[rr] < 0) continue; pAR.emplace_back(ragToAnim[rr], rr); pRA.emplace_back(rr, ragToAnim[rr]); }
    std::vector<std::int16_t> unmapped; for (int i = 0; i < static_cast<int>(data.bones.size()); ++i) if (!mappedAnim[static_cast<std::size_t>(i)]) unmapped.push_back(static_cast<std::int16_t>(i));
    auto map0 = emitMapper(reg, sAnim, sRag, wAnim, wRag, pAR, {});
    auto map1 = emitMapper(reg, sRag, sAnim, wRag, wAnim, pRA, unmapped);
    if (!map0 || !map1) return nullptr;

    // resource tree (bumper + ragdoll body handles nested by ragdoll parent hierarchy, children by name)
    const int n = static_cast<int>(rag.bones.size());
    std::vector<std::shared_ptr<SchemaObject>> cons2(static_cast<std::size_t>(n));
    for (int r = 0; r < n; ++r) cons2[static_cast<std::size_t>(r)] = emitResourceCon(reg, rag.bones[static_cast<std::size_t>(r)].name, bodySOs[static_cast<std::size_t>(r)], shapeSOs[static_cast<std::size_t>(r)]);
    std::shared_ptr<SchemaObject> ragRoot;
    for (int r = 0; r < n; ++r) {
        const int p = rag.bones[static_cast<std::size_t>(r)].parentIndex;
        if (p >= 0 && p < n) cons2[static_cast<std::size_t>(p)]->FieldRef("children").objs.push_back(cons2[static_cast<std::size_t>(r)]);
        else ragRoot = cons2[static_cast<std::size_t>(r)];
    }
    const auto byName = [](const std::shared_ptr<IHavokObject>& a, const std::shared_ptr<IHavokObject>& b) {
        auto sa = std::dynamic_pointer_cast<SchemaObject>(a); auto sb = std::dynamic_pointer_cast<SchemaObject>(b);
        return (sa ? sa->FieldRef("name").str : std::string{}) < (sb ? sb->FieldRef("name").str : std::string{}); };
    for (auto& c : cons2) { auto& ch = c->FieldRef("children").objs; std::sort(ch.begin(), ch.end(), byName); }
    auto resRoot = mkS(reg, "hkMemoryResourceContainer"); if (!resRoot) return nullptr;
    resRoot->FieldRef("name").str = "";
    { auto& ch = resRoot->FieldRef("children").objs;
      if (bumperBody) { auto cb = mkS(reg, "hkMemoryResourceContainer"); cb->FieldRef("name").str = "CharacterBumper";
          auto& hs = cb->FieldRef("resourceHandles").objs;
          { auto h = mkS(reg, "hkMemoryResourceHandle"); h->FieldRef("name").str = "hkRigidBody"; h->FieldRef("variant").obj = bumperBody; hs.push_back(h); }
          { auto h = mkS(reg, "hkMemoryResourceHandle"); h->FieldRef("name").str = "hkpShapeInfo"; h->FieldRef("variant").obj = emitShapeInfo(reg, bumperShape); hs.push_back(h); }
          ch.push_back(cb); }
      if (ragRoot) ch.push_back(ragRoot);
      std::sort(ch.begin(), ch.end(), byName); }

    auto root = mkS(reg, "hkRootLevelContainer"); if (!root) return nullptr;
    if (!addVar(reg, *root, "Merged Animation Container", "hkaAnimationContainer", animCont) ||
        !addVar(reg, *root, "Physics Data", "hkpPhysicsData", phys) ||
        !addVar(reg, *root, "RagdollInstance", "hkaRagdollInstance", ragInst) ||
        !addVar(reg, *root, "SkeletonMapper", "hkaSkeletonMapper", map0) ||
        !addVar(reg, *root, "SkeletonMapper", "hkaSkeletonMapper", map1) ||
        !addVar(reg, *root, "Resource Data", "hkMemoryResourceContainer", resRoot)) return nullptr;
    return root;
}

SkeletonCompileResult serializeRoot(const std::shared_ptr<SchemaObject>& root, const HKXHeader& header) {
    SkeletonCompileResult r;
    if (!root) { r.error = "assembly failed (unregistered class or empty graph)"; return r; }
    havok::PackFileSerializer ser;
    havok::BinaryWriterEx bw;
    ser.Serialize(root, bw, header);
    r.bytes = bw.Data();
    r.ok = true;
    return r;
}

} // namespace

SkeletonCompileResult CompileSkeleton(const SkeletonData& data, const HKXHeader& header) {
    try {
        schema::SchemaRegistry* reg = schema::SharedRegistry();
        if (!reg) return { false, "schema registry unavailable (" + schema::SharedRegistryError() + ")", {} };
        return serializeRoot(assembleAnim(data, *reg), header);
    } catch (const std::exception& e) { return { false, e.what(), {} }; }
}

SkeletonCompileResult CompileSkeletonFull(const SkeletonData& data, const HKXHeader& header) {
    try {
        schema::SchemaRegistry* reg = schema::SharedRegistry();
        if (!reg) return { false, "schema registry unavailable (" + schema::SharedRegistryError() + ")", {} };
        return serializeRoot(assembleFull(data, *reg), header);
    } catch (const std::exception& e) { return { false, e.what(), {} }; }
}

SkeletonCompileResult CompileSkeletonOverBase(const SkeletonData& animBones, const std::vector<std::uint8_t>& baseBytes) {
    SkeletonCompileResult r;
    try {
        schema::SchemaRegistry* reg = schema::SharedRegistry();
        if (!reg) { r.error = "schema registry unavailable (" + schema::SharedRegistryError() + ")"; return r; }
        havok::BinaryReaderEx br(baseBytes);
        havok::PackFileDeserializer des;
        // Do NOT tolerate unregistered here: over-base rebuilds+reserializes the WHOLE graph, so a
        // dropped object is silent data loss. Fail loudly on a class the schema can't represent (e.g.
        // hkpMoppBvTreeShape) instead — matches the typed CompileSkeletonOverBase.
        des.ObjectFactory = havok::io::MakeSchemaFactory(*reg);
        auto root = std::dynamic_pointer_cast<SchemaObject>(des.Deserialize(br));
        if (!root) { r.error = "base is not a valid packfile root"; return r; }

        // find the animation skeleton (first hkaAnimationContainer's skeletons[0])
        std::shared_ptr<SchemaObject> anim;
        for (auto& nvObj : root->FieldRef("namedVariants").objs) {
            auto nv = std::dynamic_pointer_cast<SchemaObject>(nvObj); if (!nv) continue;
            if (nv->FieldRef("className").str == "hkaAnimationContainer") {
                auto ac = std::dynamic_pointer_cast<SchemaObject>(nv->FieldRef("variant").obj);
                if (ac) { auto& sk = ac->FieldRef("skeletons").objs; if (!sk.empty()) anim = std::dynamic_pointer_cast<SchemaObject>(sk[0]); }
                break;
            }
        }
        if (!anim) { r.error = "base has no animation skeleton"; return r; }

        // rebuild the three parallel bone arrays; leave name / referenceFloats / floatSlots / localFrames
        // and every other variant untouched.
        { auto& pi = anim->FieldRef("parentIndices").raw; pi.clear();
          for (const auto& b : animBones.bones) { std::int16_t p = static_cast<std::int16_t>(b.parentIndex); std::uint8_t bb[2]; std::memcpy(bb, &p, 2); pi.insert(pi.end(), bb, bb + 2); } }
        { auto& bones = anim->FieldRef("bones").objs; bones.clear();
          for (const auto& b : animBones.bones) { auto bo = mkS(*reg, "hkaBone"); if (!bo) { r.error = "hkaBone unregistered"; return r; }
              bo->FieldRef("name").str = b.name; bo->FieldRef("lockTranslation").raw = e8(b.lockTranslation ? 1 : 0); bones.push_back(bo); } }
        { auto& rp = anim->FieldRef("referencePose").raw; rp.clear(); for (const auto& b : animBones.bones) apQt(rp, b.refPose); }

        havok::PackFileSerializer ser;
        havok::BinaryWriterEx bw(/*bigEndian*/ false, /*uSizeLong*/ true);
        ser.Serialize(root, bw, des._header);   // the BASE's own header → byte-exact on identity
        r.bytes = bw.Take();
        r.ok = true;
    } catch (const std::exception& e) { r.ok = false; r.error = e.what(); r.bytes.clear(); }
    return r;
}

SkeletonCompileResult CompileSkeletonToFile(const SkeletonData& data, const std::filesystem::path& outPath,
                                            bool validate, const HKXHeader& header) {
    SkeletonCompileResult r = CompileSkeleton(data, header);
    if (!r.ok) return r;
    if (validate) {
        try {
            schema::SchemaRegistry* reg = schema::SharedRegistry();
            havok::BinaryReaderEx br(r.bytes);
            havok::PackFileDeserializer des; des.SetTolerateUnregistered(true);
            if (reg) des.ObjectFactory = havok::io::MakeSchemaFactory(*reg);
            auto root = std::dynamic_pointer_cast<SchemaObject>(des.Deserialize(br));
            if (!root || root->FieldRef("namedVariants").objs.empty()) {
                r.ok = false; r.error = "skeleton packfile did not validate"; return r;
            }
        } catch (const std::exception& e) { r.ok = false; r.error = std::string("skeleton packfile did not validate: ") + e.what(); return r; }
    }
    std::ofstream f(outPath, std::ios::binary);
    if (!f) { r.ok = false; r.error = "cannot open output: " + outPath.string(); return r; }
    f.write(reinterpret_cast<const char*>(r.bytes.data()), static_cast<std::streamsize>(r.bytes.size()));
    if (!f) { r.ok = false; r.error = "write failed: " + outPath.string(); }
    return r;
}

} // namespace havok::skeleton
