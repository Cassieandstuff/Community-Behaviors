// SkeletonImport — schema-native skeleton.hkx reader on the generic io::SchemaObject path
// (MakeSchemaFactory over the shared registry). Every field is read off a SchemaObject's
// tagged FieldValue store (inline structs → .obj, pointers → .obj/.objs, scalars/vec4/qstransform →
// raw bytes), driven by the schema/ class descriptors.

#include "decompile/SkeletonImport.h"

#include <codec/serialization/HavokIo.h>          // io::SchemaObject + DeserializeSchema
#include <interface/reflection/HavokSchema.h>  // schema::SharedRegistry
#include <decompile/BehaviorDecompile.h>       // ParseHkyReflective + SerializeGraph (ReadSkeletonUnit)

#include <cmath>
#include <cstring>
#include <exception>
#include <unordered_map>

namespace CB::engine::skeleton {
using namespace CB::engine::codec;
using namespace CB::engine::common;

namespace {

using CB::engine::codec::io::AsSchema;
using CB::engine::codec::io::FindVariant;
using CB::engine::codec::io::SchemaObject;

// A nested inline-struct or pointed-to object field → its SchemaObject (nullptr if absent/null).
SchemaObject* childOf(SchemaObject& o, const char* n) {
    return o.HasField(n) ? o.Child(n) : nullptr;
}

float f32(SchemaObject& o, const char* n) {
    return o.HasField(n) ? o.GetFloat(n) : 0.f;
}

// A vector4-typed field, or the k-th vector4 of a `vector4 count:N` field (e.g. hkMotionState.transform,
// hkTransform.data — 4 consecutive Vector4). Zero if the field/element is missing.
Vector4 vec4(SchemaObject& o, const char* n, int k = 0) {
    Vector4 v{};
    const auto* fv = o.FindField(n);
    if (!fv) return v;
    const auto& r = fv->raw;
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

SkeletonData readOneSkeleton(SchemaObject& skel) {
    SkeletonData sd;
    sd.name = skel.Str("name");

    const auto& bones = skel.Children("bones");
    const auto par = skel.GetArray<std::int16_t>("parentIndices");
    const auto& poseRaw = skel.FieldRef("referencePose").raw;  // QSTransform[] (48B each)

    // parentIndices / bones / referencePose are parallel arrays; the wild does not guarantee equal
    // lengths, so index each independently (a short referencePose otherwise reads off the end).
    sd.bones.reserve(bones.size());
    for (std::size_t i = 0; i < bones.size(); ++i) {
        auto b = AsSchema(bones[i]); if (!b) continue;
        SkeletonBoneData bd;
        bd.name = b->Str("name");
        bd.lockTranslation = b->GetBool("lockTranslation");
        if (i < par.size()) bd.parentIndex = par[i];
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
        // exotic creature collision shapes may be unported — skip, don't fail
        auto root = CB::engine::codec::io::DeserializeSchema(std::span<const std::uint8_t>(data, size), *reg, /*tolerateUnregistered*/ true);
        if (!root) { if (err) *err = "not a valid packfile root"; return false; }

        // The animation container carries the skeleton list in file order: [0]=anim skeleton, [1]=ragdoll.
        auto container = FindVariant(*root, "hkaAnimationContainer");
        if (!container) return true;   // parsed, but no skeletons
        const auto& skels = container->Children("skeletons");
        out.reserve(skels.size());
        for (auto& sObj : skels)
            if (auto s = AsSchema(sObj)) out.push_back(readOneSkeleton(*s));
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
        // creature ragdolls may use exotic, unported collision shapes;
        // skip them (shape → null → default capsule) instead of failing.
        auto root = CB::engine::codec::io::DeserializeSchema(std::span<const std::uint8_t>(data, size), *reg, /*tolerateUnregistered*/ true);
        if (!root) return true;

        auto pd = FindVariant(*root, "hkpPhysicsData");
        if (!pd) return true;
        const auto& systems = pd->Children("systems");
        if (systems.empty()) return true;
        auto sys = AsSchema(systems[0]);
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
            for (auto& nvObj : root->Children("namedVariants")) {
                auto nv = AsSchema(nvObj); if (!nv) continue;
                if (nv->Str("className") != "hkaSkeletonMapper") continue;
                auto m = nv->Child("variant"); if (!m) continue;
                auto mp = childOf(*m, "mapping"); if (!mp) continue;
                auto skA = mp->Child("skeletonA");
                auto skB = mp->Child("skeletonB");
                if (!skA || !skB) continue;
                const auto& bonesA = skA->Children("bones");
                const auto& bonesB = skB->Children("bones");
                if (static_cast<int>(bonesA.size()) != static_cast<int>(animSkel.bones.size())) continue;
                for (auto& smObj : mp->Children("simpleMappings")) {
                    auto sm = AsSchema(smObj); if (!sm) continue;
                    const auto ba = static_cast<std::int16_t>(sm->GetInt("boneA"));
                    const auto bb = static_cast<std::int16_t>(sm->GetInt("boneB"));
                    if (ba < 0 || ba >= static_cast<int>(bonesA.size())) continue;
                    if (bb < 0 || bb >= static_cast<int>(bonesB.size())) continue;
                    auto aN = AsSchema(bonesA[ba]); auto bN = AsSchema(bonesB[bb]);
                    if (!aN || !bN) continue;
                    if (auto ai = boneIdx.find(aN->Str("name")); ai != boneIdx.end())
                        ragToAnim[bN->Str("name")] = ai->second;
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
        for (auto& rbObj : sys->Children("rigidBodies")) {
            auto rb = AsSchema(rbObj); if (!rb) continue;
            const std::string rname = rb->Str("name");
            const int bi = mapName(rname);

            auto shapeSO = [&]() -> SchemaObject* {
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
        for (auto& cObj : sys->Children("constraints")) {
            auto ci = AsSchema(cObj); if (!ci) continue;
            const auto& ents = ci->Children("entities");
            if (ents.empty()) continue;
            auto child = AsSchema(ents[0]); if (!child) continue;
            const int bi = mapName(child->Str("name"));
            if (bi < 0 || !animSkel.bones[bi].physics) continue;

            auto cdata = ci->Child("data"); if (!cdata) continue;
            auto atoms = childOf(*cdata, "atoms"); if (!atoms) continue;
            BoneJoint j;
            SchemaObject* transforms = nullptr;
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
        if (auto ac = FindVariant(*root, "hkaAnimationContainer")) {
            const auto& skels = ac->Children("skeletons");
            if (skels.size() >= 2) {
                if (auto rag = AsSchema(skels[1])) {
                    const auto& rbones = rag->Children("bones");
                    const auto& rpose = rag->FieldRef("referencePose").raw;
                    for (std::size_t k = 0; k < rbones.size() && (k + 1) * 48 <= rpose.size(); ++k) {
                        auto rbn = AsSchema(rbones[k]); if (!rbn) continue;
                        const int bi = mapName(rbn->Str("name"));
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

bool ReadSkeletonUnit(const CB::engine::common::IUnitSource& unit, const schema::SchemaRegistry& reg,
                      const std::string& unitPath, SkeletonData& out, std::string* err) {
    decompile::ParsedHky graph;
    std::vector<std::uint8_t> bytes;
    std::string e;
    if (!decompile::ParseHkyReflective(unit, reg, graph, e, nullptr, unitPath) || !decompile::SerializeGraph(graph, bytes, e)) {
        if (err) *err = unitPath + ": " + e;
        return false;
    }
    std::vector<SkeletonData> skels;
    if (!LoadSkeletonsFromHkx(bytes.data(), bytes.size(), skels, err)) return false;
    if (skels.empty()) { if (err) *err = unitPath + ": the unit holds no skeleton"; return false; }
    out = std::move(skels[0]);
    return ReadSkeletonPhysics(bytes.data(), bytes.size(), out, err);
}

} // namespace CB::engine::skeleton
