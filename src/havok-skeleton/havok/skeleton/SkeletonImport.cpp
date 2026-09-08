// SkeletonImport — schema-native skeleton.hkx reader. The exact behavioral mirror of havok-core's
// typed havok::sct reader, rebuilt on havok-io's generic SchemaObject path (MakeSchemaFactory over the
// shared registry) so it carries no havok-core dependency. Every field is read off a SchemaObject's
// tagged FieldValue store (inline structs → .obj, pointers → .obj/.objs, scalars/vec4/qstransform →
// raw bytes), driven by the Havok/ class descriptors that the hkx-roundtrip gate already proves faithful.

#include "havok/skeleton/SkeletonImport.h"

#include <havok-io/HavokIo.h>          // io::SchemaObject + MakeSchemaFactory
#include <havok-schema/HavokSchema.h>  // schema::SharedRegistry

#include "havok/core/BinaryReaderEx.h"
#include "havok/core/PackFileDeserializer.h"

#include <cmath>
#include <cstring>
#include <exception>
#include <unordered_map>

namespace havok::skeleton {

namespace {

using havok::io::SchemaObject;

std::shared_ptr<SchemaObject> asSO(const std::shared_ptr<IHavokObject>& o) {
    return std::dynamic_pointer_cast<SchemaObject>(o);
}

// A nested inline-struct or pointed-to object field → its SchemaObject (nullptr if absent/null).
std::shared_ptr<SchemaObject> childOf(SchemaObject& o, const char* n) {
    if (!o.HasField(n)) return nullptr;
    return asSO(o.FieldRef(n).obj);
}

float f32(SchemaObject& o, const char* n) {
    if (!o.HasField(n)) return 0.f;
    const auto& r = o.FieldRef(n).raw; float v = 0.f;
    if (r.size() >= 4) std::memcpy(&v, r.data(), 4); return v;
}

// A vector4-typed field, or the k-th vector4 of a `vector4 count:N` field (e.g. hkMotionState.transform,
// hkTransform.data — 4 consecutive Vector4). Zero if the field/element is missing.
Vector4 vec4(SchemaObject& o, const char* n, int k = 0) {
    Vector4 v{};
    if (!o.HasField(n)) return v;
    const auto& r = o.FieldRef(n).raw;
    const std::size_t off = static_cast<std::size_t>(k) * 16;
    if (r.size() >= off + 16) std::memcpy(&v, r.data() + off, 16);
    return v;
}

QSTransform qst(const std::vector<std::uint8_t>& raw, std::size_t idx) {
    QSTransform t{};
    const std::size_t off = idx * 48;
    if (raw.size() >= off + 48) std::memcpy(&t, raw.data() + off, 48);
    return t;
}

std::string className(SchemaObject& o) { return o.ClassName() ? o.ClassName() : ""; }

// The one hkaAnimationContainer variant's SchemaObject, or nullptr.
std::shared_ptr<SchemaObject> findVariant(SchemaObject& root, const char* cls) {
    if (!root.HasField("namedVariants")) return nullptr;
    for (auto& nvObj : root.FieldRef("namedVariants").objs) {
        auto nv = asSO(nvObj); if (!nv) continue;
        if (nv->FieldRef("className").str == cls) return asSO(nv->FieldRef("variant").obj);
    }
    return nullptr;
}

SkeletonData readOneSkeleton(SchemaObject& skel) {
    SkeletonData sd;
    sd.name = skel.FieldRef("name").str;

    auto& bones = skel.FieldRef("bones").objs;
    const auto& parRaw = skel.FieldRef("parentIndices").raw;   // int16[]
    const auto& poseRaw = skel.FieldRef("referencePose").raw;  // QSTransform[] (48B each)

    // parentIndices / bones / referencePose are parallel arrays; the wild does not guarantee equal
    // lengths, so index each independently (a short referencePose otherwise reads off the end).
    sd.bones.reserve(bones.size());
    for (std::size_t i = 0; i < bones.size(); ++i) {
        auto b = asSO(bones[i]); if (!b) continue;
        SkeletonBoneData bd;
        bd.name = b->FieldRef("name").str;
        const auto& lt = b->FieldRef("lockTranslation").raw;
        bd.lockTranslation = !lt.empty() && lt[0] != 0;
        if ((i + 1) * 2 <= parRaw.size()) {
            std::int16_t p = 0; std::memcpy(&p, parRaw.data() + i * 2, 2); bd.parentIndex = p;
        }
        if ((i + 1) * 48 <= poseRaw.size()) bd.refPose = qst(poseRaw, i);
        sd.bones.push_back(std::move(bd));
    }
    return sd;
}

} // namespace

bool LoadSkeletonsFromHkx(const std::uint8_t* data, std::size_t size,
                          std::vector<SkeletonData>& out, std::string* err)
{
    out.clear();
    if (data == nullptr || size == 0) { if (err) *err = "empty HKX buffer"; return false; }

    schema::SchemaRegistry* reg = schema::SharedRegistry();
    if (!reg) { if (err) *err = "schema registry unavailable (" + schema::SharedRegistryError() + ")"; return false; }

    try {
        std::vector<std::uint8_t> bytes(data, data + size);
        havok::BinaryReaderEx br(bytes);
        havok::PackFileDeserializer des;
        des.SetTolerateUnregistered(true);   // exotic creature collision shapes may be unported — skip, don't fail
        des.ObjectFactory = havok::io::MakeSchemaFactory(*reg);
        auto root = asSO(des.Deserialize(br));
        if (!root) { if (err) *err = "not a valid packfile root"; return false; }

        // The animation container carries the skeleton list in file order: [0]=anim skeleton, [1]=ragdoll.
        auto container = findVariant(*root, "hkaAnimationContainer");
        if (!container) return true;   // parsed, but no skeletons
        auto& skels = container->FieldRef("skeletons").objs;
        out.reserve(skels.size());
        for (auto& sObj : skels)
            if (auto s = asSO(sObj)) out.push_back(readOneSkeleton(*s));
        return true;
    } catch (const std::exception& ex) {
        if (err) *err = ex.what();
        out.clear();
        return false;
    }
}

bool ReadSkeletonPhysics(const std::uint8_t* data, std::size_t size,
                         SkeletonData& animSkel, std::string* err)
{
    if (data == nullptr || size == 0) return true;   // nothing to attach

    schema::SchemaRegistry* reg = schema::SharedRegistry();
    if (!reg) { if (err) *err = "schema registry unavailable (" + schema::SharedRegistryError() + ")"; return false; }

    try {
        std::vector<std::uint8_t> bytes(data, data + size);
        havok::BinaryReaderEx br(bytes);
        havok::PackFileDeserializer des;
        des.SetTolerateUnregistered(true);   // creature ragdolls may use exotic, unported collision shapes;
                                             // skip them (shape → null → default capsule) instead of failing.
        des.ObjectFactory = havok::io::MakeSchemaFactory(*reg);
        auto root = asSO(des.Deserialize(br));
        if (!root) return true;

        auto pd = findVariant(*root, "hkpPhysicsData");
        if (!pd) return true;
        auto& systems = pd->FieldRef("systems").objs;
        if (systems.empty()) return true;
        auto sys = asSO(systems[0]);
        if (!sys) return true;

        const auto strip = [](std::string n) {
            const std::string p = "Ragdoll_";
            return n.rfind(p, 0) == 0 ? n.substr(p.size()) : n;
        };
        const auto deg = [](float rad) { return rad * 57.29577951308232f; };

        std::unordered_map<std::string, int> boneIdx;
        for (int i = 0; i < static_cast<int>(animSkel.bones.size()); ++i)
            boneIdx.emplace(animSkel.bones[i].name, i);

        // ragdoll body/bone name → ANIM bone index. The AUTHORITATIVE correspondence is the anim↔ragdoll
        // hkaSkeletonMapper (simpleMappings: boneA = anim idx, boneB = ragdoll idx). Build ragdollBoneName
        // → animBoneIndex from the mapper whose A skeleton IS the anim skeleton; the "Ragdoll_" name-strip
        // stays as the fallback for anything the mapper doesn't cover (esp. creatures).
        std::unordered_map<std::string, int> ragToAnim;
        if (root->HasField("namedVariants")) {
            for (auto& nvObj : root->FieldRef("namedVariants").objs) {
                auto nv = asSO(nvObj); if (!nv) continue;
                if (nv->FieldRef("className").str != "hkaSkeletonMapper") continue;
                auto m = asSO(nv->FieldRef("variant").obj); if (!m) continue;
                auto mp = childOf(*m, "mapping"); if (!mp) continue;
                auto skA = asSO(mp->FieldRef("skeletonA").obj);
                auto skB = asSO(mp->FieldRef("skeletonB").obj);
                if (!skA || !skB) continue;
                auto& bonesA = skA->FieldRef("bones").objs;
                auto& bonesB = skB->FieldRef("bones").objs;
                if (static_cast<int>(bonesA.size()) != static_cast<int>(animSkel.bones.size())) continue;
                for (auto& smObj : mp->FieldRef("simpleMappings").objs) {
                    auto sm = asSO(smObj); if (!sm) continue;
                    std::int16_t ba = 0, bb = 0;
                    { const auto& r = sm->FieldRef("boneA").raw; if (r.size() >= 2) std::memcpy(&ba, r.data(), 2); }
                    { const auto& r = sm->FieldRef("boneB").raw; if (r.size() >= 2) std::memcpy(&bb, r.data(), 2); }
                    if (ba < 0 || ba >= static_cast<int>(bonesA.size())) continue;
                    if (bb < 0 || bb >= static_cast<int>(bonesB.size())) continue;
                    auto aN = asSO(bonesA[ba]); auto bN = asSO(bonesB[bb]);
                    if (!aN || !bN) continue;
                    if (auto ai = boneIdx.find(aN->FieldRef("name").str); ai != boneIdx.end())
                        ragToAnim[bN->FieldRef("name").str] = ai->second;
                }
                break;
            }
        }
        const auto mapName = [&](const std::string& raw) -> int {
            if (auto it = ragToAnim.find(raw);      it != ragToAnim.end()) return it->second;
            if (auto it = boneIdx.find(strip(raw)); it != boneIdx.end())   return it->second;
            return -1;
        };

        // Rigid bodies → per-bone mass + radius + capsule (the authored body knobs; the rest derives).
        for (auto& rbObj : sys->FieldRef("rigidBodies").objs) {
            auto rb = asSO(rbObj); if (!rb) continue;
            const std::string rname = rb->FieldRef("name").str;
            const int bi = mapName(rname);

            auto shapeSO = [&]() -> std::shared_ptr<SchemaObject> {
                auto coll = childOf(*rb, "collidable"); if (!coll) return nullptr;
                return childOf(*coll, "shape");
            }();
            auto mat = childOf(*rb, "material");

            if (bi < 0) {
                // A body that maps to no bone: the CharacterBumper (persistent authored content). Capture it.
                if (rname == "CharacterBumper") {
                    SkeletonBumper bp;
                    if (auto motion = childOf(*rb, "motion"))
                        if (auto ms = childOf(*motion, "motionState"))
                            bp.pos = vec4(*ms, "transform", 3);
                    if (shapeSO && className(*shapeSO) == "hkpCapsuleShape") {
                        bp.capsule = BoneCapsule{ vec4(*shapeSO, "vertexA"), vec4(*shapeSO, "vertexB") };
                        bp.radius  = f32(*shapeSO, "radius");
                    }
                    if (mat) { bp.friction = f32(*mat, "friction"); bp.restitution = f32(*mat, "restitution"); }
                    animSkel.bumper = bp;
                }
                continue;
            }

            BonePhysics ph;
            if (auto motion = childOf(*rb, "motion")) {
                const Vector4 iam = vec4(*motion, "inertiaAndMassInv");
                ph.mass = iam.w > 1e-9f ? 1.0f / iam.w : 0.0f;
            }
            if (shapeSO && className(*shapeSO) == "hkpCapsuleShape") {
                ph.radius  = f32(*shapeSO, "radius");
                ph.capsule = BoneCapsule{ vec4(*shapeSO, "vertexA"), vec4(*shapeSO, "vertexB") };
            }
            // Material: capture only as an OVERRIDE where it deviates from the compiler defaults (0.3/0.8).
            if (mat) {
                const float fr = f32(*mat, "friction"), re = f32(*mat, "restitution");
                if (std::fabs(fr - 0.3f) > 1e-4f) ph.friction    = fr;
                if (std::fabs(re - 0.8f) > 1e-4f) ph.restitution = re;
            }
            animSkel.bones[bi].physics = ph;
        }

        // Constraints → the joint on the CHILD bone (entities[0]=child body, [1]=parent body).
        for (auto& cObj : sys->FieldRef("constraints").objs) {
            auto ci = asSO(cObj); if (!ci) continue;
            auto& ents = ci->FieldRef("entities").objs;
            if (ents.empty()) continue;
            auto child = asSO(ents[0]); if (!child) continue;
            const int bi = mapName(child->FieldRef("name").str);
            if (bi < 0 || !animSkel.bones[bi].physics) continue;

            auto cdata = asSO(ci->FieldRef("data").obj); if (!cdata) continue;
            auto atoms = childOf(*cdata, "atoms"); if (!atoms) continue;
            BoneJoint j;
            std::shared_ptr<SchemaObject> transforms;
            const std::string dcls = className(*cdata);
            if (dcls == "hkpRagdollConstraintData") {
                j.type = BoneJoint::Type::Ragdoll;
                if (auto tw = childOf(*atoms, "twistLimit"))  { j.twistMin = deg(f32(*tw, "minAngle")); j.twistMax = deg(f32(*tw, "maxAngle")); }
                if (auto co = childOf(*atoms, "coneLimit"))   { j.coneMax  = deg(f32(*co, "maxAngle")); }
                if (auto pl = childOf(*atoms, "planesLimit")) { j.planeMin = deg(f32(*pl, "minAngle")); j.planeMax = deg(f32(*pl, "maxAngle")); }
                transforms = childOf(*atoms, "transforms");
            } else if (dcls == "hkpLimitedHingeConstraintData") {
                j.type = BoneJoint::Type::Hinge;
                if (auto an = childOf(*atoms, "angLimit")) { j.angMin = deg(f32(*an, "minAngle")); j.angMax = deg(f32(*an, "maxAngle")); }
                transforms = childOf(*atoms, "transforms");
            } else {
                continue;
            }
            // frameA col0/col1 (child-local) = the joint's twist/plane axes — authored rig intent.
            if (transforms) {
                if (auto tA = childOf(*transforms, "transformA")) {
                    j.twistAxis = vec4(*tA, "data", 0);
                    j.planeAxis = vec4(*tA, "data", 1);
                }
            }
            animSkel.bones[bi].physics->joint = j;
        }

        // The ragdoll BIND POSE: the ragdoll skeleton (hkaAnimationContainer.skeletons[1]) carries each
        // ragdoll bone's local refpose (relative to its ragdoll parent). Attach it to the matching physics
        // bone so the ragdoll skeleton / bodies / mappers compile in the ragdoll frame (not anim).
        if (auto ac = findVariant(*root, "hkaAnimationContainer")) {
            auto& skels = ac->FieldRef("skeletons").objs;
            if (skels.size() >= 2) {
                if (auto rag = asSO(skels[1])) {
                    auto& rbones = rag->FieldRef("bones").objs;
                    const auto& rpose = rag->FieldRef("referencePose").raw;
                    for (std::size_t k = 0; k < rbones.size() && (k + 1) * 48 <= rpose.size(); ++k) {
                        auto rbn = asSO(rbones[k]); if (!rbn) continue;
                        const int bi = mapName(rbn->FieldRef("name").str);
                        if (bi >= 0 && animSkel.bones[bi].physics)
                            animSkel.bones[bi].physics->ragdollLocal = qst(rpose, k);
                    }
                }
            }
        }
        return true;
    } catch (const std::exception& ex) {
        if (err) *err = ex.what();
        return false;
    }
}

} // namespace havok::skeleton
