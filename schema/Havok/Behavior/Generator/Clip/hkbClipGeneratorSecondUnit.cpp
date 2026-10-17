/// @file
/// @brief The clip's second vanilla translation unit: its internal-state accessors (vtable slots 13, 14, 16) and the
///        three track-copy helpers sampleAnimation calls.
/// @note Not a Havok file name — no string in the exe names this unit. The evidence that it IS a unit of its own:
///       every other hkb node class keeps slots 13/14/16 as the last functions of its own .cpp's run of code, but the
///       clip's sit at 0x140b030c0, far from hkbClipGenerator.cpp (0x140acd450–0x140ad138d), after
///       hkbGeneratorTransitionEffect's unit, followed by the helpers (0x140b03380–0x140b038d9, called only from
///       sampleAnimation) and then hkbStateMachine's slots 13/14/16 (0x140b038e0) — which, when matched, likely
///       belong in this same unit.
#include <Behavior/Generator/Clip/hkbClipGenerator.h>

#include <Animation/Rig/hkaSkeleton.h>

namespace CB { namespace schema
{
    hkReferencedObject* hkbClipGenerator::createInternalState()
    {
        return new hkbClipGeneratorInternalState;
    }

    /// @note Vanilla keeps the loop-invariant `identity`/`mirror` tests as materialized bools (setz/setnz, spilled),
    ///       24247 folds them back into re-testing the loaded size/pointer (bool, hkBool and in-loop forms all tried;
    ///       rules G and A accept it). The mirror/else-if split is what reproduces vanilla's duplicated bound check.
    void hkbClipGenerator::copySampledPose(int numTracks, hkQsTransform* sampled, int numPoseLocal, hkQsTransform* poseLocal,
                                           hkBitField& bonesSampled, const hkArray<hkInt16>& trackToBone, bool isAdditive,
                                           const hkaMirroredSkeleton* mirroredSkeleton, bool mirrorTransforms)
    {
        const bool mirror     = (mirroredSkeleton != HK_NULL);
        const bool identity   = (trackToBone.getSize() == 0);
        int        numSampled = 0;
        for (hkInt16 t = 0; t < numTracks; t++) {
            hkInt16        bone      = identity ? t : trackToBone[t];
            hkQsTransform& transform = sampled[t];
            if (mirror) {
                bone = mirroredSkeleton->getBonePair(bone);
                if (bone >= numPoseLocal) continue;
                if (mirrorTransforms) mirroredSkeleton->mirrorBone(transform, bone, isAdditive);
            } else if (bone >= numPoseLocal) {
                continue;
            }
            poseLocal[bone] = transform;
            bonesSampled.set(bone);
            numSampled++;
        }
        bonesSampled.assign(numPoseLocal, numSampled == numPoseLocal);
    }

    void hkbClipGenerator::copySampledFloats(int numTracks, const hkReal* sampled, int numFloatSlots, const hkaAnimationBinding* binding,
                                             hkReal* floatSlots)
    {
        const bool identity = (binding->m_floatTrackToFloatSlotIndices.getSize() == 0);
        for (int t = 0; t < numTracks; t++) {
            const int slot = identity ? t : binding->m_floatTrackToFloatSlotIndices[t];
            if (slot < numFloatSlots) floatSlots[slot] = sampled[t];
        }
    }

    void hkbClipGenerator::blendSampledFloats(int numTracks, const hkReal* sampled, int numFloatSlots, const hkaAnimationBinding* binding,
                                              hkReal weight, hkReal* floatSlots)
    {
        const bool identity = (binding->m_floatTrackToFloatSlotIndices.getSize() == 0);
        for (int t = 0; t < numTracks; t++) {
            const int slot = identity ? t : binding->m_floatTrackToFloatSlotIndices[t];
            if (slot < numFloatSlots) {
                const hkReal current = floatSlots[slot];
                /// @note The min2 against 1 is a no-op on a 0/1 value, but vanilla computes it (minss).
                const hkReal keep    = hkMath::min2(hkReal(current != HK_REAL_MAX), 1.0f);
                floatSlots[slot]     = keep * current + weight * sampled[t];
            }
        }
    }
} }
