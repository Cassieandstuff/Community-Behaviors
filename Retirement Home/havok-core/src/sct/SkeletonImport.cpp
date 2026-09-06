#include "havok/sct/SkeletonImport.h"

#include "havok/classes/Animation.h"
#include "havok/classes/Graph.h"       // hkRootLevelContainer
#include "havok/classes/Physics.h"     // hkpPhysicsData / rigid bodies / constraints (ReadSkeletonPhysics)
#include "havok/core/BinaryReaderEx.h"
#include "havok/core/PackFileDeserializer.h"

#include <cmath>
#include <exception>
#include <unordered_map>

namespace havok::sct {

bool LoadSkeletonsFromHkx(const std::uint8_t* data, std::size_t size,
                          std::vector<SkeletonData>& out,
                          std::string* err)
{
    out.clear();

    if (data == nullptr || size == 0) {
        if (err) *err = "empty HKX buffer";
        return false;
    }

    try {
        std::vector<std::uint8_t> bytes(data, data + size);

        // DeserializePartially sniffs the endian byte at 0x11 and overwrites
        // br.BigEndian before reading the header, so these initial flags are
        // just a starting state, not an assumption about the file.
        PackFileDeserializer des;
        BinaryReaderEx br(/*bigEndian*/ false, /*uSizeLong*/ true, bytes);
        des.DeserializePartially(br);

        // Walk __data__ looking only for hkaSkeleton. Deserialize() would walk
        // the whole graph from the root instead, and a vanilla skeleton.hkx also
        // references ragdoll/physics classes that are not ported — that walk
        // throws on the first one. See ConstructAllOfClass.
        BinaryReaderEx dataReader(des._header.Endian == 0,
                                  des._header.PointerSize == 8,
                                  des.DataSectionBytes());

        const auto objects = des.ConstructAllOfClass(dataReader, "hkaSkeleton");

        out.reserve(objects.size());
        for (const auto& obj : objects) {
            const auto skel = std::dynamic_pointer_cast<hkaSkeleton>(obj);
            if (!skel) continue;

            SkeletonData sd;
            sd.name = skel->m_name;

            // parentIndices / bones / referencePose are parallel arrays. Files in
            // the wild are not guaranteed to agree on their lengths, so index
            // each independently rather than trusting one count for all three —
            // a short referencePose otherwise reads off the end.
            const std::size_t n = skel->m_bones.size();
            sd.bones.reserve(n);

            for (std::size_t i = 0; i < n; ++i) {
                SkeletonBoneData b;
                b.name = skel->m_bones[i].m_name;
                b.lockTranslation = skel->m_bones[i].m_lockTranslation;
                b.parentIndex = (i < skel->m_parentIndices.size())
                                    ? static_cast<int>(skel->m_parentIndices[i])
                                    : -1;
                if (i < skel->m_referencePose.size())
                    b.refPose = skel->m_referencePose[i];
                sd.bones.push_back(std::move(b));
            }

            out.push_back(std::move(sd));
        }

        return true;
    }
    catch (const std::exception& ex) {
        // The deserializer signals malformed files by throwing. Callers are UI
        // code, so convert to a message rather than letting it escape.
        if (err) *err = ex.what();
        out.clear();
        return false;
    }
}

bool ReadSkeletonPhysics(const std::uint8_t* data, std::size_t size,
                         SkeletonData& animSkel, std::string* err)
{
    if (data == nullptr || size == 0) return true;   // nothing to attach
    try {
        std::vector<std::uint8_t> bytes(data, data + size);
        PackFileDeserializer des;
        des.SetTolerateUnregistered(true);   // creature ragdoll bodies may use exotic, unported collision
                                             // shapes; skip them (body/joint/mass still read, shape -> null
                                             // -> compiler derives a default capsule) instead of failing.
        BinaryReaderEx        br(/*bigEndian*/ false, /*uSizeLong*/ true, bytes);
        // Full construct — the ragdoll/physics classes ARE ported now (hkx-roundtrip proves it), so the
        // whole-graph walk that LoadSkeletonsFromHkx avoids is safe here.
        auto root = std::dynamic_pointer_cast<hkRootLevelContainer>(des.Deserialize(br));
        if (!root) return true;

        std::shared_ptr<hkpPhysicsData> pd;
        for (const auto& nv : root->m_namedVariants)
            if (auto p = std::dynamic_pointer_cast<hkpPhysicsData>(nv.m_variant)) { pd = p; break; }
        if (!pd || pd->m_systems.empty()) return true;   // no physics → nothing derived-away to author
        const auto sys = pd->m_systems[0];

        const auto strip = [](std::string n) {
            const std::string p = "Ragdoll_";
            return n.rfind(p, 0) == 0 ? n.substr(p.size()) : n;
        };
        const auto deg = [](float rad) { return rad * 57.29577951308232f; };

        std::unordered_map<std::string, int> boneIdx;
        for (int i = 0; i < static_cast<int>(animSkel.bones.size()); ++i)
            boneIdx.emplace(animSkel.bones[i].name, i);

        // Ragdoll body/bone name -> ANIM bone index. The character names ragdoll bodies
        // "Ragdoll_<animBone>", so a plain strip matches; but CREATURES name them
        // "Ragdoll_<ragdollBone>" where the ragdoll bone name differs from the anim bone (e.g. the
        // body "Ragdoll_NPC Spine01" pairs with anim bone "NPC Spine1"), so the strip matches NOTHING
        // and every creature read 0 physics. The AUTHORITATIVE correspondence is the anim<->ragdoll
        // hkaSkeletonMapper (simpleMappings: boneA = anim idx, boneB = ragdoll idx). Build
        // ragdollBoneName -> animBoneIndex from it (the ragdoll bone name == the rigid-body name); the
        // legacy name-strip stays as the fallback for anything the mapper doesn't cover.
        std::unordered_map<std::string, int> ragToAnim;
        for (const auto& nv : root->m_namedVariants) {
            auto m = std::dynamic_pointer_cast<hkaSkeletonMapper>(nv.m_variant);
            if (!m) continue;
            const auto& d = m->m_mapping;
            if (!d.m_skeletonA || !d.m_skeletonB ||
                static_cast<int>(d.m_skeletonA->m_bones.size()) != static_cast<int>(animSkel.bones.size()))
                continue;   // want the mapper whose A skeleton IS the anim skeleton
            for (const auto& sm : d.m_simpleMappings) {
                if (sm.m_boneA < 0 || sm.m_boneA >= static_cast<int>(d.m_skeletonA->m_bones.size())) continue;
                if (sm.m_boneB < 0 || sm.m_boneB >= static_cast<int>(d.m_skeletonB->m_bones.size())) continue;
                if (auto ai = boneIdx.find(d.m_skeletonA->m_bones[sm.m_boneA].m_name); ai != boneIdx.end())
                    ragToAnim[d.m_skeletonB->m_bones[sm.m_boneB].m_name] = ai->second;
            }
            break;
        }
        const auto mapName = [&](const std::string& raw) -> int {
            if (auto it = ragToAnim.find(raw);          it != ragToAnim.end()) return it->second;
            if (auto it = boneIdx.find(strip(raw));     it != boneIdx.end())   return it->second;
            return -1;
        };

        // Rigid bodies → per-bone mass + radius (the two authored body knobs; the rest derives).
        for (const auto& rb : sys->m_rigidBodies) {
            if (!rb) continue;
            const int bi = mapName(rb->m_name);
            if (bi < 0) {
                // A body that maps to no bone: the CharacterBumper (persistent authored content). Capture it.
                if (rb->m_name == "CharacterBumper") {
                    SkeletonBumper bp;
                    bp.pos = rb->m_motion.m_motionState.m_transform[3];
                    if (auto cap = std::dynamic_pointer_cast<hkpCapsuleShape>(rb->m_collidable.m_shape)) {
                        bp.capsule = BoneCapsule{ cap->m_vertexA, cap->m_vertexB };
                        bp.radius  = cap->m_radius;
                    }
                    bp.friction    = rb->m_material.m_friction;
                    bp.restitution = rb->m_material.m_restitution;
                    animSkel.bumper = bp;
                }
                continue;
            }
            BonePhysics ph;
            const float invM = rb->m_motion.m_inertiaAndMassInv.w;
            ph.mass   = invM > 1e-9f ? 1.0f / invM : 0.0f;
            if (auto cap = std::dynamic_pointer_cast<hkpCapsuleShape>(rb->m_collidable.m_shape)) {
                ph.radius  = cap->m_radius;
                // Endpoints are in the body's local frame, which == the ragdoll bone's frame (they're
                // keyframed together), so they store directly as bone-local. Authored intent — see BoneCapsule.
                ph.capsule = BoneCapsule{ cap->m_vertexA, cap->m_vertexB };
            }
            // Material: capture only as an OVERRIDE where it deviates from the compiler defaults (0.3/0.8).
            if (std::fabs(rb->m_material.m_friction    - 0.3f) > 1e-4f) ph.friction    = rb->m_material.m_friction;
            if (std::fabs(rb->m_material.m_restitution - 0.8f) > 1e-4f) ph.restitution = rb->m_material.m_restitution;
            animSkel.bones[bi].physics = ph;
        }

        // Constraints → the joint on the CHILD bone (entities[0]=child body, [1]=parent body).
        for (const auto& c : sys->m_constraints) {
            const auto ci = std::dynamic_pointer_cast<hkpConstraintInstance>(c);
            if (!ci || !ci->m_entities[0]) continue;
            const int bi = mapName(ci->m_entities[0]->m_name);
            if (bi < 0 || !animSkel.bones[bi].physics) continue;
            BoneJoint j;
            const hkpSetLocalTransformsConstraintAtom* tf = nullptr;
            if (const auto rg = std::dynamic_pointer_cast<hkpRagdollConstraintData>(ci->m_data)) {
                const auto& at = rg->m_atoms;
                j.type     = BoneJoint::Type::Ragdoll;
                j.twistMin = deg(at.m_twistLimit.m_minAngle);
                j.twistMax = deg(at.m_twistLimit.m_maxAngle);
                j.coneMax  = deg(at.m_coneLimit.m_maxAngle);
                j.planeMin = deg(at.m_planesLimit.m_minAngle);
                j.planeMax = deg(at.m_planesLimit.m_maxAngle);
                tf = &at.m_transforms;
            } else if (const auto hg = std::dynamic_pointer_cast<hkpLimitedHingeConstraintData>(ci->m_data)) {
                const auto& at = hg->m_atoms;
                j.type   = BoneJoint::Type::Hinge;
                j.angMin = deg(at.m_angLimit.m_minAngle);
                j.angMax = deg(at.m_angLimit.m_maxAngle);
                tf = &at.m_transforms;
            } else {
                continue;
            }
            // frameA col0/col1 (child-local) = the joint's twist/plane axes — authored rig intent.
            j.twistAxis = tf->m_transformA.m_data[0];
            j.planeAxis = tf->m_transformA.m_data[1];
            animSkel.bones[bi].physics->joint = j;
        }

        // The ragdoll BIND POSE: the ragdoll skeleton (hkaAnimationContainer.m_skeletons[1]) carries each
        // ragdoll bone's local refpose (relative to its ragdoll parent). Attach it to the matching physics
        // bone so the ragdoll skeleton / bodies / mappers compile in the ragdoll frame (not anim).
        for (const auto& nv : root->m_namedVariants) {
            auto ac = std::dynamic_pointer_cast<hkaAnimationContainer>(nv.m_variant);
            if (!ac || ac->m_skeletons.size() < 2) continue;
            const auto& rag = ac->m_skeletons[1];
            for (std::size_t k = 0; k < rag->m_bones.size() && k < rag->m_referencePose.size(); ++k) {
                const int bi = mapName(rag->m_bones[k].m_name);   // ragdoll bone name -> anim idx (mapper)
                if (bi >= 0 && animSkel.bones[bi].physics)
                    animSkel.bones[bi].physics->ragdollLocal = rag->m_referencePose[k];
            }
            break;
        }
        return true;
    }
    catch (const std::exception& ex) {
        if (err) *err = ex.what();
        return false;
    }
}

} // namespace havok::sct
