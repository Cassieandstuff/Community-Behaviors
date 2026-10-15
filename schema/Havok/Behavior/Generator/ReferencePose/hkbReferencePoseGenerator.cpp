/// @file
/// @brief hkbReferencePoseGenerator, reimplemented against vanilla (AE 1.6.1170).
#include <Behavior/Generator/ReferencePose/hkbReferencePoseGenerator.h>

#include <Animation/Rig/hkaSkeleton.h>
#include <Common/Base/Container/BitField/hkBitField.h>
#include <Common/Base/Container/String/hkString.h>

namespace CB { namespace schema
{
    /// @note Both constructors are VARIANT: VS2015 24247 stores hkbNode::m_cloneState before loading the vtable address,
    ///       vanilla after (schedule only, as hkbSequence's).
    hkbReferencePoseGenerator::hkbReferencePoseGenerator()
    :   m_skeleton(HK_NULL)
    {
    }

    hkbReferencePoseGenerator::hkbReferencePoseGenerator(const hkaSkeleton* skeleton)
    :   m_skeleton(skeleton)
    {
    }

    const void* hkbReferencePoseGenerator::getClassType() const
    {
        return &staticClass();
    }

    hkbNode* hkbReferencePoseGenerator::cloneNode(hkbBehaviorGraph& rootBehavior) const
    {
        hkbReferencePoseGenerator* shared = const_cast<hkbReferencePoseGenerator*>(this);
        shared->m_cloneState              = CLONE_STATE_SHARABLE;
        shared->addReference();
        return shared;
    }

    /// @note The partition bit field after the pose is set whole (every partition sampled) and wraps the track's own
    ///       storage, so its destruction frees nothing. VARIANT: the output and the pose count trade rbx/rsi.
    void hkbReferencePoseGenerator::generate(const hkbContext& context, const hkbGeneratorOutput** activeChildrenOutput, hkbGeneratorOutput& output,
                                             hkReal timeOffset)
    {
        if (output.isValid(hkbGeneratorOutput::TRACK_POSE)) {
            hkbGeneratorOutput::TrackHeader& poseHeader   = output.getTrackHeader(hkbGeneratorOutput::TRACK_POSE);
            const int                        numPoseLocal = poseHeader.m_numData;
            if (numPoseLocal > 0) {
                const hkaSkeleton* skeleton = m_skeleton ? m_skeleton : context.m_character->m_setup->m_animationSkeleton.val();
                hkQsTransform*     pose     = output.accessPoseLocal();
                hkString::memCpy16(pose, skeleton->m_referencePose.m_data, numPoseLocal * (sizeof(hkQsTransform) / 16));
                poseHeader.m_onFraction = 1.0f;
                hkBitField partitionsSampled(numPoseLocal + 1, reinterpret_cast<hkUint32*>(pose + numPoseLocal), 1);
            }
        }

        output.accessWorldFromModel()                                                   = *context.m_character->m_worldFromModel;
        output.getTrackHeader(hkbGeneratorOutput::TRACK_WORLD_FROM_MODEL).m_onFraction = 1.0f;
        if (output.isValid(hkbGeneratorOutput::TRACK_EXTRACTED_MOTION)) {
            static_cast<hkQsTransform*>(output.getTrackData(hkbGeneratorOutput::TRACK_EXTRACTED_MOTION))->setIdentity();
            output.getTrackHeader(hkbGeneratorOutput::TRACK_EXTRACTED_MOTION).m_onFraction = 1.0f;
        }
    }
} }
